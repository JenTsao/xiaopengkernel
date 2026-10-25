#pragma once

#include <atomic>
#include <condition_variable>
#include <engine/parse_task.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <css/css_parser.hpp>
#include <dom/html_parser.hpp>
#include <loader/loader.hpp>
#include <loader/url.hpp>

namespace xiaopeng {
namespace engine {

/// Background worker thread that performs HTML tokenization, tree building,
/// and CSS collection + parsing off the main thread.
class ParseThread {
public:
  ParseThread() = default;
  ~ParseThread() { stop(); }

  // Non-copyable, non-movable
  ParseThread(const ParseThread &) = delete;
  ParseThread &operator=(const ParseThread &) = delete;

  /// Start the background worker thread.
  void start() {
    if (running_.load())
      return;
    running_.store(true);
    thread_ = std::thread(&ParseThread::workerLoop, this);
    std::cout << "[ParseThread] Worker thread started" << std::endl;
  }

  /// Gracefully stop the worker thread and join.
  void stop() {
    {
      // Flip the flag under the mutex: notifying after an unlocked store can
      // land between the worker's predicate check and its wait, and the lost
      // wakeup makes join() hang forever
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_.load())
        return;
      running_.store(false);
    }
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
    std::cout << "[ParseThread] Worker thread stopped" << std::endl;
  }

  /// Submit an HTML string for asynchronous parsing.
  /// @param html          The raw HTML source to parse.
  /// @param baseUrl       Base URL for resolving relative stylesheet paths.
  /// @param enableNetwork Whether to fetch external CSS via the Loader.
  /// @return A shared_ptr to the ParseTask that will hold the result.
  std::shared_ptr<ParseTask> submit(const std::string &html,
                                    const std::string &baseUrl,
                                    bool enableNetwork) {
    auto task = std::make_shared<ParseTask>();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push({html, baseUrl, enableNetwork, task});
    }
    cv_.notify_one();
    return task;
  }

private:
  /// Internal work item queued for the worker thread.
  struct WorkItem {
    std::string html;
    std::string baseUrl;
    bool enableNetwork;
    std::shared_ptr<ParseTask> task;
  };

  void workerLoop() {
    while (running_.load()) {
      WorkItem item;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || !running_.load(); });
        if (!running_.load() && queue_.empty())
          break;
        if (queue_.empty())
          continue;
        item = std::move(queue_.front());
        queue_.pop();
      }

      // Execute the parse work
      ParseResult result = executeWork(item);
      item.task->setResult(std::move(result));
    }
  }

  /// Core parse logic — runs entirely on the worker thread.
  /// This is a pure-ish function: it reads the HTML string and produces a
  /// ParseResult. The only side-effect is network I/O for external CSS,
  /// which uses the thread-safe Loader singleton.
  static ParseResult executeWork(const WorkItem &item) {
    ParseResult result;
    result.baseUrl = item.baseUrl;

    std::cout << "[ParseThread] Parsing HTML (" << item.html.size()
              << " bytes)..." << std::endl;

    // ── Step 1: Parse HTML → DOM ─────────────────────────────────────
    auto parseResult = dom::HtmlParser::parseHtml(item.html);
    if (!parseResult.ok()) {
      result.success = false;
      result.error = "HTML parsing failed";
      return result;
    }
    result.document = parseResult.document;

    // ── Step 2: Check for <base href="..."> override ─────────────────
    std::string baseHref = dom::HtmlParser::extractBaseHref(result.document);
    if (!baseHref.empty()) {
      result.baseUrl = baseHref;
      std::cout << "[ParseThread] Base URL set from <base> tag: "
                << result.baseUrl << std::endl;
    }

    // ── Step 3: Build ResourceTree from discovered sub-resources ─────
    auto tree = std::make_shared<ResourceTree>();

    // 3a. Collect inline <style> content (no fetch needed)
    std::string inlineCss;
    auto styleElements = result.document->getElementsByTagName("style");
    for (const auto &style : styleElements) {
      inlineCss += style->textContent() + " ";
    }

    // 3b. Register external <link rel="stylesheet"> into tree
    if (item.enableNetwork) {
      auto externalUrls = dom::HtmlParser::extractStylesheets(result.document);
      for (const auto &href : externalUrls) {
        tree->addStylesheet(href);
      }
    }

    // 3c. Register scripts into tree with correct mode
    result.scripts = dom::HtmlParser::extractScriptData(result.document);
    for (const auto &script : result.scripts) {
      ScriptMode mode = ScriptMode::Classic;
      if (script.async) {
        mode = ScriptMode::Async;
      } else if (script.defer) {
        mode = ScriptMode::Defer;
      }

      if (!script.src.empty()) {
        // External script — needs fetching
        tree->addScript(script.src, mode);
      } else if (!script.content.empty()) {
        // Inline script — already available
        tree->addScript("", mode, script.content);
      }
    }

    // 3d. Register images into tree
    if (item.enableNetwork) {
      auto imageUrls = dom::HtmlParser::extractImages(result.document);
      for (const auto &src : imageUrls) {
        tree->addImage(src);
      }
    }

    // ── Step 4: Parallel fetch all external resources ────────────────
    if (item.enableNetwork && tree->size() > 0) {
      tree->fetchAll(result.baseUrl);
    }

    // ── Step 5: Build merged CSS StyleSheet ──────────────────────────
    std::string fullCss = inlineCss;
    for (const auto *cssNode : tree->stylesheets()) {
      if (cssNode->isLoaded() && !cssNode->content.empty()) {
        fullCss += cssNode->content + " ";
        std::cout << "[ParseThread] Applied external stylesheet: "
                  << cssNode->url << " (" << cssNode->content.size()
                  << " bytes)" << std::endl;
      }
    }

    if (!fullCss.empty()) {
      css::CssParser parser(fullCss);
      result.stylesheet = parser.parse();
      std::cout << "[ParseThread] Parsed " << result.stylesheet.rules.size()
                << " CSS rules" << std::endl;
    }

    // ── Step 6: Store the tree in the result ─────────────────────────
    result.resourceTree = tree;

    std::cout << "[ParseThread] Collected " << result.scripts.size()
              << " script(s) for main-thread execution" << std::endl;

    result.success = true;
    return result;
  }

  /// URL resolution helper (duplicated from BrowserEngine for thread safety).
  static std::string resolveUrl(const std::string &baseUrl,
                                const std::string &relativeUrl) {
    // Already absolute?
    if (relativeUrl.find("://") != std::string::npos) {
      return relativeUrl;
    }
    if (baseUrl.empty()) {
      return relativeUrl;
    }

    auto baseResult = loader::Url::parse(baseUrl);
    if (baseResult.isOk()) {
      auto resolved = baseResult.unwrap().resolve(relativeUrl);
      if (resolved.isValid()) {
        return resolved.toString();
      }
    }

    // Fallback: simple concatenation
    std::string base = baseUrl;
    if (!base.empty() && base.back() != '/') {
      base += '/';
    }
    return base + relativeUrl;
  }

  /// Fetch a resource over the network using the Loader singleton.
  static std::string fetchResource(const std::string &url) {
    auto loadResult = loader::Loader::instance().load(url);
    if (loadResult.isOk()) {
      auto resource = loadResult.unwrap();
      return std::string(resource->data.begin(), resource->data.end());
    }
    std::cerr << "[ParseThread] Failed to load: " << url << " - "
              << loadResult.errorMessage() << std::endl;
    return "";
  }

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<WorkItem> queue_;
  std::atomic<bool> running_{false};
};

} // namespace engine
} // namespace xiaopeng
