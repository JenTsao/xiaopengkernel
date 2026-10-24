#pragma once

#include <iostream>
#include <quickjs.h>
#include <script/js_binding.hpp>
#include <string>
#include <vector>

namespace xiaopeng {
namespace script {

class ConsoleBinding {
public:
  static void registerBinding(JSContext *ctx) {
    // Create console object
    JSValue console = JS_NewObject(ctx);

    // Add log function
    JS_SetPropertyStr(ctx, console, "log",
                      JS_NewCFunction(ctx, params_log, "log", 1));

    // Add other methods if needed (info, warn, error - map to log for now)
    JS_SetPropertyStr(ctx, console, "info",
                      JS_NewCFunction(ctx, params_log, "info", 1));
    JS_SetPropertyStr(ctx, console, "warn",
                      JS_NewCFunction(ctx, params_log, "warn", 1));
    JS_SetPropertyStr(ctx, console, "error",
                      JS_NewCFunction(ctx, params_log, "error", 1));
    JS_SetPropertyStr(ctx, console, "assert",
                      JS_NewCFunction(ctx, console_assert, "assert", 1));

    // Set global console
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "console", console);
    JS_FreeValue(ctx, global);
  }

private:
  // console.assert(condition, ...data): logs when condition is falsy and,
  // like browsers, never throws
  static JSValue console_assert(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv) {
    (void)this_val;
    if (argc >= 1 && JS_ToBool(ctx, argv[0]) > 0)
      return JS_UNDEFINED;
    std::string output = "Assertion failed";
    for (int i = 1; i < argc; ++i) {
      output += (i == 1 ? ": " : " ");
      output += JSBinding::toStdString(ctx, argv[i]);
    }
    std::cerr << "[JS Console] " << output << std::endl;
    return JS_UNDEFINED;
  }

  static JSValue params_log(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv) {
    (void)this_val;
    std::string output;
    for (int i = 0; i < argc; ++i) {
      if (i > 0)
        output += " ";
      output += JSBinding::toStdString(ctx, argv[i]);
    }
    std::cout << "[JS Console] " << output << std::endl;
    return JS_UNDEFINED;
  }
};

} // namespace script
} // namespace xiaopeng
