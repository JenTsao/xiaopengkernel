#include <iostream>
#include <script/script_engine.hpp>
#include <vector>
#include <dom/event_system.hpp>

namespace xiaopeng {
namespace script {

ScriptEngine::ScriptEngine() {}

ScriptEngine::~ScriptEngine() {
#ifdef ENABLE_QUICKJS
  TimerBinding::cleanup();
  EventBinding::cleanup();
  DOMBinding::cleanup();
  dom::EventSystem::setEventDispatchCallback(nullptr);
  if (m_ctx)
    JS_FreeContext(m_ctx);
  if (m_runtime)
    JS_FreeRuntime(m_runtime);
#endif
}

bool ScriptEngine::initialize() {
#ifdef ENABLE_QUICKJS
  m_runtime = JS_NewRuntime();
  if (!m_runtime) {
    std::cerr << "Failed to create QuickJS runtime" << std::endl;
    return false;
  }
  JS_SetRuntimeOpaque(m_runtime, this);

  m_ctx = JS_NewContext(m_runtime);
  if (!m_ctx) {
    std::cerr << "Failed to create QuickJS context" << std::endl;
    JS_FreeRuntime(m_runtime);
    m_runtime = nullptr;
    return false;
  }

  m_initialized = true;

  // Register Bindings
  ConsoleBinding::registerBinding(m_ctx);
  TimerBinding::registerBinding(m_ctx);
  DOMBinding::registerBinding(m_ctx);

  dom::EventSystem::setEventDispatchCallback([this](dom::NodePtr node, const std::shared_ptr<dom::Event> &event, dom::EventPhase phase) {
    if (!this->m_ctx || !node) return;
    // Only listeners registered for this phase fire (capture vs bubble),
    // otherwise ancestor handlers run twice per event.
    auto listenerIds = node->getListenerIdsForPhase(event->type(), phase);
    if (!listenerIds.empty()) {
      // Create a full JS Event wrapper that connects back to the C++ event
      JSValue eventObj = DOMBinding::wrapEvent(this->m_ctx, event);

      // Dispatch the event to the matching listeners
      EventBinding::dispatch(this->m_ctx, listenerIds, eventObj);

      // Free our reference
      JS_FreeValue(this->m_ctx, eventObj);
    }
  });

  return true;
#else
  std::cerr << "QuickJS support is disabled in build." << std::endl;
  return false;
#endif
}

void ScriptEngine::evaluate(const std::string &script,
                            const std::string &filename) {
#ifdef ENABLE_QUICKJS
  if (!m_initialized)
    return;

  JSValue val =
      JS_Eval(m_ctx, script.c_str(), script.length(), filename.c_str(), 0);

  if (JS_IsException(val)) {
    JSValue exception = JS_GetException(m_ctx);
    const char *str = JS_ToCString(m_ctx, exception);
    if (str) {
      std::cerr << "JS Exception: " << str << std::endl;
      JS_FreeCString(m_ctx, str);
    }
    JS_FreeValue(m_ctx, exception);
  } else {
    // Optional: print result?
    // const char* str = JS_ToCString(m_ctx, val);
    // if (str) {
    //    std::cout << "JS Result: " << str << std::endl;
    //    JS_FreeCString(m_ctx, str);
    // }
  }

  JS_FreeValue(m_ctx, val);
#endif
}

void ScriptEngine::registerDOM(std::shared_ptr<dom::Document> doc) {
#ifdef ENABLE_QUICKJS
  if (!m_initialized || !doc)
    return;

  JSValue docObj = DOMBinding::wrapDocument(m_ctx, doc);

  JSValue global = JS_GetGlobalObject(m_ctx);
  JS_SetPropertyStr(m_ctx, global, "document", docObj);
  JS_FreeValue(m_ctx, global);
#endif
}

bool ScriptEngine::tickTimers() {
#ifdef ENABLE_QUICKJS
  return TimerBinding::tick();
#else
  return false;
#endif
}

bool ScriptEngine::tickMicrotasks() {
#ifdef ENABLE_QUICKJS
  if (!m_initialized || !m_ctx)
    return false;

  JSContext *ctx1;
  int err;
  bool executed = false;

  for (;;) {
    err = JS_ExecutePendingJob(JS_GetRuntime(m_ctx), &ctx1);
    if (err <= 0) {
      if (err < 0) {
        // Exception
        JSValue exception = JS_GetException(ctx1);
        const char *str = JS_ToCString(ctx1, exception);
        if (str) {
          std::cerr << "Microtask Exception: " << str << std::endl;
          JS_FreeCString(ctx1, str);
        }
        JS_FreeValue(ctx1, exception);
      }
      break;
    }
    executed = true;
  }
  return executed;
#else
  return false;
#endif
}

std::vector<uint8_t>
ScriptEngine::compileToBytecode(const std::string &script,
                                const std::string &filename) {
#ifdef ENABLE_QUICKJS
  if (!m_initialized || !m_ctx)
    return {};

  JSValue obj = JS_Eval(m_ctx, script.c_str(), script.length(),
                        filename.c_str(), JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(obj)) {
    JSValue exception = JS_GetException(m_ctx);
    const char *str = JS_ToCString(m_ctx, exception);
    if (str) {
      std::cerr << "Compile Exception: " << str << std::endl;
      JS_FreeCString(m_ctx, str);
    }
    JS_FreeValue(m_ctx, exception);
    JS_FreeValue(m_ctx, obj);
    return {};
  }

  size_t out_buf_len;
  uint8_t *out_buf =
      JS_WriteObject(m_ctx, &out_buf_len, obj, JS_WRITE_OBJ_BYTECODE);
  JS_FreeValue(m_ctx, obj);

  if (!out_buf)
    return {};

  std::vector<uint8_t> bytecode(out_buf, out_buf + out_buf_len);
  js_free(m_ctx, out_buf);
  return bytecode;
#else
  return {};
#endif
}

void ScriptEngine::evaluateBytecode(const std::vector<uint8_t> &bytecode) {
#ifdef ENABLE_QUICKJS
  if (!m_initialized || bytecode.empty())
    return;

  JSValue obj = JS_ReadObject(m_ctx, bytecode.data(), bytecode.size(),
                              JS_READ_OBJ_BYTECODE);
  if (JS_IsException(obj)) {
    JSValue exception = JS_GetException(m_ctx);
    const char *str = JS_ToCString(m_ctx, exception);
    if (str) {
      std::cerr << "Bytecode Read JS Exception: " << str << std::endl;
      JS_FreeCString(m_ctx, str);
    }
    JS_FreeValue(m_ctx, exception);
    JS_FreeValue(m_ctx, obj);
    return;
  }

  JSValue val = JS_EvalFunction(m_ctx, obj); // obj is freed by JS_EvalFunction
  if (JS_IsException(val)) {
    JSValue exception = JS_GetException(m_ctx);
    const char *str = JS_ToCString(m_ctx, exception);
    if (str) {
      std::cerr << "Bytecode Exec JS Exception: " << str << std::endl;
      JS_FreeCString(m_ctx, str);
    }
    JS_FreeValue(m_ctx, exception);
  }
  JS_FreeValue(m_ctx, val);
#endif
}

} // namespace script
} // namespace xiaopeng
