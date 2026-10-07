#pragma once

#include <cstring> // For memset
#include <dom/dom.hpp>
#include <dom/dom_enhancements.hpp>
#include <dom/html_parser.hpp>
#include <iostream>
#include <memory>
#include <mutex>
#include <quickjs.h>
#include <script/event_binding.hpp>
#include <script/js_binding.hpp>
#include <dom/event_system.hpp>

namespace xiaopeng {
namespace script {

// ============================================================
//  DOMBinding — Phase 1: Complete JS DOM API
//  Covers:
//    - Document: body, head, title, documentElement, URL, readyState,
//                createTextNode, createDocumentFragment, createComment
//    - Element:  parentNode, parentElement, childNodes, children,
//                firstChild, lastChild, firstElementChild, lastElementChild,
//                nextSibling, previousSibling, nextElementSibling,
//                previousElementSibling, nodeValue, nodeName,
//                className (get/set), classList,
//                insertBefore, replaceChild, remove, cloneNode,
//                contains, isEqualNode, closest, matches
//    - NodeList: live-length array-like with item(index)
//    - classList: add, remove, toggle, contains, replace, item, toString
// ============================================================

class DOMBinding {
public:
  static void registerBinding(JSContext *ctx) {
    JS_NewClassID(&s_elementClassId);
    JS_NewClassID(&s_documentClassId);
    JS_NewClassID(&s_nodeListClassId);
    JS_NewClassID(&s_classListClassId);
    JS_NewClassID(&s_textNodeClassId);
    JS_NewClassID(&s_eventClassId);

    // Element class
    JSClassDef elementDef;
    memset(&elementDef, 0, sizeof(JSClassDef));
    elementDef.class_name = "Element";
    elementDef.finalizer = [](JSRuntime *, JSValue) {};
    JS_NewClass(JS_GetRuntime(ctx), s_elementClassId, &elementDef);

    // Document class
    JSClassDef docDef;
    memset(&docDef, 0, sizeof(JSClassDef));
    docDef.class_name = "Document";
    docDef.finalizer = [](JSRuntime *, JSValue) {};
    JS_NewClass(JS_GetRuntime(ctx), s_documentClassId, &docDef);

    // NodeList class
    JSClassDef nlDef;
    memset(&nlDef, 0, sizeof(JSClassDef));
    nlDef.class_name = "NodeList";
    nlDef.finalizer = [](JSRuntime *, JSValue) {};
    JS_NewClass(JS_GetRuntime(ctx), s_nodeListClassId, &nlDef);

    // DOMTokenList (classList) class
    JSClassDef clDef;
    memset(&clDef, 0, sizeof(JSClassDef));
    clDef.class_name = "DOMTokenList";
    clDef.finalizer = [](JSRuntime *, JSValue) {};
    JS_NewClass(JS_GetRuntime(ctx), s_classListClassId, &clDef);

    // Text class
    JSClassDef textDef;
    memset(&textDef, 0, sizeof(JSClassDef));
    textDef.class_name = "Text";
    textDef.finalizer = [](JSRuntime *, JSValue) {};
    JS_NewClass(JS_GetRuntime(ctx), s_textNodeClassId, &textDef);

    // Event class
    JSClassDef eventDef;
    memset(&eventDef, 0, sizeof(JSClassDef));
    eventDef.class_name = "Event";
    eventDef.finalizer = [](JSRuntime *, JSValue val) {
      auto* evPtr = (std::shared_ptr<dom::Event>*)JS_GetOpaque(val, s_eventClassId);
      if (evPtr) {
        delete evPtr;
      }
    };
    JS_NewClass(JS_GetRuntime(ctx), s_eventClassId, &eventDef);
  }

  // ── wrapDocument ────────────────────────────────────────────
  static JSValue wrapDocument(JSContext *ctx,
                              std::shared_ptr<dom::Document> doc) {
    JSValue obj = JS_NewObjectClass(ctx, s_documentClassId);
    if (JS_IsException(obj))
      return obj;

    JS_SetOpaque(obj, doc.get());

    // Store shared_ptr to keep document alive
    JS_SetPropertyStr(ctx, obj, "___doc_ptr",
                      JS_NewBigUint64(ctx, (uint64_t)new std::shared_ptr<dom::Document>(doc)));

    // --- Methods ---
    auto bind = [&](const char *name, JSCFunction func, int argc) {
      JS_SetPropertyStr(ctx, obj, name,
                        JS_NewCFunction(ctx, func, name, argc));
    };

    bind("getElementById",        document_getElementById, 1);
    bind("getElementsByTagName",  document_getElementsByTagName, 1);
    bind("getElementsByClassName",document_getElementsByClassName, 1);
    bind("querySelector",         document_querySelector, 1);
    bind("querySelectorAll",      document_querySelectorAll, 1);
    bind("createElement",         document_createElement, 1);
    bind("createTextNode",        document_createTextNode, 1);
    bind("createDocumentFragment", document_createDocumentFragment, 0);
    bind("createComment",         document_createComment, 1);
    bind("createProcessingInstruction", document_createProcessingInstruction, 2);
    bind("importNode",            document_importNode, 2);
    bind("adoptNode",             document_adoptNode, 1);
    bind("getElementsByName",     document_getElementsByName, 1);
    bind("addEventListener",      element_addEventListener, 2);
    bind("removeEventListener",   element_removeEventListener, 2);
    bind("dispatchEvent",         element_dispatchEvent, 1);

    // --- Properties ---
    // document.body
    JSAtom atom = JS_NewAtom(ctx, "body");
    JS_DefinePropertyGetSet(
        ctx, obj, atom,
        JS_NewCFunction(ctx, document_get_body, "get_body", 0),
        JS_NewCFunction(ctx, document_set_body, "set_body", 1),
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    // document.head
    atom = JS_NewAtom(ctx, "head");
    JS_DefinePropertyGetSet(
        ctx, obj, atom,
        JS_NewCFunction(ctx, document_get_head, "get_head", 0),
        JS_UNDEFINED,
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    // document.documentElement
    atom = JS_NewAtom(ctx, "documentElement");
    JS_DefinePropertyGetSet(
        ctx, obj, atom,
        JS_NewCFunction(ctx, document_get_documentElement, "get_documentElement", 0),
        JS_UNDEFINED,
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    // document.title (get/set)
    atom = JS_NewAtom(ctx, "title");
    JS_DefinePropertyGetSet(
        ctx, obj, atom,
        JS_NewCFunction(ctx, document_get_title, "get_title", 0),
        JS_NewCFunction(ctx, document_set_title, "set_title", 1),
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    // document.URL (read-only)
    atom = JS_NewAtom(ctx, "URL");
    JS_DefinePropertyGetSet(
        ctx, obj, atom,
        JS_NewCFunction(ctx, document_get_URL, "get_URL", 0),
        JS_UNDEFINED,
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    // document.readyState (read-only)
    atom = JS_NewAtom(ctx, "readyState");
    JS_DefinePropertyGetSet(
        ctx, obj, atom,
        JS_NewCFunction(ctx, document_get_readyState, "get_readyState", 0),
        JS_UNDEFINED,
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    return obj;
  }

  // ── wrapElement ─────────────────────────────────────────────
  static JSValue wrapElement(JSContext *ctx, dom::Element *el) {
    if (!el) return JS_NULL;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        try {
            s_createdNodes.push_back(el->shared_from_this());
        } catch(...) {}
    }
    JSValue obj = JS_NewObjectClass(ctx, s_elementClassId);
    if (JS_IsException(obj))
      return obj;

    JS_SetOpaque(obj, el);

    // --- Existing properties ---
    bindGetSet(ctx, obj, "innerHTML",
               element_get_innerHTML, element_set_innerHTML);
    bindReadOnly(ctx, obj, "id",      element_get_id);
    bindReadOnly(ctx, obj, "tagName", element_get_tagName);
    bindGetSet(ctx, obj, "textContent",
               element_get_textContent, element_set_textContent);
    bindGetSet(ctx, obj, "style",
               element_get_style, element_set_style);

    // --- NEW: className (get/set) ---
    bindGetSet(ctx, obj, "className",
               element_get_className, element_set_className);

    // --- NEW: classList (read-only, returns DOMTokenList) ---
    bindReadOnly(ctx, obj, "classList", element_get_classList);

    // --- NEW: Node tree properties ---
    bindReadOnly(ctx, obj, "parentNode",         node_get_parentNode);
    bindReadOnly(ctx, obj, "parentElement",      node_get_parentElement);
    bindReadOnly(ctx, obj, "childNodes",         node_get_childNodes);
    bindReadOnly(ctx, obj, "children",           node_get_children);
    bindReadOnly(ctx, obj, "firstChild",         node_get_firstChild);
    bindReadOnly(ctx, obj, "lastChild",          node_get_lastChild);
    bindReadOnly(ctx, obj, "firstElementChild",  node_get_firstElementChild);
    bindReadOnly(ctx, obj, "lastElementChild",   node_get_lastElementChild);
    bindReadOnly(ctx, obj, "nextSibling",        node_get_nextSibling);
    bindReadOnly(ctx, obj, "previousSibling",    node_get_previousSibling);
    bindReadOnly(ctx, obj, "nextElementSibling", node_get_nextElementSibling);
    bindReadOnly(ctx, obj, "previousElementSibling", node_get_previousElementSibling);

    // --- NEW: nodeName, nodeValue, nodeType ---
    bindReadOnly(ctx, obj, "nodeName",  node_get_nodeName);
    bindGetSet(ctx, obj, "nodeValue",
               node_get_nodeValue, node_set_nodeValue);
    bindReadOnly(ctx, obj, "nodeType",  node_get_nodeType);

    // --- NEW: childElementCount ---
    bindReadOnly(ctx, obj, "childElementCount", element_get_childElementCount);

    // --- outerHTML (get/set), isConnected ---
    bindGetSet(ctx, obj, "outerHTML",
               element_get_outerHTML, element_set_outerHTML);
    bindReadOnly(ctx, obj, "isConnected", node_get_isConnected);

    // --- Attribute methods ---
    auto bindMethod = [&](const char *name, JSCFunction func, int argc) {
      JS_SetPropertyStr(ctx, obj, name,
                        JS_NewCFunction(ctx, func, name, argc));
    };

    bindMethod("setAttribute",       element_setAttribute, 2);
    bindMethod("getAttribute",       element_getAttribute, 1);
    bindMethod("hasAttribute",       element_hasAttribute, 1);
    bindMethod("removeAttribute",    element_removeAttribute, 1);
    bindMethod("getAttributeNames",  element_getAttributeNames, 0);
    bindMethod("hasAttributes",      element_hasAttributes, 0);
    bindMethod("toggleAttribute",    element_toggleAttribute, 2);

    // --- WHATWG ParentNode / ChildNode (variadic, strings become Text) ---
    bindMethod("append",          node_append, -1);
    bindMethod("prepend",         node_prepend, -1);
    bindMethod("replaceChildren", node_replaceChildren, -1);
    bindMethod("before",          node_before, -1);
    bindMethod("after",           node_after, -1);
    bindMethod("replaceWith",     node_replaceWith, -1);

    // --- insertAdjacent* + dataset ---
    bindMethod("insertAdjacentHTML",    element_insertAdjacentHTML, 2);
    bindMethod("insertAdjacentText",    element_insertAdjacentText, 2);
    bindMethod("insertAdjacentElement", element_insertAdjacentElement, 2);
    bindReadOnly(ctx, obj, "dataset", element_get_dataset);

    // --- DOM manipulation ---
    bindMethod("appendChild",        element_appendChild, 1);
    bindMethod("removeChild",        element_removeChild, 1);
    bindMethod("insertBefore",       element_insertBefore, 2);
    bindMethod("replaceChild",       element_replaceChild, 2);
    bindMethod("remove",             element_remove, 0);
    bindMethod("cloneNode",          element_cloneNode, 1);
    bindMethod("contains",           element_contains, 1);
    bindMethod("isEqualNode",        element_isEqualNode, 1);

    // --- Collection methods ---
    bindMethod("getElementsByTagName",   element_getElementsByTagName, 1);
    bindMethod("getElementsByClassName", element_getElementsByClassName, 1);
    bindMethod("querySelector",          element_querySelector, 1);
    bindMethod("querySelectorAll",       element_querySelectorAll, 1);
    bindMethod("matches",                element_matches, 1);
    bindMethod("closest",                element_closest, 1);

    // --- Events ---
    bindMethod("addEventListener",    element_addEventListener, 2);
    bindMethod("removeEventListener", element_removeEventListener, 2);
    bindMethod("dispatchEvent",       element_dispatchEvent, 1);

    return obj;
  }

  // ── wrapTextNode ────────────────────────────────────────────
  static JSValue wrapTextNode(JSContext *ctx, dom::TextNode *node) {
    if (!node) return JS_NULL;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        try {
            s_createdNodes.push_back(node->shared_from_this());
        } catch(...) {}
    }
    JSValue obj = JS_NewObjectClass(ctx, s_textNodeClassId);
    if (JS_IsException(obj))
      return obj;

    JS_SetOpaque(obj, node);

    // Node properties
    bindReadOnly(ctx, obj, "parentNode",    node_get_parentNode);
    bindReadOnly(ctx, obj, "parentElement", node_get_parentElement);
    bindReadOnly(ctx, obj, "nextSibling",   node_get_nextSibling);
    bindReadOnly(ctx, obj, "previousSibling", node_get_previousSibling);
    bindReadOnly(ctx, obj, "nextElementSibling", text_get_nextElementSibling);
    bindReadOnly(ctx, obj, "previousElementSibling", text_get_previousElementSibling);
    bindReadOnly(ctx, obj, "isConnected",   node_get_isConnected);
    bindReadOnly(ctx, obj, "nodeName",      node_get_nodeName);
    bindReadOnly(ctx, obj, "nodeType",      node_get_nodeType);
    bindGetSet(ctx, obj, "nodeValue",       node_get_nodeValue, node_set_nodeValue);
    bindGetSet(ctx, obj, "textContent",     node_get_nodeValue, node_set_nodeValue);
    bindGetSet(ctx, obj, "data",            node_get_nodeValue, node_set_nodeValue);
    JS_SetPropertyStr(ctx, obj, "splitText",
                      JS_NewCFunction(ctx, text_splitText, "splitText", 1));

    // CharacterData methods
    auto bindTextMethod = [&](const char *name, JSCFunction func, int argc) {
      JS_SetPropertyStr(ctx, obj, name,
                        JS_NewCFunction(ctx, func, name, argc));
    };
    bindTextMethod("substringData", text_substringData, 2);
    bindTextMethod("appendData",    text_appendData, 1);
    bindTextMethod("insertData",    text_insertData, 2);
    bindTextMethod("deleteData",    text_deleteData, 2);
    bindTextMethod("replaceData",   text_replaceData, 3);

    return obj;
  }
  // ── wrapFragment: DocumentFragment with node-manipulation methods ──
  static JSValue wrapFragment(JSContext *ctx,
                              std::shared_ptr<dom::DocumentFragment> frag) {
    if (!frag) return JS_NULL;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      s_createdNodes.push_back(frag);
    }
    JSValue obj = JS_NewObjectClass(ctx, s_elementClassId);
    if (JS_IsException(obj)) return obj;

    JS_SetOpaque(obj, static_cast<dom::Node *>(frag.get()));
    JS_SetPropertyStr(ctx, obj, "nodeType", JS_NewInt32(ctx, 11));
    JS_SetPropertyStr(ctx, obj, "nodeName",
                      JSBinding::toJSString(ctx, frag->nodeName()));

    auto bindMethod = [&](const char *name, JSCFunction func, int argc) {
      JS_SetPropertyStr(ctx, obj, name,
                        JS_NewCFunction(ctx, func, name, argc));
    };
    auto bindReadOnly = [&](const char *name, JSCFunction getter) {
      JSAtom atom = JS_NewAtom(ctx, name);
      JS_DefinePropertyGetSet(
          ctx, obj, atom,
          JS_NewCFunction(ctx, getter, (std::string("get_") + name).c_str(), 0),
          JS_UNDEFINED,
          JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
      JS_FreeAtom(ctx, atom);
    };

    bindMethod("appendChild",  element_appendChild, 1);
    bindMethod("removeChild",  element_removeChild, 1);
    bindMethod("insertBefore", element_insertBefore, 2);
    bindMethod("replaceChild", element_replaceChild, 2);
    bindMethod("append",       node_append, -1);
    bindMethod("prepend",      node_prepend, -1);
    bindMethod("replaceChildren", node_replaceChildren, -1);
    bindMethod("cloneNode",    element_cloneNode, 1);
    bindMethod("querySelector",       fragment_querySelector, 1);
    bindMethod("querySelectorAll",    fragment_querySelectorAll, 1);
    bindReadOnly("childNodes",        node_get_childNodes);
    bindReadOnly("children",          node_get_children);
    bindReadOnly("firstChild",        node_get_firstChild);
    bindReadOnly("lastChild",         node_get_lastChild);
    bindReadOnly("firstElementChild", node_get_firstElementChild);
    bindReadOnly("lastElementChild",  node_get_lastElementChild);

    return obj;
  }

  // fragment.querySelector(selector)
  static JSValue fragment_querySelector(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || node->nodeType() != dom::NodeType::DocumentFragment || argc < 1)
      return JS_EXCEPTION;
    auto frag = std::static_pointer_cast<dom::DocumentFragment>(
        node->shared_from_this());
    auto found = frag->querySelector(JSBinding::toStdString(ctx, argv[0]));
    return found ? wrapElement(ctx, found.get()) : JS_NULL;
  }

  // fragment.querySelectorAll(selector)
  static JSValue fragment_querySelectorAll(JSContext *ctx,
                                           JSValueConst this_val, int argc,
                                           JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || node->nodeType() != dom::NodeType::DocumentFragment || argc < 1)
      return JS_EXCEPTION;
    auto frag = std::static_pointer_cast<dom::DocumentFragment>(
        node->shared_from_this());
    return wrapElementArray(
        ctx, frag->querySelectorAll(JSBinding::toStdString(ctx, argv[0])));
  }

  // ── wrapEvent ────────────────────────────────────────────────
  static JSValue wrapEvent(JSContext *ctx, std::shared_ptr<dom::Event> ev) {
    if (!ev) return JS_NULL;
    JSValue obj = JS_NewObjectClass(ctx, s_eventClassId);
    if (JS_IsException(obj)) return obj;

    // We store a copy of the shared_ptr in Opaque so it stays alive
    auto* evPtr = new std::shared_ptr<dom::Event>(ev);
    JS_SetOpaque(obj, evPtr);

    bindReadOnly(ctx, obj, "type", event_get_type);
    bindReadOnly(ctx, obj, "bubbles", event_get_bubbles);
    bindReadOnly(ctx, obj, "cancelable", event_get_cancelable);
    bindReadOnly(ctx, obj, "eventPhase", event_get_eventPhase);
    bindReadOnly(ctx, obj, "target", event_get_target);
    bindReadOnly(ctx, obj, "currentTarget", event_get_currentTarget);

    JS_SetPropertyStr(ctx, obj, "stopPropagation",
                      JS_NewCFunction(ctx, event_stopPropagation, "stopPropagation", 0));
    JS_SetPropertyStr(ctx, obj, "preventDefault",
                      JS_NewCFunction(ctx, event_preventDefault, "preventDefault", 0));

    return obj;
  }

  static void cleanup() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_createdElements.clear();
    s_createdNodes.clear();
  }

private:
  static JSClassID s_elementClassId;
  static JSClassID s_documentClassId;
  static JSClassID s_nodeListClassId;
  static JSClassID s_classListClassId;
  static JSClassID s_textNodeClassId;
  static JSClassID s_eventClassId;

  static inline std::vector<std::weak_ptr<dom::Element>> s_createdElements;
  static inline std::vector<dom::NodePtr> s_createdNodes; // BUG FIX: track all created nodes
  static inline std::mutex s_mutex;

  // ── Helper: get Node* from either element or text class ──
  static dom::Node *getNodeFromThis(JSContext *ctx, JSValueConst this_val) {
    (void)ctx;
    dom::Node *node = (dom::Node *)JS_GetOpaque(this_val, s_elementClassId);
    if (!node)
      node = (dom::Node *)JS_GetOpaque(this_val, s_textNodeClassId);
    if (!node)
      node = (dom::Node *)JS_GetOpaque(this_val, s_documentClassId);
    return node;
  }

  // ── Helper: shared_ptr to any wrapped node (Element, Text, Fragment) ──
  static dom::NodePtr getNodeArg(JSContext *ctx, JSValueConst value) {
    (void)ctx;
    dom::Node *node = (dom::Node *)JS_GetOpaque(value, s_elementClassId);
    if (!node)
      node = (dom::Node *)JS_GetOpaque(value, s_textNodeClassId);
    if (!node)
      return nullptr;
    try {
      return node->shared_from_this();
    } catch (...) {
      return nullptr;
    }
  }

  // ── Helper: variadic node arguments; strings become Text nodes ──
  static std::vector<dom::NodePtr> getNodeArgs(JSContext *ctx, int argc,
                                               JSValueConst *argv) {
    std::vector<dom::NodePtr> out;
    out.reserve((size_t)argc);
    for (int i = 0; i < argc; ++i) {
      if (dom::NodePtr n = getNodeArg(ctx, argv[i])) {
        out.push_back(n);
      } else if (JS_IsString(argv[i])) {
        out.push_back(
            std::make_shared<dom::TextNode>(JSBinding::toStdString(ctx, argv[i])));
      }
    }
    return out;
  }

  // ── Helper: Element* from this, rejecting non-element receivers ──
  static dom::Element *getElementFromThis(JSContext *ctx,
                                          JSValueConst this_val) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || node->nodeType() != dom::NodeType::Element)
      return nullptr;
    return static_cast<dom::Element *>(node);
  }

  // ── Helper: bind read-only getter ──
  static void bindReadOnly(JSContext *ctx, JSValue obj, const char *name,
                           JSCFunction getter) {
    JSAtom atom = JS_NewAtom(ctx, name);
    JS_DefinePropertyGetSet(
        ctx, obj, atom,
        JS_NewCFunction(ctx, getter, (std::string("get_") + name).c_str(), 0),
        JS_UNDEFINED,
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);
  }

  // ── Helper: bind getter+setter ──
  static void bindGetSet(JSContext *ctx, JSValue obj, const char *name,
                         JSCFunction getter, JSCFunction setter) {
    JSAtom atom = JS_NewAtom(ctx, name);
    JS_DefinePropertyGetSet(
        ctx, obj, atom,
        JS_NewCFunction(ctx, getter, (std::string("get_") + name).c_str(), 0),
        JS_NewCFunction(ctx, setter, (std::string("set_") + name).c_str(), 1),
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);
  }

public:
  // ── Helper: wrap child node (Element or TextNode) ──
  static JSValue wrapNode(JSContext *ctx, dom::NodePtr node) {
    if (!node)
      return JS_NULL;
    if (node->nodeType() == dom::NodeType::Element) {
      return wrapElement(ctx, static_cast<dom::Element *>(node.get()));
    } else if (node->nodeType() == dom::NodeType::Text) {
      return wrapTextNode(ctx, static_cast<dom::TextNode *>(node.get()));
    }
    // Comment, ProcessingInstruction etc. → generic object with nodeType
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "nodeType",
                      JS_NewInt32(ctx, static_cast<int>(node->nodeType())));
    JS_SetPropertyStr(ctx, obj, "nodeName",
                      JSBinding::toJSString(ctx, node->nodeName()));
    if (node->nodeType() == dom::NodeType::Comment) {
      auto *comment = static_cast<dom::CommentNode *>(node.get());
      JS_SetPropertyStr(ctx, obj, "data",
                        JSBinding::toJSString(ctx, comment->data()));
    } else if (node->nodeType() == dom::NodeType::ProcessingInstruction) {
      auto *pi = static_cast<dom::ProcessingInstructionNode *>(node.get());
      JS_SetPropertyStr(ctx, obj, "target",
                        JSBinding::toJSString(ctx, pi->target()));
      JS_SetPropertyStr(ctx, obj, "data",
                        JSBinding::toJSString(ctx, pi->data()));
    }
    return obj;
  }

  // ── Helper: wrap vector<ElementPtr> as JS Array ──
  static JSValue wrapElementArray(JSContext *ctx,
                                  const std::vector<dom::ElementPtr> &elems) {
    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < elems.size(); ++i) {
      JS_DefinePropertyValueUint32(
          ctx, arr, i, wrapElement(ctx, elems[i].get()),
          JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
    }
    return arr;
  }

  // ── Helper: create a live NodeList wrapping childNodes ──
  static JSValue wrapChildNodes(JSContext *ctx, dom::Node *node) {
    JSValue arr = JS_NewArray(ctx);
    auto &children = node->childNodes();
    for (size_t i = 0; i < children.size(); ++i) {
      JS_DefinePropertyValueUint32(
          ctx, arr, i, wrapNode(ctx, children[i]),
          JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
    }
    JS_SetPropertyStr(ctx, arr, "length", JS_NewInt32(ctx, (int32_t)children.size()));
    return arr;
  }

  // ══════════════════════════════════════════════════════════
  //  DOCUMENT METHODS
  // ══════════════════════════════════════════════════════════

  // ── Helper: get Event* from event class ──
  static std::shared_ptr<dom::Event> getEventFromThis(JSContext *ctx, JSValueConst this_val) {
    (void)ctx;
    auto* evPtr = (std::shared_ptr<dom::Event>*)JS_GetOpaque(this_val, s_eventClassId);
    if (evPtr) return *evPtr;
    return nullptr;
  }

  // ── Event Getters/Methods ──
  static JSValue event_get_type(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto ev = getEventFromThis(ctx, this_val);
    if (!ev) return JS_EXCEPTION;
    return JSBinding::toJSString(ctx, ev->type());
  }

  static JSValue event_get_bubbles(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto ev = getEventFromThis(ctx, this_val);
    if (!ev) return JS_EXCEPTION;
    return JS_NewBool(ctx, ev->bubbles());
  }

  static JSValue event_get_cancelable(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto ev = getEventFromThis(ctx, this_val);
    if (!ev) return JS_EXCEPTION;
    return JS_NewBool(ctx, ev->cancelable());
  }

  static JSValue event_get_eventPhase(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto ev = getEventFromThis(ctx, this_val);
    if (!ev) return JS_EXCEPTION;
    return JS_NewInt32(ctx, static_cast<int32_t>(ev->phase()));
  }

  static JSValue event_get_target(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto ev = getEventFromThis(ctx, this_val);
    if (!ev) return JS_EXCEPTION;
    return wrapNode(ctx, ev->target());
  }

  static JSValue event_get_currentTarget(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto ev = getEventFromThis(ctx, this_val);
    if (!ev) return JS_EXCEPTION;
    return wrapNode(ctx, ev->currentTarget());
  }

  static JSValue event_stopPropagation(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto ev = getEventFromThis(ctx, this_val);
    if (!ev) return JS_EXCEPTION;
    ev->stopPropagation();
    return JS_UNDEFINED;
  }

  static JSValue event_preventDefault(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto ev = getEventFromThis(ctx, this_val);
    if (!ev) return JS_EXCEPTION;
    ev->preventDefault();
    return JS_UNDEFINED;
  }

  static dom::Document *getDoc(JSContext *ctx, JSValueConst this_val) {
    (void)ctx;
    return (dom::Document *)JS_GetOpaque(this_val, s_documentClassId);
  }

  // document.getElementById(id)
  static JSValue document_getElementById(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    auto el = doc->getElementById(JSBinding::toStdString(ctx, argv[0]));
    return el ? wrapElement(ctx, el.get()) : JS_NULL;
  }

  // document.getElementsByTagName(name)
  static JSValue document_getElementsByTagName(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    return wrapElementArray(ctx, doc->getElementsByTagName(JSBinding::toStdString(ctx, argv[0])));
  }

  // document.getElementsByClassName(name)
  static JSValue document_getElementsByClassName(JSContext *ctx, JSValueConst this_val,
                                                  int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    return wrapElementArray(ctx, doc->getElementsByClassName(JSBinding::toStdString(ctx, argv[0])));
  }

  // document.querySelector(selector)
  static JSValue document_querySelector(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    auto el = doc->querySelector(JSBinding::toStdString(ctx, argv[0]));
    return el ? wrapElement(ctx, el.get()) : JS_NULL;
  }

  // document.querySelectorAll(selector)
  static JSValue document_querySelectorAll(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    return wrapElementArray(ctx, doc->querySelectorAll(JSBinding::toStdString(ctx, argv[0])));
  }

  // document.createElement(tagName)
  static JSValue document_createElement(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    auto el = doc->createElement(JSBinding::toStdString(ctx, argv[0]));
    if (!el) return JS_NULL;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      s_createdNodes.push_back(el);
      s_createdElements.push_back(el);
      if (s_createdElements.size() > 100) {
        auto it = s_createdElements.begin();
        while (it != s_createdElements.end()) {
          if (it->expired()) it = s_createdElements.erase(it);
          else ++it;
        }
      }
    }
    return wrapElement(ctx, el.get());
  }

  // document.createTextNode(data)
  static JSValue document_createTextNode(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    auto node = doc->createTextNode(JSBinding::toStdString(ctx, argv[0]));
    if (!node) return JS_NULL;
    { std::lock_guard<std::mutex> lock(s_mutex); s_createdNodes.push_back(node); }
    return wrapTextNode(ctx, static_cast<dom::TextNode *>(node.get()));
  }

  // document.createDocumentFragment()
  static JSValue document_createDocumentFragment(JSContext *ctx, JSValueConst this_val,
                                                  int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto *doc = getDoc(ctx, this_val);
    if (!doc) return JS_EXCEPTION;
    auto frag = doc->createDocumentFragment();
    if (!frag) return JS_NULL;
    return wrapFragment(ctx, frag);
  }

  // document.createComment(data)
  static JSValue document_createComment(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    auto node = doc->createComment(JSBinding::toStdString(ctx, argv[0]));
    if (!node) return JS_NULL;
    { std::lock_guard<std::mutex> lock(s_mutex); s_createdNodes.push_back(node); }
    JSValue obj = JS_NewObjectClass(ctx, s_elementClassId);
    if (JS_IsException(obj)) return obj;
    JS_SetOpaque(obj, static_cast<dom::Node *>(node.get()));
    JS_SetPropertyStr(ctx, obj, "nodeType", JS_NewInt32(ctx, 8));
    JS_SetPropertyStr(ctx, obj, "nodeName", JS_NewString(ctx, "#comment"));
    JS_SetPropertyStr(ctx, obj, "data",
                      JSBinding::toJSString(ctx, JSBinding::toStdString(ctx, argv[0])));
    return obj;
  }

  // document.createProcessingInstruction(target, data)
  static JSValue document_createProcessingInstruction(JSContext *ctx,
                                                       JSValueConst this_val,
                                                       int argc,
                                                       JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 2) return JS_EXCEPTION;
    auto pi = doc->createProcessingInstruction(
        JSBinding::toStdString(ctx, argv[0]),
        JSBinding::toStdString(ctx, argv[1]));
    if (!pi) return JS_NULL;
    { std::lock_guard<std::mutex> lock(s_mutex); s_createdNodes.push_back(pi); }
    return wrapNode(ctx, pi);
  }

  // document.importNode(node[, deep=true])
  static JSValue document_importNode(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    auto node = getNodeArg(ctx, argv[0]);
    if (!node) {
      JS_ThrowTypeError(ctx, "importNode: argument is not a Node");
      return JS_EXCEPTION;
    }
    bool deep = argc < 2 || JS_ToBool(ctx, argv[1]) != 0;
    auto imported = doc->importNode(node, deep);
    { std::lock_guard<std::mutex> lock(s_mutex); s_createdNodes.push_back(imported); }
    return wrapNode(ctx, imported);
  }

  // document.adoptNode(node)
  static JSValue document_adoptNode(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    auto node = getNodeArg(ctx, argv[0]);
    if (!node) {
      JS_ThrowTypeError(ctx, "adoptNode: argument is not a Node");
      return JS_EXCEPTION;
    }
    auto adopted = doc->adoptNode(node);
    return wrapNode(ctx, adopted);
  }

  // document.getElementsByName(name)
  static JSValue document_getElementsByName(JSContext *ctx,
                                            JSValueConst this_val, int argc,
                                            JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    return wrapElementArray(
        ctx, doc->getElementsByName(JSBinding::toStdString(ctx, argv[0])));
  }

  // document.body (getter)
  static JSValue document_get_body(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto *doc = getDoc(ctx, this_val);
    if (!doc) return JS_EXCEPTION;
    auto body = doc->body();
    return body ? wrapElement(ctx, body.get()) : JS_NULL;
  }

  // document.body (setter)
  static JSValue document_set_body(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    dom::Element *el = (dom::Element *)JS_GetOpaque(argv[0], s_elementClassId);
    if (!el) return JS_EXCEPTION;
    try {
      doc->setBody(std::static_pointer_cast<dom::Element>(el->shared_from_this()));
    } catch (...) {
      return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
  }

  // document.head (getter)
  static JSValue document_get_head(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto *doc = getDoc(ctx, this_val);
    if (!doc) return JS_EXCEPTION;
    auto head = doc->head();
    return head ? wrapElement(ctx, head.get()) : JS_NULL;
  }

  // document.documentElement (getter)
  static JSValue document_get_documentElement(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto *doc = getDoc(ctx, this_val);
    if (!doc) return JS_EXCEPTION;
    auto html = doc->documentElement();
    return html ? wrapElement(ctx, html.get()) : JS_NULL;
  }

  // document.title (getter)
  static JSValue document_get_title(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto *doc = getDoc(ctx, this_val);
    if (!doc) return JS_EXCEPTION;
    return JSBinding::toJSString(ctx, doc->titleText());
  }

  // document.title (setter)
  static JSValue document_set_title(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    auto *doc = getDoc(ctx, this_val);
    if (!doc || argc < 1) return JS_EXCEPTION;
    doc->setTitleText(JSBinding::toStdString(ctx, argv[0]));
    return JS_UNDEFINED;
  }

  // document.URL (getter)
  static JSValue document_get_URL(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    auto *doc = getDoc(ctx, this_val);
    if (!doc) return JS_EXCEPTION;
    return JSBinding::toJSString(ctx, doc->url());
  }

  // document.readyState (getter)
  static JSValue document_get_readyState(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    (void)this_val;
    // Simplified: always "complete" since we parse synchronously before executing JS
    return JS_NewString(ctx, "complete");
  }

  // ══════════════════════════════════════════════════════════
  //  NODE TREE PROPERTIES (shared by Element & Text)
  // ══════════════════════════════════════════════════════════

  // node.parentNode
  static JSValue node_get_parentNode(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    auto parent = node->parentNode();
    if (!parent) return JS_NULL;
    if (parent->nodeType() == dom::NodeType::Element)
      return wrapElement(ctx, static_cast<dom::Element *>(parent.get()));
    if (parent->nodeType() == dom::NodeType::Document)
      return JS_NULL; // Don't expose raw document from here
    return wrapNode(ctx, parent);
  }

  // node.parentElement
  static JSValue node_get_parentElement(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    auto parent = node->parentNode();
    if (parent && parent->nodeType() == dom::NodeType::Element)
      return wrapElement(ctx, static_cast<dom::Element *>(parent.get()));
    return JS_NULL;
  }

  // node.childNodes
  static JSValue node_get_childNodes(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    return wrapChildNodes(ctx, node);
  }

  // node.children (only Element children)
  static JSValue node_get_children(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    JSValue arr = JS_NewArray(ctx);
    uint32_t idx = 0;
    for (const auto &child : node->childNodes()) {
      if (child->nodeType() == dom::NodeType::Element) {
        JS_DefinePropertyValueUint32(
            ctx, arr, idx++,
            wrapElement(ctx, static_cast<dom::Element *>(child.get())),
            JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
      }
    }
    return arr;
  }

  // node.firstChild
  static JSValue node_get_firstChild(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    return wrapNode(ctx, node->firstChild());
  }

  // node.lastChild
  static JSValue node_get_lastChild(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    return wrapNode(ctx, node->lastChild());
  }

  // node.firstElementChild
  static JSValue node_get_firstElementChild(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    if (node->nodeType() == dom::NodeType::Element) {
      auto child = static_cast<dom::Element *>(node)->firstElementChild();
      return child ? wrapElement(ctx, child.get()) : JS_NULL;
    }
    if (node->nodeType() == dom::NodeType::DocumentFragment) {
      auto child = static_cast<dom::DocumentFragment *>(node)->firstElementChild();
      return child ? wrapElement(ctx, child.get()) : JS_NULL;
    }
    return JS_NULL;
  }

  // node.lastElementChild
  static JSValue node_get_lastElementChild(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    if (node->nodeType() == dom::NodeType::Element) {
      auto child = static_cast<dom::Element *>(node)->lastElementChild();
      return child ? wrapElement(ctx, child.get()) : JS_NULL;
    }
    if (node->nodeType() == dom::NodeType::DocumentFragment) {
      auto child = static_cast<dom::DocumentFragment *>(node)->lastElementChild();
      return child ? wrapElement(ctx, child.get()) : JS_NULL;
    }
    return JS_NULL;
  }

  // node.nextSibling
  static JSValue node_get_nextSibling(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    return wrapNode(ctx, node->nextSibling());
  }

  // node.previousSibling
  static JSValue node_get_previousSibling(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    return wrapNode(ctx, node->previousSibling());
  }

  // node.nextElementSibling
  static JSValue node_get_nextElementSibling(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el) return JS_EXCEPTION;
    auto sib = el->nextElementSibling();
    return sib ? wrapElement(ctx, sib.get()) : JS_NULL;
  }

  // node.previousElementSibling
  static JSValue node_get_previousElementSibling(JSContext *ctx, JSValueConst this_val,
                                                  int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el) return JS_EXCEPTION;
    auto sib = el->previousElementSibling();
    return sib ? wrapElement(ctx, sib.get()) : JS_NULL;
  }

  // node.nodeName
  static JSValue node_get_nodeName(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    // For elements, tagName (uppercase). For text, "#text".
    if (node->nodeType() == dom::NodeType::Element) {
      return JSBinding::toJSString(ctx, static_cast<dom::Element *>(node)->tagName());
    }
    return JSBinding::toJSString(ctx, node->nodeName());
  }

  // node.nodeValue (getter)
  static JSValue node_get_nodeValue(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    if (node->nodeType() == dom::NodeType::Text) {
      return JSBinding::toJSString(ctx,
          static_cast<dom::TextNode *>(node)->data());
    }
    return JS_NULL;
  }

  // node.nodeValue (setter)
  static JSValue node_set_nodeValue(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || argc < 1) return JS_EXCEPTION;
    if (node->nodeType() == dom::NodeType::Text) {
      static_cast<dom::TextNode *>(node)->setData(
          JSBinding::toStdString(ctx, argv[0]));
    }
    return JS_UNDEFINED;
  }

  // node.nodeType
  static JSValue node_get_nodeType(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    return JS_NewInt32(ctx, static_cast<int>(node->nodeType()));
  }

  // ══════════════════════════════════════════════════════════
  //  ELEMENT PROPERTIES (className, classList, childElementCount)
  // ══════════════════════════════════════════════════════════

  // element.className (getter)
  static JSValue element_get_className(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el) return JS_EXCEPTION;
    return JSBinding::toJSString(ctx, el->className());
  }

  // element.className (setter)
  static JSValue element_set_className(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    el->setClassName(JSBinding::toStdString(ctx, argv[0]));
    return JS_UNDEFINED;
  }

  // element.classList (getter) — returns a DOMTokenList proxy
  static JSValue element_get_classList(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el) return JS_EXCEPTION;

    JSValue obj = JS_NewObjectClass(ctx, s_classListClassId);
    if (JS_IsException(obj)) return obj;
    JS_SetOpaque(obj, el);

    // Methods
    auto bind = [&](const char *name, JSCFunction func, int narg) {
      JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, func, name, narg));
    };

    bind("add",      classList_add,      1);  // variadic handled in impl
    bind("remove",   classList_remove,   1);
    bind("toggle",   classList_toggle,   1);
    bind("contains", classList_contains, 1);
    bind("replace",  classList_replace,  2);
    bind("item",     classList_item,     1);
    bind("toString", classList_toString, 0);

    // length property
    auto classes = el->classList();
    JS_SetPropertyStr(ctx, obj, "length", JS_NewInt32(ctx, (int32_t)classes.size()));

    // Index properties (0, 1, 2, ...)
    for (size_t i = 0; i < classes.size(); ++i) {
      JS_DefinePropertyValueUint32(ctx, obj, (uint32_t)i,
          JSBinding::toJSString(ctx, classes[i]),
          JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
    }

    return obj;
  }

  // classList.add(tokens...)
  static JSValue classList_add(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_classListClassId);
    if (!el) return JS_EXCEPTION;
    for (int i = 0; i < argc; ++i) {
      el->addClass(JSBinding::toStdString(ctx, argv[i]));
    }
    return JS_UNDEFINED;
  }

  // classList.remove(tokens...)
  static JSValue classList_remove(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_classListClassId);
    if (!el) return JS_EXCEPTION;
    for (int i = 0; i < argc; ++i) {
      el->removeClass(JSBinding::toStdString(ctx, argv[i]));
    }
    return JS_UNDEFINED;
  }

  // classList.toggle(token [, force])
  static JSValue classList_toggle(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_classListClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    std::string token = JSBinding::toStdString(ctx, argv[0]);
    bool hasForce = argc >= 2 && !JS_IsUndefined(argv[1]);
    if (hasForce) {
      bool force = JS_ToBool(ctx, argv[1]);
      if (force) {
        el->addClass(token);
        return JS_TRUE;
      } else {
        el->removeClass(token);
        return JS_FALSE;
      }
    }
    // Toggle without force
    bool existed = el->hasClass(token);
    if (existed) el->removeClass(token);
    else el->addClass(token);
    return JS_NewBool(ctx, !existed);
  }

  // classList.contains(token)
  static JSValue classList_contains(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_classListClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    return JS_NewBool(ctx, el->hasClass(JSBinding::toStdString(ctx, argv[0])));
  }

  // classList.replace(oldToken, newToken)
  static JSValue classList_replace(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_classListClassId);
    if (!el || argc < 2) return JS_EXCEPTION;
    std::string oldToken = JSBinding::toStdString(ctx, argv[0]);
    std::string newToken = JSBinding::toStdString(ctx, argv[1]);
    bool had = el->hasClass(oldToken);
    if (had) {
      el->removeClass(oldToken);
      el->addClass(newToken);
    }
    return JS_NewBool(ctx, had);
  }

  // classList.item(index)
  static JSValue classList_item(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_classListClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    int32_t index;
    JS_ToInt32(ctx, &index, argv[0]);
    auto classes = el->classList();
    if (index < 0 || (size_t)index >= classes.size()) return JS_NULL;
    return JSBinding::toJSString(ctx, classes[index]);
  }

  // classList.toString()
  static JSValue classList_toString(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_classListClassId);
    if (!el) return JS_EXCEPTION;
    return JSBinding::toJSString(ctx, el->className());
  }

  // element.childElementCount
  static JSValue element_get_childElementCount(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    if (node->nodeType() == dom::NodeType::Element) {
      return JS_NewInt32(ctx, (int32_t)static_cast<dom::Element *>(node)->childElementCount());
    }
    if (node->nodeType() == dom::NodeType::DocumentFragment) {
      return JS_NewInt32(ctx, (int32_t)static_cast<dom::DocumentFragment *>(node)->childElementCount());
    }
    return JS_NewInt32(ctx, 0);
  }

  // ══════════════════════════════════════════════════════════
  //  EXTENDED DOM APIs (WHATWG DOM completion)
  // ══════════════════════════════════════════════════════════

  // node.isConnected
  static JSValue node_get_isConnected(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    return JS_NewBool(ctx, node->isConnected());
  }

  // element.outerHTML (getter)
  static JSValue element_get_outerHTML(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = getElementFromThis(ctx, this_val);
    if (!el) return JS_EXCEPTION;
    return JSBinding::toJSString(ctx, el->outerHTML());
  }

  // element.outerHTML = html
  static JSValue element_set_outerHTML(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    dom::Element *el = getElementFromThis(ctx, this_val);
    if (!el || argc < 1) return JS_EXCEPTION;
    el->setOuterHTML(JSBinding::toStdString(ctx, argv[0]));
    return JS_UNDEFINED;
  }

  // node.append(nodes/text...)
  static JSValue node_append(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    node->append(getNodeArgs(ctx, argc, argv));
    return JS_UNDEFINED;
  }

  // node.prepend(nodes/text...)
  static JSValue node_prepend(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    node->prepend(getNodeArgs(ctx, argc, argv));
    return JS_UNDEFINED;
  }

  // node.replaceChildren(nodes/text...)
  static JSValue node_replaceChildren(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    node->replaceChildren(getNodeArgs(ctx, argc, argv));
    return JS_UNDEFINED;
  }

  // node.before(nodes/text...)
  static JSValue node_before(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    node->before(getNodeArgs(ctx, argc, argv));
    return JS_UNDEFINED;
  }

  // node.after(nodes/text...)
  static JSValue node_after(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    node->after(getNodeArgs(ctx, argc, argv));
    return JS_UNDEFINED;
  }

  // node.replaceWith(nodes/text...)
  static JSValue node_replaceWith(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    node->replaceWith(getNodeArgs(ctx, argc, argv));
    return JS_UNDEFINED;
  }

  // element.getAttributeNames()
  static JSValue element_getAttributeNames(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = getElementFromThis(ctx, this_val);
    if (!el) return JS_EXCEPTION;
    JSValue arr = JS_NewArray(ctx);
    auto names = el->getAttributeNames();
    for (size_t i = 0; i < names.size(); ++i) {
      JS_DefinePropertyValueUint32(
          ctx, arr, (uint32_t)i, JSBinding::toJSString(ctx, names[i]),
          JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
    }
    return arr;
  }

  // element.hasAttributes()
  static JSValue element_hasAttributes(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = getElementFromThis(ctx, this_val);
    if (!el) return JS_EXCEPTION;
    return JS_NewBool(ctx, el->hasAttributes());
  }

  // element.toggleAttribute(name[, force])
  static JSValue element_toggleAttribute(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    dom::Element *el = getElementFromThis(ctx, this_val);
    if (!el || argc < 1) return JS_EXCEPTION;
    std::string name = JSBinding::toStdString(ctx, argv[0]);
    bool hasForce = argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]);
    bool result;
    if (hasForce) {
      result = el->toggleAttribute(name, JS_ToBool(ctx, argv[1]) != 0);
    } else {
      el->toggleAttribute(name);
      result = el->hasAttribute(name);
    }
    return JS_NewBool(ctx, result);
  }

  // element.insertAdjacentHTML(position, html)
  static JSValue element_insertAdjacentHTML(JSContext *ctx,
                                            JSValueConst this_val, int argc,
                                            JSValueConst *argv) {
    dom::Element *el = getElementFromThis(ctx, this_val);
    if (!el || argc < 2) return JS_EXCEPTION;
    el->insertAdjacentHTML(JSBinding::toStdString(ctx, argv[0]),
                           JSBinding::toStdString(ctx, argv[1]));
    return JS_UNDEFINED;
  }

  // element.insertAdjacentText(position, text)
  static JSValue element_insertAdjacentText(JSContext *ctx,
                                            JSValueConst this_val, int argc,
                                            JSValueConst *argv) {
    dom::Element *el = getElementFromThis(ctx, this_val);
    if (!el || argc < 2) return JS_EXCEPTION;
    el->insertAdjacentText(JSBinding::toStdString(ctx, argv[0]),
                           JSBinding::toStdString(ctx, argv[1]));
    return JS_UNDEFINED;
  }

  // element.insertAdjacentElement(position, element)
  static JSValue element_insertAdjacentElement(JSContext *ctx,
                                               JSValueConst this_val, int argc,
                                               JSValueConst *argv) {
    dom::Element *el = getElementFromThis(ctx, this_val);
    if (!el || argc < 2) return JS_EXCEPTION;
    auto element = getNodeArg(ctx, argv[1]);
    if (!element || element->nodeType() != dom::NodeType::Element) {
      JS_ThrowTypeError(ctx, "insertAdjacentElement: argument is not an Element");
      return JS_EXCEPTION;
    }
    el->insertAdjacentElement(
        JSBinding::toStdString(ctx, argv[0]),
        std::static_pointer_cast<dom::Element>(element));
    return JS_DupValue(ctx, argv[1]);
  }

  // ── element.dataset (get/set/has/delete/keys over data-* attributes) ──

  static dom::Element *datasetElement(JSContext *ctx, JSValueConst this_val) {
    // The dataset object holds the element's JS wrapper under "__el";
    // the wrapper's opaque carries the Element* (old QuickJS lacks
    // JS_GetBigUint64, so we can't round-trip the raw pointer).
    JSValue elVal = JS_GetPropertyStr(ctx, this_val, "__el");
    dom::Element *el =
        (dom::Element *)JS_GetOpaque(elVal, s_elementClassId);
    JS_FreeValue(ctx, elVal);
    return el;
  }

  // element.dataset (getter): object with get/set/has/delete/keys
  static JSValue element_get_dataset(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = getElementFromThis(ctx, this_val);
    if (!el) return JS_EXCEPTION;
    JSValue obj = JS_NewObject(ctx);
    // Keep the element's wrapper reachable for the dataset methods
    JS_SetPropertyStr(ctx, obj, "__el", JS_DupValue(ctx, this_val));
    auto bind = [&](const char *name, JSCFunction func, int n) {
      JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, func, name, n));
    };
    bind("get",    dataset_get,    1);
    bind("set",    dataset_set,    2);
    bind("has",    dataset_has,    1);
    bind("delete", dataset_delete, 1);
    bind("keys",   dataset_keys,   0);
    return obj;
  }

  static JSValue dataset_get(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv) {
    dom::Element *el = datasetElement(ctx, this_val);
    if (!el || argc < 1) return JS_EXCEPTION;
    auto value = el->getDataset(JSBinding::toStdString(ctx, argv[0]));
    return value.has_value() ? JSBinding::toJSString(ctx, value.value())
                             : JS_NULL;
  }

  static JSValue dataset_set(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv) {
    dom::Element *el = datasetElement(ctx, this_val);
    if (!el || argc < 2) return JS_EXCEPTION;
    el->setDataset(JSBinding::toStdString(ctx, argv[0]),
                   JSBinding::toStdString(ctx, argv[1]));
    return JS_UNDEFINED;
  }

  static JSValue dataset_has(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv) {
    dom::Element *el = datasetElement(ctx, this_val);
    if (!el || argc < 1) return JS_EXCEPTION;
    return JS_NewBool(ctx, el->hasDataset(JSBinding::toStdString(ctx, argv[0])));
  }

  static JSValue dataset_delete(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv) {
    dom::Element *el = datasetElement(ctx, this_val);
    if (!el || argc < 1) return JS_EXCEPTION;
    el->deleteDataset(JSBinding::toStdString(ctx, argv[0]));
    return JS_UNDEFINED;
  }

  static JSValue dataset_keys(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = datasetElement(ctx, this_val);
    if (!el) return JS_EXCEPTION;
    JSValue arr = JS_NewArray(ctx);
    auto keys = el->datasetKeys();
    for (size_t i = 0; i < keys.size(); ++i) {
      JS_DefinePropertyValueUint32(
          ctx, arr, (uint32_t)i, JSBinding::toJSString(ctx, keys[i]),
          JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
    }
    return arr;
  }

  // ── Text: CharacterData API + splitText + element siblings ──

  static JSValue text_substringData(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || node->nodeType() != dom::NodeType::Text) return JS_EXCEPTION;
    int32_t offset = 0, count = 0;
    if (argc >= 1) JS_ToInt32(ctx, &offset, argv[0]);
    if (argc >= 2) JS_ToInt32(ctx, &count, argv[1]);
    auto *text = static_cast<dom::TextNode *>(node);
    return JSBinding::toJSString(
        ctx, text->substringData((size_t)std::max(0, offset),
                                 (size_t)std::max(0, count)));
  }

  static JSValue text_appendData(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || node->nodeType() != dom::NodeType::Text || argc < 1)
      return JS_EXCEPTION;
    static_cast<dom::TextNode *>(node)->appendData(
        JSBinding::toStdString(ctx, argv[0]));
    return JS_UNDEFINED;
  }

  static JSValue text_insertData(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || node->nodeType() != dom::NodeType::Text || argc < 2)
      return JS_EXCEPTION;
    int32_t offset = 0;
    JS_ToInt32(ctx, &offset, argv[0]);
    static_cast<dom::TextNode *>(node)->insertData(
        (size_t)std::max(0, offset), JSBinding::toStdString(ctx, argv[1]));
    return JS_UNDEFINED;
  }

  static JSValue text_deleteData(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || node->nodeType() != dom::NodeType::Text || argc < 2)
      return JS_EXCEPTION;
    int32_t offset = 0, count = 0;
    JS_ToInt32(ctx, &offset, argv[0]);
    JS_ToInt32(ctx, &count, argv[1]);
    static_cast<dom::TextNode *>(node)->deleteData((size_t)std::max(0, offset),
                                                   (size_t)std::max(0, count));
    return JS_UNDEFINED;
  }

  static JSValue text_replaceData(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || node->nodeType() != dom::NodeType::Text || argc < 3)
      return JS_EXCEPTION;
    int32_t offset = 0, count = 0;
    JS_ToInt32(ctx, &offset, argv[0]);
    JS_ToInt32(ctx, &count, argv[1]);
    static_cast<dom::TextNode *>(node)->replaceData(
        (size_t)std::max(0, offset), (size_t)std::max(0, count),
        JSBinding::toStdString(ctx, argv[2]));
    return JS_UNDEFINED;
  }

  static JSValue text_splitText(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || node->nodeType() != dom::NodeType::Text || argc < 1)
      return JS_EXCEPTION;
    int32_t offset = 0;
    JS_ToInt32(ctx, &offset, argv[0]);
    auto *text = static_cast<dom::TextNode *>(node);
    dom::NodePtr remainder = text->splitText((size_t)std::max(0, offset));
    { std::lock_guard<std::mutex> lock(s_mutex); s_createdNodes.push_back(remainder); }
    return wrapTextNode(ctx, static_cast<dom::TextNode *>(remainder.get()));
  }

  // text.nextElementSibling / text.previousElementSibling
  static JSValue text_get_nextElementSibling(JSContext *ctx,
                                             JSValueConst this_val, int argc,
                                             JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || node->nodeType() != dom::NodeType::Text) return JS_EXCEPTION;
    auto sibling = static_cast<dom::TextNode *>(node)->nextElementSibling();
    return sibling ? wrapElement(ctx, sibling.get()) : JS_NULL;
  }

  static JSValue text_get_previousElementSibling(JSContext *ctx,
                                                 JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || node->nodeType() != dom::NodeType::Text) return JS_EXCEPTION;
    auto sibling = static_cast<dom::TextNode *>(node)->previousElementSibling();
    return sibling ? wrapElement(ctx, sibling.get()) : JS_NULL;
  }

  // ══════════════════════════════════════════════════════════
  //  EXISTING METHODS (kept from original)
  // ══════════════════════════════════════════════════════════

  // element.innerHTML getter
  static JSValue element_get_innerHTML(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el) return JS_EXCEPTION;
    return JSBinding::toJSString(ctx, el->innerHTML());
  }

  // element.innerHTML setter
  static JSValue element_set_innerHTML(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    dom::Element *el = getElementFromThis(ctx, this_val);
    if (!el || argc < 1) return JS_EXCEPTION;
    // setInnerHTML parses the fragment in this element's context
    el->setInnerHTML(JSBinding::toStdString(ctx, argv[0]));
    return JS_UNDEFINED;
  }

  // element.id getter
  static JSValue element_get_id(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el) return JS_EXCEPTION;
    return JSBinding::toJSString(ctx, el->id());
  }

  // element.tagName getter
  static JSValue element_get_tagName(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el) return JS_EXCEPTION;
    return JSBinding::toJSString(ctx, el->tagName());
  }

  // element.textContent getter
  static JSValue element_get_textContent(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el) return JS_EXCEPTION;
    return JSBinding::toJSString(ctx, el->textContent());
  }

  // element.textContent setter
  static JSValue element_set_textContent(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    el->setTextContent(JSBinding::toStdString(ctx, argv[0]));
    return JS_UNDEFINED;
  }

  // element.style getter
  static JSValue element_get_style(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el) return JS_EXCEPTION;
    return JSBinding::toJSString(ctx, el->getAttribute("style").value_or(""));
  }

  // element.style setter
  static JSValue element_set_style(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    el->setAttribute("style", JSBinding::toStdString(ctx, argv[0]));
    return JS_UNDEFINED;
  }

  // element.setAttribute(name, value)
  static JSValue element_setAttribute(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 2) return JS_EXCEPTION;
    el->setAttribute(JSBinding::toStdString(ctx, argv[0]),
                     JSBinding::toStdString(ctx, argv[1]));
    return JS_UNDEFINED;
  }

  // element.getAttribute(name)
  static JSValue element_getAttribute(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    auto val = el->getAttribute(JSBinding::toStdString(ctx, argv[0]));
    return val.has_value() ? JSBinding::toJSString(ctx, val.value()) : JS_NULL;
  }

  // element.hasAttribute(name)
  static JSValue element_hasAttribute(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    return JS_NewBool(ctx, el->hasAttribute(JSBinding::toStdString(ctx, argv[0])));
  }

  // element.removeAttribute(name)
  static JSValue element_removeAttribute(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    el->removeAttribute(JSBinding::toStdString(ctx, argv[0]));
    return JS_UNDEFINED;
  }

  // element.appendChild(child) — accepts Element, Text and DocumentFragment
  static JSValue element_appendChild(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    dom::Node *parent = getNodeFromThis(ctx, this_val);
    if (!parent || argc < 1) return JS_EXCEPTION;

    dom::NodePtr child = getNodeArg(ctx, argv[0]);
    if (!child) {
      JS_ThrowTypeError(ctx, "Argument is not a Node");
      return JS_EXCEPTION;
    }
    parent->appendChild(child);
    return JS_DupValue(ctx, argv[0]);
  }

  // element.removeChild(child)
  static JSValue element_removeChild(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    dom::Node *parent = getNodeFromThis(ctx, this_val);
    if (!parent || argc < 1) {
        JS_ThrowTypeError(ctx, "Invalid parent or missing arguments");
        return JS_EXCEPTION;
    }

    dom::NodePtr child = getNodeArg(ctx, argv[0]);
    if (!child) {
      JS_ThrowTypeError(ctx, "Argument is not a Node");
      return JS_EXCEPTION;
    }
    parent->removeChild(child);
    return JS_DupValue(ctx, argv[0]);
  }

  // element.insertBefore(newNode, referenceNode) — accepts any node;
  // null/undefined reference appends at the end
  static JSValue element_insertBefore(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    dom::Node *parent = getNodeFromThis(ctx, this_val);
    if (!parent || argc < 2) return JS_EXCEPTION;

    dom::NodePtr newNode = getNodeArg(ctx, argv[0]);
    if (!newNode) {
      JS_ThrowTypeError(ctx, "New child is not a Node");
      return JS_EXCEPTION;
    }

    dom::NodePtr refNode;
    if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
      refNode = getNodeArg(ctx, argv[1]);
      if (!refNode) {
        JS_ThrowTypeError(ctx, "Reference child is not a Node");
        return JS_EXCEPTION;
      }
    }

    parent->insertBefore(newNode, refNode);
    return JS_DupValue(ctx, argv[0]);
  }

  // element.replaceChild(newChild, oldChild) — accepts any node
  static JSValue element_replaceChild(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    dom::Node *parent = getNodeFromThis(ctx, this_val);
    if (!parent || argc < 2) {
        JS_ThrowTypeError(ctx, "Invalid parent or missing arguments");
        return JS_EXCEPTION;
    }

    dom::NodePtr newChild = getNodeArg(ctx, argv[0]);
    if (!newChild) {
      JS_ThrowTypeError(ctx, "New child is not a Node");
      return JS_EXCEPTION;
    }
    dom::NodePtr oldChild = getNodeArg(ctx, argv[1]);
    if (!oldChild) {
      JS_ThrowTypeError(ctx, "Old child is not a Node");
      return JS_EXCEPTION;
    }

    parent->replaceChild(newChild, oldChild);
    return JS_DupValue(ctx, argv[1]);
  }

  // element.remove()
  static JSValue element_remove(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) {
        JS_ThrowTypeError(ctx, "Invalid node");
        return JS_EXCEPTION;
    }
    node->remove();
    return JS_UNDEFINED;
  }

  // ── NEW: element.cloneNode(deep) ──
  static JSValue element_cloneNode(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node) return JS_EXCEPTION;
    bool deep = (argc >= 1) && JS_ToBool(ctx, argv[0]);
    auto clone = node->cloneNode(deep);
    if (!clone) return JS_NULL;
    if (clone->nodeType() == dom::NodeType::Element)
      return wrapElement(ctx, static_cast<dom::Element *>(clone.get()));
    if (clone->nodeType() == dom::NodeType::Text)
      return wrapTextNode(ctx, static_cast<dom::TextNode *>(clone.get()));
    return JS_NULL;
  }

  // ── NEW: element.contains(otherNode) ──
  // Delegates to Node::contains
  static JSValue element_contains(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || argc < 1) return JS_EXCEPTION;

    dom::Node *other = getNodeFromThis(ctx, argv[0]);
    if (!other) return JS_FALSE;

    try {
      return JS_NewBool(ctx, node->contains(other->shared_from_this()));
    } catch (...) {
      return JS_FALSE;
    }
  }

  // ── NEW: element.isEqualNode(otherNode) ──
  static JSValue element_isEqualNode(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    (void)ctx;
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;

    dom::Element *other = (dom::Element *)JS_GetOpaque(argv[0], s_elementClassId);
    if (!other) return JS_FALSE;

    // Compare tag name
    if (el->localName() != other->localName()) return JS_FALSE;

    // Compare attributes
    auto &attrs1 = el->attributes();
    auto &attrs2 = other->attributes();
    if (attrs1.size() != attrs2.size()) return JS_FALSE;
    for (const auto &a : attrs1) {
      auto v = other->getAttribute(a.name);
      if (!v.has_value() || v.value() != a.value) return JS_FALSE;
    }

    // Compare child count
    auto &c1 = el->childNodes();
    auto &c2 = other->childNodes();
    if (c1.size() != c2.size()) return JS_FALSE;

    // Recursively compare children
    for (size_t i = 0; i < c1.size(); ++i) {
      if (c1[i]->nodeType() != c2[i]->nodeType()) return JS_FALSE;
      if (c1[i]->nodeType() == dom::NodeType::Element) {
        // Recursive check via isEqualNode
        auto e1 = std::static_pointer_cast<dom::Element>(c1[i]);
        auto e2 = std::static_pointer_cast<dom::Element>(c2[i]);
        if (e1->localName() != e2->localName()) return JS_FALSE;
      } else if (c1[i]->textContent() != c2[i]->textContent()) {
        return JS_FALSE;
      }
    }
    return JS_TRUE;
  }

  // element.matches(selector)
  static JSValue element_matches(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    return JS_NewBool(ctx, el->matches(JSBinding::toStdString(ctx, argv[0])));
  }

  // element.closest(selector)
  static JSValue element_closest(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    std::string selector = JSBinding::toStdString(ctx, argv[0]);

    dom::Node *cur = el;
    while (cur) {
      if (cur->nodeType() == dom::NodeType::Element) {
        auto elem = static_cast<dom::Element *>(cur);
        if (elem->matches(selector)) {
          return wrapElement(ctx, elem);
        }
      }
      cur = cur->parentNode().get();
    }
    return JS_NULL;
  }

  // element.getElementsByTagName(name)
  static JSValue element_getElementsByTagName(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    return wrapElementArray(ctx, el->getElementsByTagName(JSBinding::toStdString(ctx, argv[0])));
  }

  // element.getElementsByClassName(name)
  static JSValue element_getElementsByClassName(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    return wrapElementArray(ctx, el->getElementsByClassName(JSBinding::toStdString(ctx, argv[0])));
  }

  // element.querySelector(selector)
  static JSValue element_querySelector(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    auto found = el->querySelector(JSBinding::toStdString(ctx, argv[0]));
    return found ? wrapElement(ctx, found.get()) : JS_NULL;
  }

  // element.querySelectorAll(selector)
  static JSValue element_querySelectorAll(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    dom::Element *el = (dom::Element *)JS_GetOpaque(this_val, s_elementClassId);
    if (!el || argc < 1) return JS_EXCEPTION;
    return wrapElementArray(ctx, el->querySelectorAll(JSBinding::toStdString(ctx, argv[0])));
  }

  // ── Event methods ──

  // element.addEventListener(type, callback[, capture=false])
  static JSValue element_addEventListener(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || argc < 2) return JS_EXCEPTION;
    if (!JS_IsFunction(ctx, argv[1])) return JS_EXCEPTION;

    std::string type = JSBinding::toStdString(ctx, argv[0]);
    bool capture = argc >= 3 && JS_ToBool(ctx, argv[2]) != 0;
    uint32_t id = EventBinding::addListener(ctx, argv[1]);
    node->addEventListener(type, id, capture);
    return JS_UNDEFINED;
  }

  // element.removeEventListener(type, callback[, capture])
  // std::function identity isn't available across the binding, so the last
  // matching entry (by capture flag when provided) is removed.
  static JSValue element_removeEventListener(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || argc < 2) return JS_EXCEPTION;

    std::string type = JSBinding::toStdString(ctx, argv[0]);
    auto it = node->eventListenerIds_.find(type);
    if (it == node->eventListenerIds_.end() || it->second.empty()) {
      return JS_UNDEFINED;
    }

    bool captureSpecified = argc >= 3 && !JS_IsUndefined(argv[2]) &&
                            !JS_IsNull(argv[2]);
    bool capture = captureSpecified ? JS_ToBool(ctx, argv[2]) != 0 : false;

    long long removeIdx = -1;
    for (size_t i = it->second.size(); i-- > 0;) {
      if (!captureSpecified || it->second[i].capture == capture) {
        removeIdx = (long long)i;
        break;
      }
    }
    if (removeIdx >= 0) {
      uint32_t id = it->second[(size_t)removeIdx].id;
      EventBinding::removeListener(id);
      it->second.erase(it->second.begin() + removeIdx);
      if (it->second.empty()) {
        node->eventListenerIds_.erase(it);
      }
    }
    return JS_UNDEFINED;
  }

  // element.dispatchEvent(eventTypeString)
  static JSValue element_dispatchEvent(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    dom::Node *node = getNodeFromThis(ctx, this_val);
    if (!node || argc < 1) return JS_EXCEPTION;

    std::string type = JSBinding::toStdString(ctx, argv[0]);
    // Create a real dom::Event with bubbling=true, cancelable=true
    auto ev = std::make_shared<dom::Event>(type, true, true);
    
    // Dispatch it through the real W3C EventSystem
    dom::EventSystem::dispatchEvent(node->shared_from_this(), ev);
    
    return JS_UNDEFINED;
  }
};

// Static member definitions (must be in a .cpp, but since this is header-only,
// we rely on inline or ODR-safe initialization)
inline JSClassID DOMBinding::s_elementClassId = 0;
inline JSClassID DOMBinding::s_documentClassId = 0;
inline JSClassID DOMBinding::s_nodeListClassId = 0;
inline JSClassID DOMBinding::s_classListClassId = 0;
inline JSClassID DOMBinding::s_textNodeClassId = 0;
inline JSClassID DOMBinding::s_eventClassId = 0;

} // namespace script
} // namespace xiaopeng
