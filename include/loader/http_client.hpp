#pragma once

#include "error.hpp"
#include "http_headers.hpp"
#include "http_message.hpp"
#include "types.hpp"
#include "url.hpp"
#include <cstring>
#include <curl/curl.h>

namespace xiaopeng {
namespace loader {

class CurlGlobal {
public:
  static CurlGlobal &instance() {
    static CurlGlobal inst;
    return inst;
  }

  bool isInitialized() const { return initialized_; }

private:
  CurlGlobal() { initialized_ = curl_global_init(CURL_GLOBAL_ALL) == CURLE_OK; }

  ~CurlGlobal() {
    if (initialized_) {
      curl_global_cleanup();
    }
  }

  CurlGlobal(const CurlGlobal &) = delete;
  CurlGlobal &operator=(const CurlGlobal &) = delete;

  bool initialized_ = false;
};

struct CurlEasyDeleter {
  void operator()(CURL *curl) const {
    if (curl)
      curl_easy_cleanup(curl);
  }
};

struct CurlSlistDeleter {
  void operator()(struct curl_slist *list) const {
    if (list)
      curl_slist_free_all(list);
  }
};

using CurlEasyPtr = std::unique_ptr<CURL, CurlEasyDeleter>;
using CurlSlistPtr = std::unique_ptr<struct curl_slist, CurlSlistDeleter>;

class HttpClient {
public:
  struct Config {
    std::string userAgent = "XiaopengKernel/0.1.0";
    std::chrono::milliseconds defaultTimeout{30000};
    std::chrono::milliseconds connectTimeout{10000};
    bool followRedirects = true;
    int maxRedirects = 20;
    bool verifySsl = true;
    std::string caBundlePath;
    std::string proxyUrl;
    std::string proxyUsername;
    std::string proxyPassword;
    bool enableHttp2 = true;
    bool enableCompression = true;
    size_t maxResponseSize = 100 * 1024 * 1024;
  };

  HttpClient() { CurlGlobal::instance(); }

  explicit HttpClient(const Config &config) : config_(config) {
    CurlGlobal::instance();
  }

  Future<Result<HttpResponse>> executeAsync(const HttpRequest &request) {
    return std::async(std::launch::async,
                      [this, request]() { return execute(request); });
  }

  Result<HttpResponse> execute(const HttpRequest &request) {
    CurlEasyPtr curl(curl_easy_init());
    if (!curl) {
      return Result<HttpResponse>(ErrorCode::NetworkError,
                                  "Failed to initialize CURL");
    }

    HttpResponse response;
    response.receivedAt = std::chrono::steady_clock::now();

    auto startTime = std::chrono::steady_clock::now();

    struct curl_slist *rawHeaders = nullptr;
    // We use raw pointer for building, then wrap it.
    // Or better: wrap it and release/reset?
    // curl_slist_append returns the NEW list head.

    // Let's keep using raw pointer for setup, then wrap immediately.
    if (!setupRequest(curl.get(), request, rawHeaders)) {
      if (rawHeaders)
        curl_slist_free_all(rawHeaders);
      return Result<HttpResponse>(ErrorCode::HttpRequestError,
                                  "Failed to setup request");
    }
    CurlSlistPtr requestHeaders(rawHeaders); // Takes ownership

    // Pass the headers to curl AGAIN?
    // update: setupRequest calls curl_easy_setopt(..., CURLOPT_HTTPHEADER,
    // outHeaders); So curl easy handle "knows" about the list. We just need to
    // keep it alive until perform is done, and free it essentially.

    if (!setupResponseHandlers(curl.get(), response)) {
      return Result<HttpResponse>(ErrorCode::HttpRequestError,
                                  "Failed to setup response handlers");
    }

    CURLcode res = curl_easy_perform(curl.get());

    // requestHeaders goes out of scope and frees list safely.

    auto endTime = std::chrono::steady_clock::now();
    response.totalTime =
        std::chrono::duration_cast<Duration>(endTime - startTime);

    if (res != CURLE_OK) {
      return Result<HttpResponse>(mapCurlError(res), curl_easy_strerror(res));
    }

    if (!extractResponseInfo(curl.get(), response)) {
      return Result<HttpResponse>(ErrorCode::HttpResponseError,
                                  "Failed to extract response info");
    }

    return response;
  }

  Result<HttpResponse> get(const Url &url) {
    return execute(HttpRequest::get(url));
  }

  Result<HttpResponse> post(const Url &url, const ByteBuffer &body) {
    return execute(HttpRequest::post(url, body));
  }

  Result<HttpResponse> post(const Url &url, const std::string &body) {
    return execute(HttpRequest::post(url, body));
  }

  Result<HttpResponse> postJson(const Url &url, const std::string &json) {
    return execute(HttpRequest::postJson(url, json));
  }

  Result<HttpResponse> put(const Url &url, const ByteBuffer &body) {
    return execute(HttpRequest::put(url, body));
  }

  Result<HttpResponse> del(const Url &url) {
    return execute(HttpRequest::del(url));
  }

  Result<HttpResponse> head(const Url &url) {
    return execute(HttpRequest::head(url));
  }

  void setConfig(const Config &config) { config_ = config; }

  const Config &config() const { return config_; }

  void setCookieJar(Ptr<CookieJar> jar) { cookieJar_ = std::move(jar); }

  Ptr<CookieJar> cookieJar() const { return cookieJar_; }

private:
  bool setupRequest(CURL *curl, const HttpRequest &request,
                    struct curl_slist *&outHeaders) {
    std::string urlStr = request.url.toString();
    curl_easy_setopt(curl, CURLOPT_URL, urlStr.c_str());

    // GET/HEAD/POST use libcurl's native options. CURLOPT_CUSTOMREQUEST only
    // replaces the method string: a custom "HEAD" still waits for a body
    // (hanging until the timeout) and a custom "POST" keeps POST across a
    // 303 redirect instead of switching to GET.
    switch (request.method) {
    case HttpMethod::Get:
      curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
      break;
    case HttpMethod::Head:
      curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
      break;
    case HttpMethod::Post:
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      if (request.body.empty()) {
        // Without POSTFIELDS libcurl would read the body from stdin
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
      }
      break;
    default:
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST,
                       methodToString(request.method).c_str());
      break;
    }

    if (!request.body.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                       static_cast<long>(request.body.size()));
    }

    outHeaders = nullptr;

    for (const auto &[name, value] : request.headers.all()) {
      std::string header = name + ": " + value;
      outHeaders = curl_slist_append(outHeaders, header.c_str());
    }

    if (!request.headers.has("user-agent")) {
      outHeaders = curl_slist_append(
          outHeaders, ("User-Agent: " + config_.userAgent).c_str());
    }

    if (!request.headers.has("accept-encoding") && config_.enableCompression) {
      outHeaders =
          curl_slist_append(outHeaders, "Accept-Encoding: gzip, deflate, br");
    }

    if (!request.headers.has("host")) {
      std::string host = request.url.host();
      if (request.url.port() != request.url.defaultPort()) {
        host += ":" + std::to_string(request.url.port());
      }
      outHeaders = curl_slist_append(outHeaders, ("Host: " + host).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, outHeaders);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request.timeout.count());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                     config_.connectTimeout.count());

    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,
                     request.followRedirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, request.maxRedirects);

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, request.verifySsl ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, request.verifySsl ? 2L : 0L);

    if (!config_.caBundlePath.empty()) {
      curl_easy_setopt(curl, CURLOPT_CAINFO, config_.caBundlePath.c_str());
    }

    if (!config_.proxyUrl.empty()) {
      curl_easy_setopt(curl, CURLOPT_PROXY, config_.proxyUrl.c_str());
      if (!config_.proxyUsername.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME,
                         config_.proxyUsername.c_str());
        curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD,
                         config_.proxyPassword.c_str());
      }
    }

    if (config_.enableHttp2) {
      curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    }

    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    return true;
  }

  bool setupResponseHandlers(CURL *curl, HttpResponse &response) {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

    return true;
  }

  bool extractResponseInfo(CURL *curl, HttpResponse &response) {
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    response.statusCode = static_cast<HttpStatusCode>(httpCode);

    char *effectiveUrl = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
    if (effectiveUrl) {
      auto urlResult = Url::parse(effectiveUrl);
      if (urlResult.isOk()) {
        response.finalUrl = urlResult.unwrap();
      }
    }

    double totalTime = 0;
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
    response.totalTime =
        std::chrono::milliseconds(static_cast<long>(totalTime * 1000));

    double namelookupTime = 0;
    curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &namelookupTime);
    response.dnsTime =
        std::chrono::milliseconds(static_cast<long>(namelookupTime * 1000));

    double connectTime = 0;
    curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connectTime);
    response.connectTime =
        std::chrono::milliseconds(static_cast<long>(connectTime * 1000));

    double appconnectTime = 0;
    curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &appconnectTime);
    response.sslTime = std::chrono::milliseconds(
        static_cast<long>((appconnectTime - connectTime) * 1000));

    double downloadTime = 0;
    curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &downloadTime);
    response.downloadTime = std::chrono::milliseconds(
        static_cast<long>((downloadTime - appconnectTime) * 1000));

    response.statusMessage = getStatusMessage(response.statusCode);

    return true;
  }

  static size_t writeCallback(char *ptr, size_t size, size_t nmemb,
                              void *userdata) {
    size_t totalSize = size * nmemb;
    auto *body = static_cast<ByteBuffer *>(userdata);
    body->insert(body->end(), ptr, ptr + totalSize);
    return totalSize;
  }

  static size_t headerCallback(char *ptr, size_t size, size_t nmemb,
                               void *userdata) {
    size_t totalSize = size * nmemb;
    auto *headers = static_cast<HttpHeaders *>(userdata);

    std::string_view header(ptr, totalSize);

    // With FOLLOWLOCATION every hop's headers arrive here; a new status line
    // starts a new response, so only the final response's headers remain
    // (otherwise get("Content-Type") returned the redirect's value)
    if (header.size() >= 5 && header.substr(0, 5) == "HTTP/") {
      headers->clear();
      return totalSize;
    }

    auto colonPos = header.find(':');
    if (colonPos != std::string_view::npos) {
      std::string name(header.substr(0, colonPos));
      std::string value(header.substr(colonPos + 1));

      while (!value.empty() &&
             (value.front() == ' ' || value.front() == '\t')) {
        value.erase(0, 1);
      }
      while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
      }

      headers->add(name, value);
    }

    return totalSize;
  }

  static ErrorCode mapCurlError(CURLcode code) {
    switch (code) {
    case CURLE_OK:
      return ErrorCode::Success;
    case CURLE_UNSUPPORTED_PROTOCOL:
      return ErrorCode::UrlInvalidScheme;
    case CURLE_URL_MALFORMAT:
      return ErrorCode::UrlParseError;
    case CURLE_COULDNT_RESOLVE_HOST:
      return ErrorCode::DnsResolutionFailed;
    case CURLE_COULDNT_CONNECT:
      return ErrorCode::ConnectionFailed;
    case CURLE_OPERATION_TIMEDOUT:
      return ErrorCode::ConnectionTimeout;
    case CURLE_SSL_CONNECT_ERROR:
      return ErrorCode::SslHandshakeFailed;
    case CURLE_SSL_CACERT:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
      return ErrorCode::SslCertificateError;
    case CURLE_TOO_MANY_REDIRECTS:
      return ErrorCode::HttpRedirectLimitExceeded;
    case CURLE_GOT_NOTHING:
    case CURLE_HTTP_RETURNED_ERROR:
      return ErrorCode::HttpResponseError;
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
      return ErrorCode::NetworkError;
    default:
      return ErrorCode::NetworkError;
    }
  }

  Config config_;
  Ptr<CookieJar> cookieJar_;
};

} // namespace loader
} // namespace xiaopeng
