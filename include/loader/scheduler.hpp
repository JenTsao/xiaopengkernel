#pragma once

#include "cache.hpp"
#include "connection_pool.hpp"
#include "error.hpp"
#include "http_client.hpp"
#include "resource.hpp"
#include "resource_handler.hpp"
#include "types.hpp"
#include "url.hpp"
#include <atomic>
#include <queue>
#include <shared_mutex>

namespace xiaopeng {
namespace loader {

struct LoadTask {
  std::string id;
  Url url;
  LoadOptions options;
  std::function<void(Result<Ptr<Resource>>)> callback;
  ResourcePriority priority = ResourcePriority::Medium;
  TimePoint createdAt;
  // Written under the scheduler mutex but read by the worker without it
  std::atomic<bool> cancelled{false};

  // "a < b" means a runs after b: lower priority, or same priority but newer
  bool operator<(const LoadTask &other) const {
    if (priority != other.priority) {
      return static_cast<int>(priority) > static_cast<int>(other.priority);
    }
    return createdAt > other.createdAt;
  }
};

// std::priority_queue keeps the greatest element on top, so the comparator
// is plain "less": highest priority first, FIFO within a priority
struct LoadTaskCompare {
  bool operator()(const Ptr<LoadTask> &a, const Ptr<LoadTask> &b) const {
    return *a < *b;
  }
};

struct SchedulerStats {
  std::uint64_t totalTasks = 0;
  std::uint64_t completedTasks = 0;
  std::uint64_t failedTasks = 0;
  std::uint64_t cancelledTasks = 0;
  std::uint64_t pendingTasks = 0;
  std::uint64_t activeTasks = 0;
  std::uint64_t maxConcurrent = 0;
};

class LoadScheduler {
public:
  struct Config {
    size_t maxConcurrent = 6;
    size_t maxConcurrentPerHost = 4;
    std::chrono::milliseconds taskTimeout{60000};
    bool enablePreload = true;
    bool enableLazyLoad = true;
  };

  LoadScheduler() : running_(false) {}
  explicit LoadScheduler(const Config &config)
      : config_(config), running_(false) {}

  ~LoadScheduler() { stop(); }

  void start() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (running_)
      return;

    running_ = true;

    for (size_t i = 0; i < config_.maxConcurrent; ++i) {
      workers_.emplace_back([this] { workerLoop(); });
    }
  }

  void stop() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!running_)
        return;
      running_ = false;
      cv_.notify_all();
    }

    for (auto &worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    workers_.clear();
  }

  std::string schedule(const Url &url, const LoadOptions &options,
                       std::function<void(Result<Ptr<Resource>>)> callback) {
    auto task = std::make_shared<LoadTask>();
    task->id = generateTaskId();
    task->url = url;
    task->options = options;
    task->callback = std::move(callback);
    task->priority = options.priority;
    task->createdAt = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> lock(mutex_);

    if (pendingTasks_.size() >= 1000) {
      lock.unlock();
      if (task->callback) {
        task->callback(
            Result<Ptr<Resource>>(ErrorCode::ResourceLoadFailed, "Queue full"));
      }
      return "";
    }

    pendingTasks_.push(task);
    pendingMap_[task->id] = task;
    stats_.totalTasks++;
    stats_.pendingTasks++;

    cv_.notify_one();

    return task->id;
  }

  Future<Result<Ptr<Resource>>> loadAsync(const Url &url,
                                          const LoadOptions &options = {}) {
    auto promise = std::make_shared<Promise<Result<Ptr<Resource>>>>();
    auto future = promise->get_future();

    schedule(url, options, [promise](Result<Ptr<Resource>> result) {
      promise->set_value(std::move(result));
    });

    return future;
  }

  Result<Ptr<Resource>> load(const Url &url, const LoadOptions &options = {}) {
    auto future = loadAsync(url, options);
    return future.get();
  }

  // Cancelled tasks still complete their callback with ResourceCancelled, so
  // loadAsync() futures never end with a broken promise
  bool cancel(const std::string &taskId) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto it = pendingMap_.find(taskId);
    if (it != pendingMap_.end()) {
      // Still queued: the worker that dequeues it reports the cancellation
      // and does the pending/cancelled accounting
      it->second->cancelled = true;
      pendingMap_.erase(it);
      return true;
    }

    auto activeIt = activeTasks_.find(taskId);
    if (activeIt != activeTasks_.end()) {
      activeIt->second->cancelled = true;
      return true;
    }

    return false;
  }

  void cancelAll() {
    std::vector<Ptr<LoadTask>> dropped;
    {
      std::unique_lock<std::mutex> lock(mutex_);

      while (!pendingTasks_.empty()) {
        auto task = pendingTasks_.top();
        pendingTasks_.pop();
        task->cancelled = true;
        stats_.cancelledTasks++;
        dropped.push_back(task);
      }

      for (auto &[id, task] : activeTasks_) {
        task->cancelled = true;
      }

      pendingMap_.clear();
      stats_.pendingTasks = 0;
    }

    for (auto &task : dropped) {
      notifyCancelled(task);
    }
  }

  void setHttpClient(Ptr<HttpClient> client) {
    httpClient_ = std::move(client);
  }

  void setCache(Ptr<HttpCache> cache) { cache_ = std::move(cache); }

  void setHandlerRegistry(Ptr<ResourceHandlerRegistry> registry) {
    handlerRegistry_ = std::move(registry);
  }

  SchedulerStats stats() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return stats_;
  }

  bool isRunning() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return running_;
  }

  void setMaxConcurrent(size_t max) {
    std::unique_lock<std::mutex> lock(mutex_);
    config_.maxConcurrent = max;
  }

  size_t queueSize() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return pendingTasks_.size();
  }

private:
  static void notifyCancelled(const Ptr<LoadTask> &task) {
    if (task->callback) {
      task->callback(Result<Ptr<Resource>>(ErrorCode::ResourceCancelled,
                                           "Task cancelled"));
    }
  }

  void workerLoop() {
    while (true) {
      Ptr<LoadTask> task;

      {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait(lock, [this] { return !running_ || !pendingTasks_.empty(); });

        if (!running_)
          return;

        if (pendingTasks_.empty())
          continue;

        task = pendingTasks_.top();
        pendingTasks_.pop();
        pendingMap_.erase(task->id);
        stats_.pendingTasks--;

        if (task->cancelled) {
          stats_.cancelledTasks++;
          lock.unlock();
          notifyCancelled(task);
          continue;
        }

        activeTasks_[task->id] = task;
        stats_.activeTasks++;
        stats_.maxConcurrent =
            std::max(stats_.maxConcurrent, stats_.activeTasks);
      }

      auto result = executeTask(task);

      std::unique_lock<std::mutex> lock(mutex_);
      activeTasks_.erase(task->id);
      stats_.activeTasks--;

      if (task->cancelled) {
        stats_.cancelledTasks++;
      } else if (result.isOk()) {
        stats_.completedTasks++;
      } else {
        stats_.failedTasks++;
      }

      lock.unlock();

      if (task->callback) {
        task->callback(std::move(result));
      }
    }
  }

  Result<Ptr<Resource>> executeTask(Ptr<LoadTask> task) {
    if (task->cancelled) {
      return Result<Ptr<Resource>>(ErrorCode::ResourceCancelled,
                                   "Task cancelled");
    }

    auto resource = std::make_shared<Resource>();
    resource->url = task->url;
    resource->priority = task->priority;
    resource->state = LoadState::Loading;
    resource->startTime = std::chrono::steady_clock::now();

    if (task->options.useCache && cache_) {
      auto cached = cache_->get(task->url);
      if (cached && cached->isFresh()) {
        resource->data = std::move(cached->data);
        resource->mimeType = cached->mimeType;
        resource->responseHeaders = std::move(cached->headers);
        resource->fromCache = true;
        resource->state = LoadState::Loaded;
        resource->endTime = std::chrono::steady_clock::now();
        resource->loadTime = std::chrono::duration_cast<Duration>(
            resource->endTime - resource->startTime);
        resource->size = resource->data.size();

        return resource;
      }
    }

    if (!httpClient_) {
      resource->state = LoadState::Failed;
      resource->error = ErrorCode::InvalidState;
      resource->errorMessage = "No HTTP client configured";
      return Result<Ptr<Resource>>(resource->error, resource->errorMessage);
    }

    HttpRequest request = buildRequest(task);

    auto responseResult = httpClient_->execute(request);

    if (task->cancelled) {
      resource->state = LoadState::Cancelled;
      resource->cancelled = true;
      return Result<Ptr<Resource>>(ErrorCode::ResourceCancelled,
                                   "Task cancelled");
    }

    if (responseResult.isErr()) {
      resource->state = LoadState::Failed;
      resource->error = responseResult.error();
      resource->errorMessage = responseResult.errorMessage();
      return Result<Ptr<Resource>>(resource->error, resource->errorMessage);
    }

    auto &response = responseResult.unwrap();

    // Cache before the body and headers are moved into the resource; caching
    // afterwards stored an empty body without its Cache-Control headers
    if (task->options.useCache && cache_ && response.ok() &&
        response.isCacheable()) {
      cache_->put(task->url, response);
    }

    resource->data = std::move(response.body);
    resource->mimeType = response.mimeType();
    resource->charset = response.charset();
    resource->responseHeaders = std::move(response.headers);
    resource->statusCode = response.statusCode;
    resource->size = resource->data.size();
    resource->endTime = std::chrono::steady_clock::now();
    resource->loadTime = std::chrono::duration_cast<Duration>(
        resource->endTime - resource->startTime);

    if (!response.ok()) {
      resource->state = LoadState::Failed;
      resource->error = ErrorCode::HttpResponseError;
      resource->errorMessage =
          "HTTP " + std::to_string(static_cast<int>(response.statusCode));
      return resource;
    }

    resource->type = detectResourceType(resource->mimeType);
    if (resource->type == ResourceType::Unknown) {
      resource->type = detectResourceTypeFromUrl(resource->url);
    }

    if (handlerRegistry_) {
      auto processResult = handlerRegistry_->process(*resource);
      if (processResult.isErr()) {
        resource->state = LoadState::Failed;
        resource->error = processResult.error();
        resource->errorMessage = processResult.errorMessage();
        return resource;
      }
    }

    resource->state = LoadState::Loaded;

    return resource;
  }

  HttpRequest buildRequest(Ptr<LoadTask> task) {
    HttpRequest request;
    request.url = task->url;
    request.method = task->options.method;
    request.timeout = task->options.timeout;
    request.followRedirects = task->options.followRedirects;

    for (const auto &[name, value] : task->options.headers) {
      request.headers.set(name, value);
    }

    if (!task->options.accept.empty()) {
      request.headers.setAccept(task->options.accept);
    }

    if (!task->options.acceptEncoding.empty()) {
      request.headers.setAcceptEncoding(task->options.acceptEncoding);
    }

    if (!task->options.postData.empty()) {
      request.body = task->options.postData;
    }

    return request;
  }

  static std::string generateTaskId() {
    static std::atomic<std::uint64_t> counter{0};
    return "task_" + std::to_string(++counter);
  }

  Config config_;
  std::atomic<bool> running_;

  std::priority_queue<Ptr<LoadTask>, std::vector<Ptr<LoadTask>>,
                      LoadTaskCompare>
      pendingTasks_;
  HashMap<std::string, Ptr<LoadTask>> pendingMap_;
  HashMap<std::string, Ptr<LoadTask>> activeTasks_;

  std::vector<std::thread> workers_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;

  SchedulerStats stats_;

  Ptr<HttpClient> httpClient_;
  Ptr<HttpCache> cache_;
  Ptr<ResourceHandlerRegistry> handlerRegistry_;
};

} // namespace loader
} // namespace xiaopeng
