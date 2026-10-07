#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <thread>

#include <engine/browser_engine.hpp>
#include "layout/form_controls.hpp"

namespace xiaopeng {
namespace engine {

bool BrowserEngine::initialize(const Config &cfg) {
  m_config = cfg;

  // Initialize Loader for external resource fetching
  if (m_config.enableNetworkLoading) {
    loader::Loader::instance().initialize(m_config.loaderConfig);
    std::cout << "[Engine] Loader initialized (network loading enabled)"
              << std::endl;
  }

  m_scriptEngine.setOnMutation(
      [this](dom::DirtyFlag hint) { markDirty(hint); });

  if (!m_scriptEngine.initialize()) {
    std::cerr << "[Engine] Failed to initialize ScriptEngine" << std::endl;
    return false;
  }

  if (!m_window.initialize(m_config.windowWidth, m_config.windowHeight,
                           m_config.title)) {
    std::cerr << "[Engine] Failed to initialize Window" << std::endl;
    return false;
  }

  // Choose renderer based on OpenGL availability
#ifdef ENABLE_OPENGL
  m_openglCanvas = std::make_unique<renderer::OpenGLCanvas>(m_config.windowWidth,
                                                           m_config.windowHeight);
  std::cout << "[Engine] Using OpenGL renderer" << std::endl;
#else
  m_canvas = std::make_unique<renderer::BitmapCanvas>(m_config.windowWidth,
                                                      m_config.windowHeight);
  std::cout << "[Engine] Using software renderer" << std::endl;
#endif

    // Start the background parse thread
  m_parseThread = std::make_unique<ParseThread>();
  m_parseThread->start();

  // Initialize UA Stylesheet
  std::string defaultUACss = R"(
    html, body, div, p, h1, h2, h3, h4, h5, h6, ul, ol, li, form, header, footer, nav, section, article {
        display: block;
    }
    head, style, script, link, meta, title {
        display: none;
    }
    body {
        margin: 8px;
    }
  )";
  css::CssParser uaParser(defaultUACss);
  m_uaStylesheet = uaParser.parse();

  m_initialized = true;
  return true;
}

void BrowserEngine::shutdown() {
  // Stop the parse thread first
  if (m_parseThread) {
    m_parseThread->stop();
    m_parseThread.reset();
  }
  m_pendingParse.reset();

  if (m_config.enableNetworkLoading) {
    loader::Loader::instance().shutdown();
  }

  if (m_canvas) {
    m_canvas.reset();
  }
  if (m_openglCanvas) {
    m_openglCanvas.reset();
  }
  m_initialized = false;
  m_state = EngineState::Idle;
}

void BrowserEngine::loadHTML(const std::string &html) {
  if (!m_initialized)
    return;

  std::cout << "[Engine] Loading HTML (async)..." << std::endl;

  // Set base URL from config
  m_baseUrl = m_config.baseUrl;

  // Submit parse work to the background thread 鈥?returns immediately
  m_pendingParse =
      m_parseThread->submit(html, m_baseUrl, m_config.enableNetworkLoading);
  m_state = EngineState::Loading;
  m_needsPaint = true; // Trigger a repaint to show loading state
}

void BrowserEngine::loadFile(const std::string &filepath) {
  std::ifstream file(filepath);
  if (!file.is_open()) {
    std::cerr << "[Engine] Failed to open file: " << filepath << std::endl;
    return;
  }

  // Derive base URL from absolute file path for relative resource resolution
  try {
    std::filesystem::path p = std::filesystem::absolute(filepath);
    std::string absPath = p.string();
    // Normalize path separators to / for URL compatibility
    for (auto &c : absPath) {
      if (c == '\\')
        c = '/';
    }

    // Convert to file:// URL
    m_config.baseUrl = "file:///";

    size_t lastSlash = absPath.rfind('/');
    if (lastSlash != std::string::npos) {
      m_config.baseUrl += absPath.substr(0, lastSlash + 1);
    }
    std::cout << "[Engine] Resolved base URL for file: " << m_config.baseUrl
              << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[Engine] Error resolving absolute path: " << e.what()
              << std::endl;
    m_config.baseUrl = "file:///" + filepath;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) {
    std::cerr << "[Engine] Error reading file: " << filepath << std::endl;
    return;
  }
  loadHTML(buffer.str());
}

// 鈹€鈹€鈹€ Async Parse Completion (Main Thread) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

void BrowserEngine::onParseComplete(ParseResult result) {
  if (!result.success) {
    std::cerr << "[Engine] Parse failed: " << result.error << std::endl;
    m_state = EngineState::Idle;
    return;
  }

  std::cout << "[Engine] Parse complete 鈥?integrating results on main thread"
            << std::endl;

  // Take ownership of parsed data
  m_document = std::move(result.document);
  
  // Merge UA stylesheet with document stylesheet
  m_stylesheet = m_uaStylesheet;
  m_stylesheet.rules.insert(m_stylesheet.rules.end(),
                            std::make_move_iterator(result.stylesheet.rules.begin()),
                            std::make_move_iterator(result.stylesheet.rules.end()));
                            
  m_baseUrl = result.baseUrl;
  m_resourceTree = result.resourceTree;

  // Wire up the mutation observer (main thread only)
  m_document->setMutationObserver(
      [this](dom::DirtyFlag hint) { markDirty(hint); });

  m_needsLayout = true;
  m_needsPaint = true;

  // Register DOM into script engine (main thread only 鈥?QuickJS not
  // thread-safe)
  m_scriptEngine.registerDOM(m_document);

  // Execute synchronous (classic) scripts on the main thread
  executeScripts(result.scripts);

  // Build the initial layout
  buildLayout();

  // Execute defer scripts after layout (per HTML spec)
  executeDeferScripts();

  m_state = EngineState::Ready;
  std::cout << "[Engine] Document ready" << std::endl;
}

// 鈹€鈹€鈹€ Script Execution (Main Thread) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

void BrowserEngine::executeScripts(
    const std::vector<dom::HtmlParser::ScriptData> &scripts) {
  for (const auto &script : scripts) {
    // Skip async and defer scripts 鈥?they are handled separately
    if (script.async || script.defer)
      continue;

    // Execute inline script content
    if (!script.content.empty()) {
      m_scriptEngine.evaluate(script.content);
    }

    // Fetch and execute external script (use ResourceTree if available)
    if (!script.src.empty()) {
      std::string jsContent;

      // Try to get content from ResourceTree first
      if (m_resourceTree) {
        for (const auto *node : m_resourceTree->syncScripts()) {
          if (node->url == script.src && node->isLoaded()) {
            jsContent = node->content;
            break;
          }
        }
      }

      // Fallback: fetch directly if not found in tree
      if (jsContent.empty() && m_config.enableNetworkLoading) {
        std::string resolvedUrl = resolveUrl(script.src);
        std::cout << "[Engine] Loading script: " << resolvedUrl << std::endl;
        jsContent = fetchResource(resolvedUrl);
      }

      if (!jsContent.empty()) {
        m_scriptEngine.evaluate(jsContent, script.src);
        std::cout << "[Engine] Executed sync script: " << script.src << " ("
                  << jsContent.size() << " bytes)" << std::endl;
      }
    }
  }
}

void BrowserEngine::executeDeferScripts() {
  if (!m_resourceTree)
    return;

  auto deferNodes = m_resourceTree->deferScripts();
  if (deferNodes.empty())
    return;

  std::cout << "[Engine] Executing " << deferNodes.size()
            << " defer script(s)..." << std::endl;

  for (const auto *node : deferNodes) {
    if (node->isLoaded() && !node->content.empty()) {
      m_scriptEngine.evaluate(node->content, node->url);
      std::cout << "[Engine] Executed defer script: " << node->url << " ("
                << node->content.size() << " bytes)" << std::endl;
    } else if (node->isFailed()) {
      std::cerr << "[Engine] Defer script failed to load: " << node->url
                << " - " << node->error << std::endl;
    }
  }
}

void BrowserEngine::checkAsyncScripts() {
  if (!m_resourceTree)
    return;

  auto asyncNodes = m_resourceTree->asyncScripts();
  for (const auto *node : asyncNodes) {
    if (node->isLoaded() && !node->content.empty() && !node->executed) {
      node->executed = true;
      m_scriptEngine.evaluate(node->content, node->url);
      std::cout << "[Engine] Executed async script: " << node->url << " ("
                << node->content.size() << " bytes)" << std::endl;
      m_needsPaint = true;
    }
  }
}

// 鈹€鈹€鈹€ URL Resolution & Resource Fetching 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

std::string BrowserEngine::resolveUrl(const std::string &relativeUrl) const {
  // If it's already an absolute URL, return as-is
  if (relativeUrl.find("://") != std::string::npos) {
    return relativeUrl;
  }

  // If no base URL is set, return the relative URL unchanged
  if (m_baseUrl.empty()) {
    return relativeUrl;
  }

  // Use Url::resolve() for proper relative URL resolution
  auto baseResult = loader::Url::parse(m_baseUrl);
  if (baseResult.isOk()) {
    auto resolved = baseResult.unwrap().resolve(relativeUrl);
    if (resolved.isValid()) {
      return resolved.toString();
    }
  }

  // Fallback: simple concatenation
  std::string base = m_baseUrl;
  if (!base.empty() && base.back() != '/') {
    base += '/';
  }
  return base + relativeUrl;
}

std::string BrowserEngine::fetchResource(const std::string &url) {
  if (!m_config.enableNetworkLoading) {
    return "";
  }

  std::cout << "[Engine] Fetching resource: " << url << std::endl;

  auto result = loader::Loader::instance().load(url);
  if (result.isOk()) {
    auto resource = result.unwrap();
    std::string content(resource->data.begin(), resource->data.end());
    std::cout << "[Engine] Fetched " << content.size() << " bytes from " << url
              << std::endl;
    return content;
  }

  std::cerr << "[Engine] Failed to load resource: " << url << " - "
            << result.errorMessage() << std::endl;
  return "";
}

// 鈹€鈹€鈹€ Layout (Main Thread) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

void BrowserEngine::buildLayout() {
  std::cout << "[Engine] Building Layout Tree..." << std::endl;
  m_needsLayout = false;
  layout::LayoutTreeBuilder builder(m_stylesheet);
  if (!m_document)
    return;

  auto bodyList = m_document->getElementsByTagName("body");
  if (bodyList.empty()) {
    std::cerr << "[Engine] No <body> found for layout" << std::endl;
    return;
  }

  m_layoutRoot = builder.build(bodyList[0]);
  if (m_layoutRoot) {
    // Debug: Print tree structure
    std::function<void(layout::LayoutBoxPtr, int)> printTree =
        [&](layout::LayoutBoxPtr box, int depth) {
          std::string indent(depth * 2, ' ');
          std::string tagName = "anonymous";
          if (box->node()) {
            if (box->node()->nodeType() == dom::NodeType::Element) {
              auto elem = std::dynamic_pointer_cast<dom::Element>(box->node());
              tagName = elem ? elem->tagName() : "unknown";
            } else {
              tagName = "#text";
            }
          }
          std::cout << indent << "[Layout] " << tagName << " ("
                    << (box->type() == layout::BoxType::BlockNode ? "Block"
                                                                  : "Inline")
                    << ")" << std::endl;
          for (auto &child : box->children())
            printTree(child, depth + 1);
        };
    std::cout << "[Engine] Layout Tree Structure:" << std::endl;
    printTree(m_layoutRoot, 1);

    layout::LayoutAlgorithm algo;
    layout::Dimensions viewport;
    viewport.content.width = static_cast<float>(m_config.windowWidth);
    viewport.content.height = static_cast<float>(m_config.windowHeight);

    algo.layout(m_layoutRoot, viewport);

    auto &dim = m_layoutRoot->dimensions();
    std::cout << "[Engine] Layout calculated. Root size: " << dim.content.width
              << "x" << dim.content.height << " at (" << dim.content.x << ","
              << dim.content.y << ")" << std::endl;
  } else {
    std::cerr << "[Engine] Failed to build layout root" << std::endl;
  }
}

// 鈹€鈹€鈹€ Event Loop 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

int BrowserEngine::run() {
  if (!m_initialized) {
    std::cerr << "[Engine] Run failed: not initialized" << std::endl;
    return 1;
  }

  bool running = true;

  // Clear canvas to white initially
#ifdef ENABLE_OPENGL
  if (m_openglCanvas) {
    m_openglCanvas->clear(renderer::Color::White());
  }
#else
  if (m_canvas) {
    m_canvas->clear(renderer::Color::White());
  }
#endif

  while (running) {
    // 1. Process Window Events
    if (!m_window.processEvents()) {
      running = false;
      break;
    }

    // 2. Check for async parse completion
    if (m_pendingParse && m_pendingParse->isReady()) {
      onParseComplete(m_pendingParse->getResult());
      m_pendingParse.reset();
    }

    // 3. Interactive Systems (only when document is ready)
    if (m_state == EngineState::Ready) {
      handleMouseClicks();
      handleTimers();
      handleMicrotasks();
      checkAsyncScripts();
      // Process keyboard input for form controls
      handleKeyboardInputs();
    }

    // 4. Render
    if (m_state == EngineState::Ready) {
      if (m_needsLayout) {
        buildLayout();
        m_needsPaint = true;
      }

      if (m_needsPaint && m_layoutRoot) {
#ifdef ENABLE_OPENGL
        if (m_openglCanvas) {
          m_openglCanvas->clear(renderer::Color::White());
          m_painter.paint(m_layoutRoot, *m_openglCanvas);
          m_window.drawOpenGLTexture(m_openglCanvas->getTexture());
        }
#else
        if (m_canvas) {
          m_canvas->clear(renderer::Color::White());
          m_painter.paint(m_layoutRoot, *m_canvas);
          // Paint form controls on top of the layout
          m_formMgr.renderAll(*m_canvas);
          m_window.drawBuffer(*m_canvas);
        }
#endif
        m_needsPaint = false;
      }
    } else if (m_state == EngineState::Loading && m_needsPaint) {
      // Show a blank white canvas while loading
#ifdef ENABLE_OPENGL
      if (m_openglCanvas) {
        m_openglCanvas->clear(renderer::Color::White());
        m_openglCanvas->drawText(10, 10, "Loading...",
                                renderer::Color{100, 100, 100, 255});
        m_window.drawOpenGLTexture(m_openglCanvas->getTexture());
      }
#else
      if (m_canvas) {
        m_canvas->clear(renderer::Color::White());
        m_canvas->drawText(10, 10, "Loading...",
                          renderer::Color{100, 100, 100, 255});
        // Optional: show loading, then render form placeholders if any
        m_formMgr.renderAll(*m_canvas);
        m_window.drawBuffer(*m_canvas);
      }
      #endif
      m_needsPaint = false;
    }

    // 5. Frame Limiter (~60 FPS)
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }

  return 0;
}

void BrowserEngine::handleMouseClicks() {
  auto clicks = m_window.pendingMouseClicks();
  for (const auto &click : clicks) {
    if (click.button == window::MouseButton::Left) {
      auto hitNode =
          layout::HitTest::test(m_layoutRoot, static_cast<float>(click.x),
                                static_cast<float>(click.y));
      if (hitNode) {
        // Dispatch "click" event
        auto it = hitNode->eventListenerIds_.find("click");
        if (it != hitNode->eventListenerIds_.end()) {
          JSContext *ctx = m_scriptEngine.getContext();
          if (ctx) {
            JSValue eventObj =
                script::EventBinding::createEventObject(ctx, "click");
            std::vector<uint32_t> listenerIds;
            listenerIds.reserve(it->second.size());
            for (const auto &entry : it->second) {
              listenerIds.push_back(entry.id);
            }
            script::EventBinding::dispatch(ctx, listenerIds, eventObj);
            JS_FreeValue(ctx, eventObj);
            m_needsPaint = true;
          }
        }
      }
    }
  }
}

void BrowserEngine::handleTimers() {
  if (m_scriptEngine.tickTimers()) {
    m_needsPaint = true;
  }
}

void BrowserEngine::handleMicrotasks() {
  if (m_scriptEngine.tickMicrotasks()) {
    m_needsPaint = true;
  }
}

// Bind DOM inputs to form controls (instant minimal binding)
void BrowserEngine::bindFormControlsFromDocument() {
  m_formMgr.clear();
  if (!m_document) return;

  // Bind <input>, <textarea>, <button> controls present in the document
  auto inputs = m_document->getElementsByTagName("input");
  auto textareas = m_document->getElementsByTagName("textarea");
  auto buttons = m_document->getElementsByTagName("button");

  int idx = 0;
  // Helper lambda to create and register controls
  auto registerControl = [&](const std::string &domId,
                             std::unique_ptr<layout::FormControl> ctrl) {
    if (!ctrl) return;
    layout::FormControl *raw = m_formMgr.add(std::move(ctrl));
    m_formMgr.registerDomControl(domId, raw);
  };

  for (auto &el : inputs) {
    if (!el) continue;
    std::string id = el->id();
    if (id.empty()) id = "input-" + std::to_string(idx++);
    // Only handle text-like inputs for now
    auto typeOpt = el->getAttribute("type");
    std::string type = typeOpt.value_or("text");
    std::string t = dom::toLower(type);
    if (t.empty() || t == "text") {
      auto ctrl = std::make_unique<layout::TextInput>(id);
      if (auto v = el->getAttribute("value"); v) ctrl->value = *v;
      ctrl->setPosition(10, 10 + idx * 36);
      ctrl->setSize(260, 28);
      registerControl(id, std::move(ctrl));
    }
  }

  for (auto &el : textareas) {
    if (!el) continue;
    std::string id = el->id();
    if (id.empty()) id = "textarea-" + std::to_string(idx++);
    auto ctrl = std::make_unique<layout::TextArea>(id);
    if (auto v = el->getAttribute("value"); v) ctrl->value = *v;
    ctrl->setPosition(10, 10 + idx * 40);
    ctrl->setSize(260, 96);
    registerControl(id, std::move(ctrl));
  }

  for (auto &el : buttons) {
    if (!el) continue;
    std::string id = el->id();
    if (id.empty()) id = "button-" + std::to_string(idx++);
    auto label = el->innerHTML();
    auto ctrl = std::make_unique<layout::Button>(id, label);
    ctrl->setPosition(10, 10 + idx * 40);
    ctrl->setSize(120, 32);
    registerControl(id, std::move(ctrl));
  }
}

// Keyboard input handling for focused form controls
void BrowserEngine::handleKeyboardInputs() {
  // Only when a document is ready
  if (m_state != EngineState::Ready) return;

  auto keys = m_window.pendingKeyInputs();
  if (keys.empty()) return;

  for (char c : keys) {
    if (c == '\t') {
      // Tab to next control
      m_formMgr.focusNext();
    } else {
      m_formMgr.insertCharToFocused(c);
    }
  }
  m_needsPaint = true;
}

} // namespace engine
} // namespace xiaopeng
