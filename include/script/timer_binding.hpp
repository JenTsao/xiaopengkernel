#pragma once

#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <mutex>
#include <quickjs.h>
#include <vector>


namespace xiaopeng {
namespace script {

class TimerBinding {
public:
  struct TimerEntry {
    uint32_t id;
    JSValue callback; // DupValue'd, must be freed
    std::vector<JSValue> args; // extra arguments, DupValue'd, must be freed
    JSContext *ctx;
    double fireTimeMs; // Absolute time when timer should fire
    double intervalMs; // >0 for setInterval, 0 for setTimeout
    bool cancelled;
  };

  // Register setTimeout, setInterval, clearTimeout, clearInterval on global
  static void registerBinding(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);

    JS_SetPropertyStr(ctx, global, "setTimeout",
                      JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, global, "setInterval",
                      JS_NewCFunction(ctx, js_setInterval, "setInterval", 2));
    JS_SetPropertyStr(ctx, global, "clearTimeout",
                      JS_NewCFunction(ctx, js_clearTimeout, "clearTimeout", 1));
    JS_SetPropertyStr(
        ctx, global, "clearInterval",
        JS_NewCFunction(ctx, js_clearInterval, "clearInterval", 1));

    JS_FreeValue(ctx, global);
  }

  // HTML "perform a microtask checkpoint": run every pending promise job. A
  // job that throws is reported and does not stop the remaining ones.
  static bool runMicrotasks(JSContext *ctx) {
    bool executed = false;
    JSContext *jobCtx = nullptr;
    for (;;) {
      int err = JS_ExecutePendingJob(JS_GetRuntime(ctx), &jobCtx);
      if (err == 0)
        break;
      executed = true;
      if (err < 0) {
        JSValue exc = JS_GetException(jobCtx);
        const char *str = JS_ToCString(jobCtx, exc);
        if (str) {
          std::cerr << "Microtask Exception: " << str << std::endl;
          JS_FreeCString(jobCtx, str);
        }
        JS_FreeValue(jobCtx, exc);
      }
    }
    return executed;
  }

  // Called each frame from main loop. Returns true if any timer fired.
  static bool tick() {
    double now = currentTimeMs();
    bool fired = false;

    // Collect IDs of timers to fire (avoid modifying map during iteration)
    std::vector<uint32_t> toFire;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      for (auto &pair : s_timers) {
        auto &t = pair.second;
        if (!t.cancelled && now >= t.fireTimeMs) {
          toFire.push_back(t.id);
        }
      }
    }

    for (uint32_t id : toFire) {
      // Take our own references: the callback may clearTimeout/clearInterval
      // its own id, which frees the entry's values while they still run
      JSContext *ctx = nullptr;
      JSValue fn = JS_UNDEFINED;
      std::vector<JSValue> args;
      double intervalMs = 0;
      {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_timers.find(id);
        if (it == s_timers.end() || it->second.cancelled)
          continue;
        ctx = it->second.ctx;
        fn = JS_DupValue(ctx, it->second.callback);
        for (const auto &a : it->second.args)
          args.push_back(JS_DupValue(ctx, a));
        intervalMs = it->second.intervalMs;
      }

      // The lock is released while JS runs: the callback may call
      // setTimeout/clearTimeout, which lock s_mutex themselves
      JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, static_cast<int>(args.size()),
                            args.data());
      if (JS_IsException(ret)) {
        JSValue exc = JS_GetException(ctx);
        const char *str = JS_ToCString(ctx, exc);
        if (str) {
          std::cout << "[Timer] Exception in handler (id=" << id << "): " << str
                    << std::endl;
          JS_FreeCString(ctx, str);
        }
        JS_FreeValue(ctx, exc);
      }
      JS_FreeValue(ctx, ret);
      for (auto &a : args)
        JS_FreeValue(ctx, a);
      JS_FreeValue(ctx, fn);
      fired = true;

      {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_timers.find(id);
        if (it != s_timers.end()) {
          if (intervalMs > 0 && !it->second.cancelled) {
            // Reschedule for next interval
            it->second.fireTimeMs = now + intervalMs;
          } else {
            // One-shot: remove
            release(it->second);
            s_timers.erase(it);
          }
        }
      }

      runMicrotasks(ctx);
    }

    return fired;
  }

  // Cleanup all timers (call on shutdown)
  static void cleanup() {
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto &pair : s_timers) {
      release(pair.second);
    }
    s_timers.clear();
  }

private:
  static inline std::map<uint32_t, TimerEntry> s_timers;
  static inline uint32_t s_nextId = 1;
  static inline std::mutex s_mutex;

  static void release(TimerEntry &entry) {
    JS_FreeValue(entry.ctx, entry.callback);
    for (auto &a : entry.args)
      JS_FreeValue(entry.ctx, a);
    entry.args.clear();
  }

  static double currentTimeMs() {
    using namespace std::chrono;
    return static_cast<double>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
            .count());
  }

  // setTimeout/setInterval(callback, delayMs, ...args) -> timerId
  static JSValue addTimer(JSContext *ctx, int argc, JSValueConst *argv,
                          bool repeat) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
      return JS_ThrowTypeError(ctx, "Timer callback must be a function");

    // HTML: a missing, NaN or negative timeout is 0. Intervals keep a 1 ms
    // floor so a zero interval cannot spin the event loop.
    double delay = 0;
    if (argc >= 2 && JS_ToFloat64(ctx, &delay, argv[1]) < 0)
      return JS_EXCEPTION;
    if (!(delay >= 0) || std::isinf(delay))
      delay = 0;
    if (repeat && delay < 1)
      delay = 1;

    TimerEntry entry;
    entry.id = s_nextId++;
    entry.callback = JS_DupValue(ctx, argv[0]);
    for (int i = 2; i < argc; ++i)
      entry.args.push_back(JS_DupValue(ctx, argv[i]));
    entry.ctx = ctx;
    entry.fireTimeMs = currentTimeMs() + delay;
    entry.intervalMs = repeat ? delay : 0;
    entry.cancelled = false;

    {
      std::lock_guard<std::mutex> lock(s_mutex);
      s_timers[entry.id] = entry;
    }
    return JS_NewInt32(ctx, static_cast<int32_t>(entry.id));
  }

  static JSValue js_setTimeout(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
    (void)this_val;
    return addTimer(ctx, argc, argv, false);
  }

  static JSValue js_setInterval(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
    (void)this_val;
    return addTimer(ctx, argc, argv, true);
  }

  // clearTimeout(id)
  static JSValue js_clearTimeout(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1)
      return JS_UNDEFINED;

    int32_t id = 0;
    if (JS_ToInt32(ctx, &id, argv[0]) < 0)
      return JS_EXCEPTION;

    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_timers.find(static_cast<uint32_t>(id));
    if (it != s_timers.end()) {
      it->second.cancelled = true;
      release(it->second);
      s_timers.erase(it);
    }
    return JS_UNDEFINED;
  }

  // clearInterval(id) — same as clearTimeout
  static JSValue js_clearInterval(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    return js_clearTimeout(ctx, this_val, argc, argv);
  }
};

} // namespace script
} // namespace xiaopeng
