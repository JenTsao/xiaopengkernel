#pragma once

// ═══════════════════════════════════════════════════════════════
//  DOM Enhancements: real selector engine + HTML-fragment mutation
//
//  dom.hpp only *declares* these methods: querySelector(All) / matches /
//  closest reuse the CSS selector engine (CssParser + StyleResolver), and
//  setInnerHTML / setOuterHTML / insertAdjacentHTML need HtmlParser. The CSS
//  and parser headers include dom.hpp back, so their definitions cannot live
//  in dom.hpp without an include cycle — they live here instead. Include
//  this header wherever these APIs are called.
// ═══════════════════════════════════════════════════════════════

#include "dom.hpp"
#include "../css/css_parser.hpp"
#include "../css/style_resolver.hpp"
#include "html_parser.hpp"

#include <functional>

namespace xiaopeng {
namespace dom {

namespace detail {

inline bool matchesSelectorList(const ElementPtr &element,
                                const std::vector<css::Selector> &selectors) {
  if (selectors.empty() || !element)
    return false;
  static css::StyleResolver resolver;
  for (const auto &selector : selectors) {
    if (resolver.matchSelector(element, selector))
      return true;
  }
  return false;
}

// Collect matching elements among the descendants of root, in tree order.
// When includeRoot is set and root is an element, root is tested too.
// Matching always happens against the real tree, so ancestor combinators
// see elements outside the searched subtree (WHATWG behavior).
inline std::vector<ElementPtr>
selectDescendants(const NodePtr &root,
                  const std::vector<css::Selector> &selectors,
                  bool includeRoot) {
  std::vector<ElementPtr> result;
  if (selectors.empty() || !root)
    return result;

  if (includeRoot && root->nodeType() == NodeType::Element) {
    auto elem = std::static_pointer_cast<Element>(root);
    if (matchesSelectorList(elem, selectors))
      result.push_back(elem);
  }

  std::function<void(const NodePtr &)> walk = [&](const NodePtr &node) {
    for (const auto &child : node->childNodes()) {
      if (child->nodeType() == NodeType::Element) {
        auto elem = std::static_pointer_cast<Element>(child);
        if (matchesSelectorList(elem, selectors))
          result.push_back(elem);
        walk(child);
      }
    }
  };
  walk(root);
  return result;
}

inline ElementPtr elementPtrFrom(const Element *element) {
  return const_cast<Element *>(element)->shared_from_this();
}

// Insert a node relative to elem per the insertAdjacent* positions
inline bool insertAdjacentNodeAt(Element *elem, const std::string &position,
                                 const NodePtr &node) {
  std::string pos = toLower(position);
  if (pos == "afterbegin") {
    elem->insertBefore(node, elem->firstChild());
    return true;
  }
  if (pos == "beforeend") {
    elem->appendChild(node);
    return true;
  }
  if (pos == "beforebegin" || pos == "afterend") {
    auto parent = elem->parentNode();
    if (!parent)
      return false;
    if (pos == "beforebegin") {
      parent->insertBefore(node, elem->shared_from_this());
    } else {
      parent->insertBefore(node, elem->nextSibling());
    }
    return true;
  }
  return false;
}

} // namespace detail

// ── Element: selector APIs ───────────────────────────────────

inline bool Element::matches(const std::string &selector) const {
  auto selectors = css::parseSelectorList(selector);
  return detail::matchesSelectorList(detail::elementPtrFrom(this), selectors);
}

inline ElementPtr Element::closest(const std::string &selector) const {
  auto selectors = css::parseSelectorList(selector);
  if (selectors.empty())
    return nullptr;
  ElementPtr current = detail::elementPtrFrom(this);
  while (current) {
    if (detail::matchesSelectorList(current, selectors))
      return current;
    current = current->parentElement();
  }
  return nullptr;
}

inline std::vector<ElementPtr>
Element::querySelectorAll(const std::string &selector) const {
  auto selectors = css::parseSelectorList(selector);
  if (selectors.empty())
    return {};
  // The element itself is not a candidate (WHATWG: descendants only)
  return detail::selectDescendants(detail::elementPtrFrom(this), selectors,
                                   false);
}

inline ElementPtr Element::querySelector(const std::string &selector) const {
  auto results = querySelectorAll(selector);
  return results.empty() ? nullptr : results[0];
}

// ── Document: selector APIs ──────────────────────────────────

inline std::vector<ElementPtr>
Document::querySelectorAll(const std::string &selector) const {
  auto selectors = css::parseSelectorList(selector);
  if (selectors.empty())
    return {};
  // Walk from the document node so documentElement is a candidate
  return detail::selectDescendants(
      const_cast<Document *>(this)->shared_from_this(), selectors, false);
}

inline ElementPtr Document::querySelector(const std::string &selector) const {
  auto results = querySelectorAll(selector);
  return results.empty() ? nullptr : results[0];
}

// ── DocumentFragment: selector APIs ──────────────────────────

inline std::vector<ElementPtr>
DocumentFragment::querySelectorAll(const std::string &selector) const {
  auto selectors = css::parseSelectorList(selector);
  if (selectors.empty())
    return {};
  return detail::selectDescendants(
      const_cast<DocumentFragment *>(this)->shared_from_this(), selectors,
      false);
}

inline ElementPtr
DocumentFragment::querySelector(const std::string &selector) const {
  auto results = querySelectorAll(selector);
  return results.empty() ? nullptr : results[0];
}

// ── Element: HTML-fragment mutation ──────────────────────────

inline void Element::setInnerHTML(const std::string &html) {
  removeAllChildren();
  for (const auto &node : HtmlParser::parseFragment(html)) {
    appendChild(node);
  }
}

inline void Element::setOuterHTML(const std::string &html) {
  auto parent = parentNode();
  if (!parent)
    return;
  auto self = shared_from_this();
  for (const auto &node : HtmlParser::parseFragment(html)) {
    parent->insertBefore(node, self);
  }
  parent->removeChild(self);
}

inline void Element::insertAdjacentHTML(const std::string &position,
                                        const std::string &html) {
  for (const auto &node : HtmlParser::parseFragment(html)) {
    detail::insertAdjacentNodeAt(this, position, node);
  }
}

inline void Element::insertAdjacentText(const std::string &position,
                                        const std::string &text) {
  detail::insertAdjacentNodeAt(this, position, std::make_shared<TextNode>(text));
}

inline void Element::insertAdjacentElement(const std::string &position,
                                           ElementPtr element) {
  if (element) {
    detail::insertAdjacentNodeAt(this, position, element);
  }
}

} // namespace dom
} // namespace xiaopeng
