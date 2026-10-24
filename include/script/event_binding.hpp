#pragma once

#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef ENABLE_QUICKJS
#include <quickjs.h>
#endif

namespace xiaopeng {
namespace script {

// EventBinding manages the storage of JS callback functions for DOM events.
// It maps listener IDs to QuickJS function values and provides
// addEventListener / removeEventListener / dispatchEvent logic.
class EventBinding {
public:
#ifdef ENABLE_QUICKJS

  struct ListenerEntry {
    uint32_t id;
    JSValue callback; // DupValue'd, must be freed
    JSContext *ctx;
  };

  // Register a JS callback for an event. Returns a unique listener ID.
  static uint32_t addListener(JSContext *ctx, JSValue callback) {
    std::lock_guard<std::mutex> lock(s_mutex);
    uint32_t id = s_nextId++;
    ListenerEntry entry;
    entry.id = id;
    entry.callback = JS_DupValue(ctx, callback);
    entry.ctx = ctx;
    s_listeners[id] = entry;
    return id;
  }

  // Remove a listener by ID and free the JS value.
  static void removeListener(uint32_t id) {
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_listeners.find(id);
    if (it != s_listeners.end()) {
      JS_FreeValue(it->second.ctx, it->second.callback);
      s_listeners.erase(it);
    }
  }

  // Dispatch: call all listeners for the given IDs with an event object.
  // Listeners run with this = event.currentTarget (DOM "inner invoke").
  static void dispatch(JSContext *ctx, const std::vector<uint32_t> &listenerIds,
                       JSValue eventObj) {
    JSValue thisVal = JS_GetPropertyStr(ctx, eventObj, "currentTarget");
    if (JS_IsException(thisVal)) {
      JS_FreeValue(ctx, JS_GetException(ctx));
      thisVal = JS_UNDEFINED;
    }

    for (uint32_t id : listenerIds) {
      ListenerEntry entry;
      bool found = false;
      {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_listeners.find(id);
        if (it != s_listeners.end()) {
          entry = it->second;
          // IMPORTANT: Duplicate the value to keep it alive during the call
          // potentially outside the lock (or if removeListener is called
          // re-entrantly)
          entry.callback = JS_DupValue(ctx, entry.callback);
          found = true;
        }
      }

      if (found) {
        JSValue ret = JS_Call(ctx, entry.callback, thisVal, 1, &eventObj);
        if (JS_IsException(ret)) {
          JSValue exc = JS_GetException(ctx);
          const char *str = JS_ToCString(ctx, exc);
          if (str) {
            std::cout << "[Event] Exception in handler: " << str << std::endl;
            JS_FreeCString(ctx, str);
          }
          JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, entry.callback); // Release our local reference
      }
    }

    JS_FreeValue(ctx, thisVal);
  }

  // True when listener id was registered with exactly this function object
  static bool isSameCallback(uint32_t id, JSContext *ctx, JSValueConst fn) {
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_listeners.find(id);
    return it != s_listeners.end() &&
           JS_StrictEq(ctx, it->second.callback, fn);
  }

  // Create a simple JS event object: { type: "...", target: null }
  static JSValue createEventObject(JSContext *ctx, const std::string &type) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "type", JS_NewString(ctx, type.c_str()));
    JS_SetPropertyStr(ctx, obj, "target", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "bubbles", JS_FALSE);
    JS_SetPropertyStr(ctx, obj, "cancelable", JS_FALSE);
    return obj;
  }

  // Cleanup all listeners (call on engine shutdown)
  static void cleanup() {
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto &pair : s_listeners) {
      JS_FreeValue(pair.second.ctx, pair.second.callback);
    }
    s_listeners.clear();
    s_nextId = 1;
  }

private:
  static inline uint32_t s_nextId = 1;
  static inline std::unordered_map<uint32_t, ListenerEntry> s_listeners;
  static inline std::mutex s_mutex;

#endif // ENABLE_QUICKJS
};

} // namespace script
} // namespace xiaopeng
