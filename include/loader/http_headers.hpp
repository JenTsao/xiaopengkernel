#pragma once

#include "types.hpp"
#include "error.hpp"
#include "url.hpp"
#include <set>
#include <algorithm>

namespace xiaopeng {
namespace loader {

enum class HttpMethod {
    Get,
    Post,
    Put,
    Delete,
    Head,
    Options,
    Patch,
    Trace,
    Connect
};

inline std::string methodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::Get: return "GET";
        case HttpMethod::Post: return "POST";
        case HttpMethod::Put: return "PUT";
        case HttpMethod::Delete: return "DELETE";
        case HttpMethod::Head: return "HEAD";
        case HttpMethod::Options: return "OPTIONS";
        case HttpMethod::Patch: return "PATCH";
        case HttpMethod::Trace: return "TRACE";
        case HttpMethod::Connect: return "CONNECT";
        default: return "GET";
    }
}

inline HttpMethod stringToMethod(const std::string& str) {
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper == "GET") return HttpMethod::Get;
    if (upper == "POST") return HttpMethod::Post;
    if (upper == "PUT") return HttpMethod::Put;
    if (upper == "DELETE") return HttpMethod::Delete;
    if (upper == "HEAD") return HttpMethod::Head;
    if (upper == "OPTIONS") return HttpMethod::Options;
    if (upper == "PATCH") return HttpMethod::Patch;
    if (upper == "TRACE") return HttpMethod::Trace;
    if (upper == "CONNECT") return HttpMethod::Connect;
    return HttpMethod::Get;
}

class HttpHeaders {
public:
    using HeaderMap = std::multimap<std::string, std::string, std::less<>>;
    
    HttpHeaders() = default;
    
    void set(const std::string& name, const std::string& value) {
        std::string lowerName = toLower(name);
        remove(lowerName);
        headers_.emplace(std::move(lowerName), value);
    }
    
    void add(const std::string& name, const std::string& value) {
        headers_.emplace(toLower(name), value);
    }
    
    void append(const std::string& name, const std::string& value) {
        std::string lowerName = toLower(name);
        auto range = headers_.equal_range(lowerName);
        
        if (range.first != range.second) {
            auto it = std::prev(range.second);
            it->second += ", " + value;
        } else {
            headers_.emplace(std::move(lowerName), value);
        }
    }
    
    std::optional<std::string> get(const std::string& name) const {
        std::string lowerName = toLower(name);
        auto it = headers_.find(lowerName);
        if (it != headers_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    std::vector<std::string> getAll(const std::string& name) const {
        std::vector<std::string> result;
        std::string lowerName = toLower(name);
        auto range = headers_.equal_range(lowerName);
        for (auto it = range.first; it != range.second; ++it) {
            result.push_back(it->second);
        }
        return result;
    }
    
    bool has(const std::string& name) const {
        return headers_.find(toLower(name)) != headers_.end();
    }
    
    void remove(const std::string& name) {
        std::string lowerName = toLower(name);
        headers_.erase(lowerName);
    }
    
    void clear() {
        headers_.clear();
    }
    
    bool empty() const {
        return headers_.empty();
    }
    
    size_t size() const {
        return headers_.size();
    }
    
    const HeaderMap& all() const {
        return headers_;
    }
    
    std::string toString() const {
        std::string result;
        for (const auto& [name, value] : headers_) {
            result += name + ": " + value + "\r\n";
        }
        return result;
    }
    
    std::uint64_t contentLength() const {
        auto len = get("content-length");
        if (len) {
            try {
                return std::stoull(*len);
            } catch (...) {
                return 0;
            }
        }
        return 0;
    }
    
    void setContentLength(std::uint64_t length) {
        set("content-length", std::to_string(length));
    }
    
    std::string contentType() const {
        return get("content-type").value_or("");
    }
    
    void setContentType(const std::string& type) {
        set("content-type", type);
    }
    
    std::string contentEncoding() const {
        return get("content-encoding").value_or("");
    }
    
    std::string transferEncoding() const {
        return get("transfer-encoding").value_or("");
    }
    
    bool isChunked() const {
        auto encoding = transferEncoding();
        return encoding.find("chunked") != std::string::npos;
    }
    
    std::string eTag() const {
        return get("etag").value_or("");
    }
    
    std::string lastModified() const {
        return get("last-modified").value_or("");
    }
    
    std::string cacheControl() const {
        return get("cache-control").value_or("");
    }
    
    std::string location() const {
        return get("location").value_or("");
    }
    
    std::string host() const {
        return get("host").value_or("");
    }
    
    void setHost(const std::string& host) {
        set("host", host);
    }
    
    std::string userAgent() const {
        return get("user-agent").value_or("");
    }
    
    void setUserAgent(const std::string& agent) {
        set("user-agent", agent);
    }
    
    std::string accept() const {
        return get("accept").value_or("");
    }
    
    void setAccept(const std::string& accept) {
        set("accept", accept);
    }
    
    std::string acceptEncoding() const {
        return get("accept-encoding").value_or("");
    }
    
    void setAcceptEncoding(const std::string& encoding) {
        set("accept-encoding", encoding);
    }
    
    std::string authorization() const {
        return get("authorization").value_or("");
    }
    
    void setAuthorization(const std::string& auth) {
        set("authorization", auth);
    }
    
    void setBearerToken(const std::string& token) {
        set("authorization", "Bearer " + token);
    }
    
    // RFC 7617: the credentials are "user:password" in base64
    void setBasicAuth(const std::string& username, const std::string& password) {
        static const char kAlphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const std::string credentials = username + ":" + password;
        std::string encoded;
        size_t i = 0;
        for (; i + 2 < credentials.size(); i += 3) {
            uint32_t n = (static_cast<uint8_t>(credentials[i]) << 16) |
                         (static_cast<uint8_t>(credentials[i + 1]) << 8) |
                         static_cast<uint8_t>(credentials[i + 2]);
            encoded += kAlphabet[(n >> 18) & 63];
            encoded += kAlphabet[(n >> 12) & 63];
            encoded += kAlphabet[(n >> 6) & 63];
            encoded += kAlphabet[n & 63];
        }
        if (i < credentials.size()) {
            uint32_t n = static_cast<uint8_t>(credentials[i]) << 16;
            const bool two = i + 1 < credentials.size();
            if (two)
                n |= static_cast<uint8_t>(credentials[i + 1]) << 8;
            encoded += kAlphabet[(n >> 18) & 63];
            encoded += kAlphabet[(n >> 12) & 63];
            encoded += two ? kAlphabet[(n >> 6) & 63] : '=';
            encoded += '=';
        }
        set("authorization", "Basic " + encoded);
    }
    
    std::string cookie() const {
        return get("cookie").value_or("");
    }
    
    void setCookie(const std::string& cookie) {
        set("cookie", cookie);
    }
    
    std::vector<std::string> setCookie() const {
        return getAll("set-cookie");
    }
    
    std::string origin() const {
        return get("origin").value_or("");
    }
    
    void setOrigin(const std::string& origin) {
        set("origin", origin);
    }
    
    std::string referer() const {
        return get("referer").value_or("");
    }
    
    void setReferer(const std::string& referer) {
        set("referer", referer);
    }

private:
    static std::string toLower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), 
                      [](unsigned char c) { return std::tolower(c); });
        return result;
    }
    
    HeaderMap headers_;
};

struct Cookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path = "/";
    std::chrono::system_clock::time_point expires;
    bool secure = false;
    bool httpOnly = false;
    bool session = true;
    
    bool isExpired() const {
        if (session) return false;
        return std::chrono::system_clock::now() > expires;
    }
    
    std::string toString() const {
        return name + "=" + value;
    }
};

class CookieJar {
public:
    void set(const Cookie& cookie) {
        std::string key = cookie.domain + cookie.path + cookie.name;
        cookies_[std::move(key)] = cookie;
    }
    
    std::vector<Cookie> get(const std::string& domain, const std::string& path) const {
        std::vector<Cookie> result;
        for (const auto& [key, cookie] : cookies_) {
            if (!cookie.isExpired() && 
                (domain.empty() || domainMatches(domain, cookie.domain)) &&
                (path.empty() || pathMatches(path, cookie.path))) {
                result.push_back(cookie);
            }
        }
        return result;
    }
    
    std::string getCookieHeader(const std::string& domain, const std::string& path) const {
        auto cookies = get(domain, path);
        std::string result;
        for (const auto& cookie : cookies) {
            if (!result.empty()) result += "; ";
            result += cookie.toString();
        }
        return result;
    }
    
    void clear() {
        cookies_.clear();
    }
    
    void clearExpired() {
        for (auto it = cookies_.begin(); it != cookies_.end(); ) {
            if (it->second.isExpired()) {
                it = cookies_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    static bool domainMatches(const std::string& requestDomain, const std::string& cookieDomain) {
        if (requestDomain == cookieDomain) return true;
        if (!cookieDomain.empty() && cookieDomain[0] == '.') {
            return requestDomain.size() > cookieDomain.size() &&
                   requestDomain.substr(requestDomain.size() - cookieDomain.size()) == cookieDomain;
        }
        return false;
    }
    
    static bool pathMatches(const std::string& requestPath, const std::string& cookiePath) {
        if (requestPath == cookiePath) return true;
        if (requestPath.size() > cookiePath.size() &&
            requestPath.substr(0, cookiePath.size()) == cookiePath) {
            if (cookiePath.back() == '/') return true;
            if (requestPath[cookiePath.size()] == '/') return true;
        }
        return false;
    }
    
    HashMap<std::string, Cookie> cookies_;
};

}
}
