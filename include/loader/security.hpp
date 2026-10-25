#pragma once

#include "error.hpp"
#include "http_message.hpp"
#include "types.hpp"
#include "url.hpp"
#include <regex>
#include <set>

namespace xiaopeng {
namespace loader {

enum class CorsMode { NoCors, Cors, SameOrigin };

enum class CorsCredentials { Omit, SameOrigin, Include };

enum class CorsResult { Success, Failure, NotCors };

struct SecurityPolicy {
  bool allowMixedContent = false;
  bool blockInsecureRequests = true;
  bool upgradeInsecureRequests = true;
  bool enforceSameOrigin = true;
  bool enforceCors = true;
  std::set<std::string> trustedSchemes{"http", "https", "data",
                                       "blob", "file",  "about"};
  std::set<std::string> blockedHosts;
  std::set<std::string> allowedOrigins;
};

class Origin {
public:
  Origin() = default;
  explicit Origin(const Url &url) {
    if (url.isValid()) {
      scheme_ = url.schemeString();
      host_ = url.host();
      port_ = url.port();
      valid_ = true;
    }
  }

  static Origin parse(const std::string &origin) {
    auto urlResult = Url::parse(origin);
    if (urlResult.isOk()) {
      return Origin(urlResult.unwrap());
    }
    return Origin();
  }

  static Origin opaque() {
    Origin o;
    o.opaque_ = true;
    return o;
  }

  bool isValid() const { return valid_; }
  bool isOpaque() const { return opaque_; }

  const std::string &scheme() const { return scheme_; }
  const std::string &host() const { return host_; }
  std::uint16_t port() const { return port_; }

  std::string toString() const {
    if (!valid_ || opaque_)
      return "null";
    std::string result = scheme_ + "://" + host_;
    if (port_ != 0 && port_ != defaultPort()) {
      result += ":" + std::to_string(port_);
    }
    return result;
  }

  bool sameOrigin(const Origin &other) const {
    if (!valid_ || !other.valid_)
      return false;
    if (opaque_ || other.opaque_)
      return false;
    return scheme_ == other.scheme_ && host_ == other.host_ &&
           port_ == other.port_;
  }

  bool sameOriginDomain(const Origin &other) const { return sameOrigin(other); }

  bool isSecure() const { return scheme_ == "https" || scheme_ == "wss"; }

  bool operator==(const Origin &other) const {
    return toString() == other.toString();
  }

  bool operator!=(const Origin &other) const { return !(*this == other); }

  bool operator<(const Origin &other) const {
    return toString() < other.toString();
  }

private:
  std::uint16_t defaultPort() const {
    if (scheme_ == "http" || scheme_ == "ws")
      return 80;
    if (scheme_ == "https" || scheme_ == "wss")
      return 443;
    return 0;
  }

  std::string scheme_;
  std::string host_;
  std::uint16_t port_ = 0;
  bool valid_ = false;
  bool opaque_ = false;
};

class SameOriginPolicy {
public:
  explicit SameOriginPolicy(const SecurityPolicy &policy = SecurityPolicy{})
      : policy_(policy) {}

  bool isSameOrigin(const Url &a, const Url &b) const {
    return Origin(a).sameOrigin(Origin(b));
  }

  bool isSameOrigin(const Origin &a, const Origin &b) const {
    return a.sameOrigin(b);
  }

  bool isCrossOrigin(const Url &a, const Url &b) const {
    return !isSameOrigin(a, b);
  }

  Result<void> checkSameOrigin(const Url &documentUrl,
                               const Url &resourceUrl) const {
    if (!policy_.enforceSameOrigin) {
      return Result<void>();
    }

    if (isSameOrigin(documentUrl, resourceUrl)) {
      return Result<void>();
    }

    return Result<void>(ErrorCode::SameOriginViolation,
                        "Same-origin policy violation: " +
                            resourceUrl.toString());
  }

  bool isAllowed(const Url &documentUrl, const Url &resourceUrl,
                 ResourceType resourceType) const {
    if (!policy_.enforceSameOrigin) {
      return true;
    }

    if (isSameOrigin(documentUrl, resourceUrl)) {
      return true;
    }

    if (resourceType == ResourceType::Image ||
        resourceType == ResourceType::Css ||
        resourceType == ResourceType::Media) {
      return true;
    }

    return false;
  }

  bool isMixedContent(const Url &documentUrl, const Url &resourceUrl) const {
    Origin docOrigin(documentUrl);
    Origin resOrigin(resourceUrl);

    if (!docOrigin.isSecure()) {
      return false;
    }

    return !resOrigin.isSecure();
  }

  Result<void> checkMixedContent(const Url &documentUrl,
                                 const Url &resourceUrl) const {
    if (!isMixedContent(documentUrl, resourceUrl)) {
      return Result<void>();
    }

    if (policy_.allowMixedContent) {
      return Result<void>();
    }

    return Result<void>(
        ErrorCode::MixedContentBlocked,
        "Mixed content blocked: insecure resource on secure page");
  }

  Url upgradeInsecureUrl(const Url &url) const {
    if (!policy_.upgradeInsecureRequests) {
      return url;
    }

    if (url.scheme() == UrlScheme::Http) {
      auto httpsUrl = url.toString();
      httpsUrl.replace(0, 4, "https");
      auto result = Url::parse(httpsUrl);
      if (result.isOk()) {
        return result.unwrap();
      }
    }

    return url;
  }

  bool isBlockedHost(const std::string &host) const {
    return policy_.blockedHosts.find(host) != policy_.blockedHosts.end();
  }

  bool isAllowedOrigin(const Origin &origin) const {
    if (policy_.allowedOrigins.empty()) {
      return true;
    }

    std::string originStr = origin.toString();
    return policy_.allowedOrigins.find(originStr) !=
           policy_.allowedOrigins.end();
  }

  void setPolicy(const SecurityPolicy &policy) { policy_ = policy; }

  const SecurityPolicy &policy() const { return policy_; }

private:
  SecurityPolicy policy_;
};

struct CorsConfig {
  std::string allowOrigin;
  bool allowCredentials = false;
  std::string allowMethods;
  std::string allowHeaders;
  std::string exposeHeaders;
  std::optional<std::chrono::seconds> maxAge;
};

class CorsChecker {
public:
  explicit CorsChecker(const SecurityPolicy &policy = SecurityPolicy{})
      : policy_(policy) {}

  // credentials: whether the request's credentials mode is "include"
  // (RFC 7234/WHATWG Fetch: "*" is not allowed then, and ACAC must be true)
  CorsResult checkPreflight(const HttpRequest &request,
                            const HttpResponse &response,
                            const Origin &requestOrigin,
                            bool credentials = false) const {
    if (!policy_.enforceCors) {
      return CorsResult::NotCors;
    }

    auto allowOrigin = response.headers.get("access-control-allow-origin");
    if (!allowOrigin) {
      return CorsResult::Failure;
    }

    const std::string origin = requestOrigin.toString();
    if (*allowOrigin != "*" && *allowOrigin != origin) {
      return CorsResult::Failure;
    }
    if (credentials && *allowOrigin == "*") {
      return CorsResult::Failure;
    }

    if (requestOrigin.toString() != "null") {
      auto allowCredentials =
          response.headers.get("access-control-allow-credentials");
      bool credentialsAllowed =
          allowCredentials && (*allowCredentials == "true");

      if (credentialsAllowed && *allowOrigin == "*") {
        return CorsResult::Failure;
      }
    }

    auto requestMethod = request.headers.get("access-control-request-method");
    if (requestMethod) {
      auto allowMethods = response.headers.get("access-control-allow-methods");
      if (!allowMethods) {
        return CorsResult::Failure;
      }

      if (!isMethodAllowed(*requestMethod, *allowMethods)) {
        return CorsResult::Failure;
      }
    }

    auto requestHeaders = request.headers.get("access-control-request-headers");
    if (requestHeaders) {
      auto allowHeaders = response.headers.get("access-control-allow-headers");
      if (!allowHeaders) {
        return CorsResult::Failure;
      }

      if (!areHeadersAllowed(*requestHeaders, *allowHeaders)) {
        return CorsResult::Failure;
      }
    }

    return CorsResult::Success;
  }

  CorsResult checkActualRequest(const HttpRequest &request,
                                const HttpResponse &response,
                                const Origin &requestOrigin,
                                bool credentials = false) const {
    (void)request;
    if (!policy_.enforceCors) {
      return CorsResult::NotCors;
    }

    auto allowOrigin = response.headers.get("access-control-allow-origin");
    if (!allowOrigin) {
      return CorsResult::Failure;
    }

    const std::string origin = requestOrigin.toString();
    if (*allowOrigin != "*" && *allowOrigin != origin) {
      return CorsResult::Failure;
    }

    // A wildcard cannot be used with credentials
    if (credentials) {
      if (*allowOrigin == "*")
        return CorsResult::Failure;
      auto allowCredentials =
          response.headers.get("access-control-allow-credentials");
      if (!allowCredentials || *allowCredentials != "true")
        return CorsResult::Failure;
    }

    auto varyHeader = response.headers.get("vary");
    if (varyHeader && varyHeader->find("Origin") != std::string::npos) {
      if (*allowOrigin != requestOrigin.toString() && *allowOrigin != "*") {
        return CorsResult::Failure;
      }
    }

    return CorsResult::Success;
  }

  bool needsPreflight(const HttpRequest &request, const Origin &requestOrigin,
                      const Origin &targetOrigin) const {
    if (requestOrigin.sameOrigin(targetOrigin)) {
      return false;
    }

    if (request.method != HttpMethod::Get &&
        request.method != HttpMethod::Head &&
        request.method != HttpMethod::Post) {
      return true;
    }

    if (request.headers.has("content-type")) {
      auto contentType = request.headers.contentType();
      if (!isSimpleContentType(contentType)) {
        return true;
      }
    }

    for (const auto &[name, value] : request.headers.all()) {
      if (!isSimpleHeader(name)) {
        return true;
      }
    }

    return false;
  }

  HttpRequest createPreflightRequest(const Url &url, const Origin &origin,
                                     const HttpRequest &actualRequest) const {
    HttpRequest preflight = HttpRequest::options(url);

    preflight.headers.set("Origin", origin.toString());
    preflight.headers.set("Access-Control-Request-Method",
                          methodToString(actualRequest.method));

    std::string headers;
    for (const auto &[name, value] : actualRequest.headers.all()) {
      if (!isSimpleHeader(name)) {
        if (!headers.empty())
          headers += ", ";
        headers += name;
      }
    }

    if (!headers.empty()) {
      preflight.headers.set("Access-Control-Request-Headers", headers);
    }

    return preflight;
  }

  void setPolicy(const SecurityPolicy &policy) { policy_ = policy; }

private:
  static bool isSimpleHeader(const std::string &name) {
    static const std::set<std::string> simpleHeaders = {
        "accept", "accept-language", "content-language", "content-type",
        "dpr",    "downlink",        "save-data",        "viewport-width",
        "width"};

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    return simpleHeaders.find(lower) != simpleHeaders.end();
  }

  static bool isSimpleContentType(const std::string &contentType) {
    static const std::set<std::string> simpleTypes = {
        "application/x-www-form-urlencoded", "multipart/form-data",
        "text/plain"};

    std::string lower = contentType;
    auto pos = lower.find(';');
    if (pos != std::string::npos) {
      lower = lower.substr(0, pos);
    }

    while (!lower.empty() && (lower.back() == ' ' || lower.back() == '\t')) {
      lower.pop_back();
    }

    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    return simpleTypes.find(lower) != simpleTypes.end();
  }

  static bool isMethodAllowed(const std::string &method,
                              const std::string &allowed) {
    if (allowed == "*")
      return true;

    std::string methodLower = method;
    std::transform(methodLower.begin(), methodLower.end(), methodLower.begin(),
                   ::tolower);

    std::istringstream ss(allowed);
    std::string token;
    while (std::getline(ss, token, ',')) {
      trimAsciiWhitespace(token);
      std::transform(token.begin(), token.end(), token.begin(), ::tolower);
      if (token == methodLower)
        return true;
    }
    return false;
  }

  static void trimAsciiWhitespace(std::string &s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
      s.erase(0, 1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
      s.pop_back();
  }

  static bool areHeadersAllowed(const std::string &headers,
                                const std::string &allowed) {
    if (allowed == "*")
      return true;

    // Exact (case-insensitive) list membership: a substring test let
    // "x-api-key-id" allow "x-api-key"
    std::set<std::string> allowedNames;
    std::istringstream as(allowed);
    std::string name;
    while (std::getline(as, name, ',')) {
      trimAsciiWhitespace(name);
      std::transform(name.begin(), name.end(), name.begin(), ::tolower);
      allowedNames.insert(name);
    }

    std::istringstream ss(headers);
    std::string header;
    while (std::getline(ss, header, ',')) {
      trimAsciiWhitespace(header);
      std::transform(header.begin(), header.end(), header.begin(), ::tolower);

      if (allowedNames.find(header) == allowedNames.end()) {
        return false;
      }
    }

    return true;
  }

  SecurityPolicy policy_;
};

class ContentSecurity {
public:
  struct Directive {
    std::string name;
    std::vector<std::string> values;
  };

  ContentSecurity() = default;

  // Origin of the protected document, used by the 'self' source and by
  // scheme-less host sources
  void setSelfOrigin(const Origin &origin) { selfOrigin_ = origin; }
  const Origin &selfOrigin() const { return selfOrigin_; }

  void parse(const std::string &csp) {
    directives_.clear();

    std::istringstream ss(csp);
    std::string directive;

    while (std::getline(ss, directive, ';')) {
      while (!directive.empty() &&
             (directive.front() == ' ' || directive.front() == '\t')) {
        directive.erase(0, 1);
      }
      while (!directive.empty() &&
             (directive.back() == ' ' || directive.back() == '\t')) {
        directive.pop_back();
      }

      if (directive.empty())
        continue;

      auto spacePos = directive.find(' ');
      Directive d;
      d.name = directive.substr(0, spacePos);

      std::transform(d.name.begin(), d.name.end(), d.name.begin(), ::tolower);

      if (spacePos != std::string::npos) {
        std::string values = directive.substr(spacePos + 1);
        std::istringstream vs(values);
        std::string value;
        while (vs >> value) {
          d.values.push_back(value);
        }
      }

      directives_.push_back(std::move(d));
    }
  }

  bool isAllowed(const Url &url, const std::string &directiveName) const {
    auto directive = findDirective(directiveName);
    if (!directive) {
      auto defaultSrc = findDirective("default-src");
      if (defaultSrc) {
        return matchesSource(url, defaultSrc->values);
      }
      return true;
    }

    return matchesSource(url, directive->values);
  }

  bool isScriptAllowed(const Url &url) const {
    return isAllowed(url, "script-src");
  }

  bool isStyleAllowed(const Url &url) const {
    return isAllowed(url, "style-src");
  }

  bool isImageAllowed(const Url &url) const {
    return isAllowed(url, "img-src");
  }

  bool isConnectAllowed(const Url &url) const {
    return isAllowed(url, "connect-src");
  }

  bool isFontAllowed(const Url &url) const {
    return isAllowed(url, "font-src");
  }

  bool isMediaAllowed(const Url &url) const {
    return isAllowed(url, "media-src");
  }

  bool isFrameAllowed(const Url &url) const {
    return isAllowed(url, "frame-src");
  }

private:
  const Directive *findDirective(const std::string &name) const {
    for (const auto &d : directives_) {
      if (d.name == name) {
        return &d;
      }
    }
    return nullptr;
  }

  bool matchesSource(const Url &url,
                     const std::vector<std::string> &sources) const {
    for (const auto &source : sources) {
      if (matchesSource(url, source)) {
        return true;
      }
    }
    return false;
  }

  // CSP3 §6.7.2 source expression matching
  bool matchesSource(const Url &url, const std::string &source) const {
    if (source == "*")
      return true;
    if (source == "'none'")
      return false;
    if (source == "'self'") {
      // Without a document origin nothing can be proven same-origin
      return selfOrigin_.isValid() && Origin(url).sameOrigin(selfOrigin_);
    }
    if (!source.empty() && source[0] == '\'')
      return false; // 'unsafe-inline', 'unsafe-eval', 'nonce-…', hashes
    if (source.empty())
      return false;

    // Scheme source, e.g. "https:"
    if (source.back() == ':') {
      return url.schemeString() == source.substr(0, source.size() - 1);
    }

    // Host source: [scheme://]host[:port][/path], host may be "*.domain"
    std::string rest = source;
    std::string scheme;
    size_t schemeEnd = rest.find("://");
    if (schemeEnd != std::string::npos) {
      scheme = rest.substr(0, schemeEnd);
      rest = rest.substr(schemeEnd + 3);
    }
    std::string hostPart = rest;
    std::string pathPrefix;
    size_t slash = rest.find('/');
    if (slash != std::string::npos) {
      hostPart = rest.substr(0, slash);
      pathPrefix = rest.substr(slash);
    }

    // Scheme-less sources use the document's scheme (https upgrades http)
    if (scheme.empty()) {
      if (selfOrigin_.isValid()) {
        const std::string &selfScheme = selfOrigin_.scheme();
        if (url.schemeString() != selfScheme &&
            !(selfScheme == "http" && url.schemeString() == "https"))
          return false;
      }
    } else if (url.schemeString() != scheme) {
      return false;
    }

    std::string host = hostPart;
    std::string port;
    size_t colon = hostPart.rfind(':');
    if (colon != std::string::npos) {
      host = hostPart.substr(0, colon);
      port = hostPart.substr(colon + 1);
    }

    std::string urlHost = url.host();
    std::transform(urlHost.begin(), urlHost.end(), urlHost.begin(), ::tolower);
    std::transform(host.begin(), host.end(), host.begin(), ::tolower);

    bool hostMatches = false;
    if (host.size() > 2 && host[0] == '*' && host[1] == '.') {
      // "*.example.com" matches subdomains only, not example.com itself
      const std::string suffix = host.substr(1); // ".example.com"
      hostMatches = urlHost.size() > suffix.size() &&
                    urlHost.compare(urlHost.size() - suffix.size(),
                                    suffix.size(), suffix) == 0;
    } else {
      hostMatches = urlHost == host;
    }
    if (!hostMatches)
      return false;

    if (port.empty()) {
      if (url.port() != url.defaultPort())
        return false;
    } else {
      unsigned long value = 0;
      try {
        value = std::stoul(port);
      } catch (...) {
        return false;
      }
      if (value != url.port())
        return false;
    }

    if (!pathPrefix.empty() && pathPrefix != "/") {
      if (url.path().compare(0, pathPrefix.size(), pathPrefix) != 0)
        return false;
      // A prefix not ending in '/' must end on a path segment boundary
      if (pathPrefix.back() != '/' && url.path().size() > pathPrefix.size() &&
          url.path()[pathPrefix.size()] != '/')
        return false;
    }

    return true;
  }

  std::vector<Directive> directives_;
  Origin selfOrigin_;
};

} // namespace loader
} // namespace xiaopeng
