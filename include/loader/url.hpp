#pragma once

#include "error.hpp"
#include "types.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace xiaopeng {
namespace loader {

enum class UrlScheme {
  Http,
  Https,
  Ftp,
  Ws,
  Wss,
  Data,
  Blob,
  File,
  About,
  Unknown
};

class Url {
public:
  Url() = default;

  explicit Url(const std::string &url) { parseInternal(url); }

  static Result<Url> parse(const std::string &url) {
    Url result;
    if (result.parseInternal(url)) {
      return result;
    }
    return Result<Url>(ErrorCode::UrlParseError, "Failed to parse URL: " + url);
  }

  static Url fromParts(UrlScheme scheme, const std::string &host,
                       std::uint16_t port, const std::string &path = "/",
                       const std::string &query = "",
                       const std::string &fragment = "",
                       const std::string &userinfo = "") {
    Url url;
    url.scheme_ = scheme;
    url.host_ = host;
    url.port_ = port;
    url.path_ = path.empty() ? "/" : path;
    url.query_ = query;
    url.fragment_ = fragment;
    url.userinfo_ = userinfo;
    url.valid_ = true;
    return url;
  }

  bool isValid() const { return valid_; }

  UrlScheme scheme() const { return scheme_; }
  const std::string &schemeString() const { return schemeStr_; }
  const std::string &host() const { return host_; }
  std::uint16_t port() const { return port_; }
  std::uint16_t defaultPort() const {
    switch (scheme_) {
    case UrlScheme::Http:
      return 80;
    case UrlScheme::Https:
      return 443;
    case UrlScheme::Ftp:
      return 21;
    case UrlScheme::Ws:
      return 80;
    case UrlScheme::Wss:
      return 443;
    default:
      return 0;
    }
  }
  const std::string &path() const { return path_; }
  const std::string &query() const { return query_; }
  const std::string &fragment() const { return fragment_; }
  const std::string &userinfo() const { return userinfo_; }

  std::string authority() const {
    std::string result;
    if (!userinfo_.empty()) {
      result += userinfo_ + "@";
    }
    result += host_;
    if (port_ != defaultPort() && port_ != 0) {
      result += ":" + std::to_string(port_);
    }
    return result;
  }

  std::string pathWithQuery() const {
    std::string result = path_;
    if (!query_.empty()) {
      result += "?" + query_;
    }
    return result;
  }

  std::string toString() const {
    if (!valid_)
      return "";

    std::string result = schemeStr_ + "://";
    result += authority();
    result += path_;

    if (!query_.empty()) {
      result += "?" + query_;
    }
    if (!fragment_.empty()) {
      result += "#" + fragment_;
    }

    return result;
  }

  std::string origin() const {
    if (!valid_)
      return "";

    // Origin only includes scheme, host, and port (if not default)
    std::string result = schemeStr_ + "://" + host_;
    if (port_ != defaultPort() && port_ != 0) {
      result += ":" + std::to_string(port_);
    }
    return result;
  }

  bool isSecure() const {
    return scheme_ == UrlScheme::Https || scheme_ == UrlScheme::Wss;
  }

  bool sameOrigin(const Url &other) const {
    if (!valid_ || !other.valid_)
      return false;
    return scheme_ == other.scheme_ && host_ == other.host_ &&
           port_ == other.port_;
  }

  Url resolve(const std::string &relative) const {
    if (!valid_)
      return Url();

    // Absolute URL (has a scheme)
    auto parsed = Url::parse(relative);
    if (parsed.isOk())
      return parsed.unwrap();

    // RFC 3986 §5.2.2: split the reference into path, query and fragment
    // first; resolving the whole string as a path folds "?y" into the path
    std::string ref = relative;
    std::string refQuery;
    std::string refFragment;
    size_t hash = ref.find('#');
    const bool hasFragment = hash != std::string::npos;
    if (hasFragment) {
      refFragment = ref.substr(hash + 1);
      ref.erase(hash);
    }
    size_t question = ref.find('?');
    const bool hasQuery = question != std::string::npos;
    if (hasQuery) {
      refQuery = ref.substr(question + 1);
      ref.erase(question);
    }

    Url result = *this;
    result.fragment_.clear();

    if (ref.size() >= 2 && ref[0] == '/' && ref[1] == '/') {
      // Network-path reference: keep the scheme, replace the authority
      Url temp(schemeStr_ + ":" + ref + (hasQuery ? "?" + refQuery : "") +
               (hasFragment ? "#" + refFragment : ""));
      return temp;
    }

    if (ref.empty()) {
      // "" and "#s" keep the base path and query; "?y" replaces the query
      if (hasQuery)
        result.query_ = refQuery;
    } else {
      if (ref[0] == '/') {
        result.path_ = removeDotSegments(ref);
      } else {
        // Merge with the base path (§5.2.3)
        size_t slash = result.path_.rfind('/');
        result.path_ =
            removeDotSegments((slash == std::string::npos
                                   ? std::string()
                                   : result.path_.substr(0, slash + 1)) +
                              ref);
      }
      if (hasQuery)
        result.query_ = refQuery;
      else
        result.query_.clear();
    }

    if (hasFragment)
      result.fragment_ = refFragment;

    return result;
  }

  static std::string encode(const std::string &input) {
    std::ostringstream result;
    result.fill('0');
    result << std::hex;

    for (char c : input) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
          c == '.' || c == '~') {
        result << c;
      } else {
        result << std::uppercase;
        result << '%' << std::setw(2)
               << static_cast<int>(static_cast<unsigned char>(c));
        result << std::nouppercase;
      }
    }

    return result.str();
  }

  static std::string decode(const std::string &input) {
    std::string result;
    result.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
      // "%4G" is not an escape: decode nothing instead of turning "%4" + "G"
      // into "\x04" and swallowing the G
      if (input[i] == '%' && i + 2 < input.size() && isHexDigit(input[i + 1]) &&
          isHexDigit(input[i + 2])) {
        int value = hexValue(input[i + 1]) * 16 + hexValue(input[i + 2]);
        result += static_cast<char>(value);
        i += 2;
      } else if (input[i] == '+') {
        result += ' ';
      } else {
        result += input[i];
      }
    }

    return result;
  }

  bool operator==(const Url &other) const {
    return toString() == other.toString();
  }

  bool operator!=(const Url &other) const { return !(*this == other); }

  bool operator<(const Url &other) const {
    return toString() < other.toString();
  }

private:
  static bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  }

  static int hexValue(char c) {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    return c - 'A' + 10;
  }

  // Scheme-relative port: empty means the default, digits only, <= 65535
  bool parsePort(const std::string &text) {
    if (text.empty()) {
      port_ = defaultPort();
      return true;
    }
    if (text.size() > 5)
      return false;
    std::uint32_t value = 0;
    for (char c : text) {
      if (c < '0' || c > '9')
        return false;
      value = value * 10 + static_cast<std::uint32_t>(c - '0');
    }
    if (value > 65535)
      return false;
    port_ = static_cast<std::uint16_t>(value);
    return true;
  }

  bool parseInternal(const std::string &url) {
    valid_ = false;

    if (url.empty())
      return false;

    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
      return false;
    }

    schemeStr_ = url.substr(0, schemeEnd);
    std::transform(schemeStr_.begin(), schemeStr_.end(), schemeStr_.begin(),
                   ::tolower);
    scheme_ = parseScheme(schemeStr_);

    const size_t hostStart = schemeEnd + 3;

    // A '?' inside the fragment is not the query separator, and a '/' inside
    // the query/fragment is not the path separator. Finding them without
    // those bounds made pathEnd < pathStart and underflow the substring.
    const size_t fragmentStart = url.find('#', hostStart);
    const size_t queryLimit =
        fragmentStart == std::string::npos ? url.size() : fragmentStart;
    size_t queryStart = url.find('?', hostStart);
    if (queryStart != std::string::npos && queryStart > queryLimit)
      queryStart = std::string::npos;
    const size_t pathLimit =
        std::min(queryLimit, queryStart == std::string::npos ? url.size()
                                                             : queryStart);
    size_t pathStart = url.find('/', hostStart);
    if (pathStart != std::string::npos && pathStart > pathLimit)
      pathStart = std::string::npos;

    const size_t authorityEnd =
        pathStart != std::string::npos ? pathStart : pathLimit;
    std::string authority = url.substr(hostStart, authorityEnd - hostStart);

    // The last '@' separates userinfo from the host (WHATWG)
    size_t atPos = authority.rfind('@');
    if (atPos != std::string::npos) {
      userinfo_ = authority.substr(0, atPos);
      authority = authority.substr(atPos + 1);
    }

    host_.clear();
    port_ = defaultPort();
    if (!authority.empty() && authority[0] == '[') {
      // IPv6 literal: keep the brackets so serialization round-trips
      size_t close = authority.find(']');
      if (close == std::string::npos)
        return false;
      host_ = authority.substr(0, close + 1);
      if (close + 1 < authority.size()) {
        if (authority[close + 1] != ':' ||
            !parsePort(authority.substr(close + 2)))
          return false;
      }
    } else {
      size_t colonPos = authority.rfind(':');
      if (colonPos != std::string::npos) {
        host_ = authority.substr(0, colonPos);
        if (!parsePort(authority.substr(colonPos + 1)))
          return false;
      } else {
        host_ = authority;
      }
    }

    if (host_.empty() && scheme_ != UrlScheme::File &&
        scheme_ != UrlScheme::About) {
      return false;
    }

    if (pathStart != std::string::npos) {
      path_ = url.substr(pathStart, pathLimit - pathStart);
    } else {
      path_ = "/";
    }

    if (queryStart != std::string::npos) {
      size_t queryEnd = fragmentStart == std::string::npos ? url.size()
                                                           : fragmentStart;
      if (queryStart + 1 <= queryEnd)
        query_ = url.substr(queryStart + 1, queryEnd - queryStart - 1);
    }

    if (fragmentStart != std::string::npos) {
      fragment_ = url.substr(fragmentStart + 1);
    }

    valid_ = true;
    return true;
  }

  static UrlScheme parseScheme(const std::string &scheme) {
    if (scheme == "http")
      return UrlScheme::Http;
    if (scheme == "https")
      return UrlScheme::Https;
    if (scheme == "ftp")
      return UrlScheme::Ftp;
    if (scheme == "ws")
      return UrlScheme::Ws;
    if (scheme == "wss")
      return UrlScheme::Wss;
    if (scheme == "data")
      return UrlScheme::Data;
    if (scheme == "blob")
      return UrlScheme::Blob;
    if (scheme == "file")
      return UrlScheme::File;
    if (scheme == "about")
      return UrlScheme::About;
    return UrlScheme::Unknown;
  }

  // RFC 3986 §5.2.4 "remove_dot_segments"; keeps an existing trailing slash
  static std::string removeDotSegments(const std::string &path) {
    std::string input = path;
    std::string output;

    auto dropLastSegment = [&output]() {
      if (output.empty())
        return;
      size_t slash = output.rfind('/');
      if (slash == std::string::npos)
        output.clear();
      else
        output.erase(slash);
    };

    while (!input.empty()) {
      if (input.compare(0, 3, "../") == 0) {
        input.erase(0, 3);
      } else if (input.compare(0, 2, "./") == 0) {
        input.erase(0, 2);
      } else if (input.compare(0, 3, "/./") == 0) {
        input.erase(0, 2);
      } else if (input == "/.") {
        input = "/";
      } else if (input.compare(0, 4, "/../") == 0) {
        input.erase(0, 3);
        dropLastSegment();
      } else if (input == "/..") {
        input = "/";
        dropLastSegment();
      } else if (input == "." || input == "..") {
        input.clear();
      } else {
        size_t next = input.find('/', input[0] == '/' ? 1 : 0);
        if (next == std::string::npos) {
          output += input;
          input.clear();
        } else {
          output.append(input, 0, next);
          input.erase(0, next);
        }
      }
    }
    return output;
  }

  UrlScheme scheme_ = UrlScheme::Unknown;
  std::string schemeStr_;
  std::string host_;
  std::uint16_t port_ = 0;
  std::string path_ = "/";
  std::string query_;
  std::string fragment_;
  std::string userinfo_;
  bool valid_ = false;
};

} // namespace loader
} // namespace xiaopeng
