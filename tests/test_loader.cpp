#include "test_framework.hpp"
#include <loader/loader_all.hpp>

using namespace xiaopeng::loader;

TEST(UrlParse_HttpsUrl) {
  auto result = Url::parse(
      "https://www.example.com:443/path/to/resource?query=value#fragment");

  EXPECT_TRUE(result.isOk());

  auto &url = result.unwrap();
  EXPECT_TRUE(url.isValid());
  EXPECT_EQ(UrlScheme::Https, url.scheme());
  EXPECT_STREQ("www.example.com", url.host().c_str());
  EXPECT_EQ(443, url.port());
  EXPECT_STREQ("/path/to/resource", url.path().c_str());
  EXPECT_STREQ("query=value", url.query().c_str());
  EXPECT_STREQ("fragment", url.fragment().c_str());
  EXPECT_TRUE(url.isSecure());
}

TEST(UrlParse_HttpUrl) {
  auto result = Url::parse("http://example.com/path");

  EXPECT_TRUE(result.isOk());

  auto &url = result.unwrap();
  EXPECT_EQ(UrlScheme::Http, url.scheme());
  EXPECT_EQ(80, url.port());
  EXPECT_FALSE(url.isSecure());
}

TEST(UrlParse_InvalidUrl) {
  auto result = Url::parse("not a valid url");
  EXPECT_TRUE(result.isErr());
}

TEST(UrlParse_Origin) {
  auto result = Url::parse("https://example.com:8080/path");
  EXPECT_TRUE(result.isOk());

  auto &url = result.unwrap();
  EXPECT_STREQ("https://example.com:8080", url.origin().c_str());
}

TEST(UrlParse_UserInfo) {
  auto result = Url::parse("https://user:pass@example.com:8080/path");
  EXPECT_TRUE(result.isOk());

  auto &url = result.unwrap();
  EXPECT_STREQ("user:pass", url.userinfo().c_str());
  EXPECT_STREQ("example.com", url.host().c_str());
}

TEST(UrlParse_SameOrigin) {
  auto url1 = Url::parse("https://example.com/page1").unwrap();
  auto url2 = Url::parse("https://example.com/page2").unwrap();
  auto url3 = Url::parse("https://other.com/page").unwrap();

  EXPECT_TRUE(url1.sameOrigin(url2));
  EXPECT_FALSE(url1.sameOrigin(url3));
}

TEST(UrlParse_Resolve) {
  auto baseUrl = Url::parse("https://example.com/path/to/page.html").unwrap();

  auto resolved1 = baseUrl.resolve("../other.html");
  EXPECT_STREQ("/path/other.html", resolved1.path().c_str());

  auto resolved2 = baseUrl.resolve("/absolute");
  EXPECT_STREQ("/absolute", resolved2.path().c_str());

  auto resolved3 = baseUrl.resolve("https://other.com/page");
  EXPECT_STREQ("other.com", resolved3.host().c_str());
}

TEST(UrlEncode_Encode) {
  std::string encoded = Url::encode("hello world");
  EXPECT_STREQ("hello%20world", encoded.c_str());

  encoded = Url::encode("a=b&c=d");
  EXPECT_STREQ("a%3Db%26c%3Dd", encoded.c_str());
}

TEST(UrlEncode_Decode) {
  std::string decoded = Url::decode("hello%20world");
  EXPECT_STREQ("hello world", decoded.c_str());

  decoded = Url::decode("a%3Db%26c%3Dd");
  EXPECT_STREQ("a=b&c=d", decoded.c_str());
}

// ── Regression: URL parsing / resolution ─────────────────────

TEST(UrlParse_SeparatorsInsideFragments) {
  {
    // '/' after the query is part of the query, not the path
    auto url = Url::parse("https://x.com?r=/home").unwrap();
    EXPECT_STREQ("/", url.path().c_str());
    EXPECT_STREQ("r=/home", url.query().c_str());
  }
  {
    // '?' after '#' belongs to the fragment
    auto url = Url::parse("http://a/p#f?q").unwrap();
    EXPECT_STREQ("/p", url.path().c_str());
    EXPECT_TRUE(url.query().empty());
    EXPECT_STREQ("f?q", url.fragment().c_str());
  }
  {
    auto url = Url::parse("http://a:8080/p?q=1#f").unwrap();
    EXPECT_EQ(8080, url.port());
    EXPECT_STREQ("/p", url.path().c_str());
    EXPECT_STREQ("q=1", url.query().c_str());
    EXPECT_STREQ("f", url.fragment().c_str());
  }
}

TEST(UrlParse_PortValidation) {
  EXPECT_TRUE(Url::parse("http://a:99999/").isErr());
  EXPECT_TRUE(Url::parse("http://a:-1/").isErr());
  EXPECT_TRUE(Url::parse("http://a:80abc/").isErr());
  auto empty = Url::parse("http://a:/x");
  EXPECT_TRUE(empty.isOk());
  EXPECT_EQ(80, empty.unwrap().port());
  EXPECT_EQ(0, Url::parse("http://a:0/").unwrap().port());
}

TEST(UrlParse_UserInfoAndIpv6) {
  {
    auto url = Url::parse("http://u@x@h/p").unwrap();
    EXPECT_STREQ("u@x", url.userinfo().c_str());
    EXPECT_STREQ("h", url.host().c_str());
  }
  {
    // brackets stay so serialization round-trips
    auto url = Url::parse("http://[::1]:8080/x").unwrap();
    EXPECT_STREQ("[::1]", url.host().c_str());
    EXPECT_EQ(8080, url.port());
    EXPECT_STREQ("http://[::1]:8080/x", url.toString().c_str());
    EXPECT_STREQ("http://[::1]:8080", url.origin().c_str());
  }
}

TEST(UrlDecode_InvalidEscapes) {
  EXPECT_STREQ("%4G", Url::decode("%4G").c_str());
  EXPECT_STREQ("%", Url::decode("%").c_str());
  EXPECT_STREQ("%4", Url::decode("%4").c_str());
  EXPECT_STREQ("A", Url::decode("%41").c_str());
}

TEST(UrlResolve_Rfc3986ReferenceExamples) {
  auto base = Url::parse("http://a/b/c/d;p?q").unwrap();
  auto check = [&](const std::string &ref, const std::string &path,
                   const std::string &query) {
    auto r = base.resolve(ref);
    EXPECT_TRUE(r.isValid());
    EXPECT_STREQ(path.c_str(), r.path().c_str());
    EXPECT_STREQ(query.c_str(), r.query().c_str());
  };

  check("g", "/b/c/g", "");
  check("./g", "/b/c/g", "");
  check("g/", "/b/c/g/", "");
  check("/g", "/g", "");
  check("?y", "/b/c/d;p", "y");
  check("g?y", "/b/c/g", "y");
  check("#s", "/b/c/d;p", "q");
  check("", "/b/c/d;p", "q");
  check(".", "/b/c/", "");
  check("..", "/b/", "");
  check("../g", "/b/g", "");
  check("../../../g", "/g", "");
  check("g;x?y", "/b/c/g;x", "y");

  auto fragment = base.resolve("#s");
  EXPECT_STREQ("s", fragment.fragment().c_str());

  auto network = base.resolve("//g/x");
  EXPECT_STREQ("g", network.host().c_str());
  EXPECT_STREQ("/x", network.path().c_str());
}

TEST(HttpHeaders_SetAndGet) {
  HttpHeaders headers;
  headers.set("Content-Type", "application/json");

  auto value = headers.get("content-type");
  EXPECT_TRUE(value.has_value());
  EXPECT_STREQ("application/json", value->c_str());

  value = headers.get("CONTENT-TYPE");
  EXPECT_TRUE(value.has_value());
}

TEST(HttpHeaders_MultipleValues) {
  HttpHeaders headers;
  headers.add("Set-Cookie", "a=1");
  headers.add("Set-Cookie", "b=2");

  auto values = headers.getAll("set-cookie");
  EXPECT_EQ(2, values.size());
  EXPECT_STREQ("a=1", values[0].c_str());
  EXPECT_STREQ("b=2", values[1].c_str());
}

TEST(HttpHeaders_Remove) {
  HttpHeaders headers;
  headers.set("X-Custom", "value");
  EXPECT_TRUE(headers.has("X-Custom"));

  headers.remove("X-Custom");
  EXPECT_FALSE(headers.has("X-Custom"));
}

TEST(HttpHeaders_ContentLength) {
  HttpHeaders headers;
  headers.setContentLength(12345);

  EXPECT_EQ(12345, headers.contentLength());
}

TEST(HttpRequest_Get) {
  auto url = Url::parse("https://example.com/api").unwrap();
  auto request = HttpRequest::get(url);

  EXPECT_EQ(HttpMethod::Get, request.method);
  EXPECT_TRUE(request.body.empty());
}

TEST(HttpRequest_Post) {
  auto url = Url::parse("https://example.com/api").unwrap();
  auto request = HttpRequest::postJson(url, R"({"key": "value"})");

  EXPECT_EQ(HttpMethod::Post, request.method);
  EXPECT_FALSE(request.body.empty());
  EXPECT_TRUE(request.headers.contentType().find("application/json") !=
              std::string::npos);
}

TEST(HttpRequest_ChainedConfig) {
  auto url = Url::parse("https://example.com").unwrap();
  auto request = HttpRequest::get(url)
                     .withHeader("X-Custom", "value")
                     .withTimeout(std::chrono::milliseconds(5000))
                     .withFollowRedirects(false);

  EXPECT_TRUE(request.headers.has("X-Custom"));
  EXPECT_EQ(5000, request.timeout.count());
  EXPECT_FALSE(request.followRedirects);
}

TEST(HttpStatusCode_Classification) {
  EXPECT_TRUE(isSuccess(HttpStatusCode::Ok));
  EXPECT_TRUE(isSuccess(HttpStatusCode::Created));
  EXPECT_FALSE(isSuccess(HttpStatusCode::BadRequest));

  EXPECT_TRUE(isRedirect(HttpStatusCode::MovedPermanently));
  EXPECT_TRUE(isRedirect(HttpStatusCode::Found));

  EXPECT_TRUE(isClientError(HttpStatusCode::NotFound));
  EXPECT_TRUE(isServerError(HttpStatusCode::InternalServerError));
}

TEST(Cache_PutAndGet) {
  HttpCache cache;

  auto url = Url::parse("https://example.com/resource").unwrap();

  HttpResponse response;
  response.statusCode = HttpStatusCode::Ok;
  response.body = ByteBuffer{'t', 'e', 's', 't'};
  response.headers.setContentType("text/plain");
  response.headers.set("Cache-Control", "max-age=3600");

  cache.put(url, response);

  EXPECT_EQ(1, cache.count());

  auto cached = cache.get(url);
  EXPECT_TRUE(cached.has_value());
  EXPECT_TRUE(cached->isFresh());
  EXPECT_EQ(4, cached->data.size());
}

TEST(Cache_Remove) {
  HttpCache cache;

  auto url = Url::parse("https://example.com/resource").unwrap();

  HttpResponse response;
  response.statusCode = HttpStatusCode::Ok;
  response.body = ByteBuffer{'d', 'a', 't', 'a'};
  response.headers.set("Cache-Control", "max-age=3600");

  cache.put(url, response);
  EXPECT_EQ(1, cache.count());

  cache.remove(url);
  EXPECT_EQ(0, cache.count());

  auto cached = cache.get(url);
  EXPECT_FALSE(cached.has_value());
}

TEST(Cache_Clear) {
  HttpCache cache;

  for (int i = 0; i < 5; ++i) {
    auto url =
        Url::parse("https://example.com/resource" + std::to_string(i)).unwrap();

    HttpResponse response;
    response.statusCode = HttpStatusCode::Ok;
    response.body = ByteBuffer{'d', 'a', 't', 'a'};
    response.headers.set("Cache-Control", "max-age=3600");

    cache.put(url, response);
  }

  EXPECT_EQ(5, cache.count());

  cache.clear();
  EXPECT_EQ(0, cache.count());
}

TEST(Security_SameOriginPolicy) {
  SameOriginPolicy policy;

  auto https1 = Url::parse("https://example.com/page1").unwrap();
  auto https2 = Url::parse("https://example.com/page2").unwrap();
  auto http = Url::parse("http://example.com/page").unwrap();
  auto other = Url::parse("https://other.com/page").unwrap();

  EXPECT_TRUE(policy.isSameOrigin(https1, https2));
  EXPECT_FALSE(policy.isSameOrigin(https1, http));
  EXPECT_FALSE(policy.isSameOrigin(https1, other));
}

TEST(Security_MixedContent) {
  SameOriginPolicy policy;

  auto httpsPage = Url::parse("https://example.com").unwrap();
  auto httpResource = Url::parse("http://example.com/resource.js").unwrap();
  auto httpsResource = Url::parse("https://example.com/resource.js").unwrap();

  EXPECT_TRUE(policy.isMixedContent(httpsPage, httpResource));
  EXPECT_FALSE(policy.isMixedContent(httpsPage, httpsResource));
}

TEST(Security_Origin) {
  auto url = Url::parse("https://example.com:8080/path").unwrap();
  Origin origin(url);

  EXPECT_TRUE(origin.isValid());
  EXPECT_STREQ("https", origin.scheme().c_str());
  EXPECT_STREQ("example.com", origin.host().c_str());
  EXPECT_EQ(8080, origin.port());
  EXPECT_STREQ("https://example.com:8080", origin.toString().c_str());
}

TEST(Security_OriginSameOrigin) {
  Origin o1(Url::parse("https://example.com").unwrap());
  Origin o2(Url::parse("https://example.com/page").unwrap());
  Origin o3(Url::parse("http://example.com").unwrap());

  EXPECT_TRUE(o1.sameOrigin(o2));
  EXPECT_FALSE(o1.sameOrigin(o3));
}

TEST(ResourceHandler_HtmlHandler) {
  HtmlHandler handler;

  EXPECT_TRUE(handler.canHandle(ResourceType::Html));
  EXPECT_FALSE(handler.canHandle(ResourceType::Css));
}

TEST(ResourceHandler_CssHandler) {
  CssHandler handler;

  EXPECT_TRUE(handler.canHandle(ResourceType::Css));
  EXPECT_FALSE(handler.canHandle(ResourceType::JavaScript));
}

TEST(ResourceHandler_JavaScriptHandler) {
  JavaScriptHandler handler;

  EXPECT_TRUE(handler.canHandle(ResourceType::JavaScript));
  EXPECT_FALSE(handler.canHandle(ResourceType::Html));
}

TEST(ResourceHandler_ImageHandler) {
  ImageHandler handler;

  EXPECT_TRUE(handler.canHandle(ResourceType::Image));
  EXPECT_FALSE(handler.canHandle(ResourceType::Html));
}

TEST(ResourceHandler_ImageFormatDetection) {
  ByteBuffer pngData = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  auto info = ImageHandler::detectImageFormat(pngData);
  EXPECT_STREQ("png", info.format.c_str());

  ByteBuffer jpegData = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46};
  info = ImageHandler::detectImageFormat(jpegData);
  EXPECT_STREQ("jpeg", info.format.c_str());

  ByteBuffer gifData = {'G', 'I', 'F', '8', '7', 'a'};
  info = ImageHandler::detectImageFormat(gifData);
  EXPECT_STREQ("gif", info.format.c_str());
}

TEST(ResourceHandler_Registry) {
  ResourceHandlerRegistry registry;
  registry.registerHandler(std::make_shared<HtmlHandler>());
  registry.registerHandler(std::make_shared<CssHandler>());

  EXPECT_TRUE(registry.getHandler(ResourceType::Html) != nullptr);
  EXPECT_TRUE(registry.getHandler(ResourceType::Css) != nullptr);
  EXPECT_TRUE(registry.getHandler(ResourceType::Image) == nullptr);
}

TEST(ResourceType_Detection) {
  EXPECT_EQ(ResourceType::Html, detectResourceType("text/html"));
  EXPECT_EQ(ResourceType::Css, detectResourceType("text/css"));
  EXPECT_EQ(ResourceType::JavaScript,
            detectResourceType("application/javascript"));
  EXPECT_EQ(ResourceType::Image, detectResourceType("image/png"));
  EXPECT_EQ(ResourceType::Json, detectResourceType("application/json"));
}

TEST(ResourceType_DetectionFromUrl) {
  EXPECT_EQ(ResourceType::Html,
            detectResourceTypeFromUrl(
                Url::parse("https://example.com/page.html").unwrap()));
  EXPECT_EQ(ResourceType::Css,
            detectResourceTypeFromUrl(
                Url::parse("https://example.com/style.css").unwrap()));
  EXPECT_EQ(ResourceType::JavaScript,
            detectResourceTypeFromUrl(
                Url::parse("https://example.com/script.js").unwrap()));
  EXPECT_EQ(ResourceType::Image,
            detectResourceTypeFromUrl(
                Url::parse("https://example.com/image.png").unwrap()));
}

TEST(Error_ErrorCode) {
  LoaderException ex(ErrorCode::ConnectionFailed, "Test error");

  EXPECT_EQ(ErrorCode::ConnectionFailed, ex.code());
  EXPECT_STREQ("Test error", ex.what());
}

TEST(Error_Result_Ok) {
  Result<int> result(42);

  EXPECT_TRUE(result.isOk());
  EXPECT_FALSE(result.isErr());
  EXPECT_EQ(42, result.unwrap());
}

TEST(Error_Result_Err) {
  Result<int> result(ErrorCode::ConnectionFailed, "Connection failed");

  EXPECT_TRUE(result.isErr());
  EXPECT_FALSE(result.isOk());
  EXPECT_EQ(ErrorCode::ConnectionFailed, result.error());
}

TEST(Error_Result_Map) {
  Result<int> result(42);

  auto mapped =
      result.map<std::string>([](int x) { return std::to_string(x); });

  EXPECT_TRUE(mapped.isOk());
  EXPECT_STREQ("42", mapped.unwrap().c_str());
}

TEST(Error_Result_Void) {
  Result<void> success;
  EXPECT_TRUE(success.isOk());

  Result<void> failure(ErrorCode::Unknown, "Error");
  EXPECT_TRUE(failure.isErr());
  EXPECT_EQ(ErrorCode::Unknown, failure.error());
}

TEST(ConnectionPool_AcquireAndRelease) {
  ConnectionPool pool;

  auto url = Url::parse("https://example.com").unwrap();

  auto conn1 = pool.acquire(url);
  EXPECT_TRUE(conn1 != nullptr);
  EXPECT_TRUE(conn1->inUse);

  auto conn2 = pool.acquire(url);
  EXPECT_TRUE(conn2 != nullptr);
  EXPECT_NE(conn1->id, conn2->id);

  pool.release(conn1);
  EXPECT_FALSE(conn1->inUse);
}

TEST(ConnectionPool_Clear) {
  ConnectionPool pool;

  auto url = Url::parse("https://example.com").unwrap();

  auto conn = pool.acquire(url);
  pool.release(conn);

  EXPECT_GT(pool.totalConnections(), 0);

  pool.clear();
  EXPECT_EQ(0, pool.totalConnections());
}

TEST(LoadScheduler_StartStop) {
  LoadScheduler scheduler;

  EXPECT_FALSE(scheduler.isRunning());

  scheduler.start();
  EXPECT_TRUE(scheduler.isRunning());

  scheduler.stop();
  EXPECT_FALSE(scheduler.isRunning());
}

TEST(LoadScheduler_Stats) {
  LoadScheduler scheduler;

  auto stats = scheduler.stats();
  EXPECT_EQ(0, stats.totalTasks);
  EXPECT_EQ(0, stats.completedTasks);
  EXPECT_EQ(0, stats.failedTasks);
}

TEST(Loader_Initialization) {
  LoaderConfig config;
  config.userAgent = "TestAgent/1.0";

  auto &loader = Loader::instance();
  loader.initialize(config);

  EXPECT_TRUE(loader.isInitialized());
  EXPECT_STREQ("TestAgent/1.0", loader.config().userAgent.c_str());
}

TEST(Loader_CacheOperations) {
  auto &loader = Loader::instance();

  // Reset for test
  loader.clearCache();
  EXPECT_EQ(0, loader.cacheCount());

  loader.setCacheEnabled(true);
  loader.setMaxCacheSize(1024 * 1024);

  loader.clearCache();
  EXPECT_EQ(0, loader.cacheCount());
}

TEST(Loader_SchedulerStats) {
  auto &loader = Loader::instance();

  auto stats = loader.schedulerStats();
  // Use GE because other tests might have run tasks
  EXPECT_GE(stats.totalTasks, 0);
}

// ── Regression: caching, CORS, CSP ───────────────────────────

TEST(Cache_FreshnessRules) {
  auto url = Url::parse("https://example.com/no-cache").unwrap();
  {
    HttpResponse response;
    response.statusCode = HttpStatusCode::Ok;
    response.body = ByteBuffer{'x'};
    response.headers.set("Cache-Control", "no-cache");
    HttpCache cache;
    cache.put(url, response);
    auto cached = cache.get(url);
    ASSERT_TRUE(cached.has_value());
    EXPECT_FALSE(cached->isFresh());
    EXPECT_TRUE(cached->needsRevalidation());
  }
  {
    // max-age=0 is stale right away
    HttpResponse response;
    response.statusCode = HttpStatusCode::Ok;
    response.body = ByteBuffer{'x'};
    response.headers.set("Cache-Control", "max-age=0");
    HttpCache cache;
    cache.put(url, response);
    EXPECT_FALSE(cache.get(url)->isFresh());
  }
  {
    // no freshness information and no validator: do not trust it for an hour
    HttpResponse response;
    response.statusCode = HttpStatusCode::Ok;
    response.body = ByteBuffer{'x'};
    HttpCache cache;
    cache.put(url, response);
    EXPECT_FALSE(cache.get(url)->isFresh());
  }
  {
    // Expires in the past is stale
    HttpResponse response;
    response.statusCode = HttpStatusCode::Ok;
    response.body = ByteBuffer{'x'};
    response.headers.set("Expires", "Sun, 06 Nov 1994 08:49:37 GMT");
    HttpCache cache;
    cache.put(url, response);
    EXPECT_FALSE(cache.get(url)->isFresh());
  }
  {
    // Age consumes the freshness lifetime
    HttpResponse response;
    response.statusCode = HttpStatusCode::Ok;
    response.body = ByteBuffer{'x'};
    response.headers.set("Cache-Control", "max-age=60");
    response.headers.set("Age", "120");
    HttpCache cache;
    cache.put(url, response);
    EXPECT_FALSE(cache.get(url)->isFresh());
  }
  {
    // ... but a normal max-age is fresh
    HttpResponse response;
    response.statusCode = HttpStatusCode::Ok;
    response.body = ByteBuffer{'x'};
    response.headers.set("Cache-Control", "max-age=60");
    response.headers.set("Age", "10");
    HttpCache cache;
    cache.put(url, response);
    EXPECT_TRUE(cache.get(url)->isFresh());
  }
}

TEST(HttpHeaders_BasicAuthIsBase64) {
  HttpHeaders headers;
  headers.setBasicAuth("Aladdin", "open sesame");
  EXPECT_STREQ("Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==",
               headers.get("authorization").value_or("").c_str());

  HttpHeaders one;
  one.setBasicAuth("a", "b"); // "a:b" → YTpi
  EXPECT_STREQ("Basic YTpi", one.get("authorization").value_or("").c_str());
}

TEST(Cors_CredentialsAndExactTokens) {
  auto origin = Origin::parse("https://app.example.com");
  CorsChecker checker;
  HttpRequest request;

  {
    HttpResponse wildcard;
    wildcard.headers.set("Access-Control-Allow-Origin", "*");
    // "*" cannot be used for a credentialed request
    EXPECT_TRUE(checker.checkActualRequest(request, wildcard, origin, true) ==
                CorsResult::Failure);
    EXPECT_TRUE(checker.checkActualRequest(request, wildcard, origin) ==
                CorsResult::Success);
  }
  {
    HttpResponse exact;
    exact.headers.set("Access-Control-Allow-Origin",
                      "https://app.example.com");
    EXPECT_TRUE(checker.checkActualRequest(request, exact, origin, true) ==
                CorsResult::Failure); // missing Allow-Credentials
    exact.headers.set("Access-Control-Allow-Credentials", "true");
    EXPECT_TRUE(checker.checkActualRequest(request, exact, origin, true) ==
                CorsResult::Success);
  }
  {
    // Method and header lists are exact, not substrings
    HttpResponse preflight;
    preflight.headers.set("Access-Control-Allow-Origin",
                          "https://app.example.com");
    preflight.headers.set("Access-Control-Allow-Methods", "PUT");
    preflight.headers.set("Access-Control-Allow-Headers", "x-api-key-id");
    HttpRequest ask;
    ask.headers.set("Access-Control-Request-Method", "P");
    EXPECT_TRUE(checker.checkPreflight(ask, preflight, origin) ==
                CorsResult::Failure);
    HttpRequest put;
    put.headers.set("Access-Control-Request-Method", "PUT");
    EXPECT_TRUE(checker.checkPreflight(put, preflight, origin) ==
                CorsResult::Success);
    HttpRequest headers;
    headers.headers.set("Access-Control-Request-Headers", "x-api-key");
    EXPECT_TRUE(checker.checkPreflight(headers, preflight, origin) ==
                CorsResult::Failure);
    HttpRequest exactHeader;
    exactHeader.headers.set("Access-Control-Request-Headers", "x-api-key-id");
    EXPECT_TRUE(checker.checkPreflight(exactHeader, preflight, origin) ==
                CorsResult::Success);
  }
}

TEST(Csp_SourceMatching) {
  auto self = Url::parse("https://site.example.com/page").unwrap();
  auto sameOrigin = Url::parse("https://site.example.com/app.js").unwrap();
  auto otherOrigin = Url::parse("https://evil.example.com/app.js").unwrap();
  auto cdn = Url::parse("https://x.cdn.example.com/lib.js").unwrap();
  auto bareDomain = Url::parse("https://cdn.example.com/lib.js").unwrap();
  auto plainHttp = Url::parse("http://site.example.com/app.js").unwrap();

  ContentSecurity cs;
  cs.parse("script-src 'self'");
  // 'self' needs the document origin; without it nothing matches
  EXPECT_FALSE(cs.isScriptAllowed(sameOrigin));
  cs.setSelfOrigin(Origin(self));
  EXPECT_TRUE(cs.isScriptAllowed(sameOrigin));
  EXPECT_FALSE(cs.isScriptAllowed(otherOrigin));
  EXPECT_FALSE(cs.isScriptAllowed(plainHttp));

  ContentSecurity wildcard;
  wildcard.parse("script-src *.cdn.example.com");
  EXPECT_TRUE(wildcard.isScriptAllowed(cdn));
  EXPECT_FALSE(wildcard.isScriptAllowed(bareDomain));

  ContentSecurity scheme;
  scheme.parse("script-src https:");
  EXPECT_TRUE(scheme.isScriptAllowed(cdn));
  EXPECT_FALSE(scheme.isScriptAllowed(plainHttp));

  ContentSecurity host;
  host.parse("script-src https://site.example.com");
  EXPECT_TRUE(host.isScriptAllowed(sameOrigin));
  EXPECT_FALSE(host.isScriptAllowed(cdn));

  ContentSecurity port;
  port.parse("script-src https://site.example.com:8443");
  EXPECT_FALSE(port.isScriptAllowed(sameOrigin)); // default port 443

  ContentSecurity path;
  path.parse("script-src https://site.example.com/assets/");
  EXPECT_TRUE(path.isScriptAllowed(
      Url::parse("https://site.example.com/assets/app.js").unwrap()));
  EXPECT_FALSE(
      path.isScriptAllowed(Url::parse("https://site.example.com/other.js").unwrap()));
}

int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "    XiaopengKernel Loader Unit Tests" << std::endl;
  std::cout << "========================================" << std::endl;

  return xiaopeng::test::runTests();
}
