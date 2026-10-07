#pragma once

#include "../loader/error.hpp"
#include "html_types.hpp"
#include <set>
#include <unordered_set>

namespace xiaopeng {
namespace dom {

class Element;
using ElementPtr = std::shared_ptr<Element>;

class Element : public Node {
public:
  Element(const std::string &localName, const std::string &namespaceUri = "",
          const std::string &prefix = "")
      : Node(NodeType::Element, localName), localName_(localName),
        namespaceUri_(namespaceUri), prefix_(prefix) {}

  const std::string &localName() const { return localName_; }
  const std::string &namespaceUri() const { return namespaceUri_; }
  const std::string &prefix() const { return prefix_; }

  std::string tagName() const { return toUpper(localName_); }

  std::string qualifiedName() const {
    if (prefix_.empty()) {
      return localName_;
    }
    return prefix_ + ":" + localName_;
  }

  const std::vector<Attribute> &attributes() const { return attributes_; }

  std::optional<std::string> getAttribute(const std::string &name) const {
    std::string lowerName = toLower(name);
    for (const auto &attr : attributes_) {
      if (toLower(attr.name) == lowerName) {
        return attr.value;
      }
    }
    return std::nullopt;
  }

  void setAttribute(const std::string &name, const std::string &value) {
    std::string lowerName = toLower(name);
    for (auto &attr : attributes_) {
      if (toLower(attr.name) == lowerName) {
        attr.value = value;
        notifyMutation();
        return;
      }
    }
    attributes_.emplace_back(name, value);
    notifyMutation();
  }

  void removeAttribute(const std::string &name) {
    std::string lowerName = toLower(name);
    auto it = std::remove_if(attributes_.begin(), attributes_.end(),
                             [&lowerName](const Attribute &attr) {
                               return toLower(attr.name) == lowerName;
                             });
    if (it != attributes_.end()) {
      attributes_.erase(it, attributes_.end());
      notifyMutation();
    }
  }

  bool hasAttribute(const std::string &name) const {
    std::string lowerName = toLower(name);
    for (const auto &attr : attributes_) {
      if (toLower(attr.name) == lowerName) {
        return true;
      }
    }
    return false;
  }

  void toggleAttribute(const std::string &name) {
    if (hasAttribute(name)) {
      removeAttribute(name);
    } else {
      setAttribute(name, "");
    }
  }

  // toggleAttribute with force: returns the resulting attribute presence
  bool toggleAttribute(const std::string &name, bool force) {
    if (force) {
      setAttribute(name, "");
      return true;
    }
    removeAttribute(name);
    return false;
  }

  std::vector<std::string> getAttributeNames() const {
    std::vector<std::string> names;
    names.reserve(attributes_.size());
    for (const auto &attr : attributes_) {
      names.push_back(attr.name);
    }
    return names;
  }

  bool hasAttributes() const { return !attributes_.empty(); }

  // ── dataset (data-* attributes) ─────────────────────────────
  // "userId" <-> "data-user-id"
  static std::string datasetToAttributeName(const std::string &key) {
    std::string name = "data-";
    for (char c : key) {
      if (c >= 'A' && c <= 'Z') {
        name += '-';
        name += static_cast<char>(c - 'A' + 'a');
      } else {
        name += c;
      }
    }
    return name;
  }

  static std::string datasetToKey(const std::string &attrName) {
    std::string lower = toLower(attrName);
    if (lower.size() <= 5 || lower.compare(0, 5, "data-") != 0)
      return "";
    std::string key;
    bool dash = false;
    for (size_t i = 5; i < lower.size(); ++i) {
      char c = lower[i];
      if (c == '-') {
        dash = true;
        continue;
      }
      if (dash && c >= 'a' && c <= 'z') {
        key += static_cast<char>(c - 'a' + 'A');
      } else {
        key += c;
      }
      dash = false;
    }
    return key;
  }

  std::optional<std::string> getDataset(const std::string &key) const {
    return getAttribute(datasetToAttributeName(key));
  }

  void setDataset(const std::string &key, const std::string &value) {
    setAttribute(datasetToAttributeName(key), value);
  }

  bool hasDataset(const std::string &key) const {
    return hasAttribute(datasetToAttributeName(key));
  }

  void deleteDataset(const std::string &key) {
    removeAttribute(datasetToAttributeName(key));
  }

  std::vector<std::string> datasetKeys() const {
    std::vector<std::string> keys;
    for (const auto &attr : attributes_) {
      auto k = datasetToKey(attr.name);
      if (!k.empty()) {
        keys.push_back(k);
      }
    }
    return keys;
  }

  std::string id() const { return getAttribute("id").value_or(""); }

  void setId(const std::string &id) { setAttribute("id", id); }

  std::string className() const { return getAttribute("class").value_or(""); }

  void setClassName(const std::string &className) {
    setAttribute("class", className);
  }

  std::vector<std::string> classList() const {
    std::vector<std::string> result;
    std::string classes = className();
    std::string current;
    for (char c : classes) {
      if (isWhitespace(c)) {
        if (!current.empty()) {
          result.push_back(current);
          current.clear();
        }
      } else {
        current += c;
      }
    }
    if (!current.empty()) {
      result.push_back(current);
    }
    return result;
  }

  void addClass(const std::string &className) {
    auto classes = classList();
    if (std::find(classes.begin(), classes.end(), className) == classes.end()) {
      classes.push_back(className);
    }
    std::string newClassList;
    for (const auto &cls : classes) {
      if (!newClassList.empty())
        newClassList += " ";
      newClassList += cls;
    }
    setClassName(newClassList);
  }

  void removeClass(const std::string &className) {
    auto classes = classList();
    classes.erase(std::remove(classes.begin(), classes.end(), className),
                  classes.end());
    std::string newClassList;
    for (const auto &cls : classes) {
      if (!newClassList.empty())
        newClassList += " ";
      newClassList += cls;
    }
    setClassName(newClassList);
  }

  bool hasClass(const std::string &className) const {
    auto classes = classList();
    return std::find(classes.begin(), classes.end(), className) !=
           classes.end();
  }

  std::string textContent() const override {
    std::string result;
    for (const auto &child : childNodes_) {
      if (child->nodeType() != NodeType::Comment) {
        result += child->textContent();
      }
    }
    return result;
  }

  void setTextContent(const std::string &text) override {
    childNodes_.clear();
    firstChild_.reset();
    lastChild_.reset();
    if (!text.empty()) {
      appendChild(std::make_shared<TextNode>(text));
    }
  }

  std::string innerHTML() const {
    std::string result;
    for (const auto &child : childNodes_) {
      result += child->toHtml();
    }
    return result;
  }

  void removeAllChildren() {
    childNodes_.clear();
    firstChild_.reset();
    lastChild_.reset();
  }

  std::string outerHTML() const { return toHtml(); }

  // Pseudo-class support methods
  bool isFirstChild() const {
    auto prev = previousSibling();
    return !prev;
  }

  bool isLastChild() const {
    auto next = nextSibling();
    return !next;
  }

  bool isOnlyChild() const {
    return isFirstChild() && isLastChild();
  }

  bool isEmpty() const {
    return childNodes_.empty();
  }

  bool isDocumentRoot() const {
    return localName_ == "html";
  }

  int getElementSiblingIndex() const {
    int index = 1; // nth-child is 1-indexed
    ElementPtr prev = previousElementSibling();
    while (prev) {
      index++;
      prev = prev->previousElementSibling();
    }
    return index;
  }

  int getElementSiblingIndexFromEnd() const {
    int index = 1;
    ElementPtr next = nextElementSibling();
    while (next) {
      index++;
      next = next->nextElementSibling();
    }
    return index;
  }

  int getElementSiblingIndexOfType() const {
    int index = 1;
    ElementPtr prev = previousElementSibling();
    while (prev) {
      if (prev->localName() == localName_) {
        index++;
      }
      prev = prev->previousElementSibling();
    }
    return index;
  }

  bool isHovered() const {
    return hasStateFlag(ElementState::Hovered);
  }

  bool isActive() const {
    return hasStateFlag(ElementState::Active);
  }

  bool isFocused() const {
    return hasStateFlag(ElementState::Focused);
  }

  bool isChecked() const {
    return hasStateFlag(ElementState::Checked);
  }

  bool isDisabled() const {
    return hasAttribute("disabled");
  }

  bool isLink() const {
    return localName_ == "a" && hasAttribute("href");
  }

  bool isVisited() const {
    return hasStateFlag(ElementState::Visited);
  }

  void setStateFlag(ElementState state, bool value) {
    if (value) {
      stateFlags_ |= static_cast<uint32_t>(state);
    } else {
      stateFlags_ &= ~static_cast<uint32_t>(state);
    }
    notifyMutation();
  }

  bool hasStateFlag(ElementState state) const {
    return (stateFlags_ & static_cast<uint32_t>(state)) != 0;
  }

  ElementPtr firstElementChild() const {
    for (const auto &child : childNodes_) {
      if (child->nodeType() == NodeType::Element) {
        return std::static_pointer_cast<Element>(child);
      }
    }
    return nullptr;
  }

  ElementPtr lastElementChild() const {
    for (auto it = childNodes_.rbegin(); it != childNodes_.rend(); ++it) {
      if ((*it)->nodeType() == NodeType::Element) {
        return std::static_pointer_cast<Element>(*it);
      }
    }
    return nullptr;
  }

  size_t childElementCount() const {
    size_t count = 0;
    for (const auto &child : childNodes_) {
      if (child->nodeType() == NodeType::Element) {
        count++;
      }
    }
    return count;
  }

  ElementPtr previousElementSibling() const {
    NodePtr sibling = previousSibling();
    while (sibling) {
      if (sibling->nodeType() == NodeType::Element) {
        return std::static_pointer_cast<Element>(sibling);
      }
      sibling = sibling->previousSibling();
    }
    return nullptr;
  }

  ElementPtr nextElementSibling() const {
    NodePtr sibling = nextSibling();
    while (sibling) {
      if (sibling->nodeType() == NodeType::Element) {
        return std::static_pointer_cast<Element>(sibling);
      }
      sibling = sibling->nextSibling();
    }
    return nullptr;
  }

  std::vector<ElementPtr> getElementsByTagName(const std::string &name) const {
    std::vector<ElementPtr> result;
    std::string lowerName = toLower(name);
    collectElementsByTagName(lowerName, result);
    return result;
  }

  std::vector<ElementPtr>
  getElementsByClassName(const std::string &className) const {
    std::vector<ElementPtr> result;
    collectElementsByClassName(className, result);
    return result;
  }

  ElementPtr getElementById(const std::string &id) const {
    for (const auto &child : childNodes_) {
      if (child->nodeType() == NodeType::Element) {
        auto elem = std::static_pointer_cast<Element>(child);
        if (elem->id() == id) {
          return elem;
        }
        auto found = elem->getElementById(id);
        if (found) {
          return found;
        }
      }
    }
    return nullptr;
  }

  std::vector<ElementPtr> querySelectorAll(const std::string &selector) const;
  ElementPtr querySelector(const std::string &selector) const;

  bool matches(const std::string &selector) const;
  ElementPtr closest(const std::string &selector) const;

  // HTML-fragment mutation (defined in dom/dom_enhancements.hpp — they need
  // HtmlParser, which depends on the complete dom types)
  void setInnerHTML(const std::string &html);
  void setOuterHTML(const std::string &html);
  void insertAdjacentHTML(const std::string &position, const std::string &html);
  void insertAdjacentText(const std::string &position, const std::string &text);
  void insertAdjacentElement(const std::string &position, ElementPtr element);

  bool isEqualNodeSelf(const Node &other) const override {
    const auto &o = static_cast<const Element &>(other);
    if (toLower(localName_) != toLower(o.localName_))
      return false;
    if (attributes_.size() != o.attributes_.size())
      return false;
    for (const auto &attr : attributes_) {
      auto v = o.getAttribute(attr.name);
      if (!v.has_value() || v.value() != attr.value)
        return false;
    }
    return true;
  }

  NodePtr cloneNode(bool deep = false) const override {
    auto cloned = std::make_shared<Element>(localName_, namespaceUri_, prefix_);
    cloned->attributes_ = attributes_;
    if (deep) {
      for (const auto &child : childNodes_) {
        cloned->appendChild(child->cloneNode(true));
      }
    }
    return cloned;
  }

  std::string toHtml() const override {
    std::string result;

    if (isVoidElement(localName_)) {
      result = "<" + localName_;
      for (const auto &attr : attributes_) {
        result += " " + attr.name;
        if (!attr.value.empty()) {
          result += "=\"" + escapeAttribute(attr.value) + "\"";
        }
      }
      result += ">";
    } else {
      result = "<" + localName_;
      for (const auto &attr : attributes_) {
        result += " " + attr.name;
        if (!attr.value.empty()) {
          result += "=\"" + escapeAttribute(attr.value) + "\"";
        }
      }
      result += ">";

      for (const auto &child : childNodes_) {
        result += child->toHtml();
      }

      result += "</" + localName_ + ">";
    }

    return result;
  }

  static bool isVoidElement(const std::string &tagName) {
    static const std::unordered_set<std::string> voidElements = {
        "area",  "base", "br",   "col",   "embed",  "hr",    "img",
        "input", "link", "meta", "param", "source", "track", "wbr"};
    return voidElements.find(toLower(tagName)) != voidElements.end();
  }

  static bool isRawTextElement(const std::string &tagName) {
    static const std::unordered_set<std::string> rawTextElements = {
        "script", "style", "textarea", "title"};
    return rawTextElements.find(toLower(tagName)) != rawTextElements.end();
  }

  static bool isEscapableRawTextElement(const std::string &tagName) {
    static const std::unordered_set<std::string> escapableRawTextElements = {
        "textarea", "title"};
    return escapableRawTextElements.find(toLower(tagName)) !=
           escapableRawTextElements.end();
  }

private:
  static std::string escapeAttribute(const std::string &value) {
    std::string result;
    result.reserve(value.size());
    for (char c : value) {
      switch (c) {
      case '&':
        result += "&amp;";
        break;
      case '"':
        result += "&quot;";
        break;
      case '\0':
        result += "&#0;";
        break;
      default:
        result += c;
        break;
      }
    }
    return result;
  }

  void collectElementsByTagName(const std::string &name,
                                std::vector<ElementPtr> &result) const {
    for (const auto &child : childNodes_) {
      if (child->nodeType() == NodeType::Element) {
        auto elem = std::static_pointer_cast<Element>(child);
        if (name == "*" || toLower(elem->localName_) == name) {
          result.push_back(elem);
        }
        elem->collectElementsByTagName(name, result);
      }
    }
  }

  void collectElementsByClassName(const std::string &className,
                                  std::vector<ElementPtr> &result) const {
    for (const auto &child : childNodes_) {
      if (child->nodeType() == NodeType::Element) {
        auto elem = std::static_pointer_cast<Element>(child);
        if (elem->hasClass(className)) {
          result.push_back(elem);
        }
        elem->collectElementsByClassName(className, result);
      }
    }
  }

  std::string localName_;
  std::string namespaceUri_;
  std::string prefix_;
  std::vector<Attribute> attributes_;
  uint32_t stateFlags_ = 0;
};

// ── Out-of-line definitions needing the complete Element type ──
inline std::shared_ptr<Element> Node::parentElement() const {
  auto parent = parentNode();
  if (parent && parent->nodeType() == NodeType::Element) {
    return std::static_pointer_cast<Element>(parent);
  }
  return nullptr;
}

inline ElementPtr firstElementSiblingOf(const Node *node, bool backwards) {
  NodePtr s = backwards ? node->previousSibling() : node->nextSibling();
  while (s) {
    if (s->nodeType() == NodeType::Element) {
      return std::static_pointer_cast<Element>(s);
    }
    s = backwards ? s->previousSibling() : s->nextSibling();
  }
  return nullptr;
}

inline ElementPtr TextNode::previousElementSibling() const {
  return firstElementSiblingOf(this, true);
}

inline ElementPtr TextNode::nextElementSibling() const {
  return firstElementSiblingOf(this, false);
}

inline ElementPtr CommentNode::previousElementSibling() const {
  return firstElementSiblingOf(this, true);
}

inline ElementPtr CommentNode::nextElementSibling() const {
  return firstElementSiblingOf(this, false);
}

inline ElementPtr DocumentFragment::firstElementChild() const {
  for (const auto &child : childNodes()) {
    if (child->nodeType() == NodeType::Element) {
      return std::static_pointer_cast<Element>(child);
    }
  }
  return nullptr;
}

inline ElementPtr DocumentFragment::lastElementChild() const {
  for (auto it = childNodes().rbegin(); it != childNodes().rend(); ++it) {
    if ((*it)->nodeType() == NodeType::Element) {
      return std::static_pointer_cast<Element>(*it);
    }
  }
  return nullptr;
}

inline size_t DocumentFragment::childElementCount() const {
  size_t count = 0;
  for (const auto &child : childNodes()) {
    if (child->nodeType() == NodeType::Element) {
      count++;
    }
  }
  return count;
}

class Document : public Node {
public:
  Document() : Node(NodeType::Document, "#document") {}

  void setMutationObserver(std::function<void(DirtyFlag)> observer) {
    mutationObserver_ = observer;
  }

  void notifyMutation(DirtyFlag hint = DirtyFlag::NeedsLayout) override {
    if (mutationObserver_) {
      mutationObserver_(hint);
    }
    Node::notifyMutation(hint);
  }

  std::string contentType() const { return contentType_; }
  void setContentType(const std::string &type) { contentType_ = type; }

  std::string characterSet() const { return characterSet_; }
  void setCharacterSet(const std::string &charset) { characterSet_ = charset; }

  std::string url() const { return url_; }
  void setUrl(const std::string &url) { url_ = url; }

  std::string documentUri() const { return url_; }

  std::string compatMode() const { return compatMode_; }
  void setCompatMode(const std::string &mode) { compatMode_ = mode; }

  std::string doctypeName() const { return doctypeName_; }
  void setDoctypeName(const std::string &name) { doctypeName_ = name; }

  NodePtr doctype() const {
    for (const auto &child : childNodes_) {
      if (child->nodeType() == NodeType::DocumentType) {
        return child;
      }
    }
    return nullptr;
  }

  // WHATWG: the first element child of the document (not hard-coded to html)
  ElementPtr documentElement() const {
    for (const auto &child : childNodes_) {
      if (child->nodeType() == NodeType::Element) {
        return std::static_pointer_cast<Element>(child);
      }
    }
    return nullptr;
  }

  ElementPtr head() const {
    auto html = documentElement();
    if (!html)
      return nullptr;

    for (const auto &child : html->childNodes()) {
      if (child->nodeType() == NodeType::Element) {
        auto elem = std::static_pointer_cast<Element>(child);
        if (toLower(elem->localName()) == "head") {
          return elem;
        }
      }
    }
    return nullptr;
  }

  ElementPtr body() const {
    auto html = documentElement();
    if (!html)
      return nullptr;

    for (const auto &child : html->childNodes()) {
      if (child->nodeType() == NodeType::Element) {
        auto elem = std::static_pointer_cast<Element>(child);
        if (toLower(elem->localName()) == "body") {
          return elem;
        }
      }
    }
    return nullptr;
  }

  void setBody(ElementPtr newBody) {
    auto html = documentElement();
    if (!html)
      return;

    auto oldBody = body();
    if (oldBody) {
      html->replaceChild(newBody, oldBody);
    } else {
      html->appendChild(newBody);
    }
  }

  ElementPtr title() const {
    auto h = head();
    if (!h)
      return nullptr;

    for (const auto &child : h->childNodes()) {
      if (child->nodeType() == NodeType::Element) {
        auto elem = std::static_pointer_cast<Element>(child);
        if (toLower(elem->localName()) == "title") {
          return elem;
        }
      }
    }
    return nullptr;
  }

  std::string titleText() const {
    auto t = title();
    return t ? t->textContent() : "";
  }

  void setTitleText(const std::string &text) {
    auto t = title();
    if (t) {
      t->setTextContent(text);
    }
  }

  ElementPtr createElement(const std::string &localName) {
    return std::make_shared<Element>(localName);
  }

  ElementPtr createElementNS(const std::string &namespaceUri,
                             const std::string &qualifiedName) {
    std::string prefix;
    std::string localName = qualifiedName;

    size_t colonPos = qualifiedName.find(':');
    if (colonPos != std::string::npos) {
      prefix = qualifiedName.substr(0, colonPos);
      localName = qualifiedName.substr(colonPos + 1);
    }

    return std::make_shared<Element>(localName, namespaceUri, prefix);
  }

  NodePtr createTextNode(const std::string &data) {
    return std::make_shared<TextNode>(data);
  }

  NodePtr createComment(const std::string &data) {
    return std::make_shared<CommentNode>(data);
  }

  std::shared_ptr<ProcessingInstructionNode>
  createProcessingInstruction(const std::string &target,
                              const std::string &data) {
    return std::make_shared<ProcessingInstructionNode>(target, data);
  }

  // Deep-clone a node from another document into this one (no owner-document
  // tracking yet, so import == clone; the API matches the WHATWG signature).
  NodePtr importNode(const NodePtr &node, bool deep = true) {
    if (!node)
      return nullptr;
    return node->cloneNode(deep);
  }

  // Remove a node from its current tree, transferring it to this document
  NodePtr adoptNode(const NodePtr &node) {
    if (!node)
      return nullptr;
    if (auto parent = node->parentNode()) {
      parent->removeChild(node);
    }
    return node;
  }

  NodePtr createDocumentType(const std::string &name,
                             const std::string &publicId = "",
                             const std::string &systemId = "") {
    return std::make_shared<DocumentTypeNode>(name, publicId, systemId);
  }

  std::shared_ptr<DocumentFragment> createDocumentFragment() {
    return std::make_shared<DocumentFragment>();
  }

  ElementPtr getElementById(const std::string &id) const {
    auto html = documentElement();
    if (!html)
      return nullptr;
    return html->getElementById(id);
  }

  std::vector<ElementPtr> getElementsByTagName(const std::string &name) const {
    auto html = documentElement();
    if (!html)
      return {};
    return html->getElementsByTagName(name);
  }

  std::vector<ElementPtr>
  getElementsByClassName(const std::string &className) const {
    auto html = documentElement();
    if (!html)
      return {};
    return html->getElementsByClassName(className);
  }

  std::vector<ElementPtr> getElementsByName(const std::string &name) const {
    std::vector<ElementPtr> result;
    auto root = documentElement();
    if (!root)
      return result;
    for (const auto &el : root->getElementsByTagName("*")) {
      auto value = el->getAttribute("name");
      if (value.has_value() && value.value() == name) {
        result.push_back(el);
      }
    }
    return result;
  }

  // Defined in dom/dom_enhancements.hpp (needs the selector engine).
  // Unlike the element methods, the document itself is the search root, so
  // documentElement is a candidate (e.g. document.querySelector("html")).
  std::vector<ElementPtr> querySelectorAll(const std::string &selector) const;
  ElementPtr querySelector(const std::string &selector) const;

  NodePtr cloneNode(bool deep = false) const override {
    auto cloned = std::make_shared<Document>();
    cloned->contentType_ = contentType_;
    cloned->characterSet_ = characterSet_;
    cloned->url_ = url_;
    cloned->compatMode_ = compatMode_;
    cloned->doctypeName_ = doctypeName_;

    if (deep) {
      for (const auto &child : childNodes_) {
        cloned->appendChild(child->cloneNode(true));
      }
    }

    return cloned;
  }

  std::string toHtml() const override {
    std::string result;

    auto dt = doctype();
    if (dt) {
      result += dt->toHtml() + "\n";
    }

    for (const auto &child : childNodes_) {
      if (child->nodeType() != NodeType::DocumentType) {
        result += child->toHtml();
      }
    }

    return result;
  }

private:
  std::function<void(DirtyFlag)> mutationObserver_;
  std::string contentType_ = "text/html";
  std::string characterSet_ = "UTF-8";
  std::string url_;
  std::string compatMode_ = "CSS1Compat";
  std::string doctypeName_;
};

} // namespace dom
} // namespace xiaopeng
