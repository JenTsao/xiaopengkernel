#pragma once

#include "error.hpp"
#include "http_message.hpp"
#include "types.hpp"
#include "url.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <list>
#include <optional>
#include <shared_mutex>
#include <sstream>

namespace xiaopeng {
namespace loader {

struct CacheKey;

struct CacheEntry {
  std::string key;
  ByteBuffer data;
  HttpHeaders headers;
  std::string eTag;
  std::string lastModified;
  TimePoint createdAt;
  TimePoint expiresAt;
  std::string mimeType;
  std::uint64_t size = 0;
  int hitCount = 0;
  bool mustRevalidate = false;
  bool noStore = false;
  std::list<CacheKey>::iterator lruIt;

  bool isExpired() const {
    return std::chrono::steady_clock::now() > expiresAt;
  }

  // A response marked no-cache / must-revalidate is never usable without a
  // validation round trip, so it does not count as fresh
  bool isFresh() const { return !needsRevalidation(); }

  bool needsRevalidation() const { return mustRevalidate || isExpired(); }

  std::chrono::seconds age() const {
    auto elapsed = std::chrono::steady_clock::now() - createdAt;
    return std::chrono::duration_cast<std::chrono::seconds>(elapsed);
  }

  std::chrono::seconds remainingTtl() const {
    auto remaining = expiresAt - std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(remaining);
  }
};

struct CacheKey {
  std::string url;
  std::string varyKey;

  bool operator==(const CacheKey &other) const {
    return url == other.url && varyKey == other.varyKey;
  }
};

struct CacheKeyHash {
  size_t operator()(const CacheKey &key) const {
    size_t h = std::hash<std::string>{}(key.url);
    h ^= std::hash<std::string>{}(key.varyKey) + 0x9e3779b9 + (h << 6) +
         (h >> 2);
    return h;
  }
};

class HttpCache {
public:
  struct Config {
    size_t maxSize = 50 * 1024 * 1024;
    std::chrono::seconds defaultTtl{3600};
    std::chrono::seconds maxTtl{86400 * 30};
    bool enabled = true;
    bool ignoreNoStore = false;
  };

  HttpCache() = default;
  explicit HttpCache(const Config &config) : config_(config) {}

  std::optional<CacheEntry> get(const Url &url,
                                const std::string &varyKey = "") {
    if (!config_.enabled)
      return std::nullopt;

    CacheKey key{url.toString(), varyKey};

    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it == cache_.end()) {
      return std::nullopt;
    }

    auto &entry = it->second;

    if (entry->noStore && !config_.ignoreNoStore) {
      return std::nullopt;
    }

    entry->hitCount++;

    touchEntry(entry);

    return *entry;
  }

  void put(const Url &url, const HttpResponse &response,
           const std::string &varyKey = "") {
    if (!config_.enabled)
      return;

    if (!isCacheable(response))
      return;

    CacheKey key{url.toString(), varyKey};

    auto entry = createEntry(url, response);
    if (!entry)
      return;

    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it != cache_.end()) {
      currentSize_ -= it->second->size;
      lruList_.erase(it->second->lruIt);
    }

    while (currentSize_ + entry->size > config_.maxSize && !lruList_.empty()) {
      evictOldest();
    }

    entry->lruIt = lruList_.insert(lruList_.end(), key);
    cache_[key] = entry;
    currentSize_ += entry->size;
  }

  void remove(const Url &url, const std::string &varyKey = "") {
    CacheKey key{url.toString(), varyKey};

    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it != cache_.end()) {
      currentSize_ -= it->second->size;
      lruList_.erase(it->second->lruIt);
      cache_.erase(it);
    }
  }

  void clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_.clear();
    lruList_.clear();
    currentSize_ = 0;
  }

  void cleanup() {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    for (auto it = cache_.begin(); it != cache_.end();) {
      if (it->second->isExpired()) {
        currentSize_ -= it->second->size;
        lruList_.erase(it->second->lruIt);
        it = cache_.erase(it);
      } else {
        ++it;
      }
    }
  }

  bool has(const Url &url, const std::string &varyKey = "") const {
    CacheKey key{url.toString(), varyKey};

    std::shared_lock<std::shared_mutex> lock(mutex_);
    return cache_.find(key) != cache_.end();
  }

  size_t size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return currentSize_;
  }

  size_t count() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return cache_.size();
  }

  size_t maxSize() const { return config_.maxSize; }

  bool enabled() const { return config_.enabled; }

  void setEnabled(bool enabled) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    config_.enabled = enabled;
  }

  void setMaxSize(size_t maxSize) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    config_.maxSize = maxSize;

    while (currentSize_ > config_.maxSize && !lruList_.empty()) {
      evictOldest();
    }
  }

  HttpRequest createValidationRequest(const Url &url, const CacheEntry &entry) {
    HttpRequest request = HttpRequest::get(url);

    if (!entry.eTag.empty()) {
      request.headers.set("If-None-Match", entry.eTag);
    }

    if (!entry.lastModified.empty()) {
      request.headers.set("If-Modified-Since", entry.lastModified);
    }

    return request;
  }

  bool shouldValidate(const CacheEntry &entry) const {
    return entry.needsRevalidation();
  }

  void updateFromValidation(const Url &url, CacheEntry &entry,
                            const HttpResponse &response) {
    if (response.statusCode == HttpStatusCode::NotModified) {
      // `entry` is the caller's copy from get(); refresh the stored one
      std::unique_lock<std::shared_mutex> lock(mutex_);
      auto it = cache_.find(CacheKey{url.toString(), ""});
      if (it == cache_.end())
        return;
      auto &stored = it->second;
      stored->createdAt = std::chrono::steady_clock::now();
      stored->expiresAt = calculateExpiry(response);
      stored->hitCount++;
      touchEntry(stored);
      entry.createdAt = stored->createdAt;
      entry.expiresAt = stored->expiresAt;
      return;
    }

    if (response.ok()) {
      put(url, response);
    }
  }

private:
  bool isCacheable(const HttpResponse &response) const {
    if (!response.ok() && response.statusCode != HttpStatusCode::NotModified) {
      return false;
    }

    auto cc = response.cacheControl();
    if (cc.find("no-store") != std::string::npos && !config_.ignoreNoStore) {
      return false;
    }

    if (cc.find("private") != std::string::npos) {
      return false;
    }

    if (cc.find("no-cache") != std::string::npos) {
      return true;
    }

    return true;
  }

  Ptr<CacheEntry> createEntry(const Url &url, const HttpResponse &response) {
    auto entry = std::make_shared<CacheEntry>();

    entry->key = url.toString();
    entry->data = response.body;
    entry->headers = response.headers;
    entry->eTag = response.eTag();
    entry->lastModified = response.lastModified();
    entry->createdAt = std::chrono::steady_clock::now();
    entry->expiresAt = calculateExpiry(response);
    entry->mimeType = response.mimeType();
    entry->size = response.body.size();

    auto cc = response.cacheControl();
    entry->mustRevalidate = cc.find("must-revalidate") != std::string::npos ||
                            cc.find("proxy-revalidate") != std::string::npos ||
                            cc.find("no-cache") != std::string::npos;
    entry->noStore = cc.find("no-store") != std::string::npos;

    return entry;
  }

  TimePoint calculateExpiry(const HttpResponse &response) const {
    auto now = std::chrono::steady_clock::now();
    long long lifetime = 0; // seconds

    auto maxAge = response.maxAge();
    if (maxAge) {
      lifetime = maxAge->count();
    } else if (auto expires = response.headers.get("expires")) {
      // A present Expires wins over heuristics; an invalid date is expired
      auto when = parseHttpDate(*expires);
      lifetime = when ? *when - wallClockSeconds() : 0;
    } else {
      // Heuristic freshness (RFC 7234 §4.2.2): 10% of the time since
      // Last-Modified, bounded by the default TTL; without a validator the
      // response is stale immediately instead of being trusted for 1 hour
      auto lastModified = parseHttpDate(response.lastModified());
      if (lastModified) {
        auto age = wallClockSeconds() - *lastModified;
        lifetime = std::min<long long>(std::max<long long>(age / 10, 0),
                                       config_.defaultTtl.count());
      }
    }

    if (lifetime > 0) {
      // Age already consumed part of the freshness lifetime
      if (auto age = response.headers.get("age")) {
        try {
          lifetime -= std::stoll(*age);
        } catch (...) {
        }
      }
    }

    auto ttl = std::chrono::seconds(
        std::min<long long>(std::max<long long>(lifetime, 0),
                            config_.maxTtl.count()));
    return now + ttl;
  }

  static long long wallClockSeconds() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
  }

  // HTTP-date in IMF-fixdate form ("Sun, 06 Nov 1994 08:49:37 GMT") as Unix
  // seconds, computed without timegm/mktime (TZ independent)
  static std::optional<long long> parseHttpDate(const std::string &value) {
    std::tm tm{};
    std::istringstream ss(value);
    ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");
    if (ss.fail())
      return std::nullopt;

    int year = tm.tm_year + 1900;
    const unsigned month = static_cast<unsigned>(tm.tm_mon + 1);
    const unsigned day = static_cast<unsigned>(tm.tm_mday);
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy =
        (153 * (month + (month > 2 ? -3u : 9u)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days =
        static_cast<long long>(era) * 146097 + static_cast<long long>(doe) -
        719468;
    return days * 86400 + tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
  }

  void touchEntry(Ptr<CacheEntry> entry) {
    // Preserve the original key (move it to the back of the LRU list)
    CacheKey key = *entry->lruIt;
    lruList_.erase(entry->lruIt);
    entry->lruIt = lruList_.insert(lruList_.end(), std::move(key));
  }

  void evictOldest() {
    if (lruList_.empty())
      return;

    CacheKey key = lruList_.front();
    lruList_.pop_front();

    auto it = cache_.find(key);
    if (it != cache_.end()) {
      currentSize_ -= it->second->size;
      cache_.erase(it);
    }
  }

  Config config_;
  HashMap<CacheKey, Ptr<CacheEntry>, CacheKeyHash> cache_;
  std::list<CacheKey> lruList_;
  size_t currentSize_ = 0;
  mutable std::shared_mutex mutex_;
};

} // namespace loader
} // namespace xiaopeng
