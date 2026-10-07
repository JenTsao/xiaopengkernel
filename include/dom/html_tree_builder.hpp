#pragma once

#include "../loader/error.hpp"
#include "dom.hpp"
#include "html_tokenizer.hpp"
#include <functional>
#include <stack>
#include <unordered_set>

namespace xiaopeng {
namespace dom {

enum class InsertionMode : uint8_t {
  Initial,
  BeforeHtml,
  BeforeHead,
  InHead,
  InHeadNoscript,
  AfterHead,
  InBody,
  Text,
  InTable,
  InTableText,
  InCaption,
  InColumnGroup,
  InTableBody,
  InRow,
  InCell,
  InSelect,
  InSelectInTable,
  InTemplate,
  AfterBody,
  InFrameset,
  AfterFrameset,
  AfterAfterBody,
  AfterAfterFrameset,
  Plaintext
};

struct StackItem {
  ElementPtr element;
  Token token;

  StackItem(ElementPtr e, const Token &t) : element(std::move(e)), token(t) {}
};

class HtmlTreeBuilder {
public:
  using ErrorCallback = std::function<void(const ParseError &)>;

  HtmlTreeBuilder();

  std::shared_ptr<Document> build(const std::vector<Token> &tokens);
  std::shared_ptr<Document> build(HtmlTokenizer &tokenizer);
  std::shared_ptr<Document> build(const std::string &html);
  std::shared_ptr<Document> build(const loader::ByteBuffer &html);

  // WHATWG fragment parsing: parse html with the given context element
  // (e.g. a "td" or "tr" so table fragments parse correctly). The context is
  // cloned into an internal document; the newly parsed children of the clone
  // are returned as detached nodes ready to be adopted by the caller.
  std::vector<std::shared_ptr<Node>>
  buildFragment(const std::string &html, const ElementPtr &context);

  void setErrorCallback(ErrorCallback callback) {
    errorCallback_ = std::move(callback);
  }

  bool hasError() const { return !errors_.empty(); }
  const std::vector<ParseError> &errors() const { return errors_; }

  void setScriptingFlag(bool scripting) { scripting_ = scripting; }
  bool scripting() const { return scripting_; }

  bool quirksMode() const { return quirksMode_; }

private:
  void emitError(const std::string &message, const Token &token);

  void processToken(const Token &token);
  void processUsingRulesFor(InsertionMode mode, const Token &token);

  void processInitialInsertionMode(const Token &token);
  void processBeforeHtmlInsertionMode(const Token &token);
  void processBeforeHeadInsertionMode(const Token &token);
  void processInHeadInsertionMode(const Token &token);
  void processAfterHeadInsertionMode(const Token &token);
  void processInBodyInsertionMode(const Token &token);
  void processTextInsertionMode(const Token &token);
  void processInTableInsertionMode(const Token &token);
  void processInTableTextInsertionMode(const Token &token);
  void processInCaptionInsertionMode(const Token &token);
  void processInColumnGroupInsertionMode(const Token &token);
  void processInTableBodyInsertionMode(const Token &token);
  void processInRowInsertionMode(const Token &token);
  void processInCellInsertionMode(const Token &token);
  void processInSelectInsertionMode(const Token &token);
  void processInSelectInTableInsertionMode(const Token &token);
  void processAfterBodyInsertionMode(const Token &token);
  void processAfterAfterBodyInsertionMode(const Token &token);

  void insertDoctype(const Token &token);
  void insertComment(const Token &token);
  void insertCharacter(const Token &token);
  void insertElement(const Token &token);
  void insertElement(const std::string &tagName);
  void insertHtmlElement();
  void insertHeadElement();
  void insertBodyElement();

  void insertTextNode(const std::string &data);

  ElementPtr createElement(const Token &token);
  ElementPtr createElement(const std::string &tagName);

  void pushStack(const ElementPtr &element, const Token &token);
  void popStack();
  ElementPtr currentElement() const;
  ElementPtr adjustedCurrentNode() const;
  bool isStackEmpty() const;
  size_t stackSize() const;

  void pushTemplateStack(const ElementPtr &element);
  void popTemplateStack();
  bool inTemplateStack() const;

  void openElementsTo(const std::string &tagName);
  void openElementsToIncluding(const std::string &tagName);
  void generateImpliedEndTags(const std::string &exclude = "");
  void generateAllImpliedEndTags();

  bool isSpecialElement(const ElementPtr &element) const;
  bool isFormattingElement(const ElementPtr &element) const;
  bool isPhrasingContent(const ElementPtr &element) const;

  bool hasElementInScope(const std::string &tagName) const;
  bool hasElementInListItemScope(const std::string &tagName) const;
  bool hasElementInButtonScope(const std::string &tagName) const;
  bool hasElementInTableScope(const std::string &tagName) const;
  bool hasElementInSelectScope(const std::string &tagName) const;

  void reconstructActiveFormattingElements();
  void pushActiveFormattingElements(const ElementPtr &element);
  void clearActiveFormattingElements();

  void acknowledgeSelfClosingFlag();

  void setFramesetOkFlag(bool ok) { framesetOk_ = ok; }
  bool framesetOk() const { return framesetOk_; }

  void setQuirksMode(bool quirks) { quirksMode_ = quirks; }

  void stopParsing() { stopParsing_ = true; }
  bool isStopped() const { return stopParsing_; }

  void switchTo(InsertionMode mode) { insertionMode_ = mode; }
  void switchToOriginalInsertionMode() {
    insertionMode_ = originalInsertionMode_;
  }

  std::string pendingTableCharacterTokens_;

  static bool isSpecialTagName(const std::string &tagName);
  static bool isFormattingTagName(const std::string &tagName);
  static bool isVoidTagName(const std::string &tagName);
  static bool isRawTextTagName(const std::string &tagName);
  static bool isEscapableRawTextTagName(const std::string &tagName);
  static bool isBasicTagName(const std::string &tagName);
  static InsertionMode insertionModeForContext(const std::string &tagName);

  std::shared_ptr<Document> document_;
  std::vector<StackItem> stack_;
  std::vector<ElementPtr> templateStack_;
  std::vector<ElementPtr> activeFormattingElements_;

  InsertionMode insertionMode_ = InsertionMode::Initial;
  InsertionMode originalInsertionMode_ = InsertionMode::Initial;

  ElementPtr headElement_;
  ElementPtr formElement_;

  bool framesetOk_ = true;
  bool quirksMode_ = false;
  bool scripting_ = false;
  bool stopParsing_ = false;
  bool fosterParenting_ = false;
  bool ignoreNextLineFeed_ = false;

  Token pendingToken_;
  bool hasPendingToken_ = false;

  std::vector<ParseError> errors_;
  ErrorCallback errorCallback_;
};

inline HtmlTreeBuilder::HtmlTreeBuilder() {
  document_ = std::make_shared<Document>();
}

inline void HtmlTreeBuilder::emitError(const std::string &message,
                                       const Token &token) {
  ParseError error(message, token.position, token.line, token.column);
  errors_.push_back(error);
  if (errorCallback_) {
    errorCallback_(error);
  }
}

inline std::shared_ptr<Document>
HtmlTreeBuilder::build(const std::vector<Token> &tokens) {
  document_ = std::make_shared<Document>();
  stack_.clear();
  templateStack_.clear();
  activeFormattingElements_.clear();
  insertionMode_ = InsertionMode::Initial;
  originalInsertionMode_ = InsertionMode::Initial;
  headElement_.reset();
  formElement_.reset();
  framesetOk_ = true;
  quirksMode_ = false;
  stopParsing_ = false;
  fosterParenting_ = false;
  errors_.clear();

  for (const auto &token : tokens) {
    if (stopParsing_)
      break;
    processToken(token);
  }

  return document_;
}

inline std::shared_ptr<Document>
HtmlTreeBuilder::build(HtmlTokenizer &tokenizer) {
  return build(tokenizer.tokenize());
}

inline std::shared_ptr<Document>
HtmlTreeBuilder::build(const std::string &html) {
  HtmlTokenizer tokenizer(html);
  return build(tokenizer);
}

inline std::shared_ptr<Document>
HtmlTreeBuilder::build(const loader::ByteBuffer &html) {
  HtmlTokenizer tokenizer(html);
  return build(tokenizer);
}

inline HtmlTreeBuilder::InsertionMode
HtmlTreeBuilder::insertionModeForContext(const std::string &tagName) {
  if (tagName == "select")
    return InsertionMode::InSelect;
  if (tagName == "td" || tagName == "th")
    return InsertionMode::InCell;
  if (tagName == "tr")
    return InsertionMode::InRow;
  if (tagName == "tbody" || tagName == "thead" || tagName == "tfoot")
    return InsertionMode::InTableBody;
  if (tagName == "caption")
    return InsertionMode::InCaption;
  if (tagName == "colgroup" || tagName == "col")
    return InsertionMode::InColumnGroup;
  if (tagName == "table")
    return InsertionMode::InTable;
  if (tagName == "head" || tagName == "noscript")
    return InsertionMode::InHead;
  if (tagName == "frameset")
    return InsertionMode::InFrameset;
  return InsertionMode::InBody;
}

inline std::vector<std::shared_ptr<Node>>
HtmlTreeBuilder::buildFragment(const std::string &html,
                               const ElementPtr &context) {
  // Fresh state (same reset sequence as build())
  document_ = std::make_shared<Document>();
  stack_.clear();
  templateStack_.clear();
  activeFormattingElements_.clear();
  insertionMode_ = InsertionMode::Initial;
  originalInsertionMode_ = InsertionMode::Initial;
  headElement_.reset();
  formElement_.reset();
  framesetOk_ = true;
  quirksMode_ = false;
  stopParsing_ = false;
  fosterParenting_ = false;
  errors_.clear();

  // Bootstrap html + body like a document parse would
  insertHtmlElement();
  insertBodyElement();

  auto body = document_->body();
  auto contextClone = std::dynamic_pointer_cast<Element>(
      context ? context->cloneNode(false) : nullptr);
  if (!body || !contextClone) {
    return {};
  }

  body->appendChild(contextClone);
  Token contextToken = Token::makeStartTag(contextClone->localName());
  pushStack(contextClone, contextToken);
  insertionMode_ =
      insertionModeForContext(toLower(contextClone->localName()));
  originalInsertionMode_ = InsertionMode::InBody;

  HtmlTokenizer tokenizer(html);
  auto tokens = tokenizer.tokenize();
  for (const auto &token : tokens) {
    if (stopParsing_)
      break;
    processToken(token);
  }

  auto children = contextClone->childNodes();
  return std::vector<std::shared_ptr<Node>>(children.begin(), children.end());
}

inline void HtmlTreeBuilder::processToken(const Token &token) {
  if (hasPendingToken_) {
    hasPendingToken_ = false;
    processToken(pendingToken_);
    if (stopParsing_)
      return;
  }

  processUsingRulesFor(insertionMode_, token);
}

inline void HtmlTreeBuilder::processUsingRulesFor(InsertionMode mode,
                                                  const Token &token) {
  switch (mode) {
  case InsertionMode::Initial:
    processInitialInsertionMode(token);
    break;
  case InsertionMode::BeforeHtml:
    processBeforeHtmlInsertionMode(token);
    break;
  case InsertionMode::BeforeHead:
    processBeforeHeadInsertionMode(token);
    break;
  case InsertionMode::InHead:
    processInHeadInsertionMode(token);
    break;
  case InsertionMode::AfterHead:
    processAfterHeadInsertionMode(token);
    break;
  case InsertionMode::InBody:
    processInBodyInsertionMode(token);
    break;
  case InsertionMode::Text:
    processTextInsertionMode(token);
    break;
  case InsertionMode::InTable:
    processInTableInsertionMode(token);
    break;
  case InsertionMode::InTableText:
    processInTableTextInsertionMode(token);
    break;
  case InsertionMode::InCaption:
    processInCaptionInsertionMode(token);
    break;
  case InsertionMode::InColumnGroup:
    processInColumnGroupInsertionMode(token);
    break;
  case InsertionMode::InTableBody:
    processInTableBodyInsertionMode(token);
    break;
  case InsertionMode::InRow:
    processInRowInsertionMode(token);
    break;
  case InsertionMode::InCell:
    processInCellInsertionMode(token);
    break;
  case InsertionMode::InSelect:
    processInSelectInsertionMode(token);
    break;
  case InsertionMode::InSelectInTable:
    processInSelectInTableInsertionMode(token);
    break;
  case InsertionMode::AfterBody:
    processAfterBodyInsertionMode(token);
    break;
  case InsertionMode::AfterAfterBody:
    processAfterAfterBodyInsertionMode(token);
    break;
  default:
    break;
  }
}

inline void HtmlTreeBuilder::processInitialInsertionMode(const Token &token) {
  if (token.isDoctype()) {
    insertDoctype(token);
    insertionMode_ = InsertionMode::BeforeHtml;
    return;
  }

  if (token.isCharacter() && isWhitespace(token.data[0])) {
    return;
  }

  if (token.isComment()) {
    insertComment(token);
    return;
  }

  if (token.isStartTag() && toLower(token.name) == "html") {
    insertElement(token);
    insertionMode_ = InsertionMode::BeforeHead;
    return;
  }

  emitError("Expected doctype", token);
  quirksMode_ = true;
  insertHtmlElement();
  insertionMode_ = InsertionMode::BeforeHead;
  processToken(token);
}

inline void
HtmlTreeBuilder::processBeforeHtmlInsertionMode(const Token &token) {
  if (token.isDoctype()) {
    emitError("Unexpected doctype", token);
    return;
  }

  if (token.isComment()) {
    insertComment(token);
    return;
  }

  if (token.isCharacter() && isWhitespace(token.data[0])) {
    return;
  }

  if (token.isStartTag() && toLower(token.name) == "html") {
    insertElement(token);
    insertionMode_ = InsertionMode::BeforeHead;
    return;
  }

  if (token.isEndTag()) {
    static const std::unordered_set<std::string> allowedEndTags = {
        "head", "body", "html", "br"};
    if (allowedEndTags.find(toLower(token.name)) == allowedEndTags.end()) {
      emitError("Unexpected end tag", token);
      return;
    }
  }

  insertHtmlElement();
  insertionMode_ = InsertionMode::BeforeHead;
  processToken(token);
}

inline void
HtmlTreeBuilder::processBeforeHeadInsertionMode(const Token &token) {
  if (token.isCharacter() && isWhitespace(token.data[0])) {
    insertCharacter(token);
    return;
  }

  if (token.isComment()) {
    insertComment(token);
    return;
  }

  if (token.isDoctype()) {
    emitError("Unexpected doctype", token);
    return;
  }

  if (token.isStartTag()) {
    std::string tagName = toLower(token.name);

    if (tagName == "html") {
      processInBodyInsertionMode(token);
      return;
    }

    if (tagName == "head") {
      insertElement(token);
      headElement_ = currentElement();
      insertionMode_ = InsertionMode::InHead;
      return;
    }
  }

  if (token.isEndTag()) {
    static const std::unordered_set<std::string> allowedEndTags = {
        "head", "body", "html", "br"};
    if (allowedEndTags.find(toLower(token.name)) == allowedEndTags.end()) {
      emitError("Unexpected end tag", token);
      return;
    }
  }

  insertHeadElement();
  insertionMode_ = InsertionMode::InHead;
  processToken(token);
}

inline void HtmlTreeBuilder::processInHeadInsertionMode(const Token &token) {
  if (token.isCharacter() && isWhitespace(token.data[0])) {
    insertCharacter(token);
    return;
  }

  if (token.isComment()) {
    insertComment(token);
    return;
  }

  if (token.isDoctype()) {
    emitError("Unexpected doctype", token);
    return;
  }

  if (token.isStartTag()) {
    std::string tagName = toLower(token.name);

    if (tagName == "html") {
      processInBodyInsertionMode(token);
      return;
    }

    if (tagName == "base" || tagName == "basefont" || tagName == "bgsound" ||
        tagName == "link" || tagName == "meta") {
      insertElement(token);
      popStack();
      return;
    }

    if (tagName == "title") {
      insertElement(token);
      originalInsertionMode_ = insertionMode_;
      insertionMode_ = InsertionMode::Text;
      return;
    }

    if (tagName == "noscript") {
      if (!scripting_) {
        insertElement(token);
        insertionMode_ = InsertionMode::InHeadNoscript;
      } else {
        insertElement(token);
        originalInsertionMode_ = insertionMode_;
        insertionMode_ = InsertionMode::Text;
      }
      return;
    }

    if (tagName == "noframes" || tagName == "style") {
      insertElement(token);
      originalInsertionMode_ = insertionMode_;
      insertionMode_ = InsertionMode::Text;
      return;
    }

    if (tagName == "script") {
      insertElement(token);
      originalInsertionMode_ = insertionMode_;
      insertionMode_ = InsertionMode::Text;
      return;
    }

    if (tagName == "head") {
      emitError("Unexpected head start tag", token);
      return;
    }
  }

  if (token.isEndTag()) {
    std::string tagName = toLower(token.name);

    if (tagName == "head") {
      popStack();
      insertionMode_ = InsertionMode::AfterHead;
      return;
    }

    static const std::unordered_set<std::string> allowedEndTags = {
        "body", "html", "br"};
    if (allowedEndTags.find(tagName) == allowedEndTags.end()) {
      emitError("Unexpected end tag in head", token);
      return;
    }
  }

  popStack();
  insertionMode_ = InsertionMode::AfterHead;
  processToken(token);
}

inline void HtmlTreeBuilder::processAfterHeadInsertionMode(const Token &token) {
  if (token.isCharacter() && isWhitespace(token.data[0])) {
    insertCharacter(token);
    return;
  }

  if (token.isComment()) {
    insertComment(token);
    return;
  }

  if (token.isDoctype()) {
    emitError("Unexpected doctype", token);
    return;
  }

  if (token.isStartTag()) {
    std::string tagName = toLower(token.name);

    if (tagName == "html") {
      processInBodyInsertionMode(token);
      return;
    }

    if (tagName == "body") {
      insertElement(token);
      framesetOk_ = false;
      insertionMode_ = InsertionMode::InBody;
      return;
    }

    if (tagName == "frameset") {
      insertElement(token);
      insertionMode_ = InsertionMode::InFrameset;
      return;
    }

    if (tagName == "base" || tagName == "basefont" || tagName == "bgsound" ||
        tagName == "link" || tagName == "meta" || tagName == "noframes" ||
        tagName == "script" || tagName == "style" || tagName == "template" ||
        tagName == "title") {
      emitError("Unexpected start tag after head", token);
      if (!headElement_) {
        insertHeadElement();
      }
      pushStack(headElement_, Token::makeStartTag("head"));
      processInHeadInsertionMode(token);
      if (!stack_.empty() && stack_.back().element == headElement_) {
        popStack();
      }
      return;
    }

    if (tagName == "head") {
      emitError("Unexpected head start tag", token);
      return;
    }
  }

  if (token.isEndTag()) {
    static const std::unordered_set<std::string> allowedEndTags = {
        "body", "html", "br"};
    if (allowedEndTags.find(toLower(token.name)) == allowedEndTags.end()) {
      emitError("Unexpected end tag after head", token);
      return;
    }
  }

  insertBodyElement();
  insertionMode_ = InsertionMode::InBody;
  processToken(token);
}

inline void HtmlTreeBuilder::processInBodyInsertionMode(const Token &token) {
  if (token.isCharacter()) {
    reconstructActiveFormattingElements();

    if (token.data == "\n" && ignoreNextLineFeed_) {
      ignoreNextLineFeed_ = false;
      return;
    }
    ignoreNextLineFeed_ = false;

    insertCharacter(token);
    framesetOk_ = false;
    return;
  }

  if (token.isComment()) {
    insertComment(token);
    return;
  }

  if (token.isDoctype()) {
    emitError("Unexpected doctype", token);
    return;
  }

  if (token.isStartTag()) {
    std::string tagName = toLower(token.name);

    if (tagName == "html") {
      emitError("Unexpected html start tag", token);
      if (stackSize() >= 1) {
        auto html = stack_[0].element;
        for (const auto &attr : token.attributes) {
          if (!html->hasAttribute(attr.name)) {
            html->setAttribute(attr.name, attr.value);
          }
        }
      }
      return;
    }

    if (tagName == "base" || tagName == "basefont" || tagName == "bgsound" ||
        tagName == "link" || tagName == "meta" || tagName == "noframes" ||
        tagName == "script" || tagName == "style" || tagName == "template" ||
        tagName == "title") {
      processInHeadInsertionMode(token);
      return;
    }

    if (tagName == "body") {
      emitError("Unexpected body start tag", token);
      if (stackSize() >= 2) {
        auto body = stack_[1].element;
        if (body && toLower(body->localName()) == "body") {
          framesetOk_ = false;
          for (const auto &attr : token.attributes) {
            if (!body->hasAttribute(attr.name)) {
              body->setAttribute(attr.name, attr.value);
            }
          }
        }
      }
      return;
    }

    if (tagName == "frameset") {
      emitError("Unexpected frameset start tag", token);
      if (!framesetOk_)
        return;

      if (stackSize() >= 2) {
        auto body = stack_[1].element;
        if (body && toLower(body->localName()) == "body") {
          body->parentNode()->removeChild(body);
        }
      }

      while (stackSize() > 1) {
        popStack();
      }

      insertElement(token);
      insertionMode_ = InsertionMode::InFrameset;
      return;
    }

    static const std::unordered_set<std::string> addressLikeTags = {
        "address", "article", "aside",  "blockquote", "center",   "details",
        "dialog",  "dir",     "div",    "dl",         "fieldset", "figcaption",
        "figure",  "footer",  "header", "hgroup",     "main",     "menu",
        "nav",     "ol",      "p",      "section",    "summary",  "ul"};
    if (addressLikeTags.find(tagName) != addressLikeTags.end()) {
      if (hasElementInButtonScope("p")) {
        Token endP = Token::makeEndTag("p");
        processInBodyInsertionMode(endP);
      }
      insertElement(token);
      return;
    }

    if (tagName == "h1" || tagName == "h2" || tagName == "h3" ||
        tagName == "h4" || tagName == "h5" || tagName == "h6") {
      if (hasElementInButtonScope("p")) {
        Token endP = Token::makeEndTag("p");
        processInBodyInsertionMode(endP);
      }

      if (stackSize() > 0) {
        auto current = currentElement();
        if (current) {
          std::string currentTag = toLower(current->localName());
          if (currentTag == "h1" || currentTag == "h2" || currentTag == "h3" ||
              currentTag == "h4" || currentTag == "h5" || currentTag == "h6") {
            emitError("Heading nested in heading", token);
            popStack();
          }
        }
      }

      insertElement(token);
      return;
    }

    if (tagName == "pre" || tagName == "listing") {
      if (hasElementInButtonScope("p")) {
        Token endP = Token::makeEndTag("p");
        processInBodyInsertionMode(endP);
      }
      insertElement(token);
      ignoreNextLineFeed_ = true;
      framesetOk_ = false;
      return;
    }

    if (tagName == "form") {
      if (formElement_) {
        emitError("Form nested in form", token);
        return;
      }
      if (hasElementInButtonScope("p")) {
        Token endP = Token::makeEndTag("p");
        processInBodyInsertionMode(endP);
      }
      insertElement(token);
      formElement_ = currentElement();
      return;
    }

    if (tagName == "li") {
      framesetOk_ = false;

      for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
        std::string nodeTag = toLower(it->element->localName());
        if (nodeTag == "li") {
          Token endLi = Token::makeEndTag("li");
          processInBodyInsertionMode(endLi);
          break;
        }
        if (isSpecialElement(it->element) && nodeTag != "address" &&
            nodeTag != "div" && nodeTag != "p") {
          break;
        }
      }

      if (hasElementInButtonScope("p")) {
        Token endP = Token::makeEndTag("p");
        processInBodyInsertionMode(endP);
      }

      insertElement(token);
      return;
    }

    if (tagName == "dd" || tagName == "dt") {
      framesetOk_ = false;

      for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
        std::string nodeTag = toLower(it->element->localName());
        if (nodeTag == "dd" || nodeTag == "dt") {
          Token endTag = Token::makeEndTag(nodeTag);
          processInBodyInsertionMode(endTag);
          break;
        }
        if (isSpecialElement(it->element) && nodeTag != "address" &&
            nodeTag != "div" && nodeTag != "p") {
          break;
        }
      }

      if (hasElementInButtonScope("p")) {
        Token endP = Token::makeEndTag("p");
        processInBodyInsertionMode(endP);
      }

      insertElement(token);
      return;
    }

    if (tagName == "plaintext") {
      if (hasElementInButtonScope("p")) {
        Token endP = Token::makeEndTag("p");
        processInBodyInsertionMode(endP);
      }
      insertElement(token);
      insertionMode_ = InsertionMode::Plaintext;
      return;
    }

    if (tagName == "button") {
      if (hasElementInScope("button")) {
        emitError("Button nested in button", token);
        Token endButton = Token::makeEndTag("button");
        processInBodyInsertionMode(endButton);
        processToken(token);
        return;
      }

      reconstructActiveFormattingElements();
      insertElement(token);
      framesetOk_ = false;
      return;
    }

    if (tagName == "a") {
      for (auto it = activeFormattingElements_.rbegin();
           it != activeFormattingElements_.rend(); ++it) {
        auto elem = *it;
        if (!elem)
          break;
        if (toLower(elem->localName()) == "a") {
          emitError("Anchor nested in anchor", token);
          Token endA = Token::makeEndTag("a");
          processInBodyInsertionMode(endA);
          break;
        }
      }

      reconstructActiveFormattingElements();
      insertElement(token);
      pushActiveFormattingElements(currentElement());
      return;
    }

    static const std::unordered_set<std::string> formattingTags = {
        "b", "big",   "code",   "em",     "font", "i",
        "s", "small", "strike", "strong", "tt",   "u"};
    if (formattingTags.find(tagName) != formattingTags.end()) {
      reconstructActiveFormattingElements();
      insertElement(token);
      pushActiveFormattingElements(currentElement());
      return;
    }

    if (tagName == "nobr") {
      reconstructActiveFormattingElements();
      if (hasElementInScope("nobr")) {
        emitError("Nobr nested in nobr", token);
        Token endNobr = Token::makeEndTag("nobr");
        processInBodyInsertionMode(endNobr);
        reconstructActiveFormattingElements();
      }
      insertElement(token);
      pushActiveFormattingElements(currentElement());
      return;
    }

    if (tagName == "table") {
      if (!quirksMode_ && hasElementInButtonScope("p")) {
        Token endP = Token::makeEndTag("p");
        processInBodyInsertionMode(endP);
      }
      insertElement(token);
      framesetOk_ = false;
      insertionMode_ = InsertionMode::InTable;
      return;
    }

    if (tagName == "input") {
      reconstructActiveFormattingElements();
      insertElement(token);
      popStack();

      auto typeAttr = token.getAttribute("type");
      if (!typeAttr || toLower(*typeAttr) != "hidden") {
        framesetOk_ = false;
      }
      return;
    }

    if (tagName == "hr") {
      if (hasElementInButtonScope("p")) {
        Token endP = Token::makeEndTag("p");
        processInBodyInsertionMode(endP);
      }
      insertElement(token);
      popStack();
      framesetOk_ = false;
      return;
    }

    if (tagName == "image") {
      emitError("Image tag should be img", token);
      Token imgToken = token;
      imgToken.name = "img";
      processInBodyInsertionMode(imgToken);
      return;
    }

    if (tagName == "img") {
      reconstructActiveFormattingElements();
      insertElement(token);
      popStack();
      framesetOk_ = false;
      return;
    }

    if (tagName == "br") {
      reconstructActiveFormattingElements();
      insertElement(token);
      popStack();
      framesetOk_ = false;
      return;
    }

    if (tagName == "textarea") {
      insertElement(token);
      ignoreNextLineFeed_ = true;
      originalInsertionMode_ = insertionMode_;
      framesetOk_ = false;
      insertionMode_ = InsertionMode::Text;
      return;
    }

    if (tagName == "xmp") {
      if (hasElementInButtonScope("p")) {
        Token endP = Token::makeEndTag("p");
        processInBodyInsertionMode(endP);
      }
      reconstructActiveFormattingElements();
      framesetOk_ = false;
      insertElement(token);
      originalInsertionMode_ = insertionMode_;
      insertionMode_ = InsertionMode::Text;
      return;
    }

    if (tagName == "iframe") {
      framesetOk_ = false;
      insertElement(token);
      originalInsertionMode_ = insertionMode_;
      insertionMode_ = InsertionMode::Text;
      return;
    }

    if (tagName == "select") {
      reconstructActiveFormattingElements();
      insertElement(token);
      framesetOk_ = false;

      if (insertionMode_ == InsertionMode::InTable ||
          insertionMode_ == InsertionMode::InCaption ||
          insertionMode_ == InsertionMode::InTableBody ||
          insertionMode_ == InsertionMode::InRow ||
          insertionMode_ == InsertionMode::InCell) {
        insertionMode_ = InsertionMode::InSelectInTable;
      } else {
        insertionMode_ = InsertionMode::InSelect;
      }
      return;
    }

    if (tagName == "optgroup" || tagName == "option") {
      if (currentElement() &&
          toLower(currentElement()->localName()) == "option") {
        Token endOption = Token::makeEndTag("option");
        processInBodyInsertionMode(endOption);
      }
      reconstructActiveFormattingElements();
      insertElement(token);
      return;
    }

    if (tagName == "math" || tagName == "svg") {
      reconstructActiveFormattingElements();
      insertElement(token);
      if (token.selfClosing) {
        popStack();
      }
      return;
    }

    if (tagName == "caption" || tagName == "col" || tagName == "colgroup" ||
        tagName == "frame" || tagName == "head" || tagName == "tbody" ||
        tagName == "td" || tagName == "tfoot" || tagName == "th" ||
        tagName == "thead" || tagName == "tr") {
      emitError("Unexpected start tag", token);
      return;
    }

    reconstructActiveFormattingElements();
    insertElement(token);
    return;
  }

  if (token.isEndTag()) {
    std::string tagName = toLower(token.name);

    if (tagName == "body") {
      if (!hasElementInScope("body")) {
        emitError("Unexpected body end tag", token);
        return;
      }
      insertionMode_ = InsertionMode::AfterBody;
      return;
    }

    if (tagName == "html") {
      if (!hasElementInScope("body")) {
        emitError("Unexpected html end tag", token);
        return;
      }
      insertionMode_ = InsertionMode::AfterBody;
      processToken(token);
      return;
    }

    static const std::unordered_set<std::string> addressLikeEndTags = {
        "address",    "article", "aside",  "blockquote", "button", "center",
        "details",    "dialog",  "dir",    "div",        "dl",     "fieldset",
        "figcaption", "figure",  "footer", "header",     "hgroup", "listing",
        "main",       "menu",    "nav",    "ol",         "pre",    "section",
        "summary",    "ul"};
    if (addressLikeEndTags.find(tagName) != addressLikeEndTags.end()) {
      if (!hasElementInScope(tagName)) {
        emitError("Unexpected end tag", token);
        return;
      }
      generateImpliedEndTags();
      if (currentElement() &&
          toLower(currentElement()->localName()) != tagName) {
        emitError("End tag mismatch", token);
      }
      openElementsTo(tagName);
      popStack();
      return;
    }

    if (tagName == "form") {
      if (!formElement_) {
        emitError("Unexpected form end tag", token);
        return;
      }
      auto form = formElement_;
      formElement_.reset();

      if (!hasElementInScope("form")) {
        emitError("Unexpected form end tag", token);
        return;
      }

      generateImpliedEndTags();
      if (currentElement() != form) {
        emitError("End tag mismatch", token);
      }
      openElementsTo("form");
      popStack();
      return;
    }

    if (tagName == "p") {
      if (!hasElementInButtonScope("p")) {
        emitError("Unexpected p end tag", token);
        Token startP = Token::makeStartTag("p");
        processInBodyInsertionMode(startP);
      }
      generateImpliedEndTags("p");
      if (currentElement() && toLower(currentElement()->localName()) != "p") {
        emitError("End tag mismatch", token);
      }
      openElementsTo("p");
      popStack();
      return;
    }

    if (tagName == "li") {
      if (!hasElementInListItemScope("li")) {
        emitError("Unexpected li end tag", token);
        return;
      }
      generateImpliedEndTags("li");
      if (currentElement() && toLower(currentElement()->localName()) != "li") {
        emitError("End tag mismatch", token);
      }
      openElementsTo("li");
      popStack();
      return;
    }

    if (tagName == "dd" || tagName == "dt") {
      if (!hasElementInScope(tagName)) {
        emitError("Unexpected end tag", token);
        return;
      }
      generateImpliedEndTags(tagName);
      if (currentElement() &&
          toLower(currentElement()->localName()) != tagName) {
        emitError("End tag mismatch", token);
      }
      openElementsTo(tagName);
      popStack();
      return;
    }

    if (tagName == "h1" || tagName == "h2" || tagName == "h3" ||
        tagName == "h4" || tagName == "h5" || tagName == "h6") {
      static const std::unordered_set<std::string> headingTags = {
          "h1", "h2", "h3", "h4", "h5", "h6"};
      bool found = false;
      for (const auto &tag : headingTags) {
        if (hasElementInScope(tag)) {
          found = true;
          break;
        }
      }
      if (!found) {
        emitError("Unexpected heading end tag", token);
        return;
      }
      generateImpliedEndTags();
      if (currentElement() &&
          headingTags.find(toLower(currentElement()->localName())) ==
              headingTags.end()) {
        emitError("End tag mismatch", token);
      }
      for (const auto &tag : headingTags) {
        if (hasElementInScope(tag)) {
          openElementsTo(tag);
          popStack();
          break;
        }
      }
      return;
    }

    static const std::unordered_set<std::string> formattingEndTags = {
        "a",    "b", "big",   "code",   "em",     "font", "i",
        "nobr", "s", "small", "strike", "strong", "tt",   "u"};
    if (formattingEndTags.find(tagName) != formattingEndTags.end()) {
      for (int i = static_cast<int>(activeFormattingElements_.size()) - 1;
           i >= 0; --i) {
        auto elem = activeFormattingElements_[i];
        if (!elem)
          continue;

        if (toLower(elem->localName()) == tagName) {
          if (!hasElementInScope(tagName)) {
            emitError("Unexpected end tag", token);
            return;
          }

          if (currentElement() != elem) {
            emitError("End tag mismatch", token);
          }

          for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
            if (it->element == elem) {
              stack_.erase(std::next(it).base());
              break;
            }
          }

          activeFormattingElements_.erase(activeFormattingElements_.begin() +
                                          i);
          return;
        }
      }
      emitError("No matching start tag", token);
      return;
    }

    if (tagName == "br") {
      emitError("Unexpected br end tag", token);
      Token startBr = Token::makeStartTag("br");
      processInBodyInsertionMode(startBr);
      return;
    }

    generateImpliedEndTags();
    if (currentElement() && toLower(currentElement()->localName()) != tagName) {
      emitError("End tag mismatch", token);
    }
    openElementsTo(tagName);
    return;
  }

  if (token.isEndOfFile()) {
    generateAllImpliedEndTags();

    while (!stack_.empty()) {
      auto elem = currentElement();
      if (elem && toLower(elem->localName()) != "body" &&
          toLower(elem->localName()) != "html") {
        emitError("Unclosed elements", token);
      }
      popStack();
    }

    stopParsing_ = true;
  }
}

inline void HtmlTreeBuilder::processTextInsertionMode(const Token &token) {
  if (token.isCharacter()) {
    insertCharacter(token);
    return;
  }

  if (token.isEndOfFile()) {
    emitError("Unexpected EOF in text", token);
    popStack();
    insertionMode_ = originalInsertionMode_;
    return;
  }

  if (token.isEndTag()) {
    popStack();
    insertionMode_ = originalInsertionMode_;
    return;
  }
}

inline void HtmlTreeBuilder::processInTableInsertionMode(const Token &token) {
  if (token.isCharacter()) {
    pendingTableCharacterTokens_ += token.data;
    insertionMode_ = InsertionMode::InTableText;
    return;
  }

  if (token.isComment()) {
    insertComment(token);
    return;
  }

  if (token.isDoctype()) {
    emitError("Unexpected doctype", token);
    return;
  }

  if (token.isStartTag()) {
    std::string tagName = toLower(token.name);

    if (tagName == "caption") {
      clearActiveFormattingElements();
      insertElement(token);
      insertionMode_ = InsertionMode::InCaption;
      return;
    }

    if (tagName == "colgroup") {
      clearActiveFormattingElements();
      insertElement(token);
      insertionMode_ = InsertionMode::InColumnGroup;
      return;
    }

    if (tagName == "col") {
      clearActiveFormattingElements();
      insertElement(Token::makeStartTag("colgroup"));
      insertionMode_ = InsertionMode::InColumnGroup;
      processToken(token);
      return;
    }

    if (tagName == "tbody" || tagName == "tfoot" || tagName == "thead") {
      clearActiveFormattingElements();
      insertElement(token);
      insertionMode_ = InsertionMode::InTableBody;
      return;
    }

    if (tagName == "td" || tagName == "th" || tagName == "tr") {
      clearActiveFormattingElements();
      insertElement(Token::makeStartTag("tbody"));
      insertionMode_ = InsertionMode::InTableBody;
      processToken(token);
      return;
    }

    if (tagName == "table") {
      emitError("Unexpected table start tag", token);
      if (hasElementInTableScope("table")) {
        Token endTable = Token::makeEndTag("table");
        processInTableInsertionMode(endTable);
        processToken(token);
      }
      return;
    }

    if (tagName == "style" || tagName == "script" || tagName == "template") {
      processInHeadInsertionMode(token);
      return;
    }

    if (tagName == "input") {
      auto typeAttr = token.getAttribute("type");
      if (typeAttr && toLower(*typeAttr) == "hidden") {
        emitError("Unexpected hidden input", token);
        insertElement(token);
        popStack();
        return;
      }
    }

    if (tagName == "form") {
      emitError("Unexpected form in table", token);
      return;
    }
  }

  if (token.isEndTag()) {
    std::string tagName = toLower(token.name);

    if (tagName == "table") {
      if (!hasElementInTableScope("table")) {
        emitError("Unexpected table end tag", token);
        return;
      }
      openElementsTo("table");
      popStack();
      insertionMode_ = InsertionMode::AfterBody;
      return;
    }

    if (tagName == "body" || tagName == "caption" || tagName == "col" ||
        tagName == "colgroup" || tagName == "html" || tagName == "tbody" ||
        tagName == "td" || tagName == "tfoot" || tagName == "th" ||
        tagName == "thead" || tagName == "tr") {
      emitError("Unexpected end tag in table", token);
      return;
    }
  }

  if (token.isEndOfFile()) {
    processInBodyInsertionMode(token);
    return;
  }

  emitError("Unexpected token in table", token);
  fosterParenting_ = true;
  processInBodyInsertionMode(token);
  fosterParenting_ = false;
}

inline void
HtmlTreeBuilder::processInTableTextInsertionMode(const Token &token) {
  if (token.isCharacter()) {
    pendingTableCharacterTokens_ += token.data;
    return;
  }

  if (!pendingTableCharacterTokens_.empty()) {
    bool hasNonWhitespace = false;
    for (char c : pendingTableCharacterTokens_) {
      if (!isWhitespace(c)) {
        hasNonWhitespace = true;
        break;
      }
    }

    if (hasNonWhitespace) {
      emitError("Non-whitespace in table text", token);
      fosterParenting_ = true;
      for (char c : pendingTableCharacterTokens_) {
        Token charToken = Token::makeCharacter(c);
        processInBodyInsertionMode(charToken);
      }
      fosterParenting_ = false;
    } else {
      for (char c : pendingTableCharacterTokens_) {
        insertCharacter(Token::makeCharacter(c));
      }
    }

    pendingTableCharacterTokens_.clear();
  }

  insertionMode_ = InsertionMode::InTable;
  processToken(token);
}

inline void HtmlTreeBuilder::processInCaptionInsertionMode(const Token &token) {
  if (token.isEndTag() && toLower(token.name) == "caption") {
    if (!hasElementInTableScope("caption")) {
      emitError("Unexpected caption end tag", token);
      return;
    }
    generateImpliedEndTags();
    if (currentElement() &&
        toLower(currentElement()->localName()) != "caption") {
      emitError("End tag mismatch", token);
    }
    openElementsTo("caption");
    popStack();
    clearActiveFormattingElements();
    insertionMode_ = InsertionMode::InTable;
    return;
  }

  if (token.isStartTag()) {
    std::string tagName = toLower(token.name);
    if (tagName == "caption" || tagName == "col" || tagName == "colgroup" ||
        tagName == "tbody" || tagName == "td" || tagName == "tfoot" ||
        tagName == "th" || tagName == "thead" || tagName == "tr") {
      emitError("Unexpected start tag in caption", token);
      if (hasElementInTableScope("caption")) {
        Token endCaption = Token::makeEndTag("caption");
        processInCaptionInsertionMode(endCaption);
        processToken(token);
      }
      return;
    }
  }

  if (token.isEndTag() && toLower(token.name) == "table") {
    emitError("Unexpected table end tag in caption", token);
    if (hasElementInTableScope("caption")) {
      Token endCaption = Token::makeEndTag("caption");
      processInCaptionInsertionMode(endCaption);
      processToken(token);
    }
    return;
  }

  processInBodyInsertionMode(token);
}

inline void
HtmlTreeBuilder::processInColumnGroupInsertionMode(const Token &token) {
  if (token.isCharacter() && isWhitespace(token.data[0])) {
    insertCharacter(token);
    return;
  }

  if (token.isComment()) {
    insertComment(token);
    return;
  }

  if (token.isDoctype()) {
    emitError("Unexpected doctype", token);
    return;
  }

  if (token.isStartTag() && toLower(token.name) == "html") {
    processInBodyInsertionMode(token);
    return;
  }

  if (token.isStartTag() && toLower(token.name) == "col") {
    insertElement(token);
    popStack();
    return;
  }

  if (token.isEndTag() && toLower(token.name) == "colgroup") {
    if (currentElement() &&
        toLower(currentElement()->localName()) == "colgroup") {
      popStack();
      insertionMode_ = InsertionMode::InTable;
    } else {
      emitError("Unexpected colgroup end tag", token);
    }
    return;
  }

  if (token.isEndTag() && toLower(token.name) == "col") {
    emitError("Unexpected col end tag", token);
    return;
  }

  if (token.isEndOfFile()) {
    processInBodyInsertionMode(token);
    return;
  }

  if (currentElement() &&
      toLower(currentElement()->localName()) == "colgroup") {
    popStack();
    insertionMode_ = InsertionMode::InTable;
    processToken(token);
    return;
  }

  emitError("Unexpected token in column group", token);
}

inline void
HtmlTreeBuilder::processInTableBodyInsertionMode(const Token &token) {
  if (token.isStartTag() && toLower(token.name) == "tr") {
    clearActiveFormattingElements();
    insertElement(token);
    insertionMode_ = InsertionMode::InRow;
    return;
  }

  if (token.isStartTag() &&
      (toLower(token.name) == "td" || toLower(token.name) == "th")) {
    emitError("Unexpected td/th start tag", token);
    clearActiveFormattingElements();
    insertElement(Token::makeStartTag("tr"));
    insertionMode_ = InsertionMode::InRow;
    processToken(token);
    return;
  }

  if (token.isEndTag() &&
      (toLower(token.name) == "tbody" || toLower(token.name) == "tfoot" ||
       toLower(token.name) == "thead")) {
    if (!hasElementInTableScope(token.name)) {
      emitError("Unexpected end tag", token);
      return;
    }
    clearActiveFormattingElements();
    openElementsTo(token.name);
    popStack();
    insertionMode_ = InsertionMode::InTable;
    return;
  }

  if ((token.isStartTag() &&
       (toLower(token.name) == "caption" || toLower(token.name) == "col" ||
        toLower(token.name) == "colgroup" || toLower(token.name) == "tbody" ||
        toLower(token.name) == "tfoot" || toLower(token.name) == "thead")) ||
      (token.isEndTag() && toLower(token.name) == "table")) {
    std::string tbodyTag;
    for (const auto &tag : {"tbody", "tfoot", "thead"}) {
      if (hasElementInTableScope(tag)) {
        tbodyTag = tag;
        break;
      }
    }

    if (tbodyTag.empty()) {
      emitError("Unexpected token in table body", token);
      return;
    }

    Token endToken = Token::makeEndTag(tbodyTag);
    processInTableBodyInsertionMode(endToken);
    processToken(token);
    return;
  }

  if (token.isEndTag() &&
      (toLower(token.name) == "body" || toLower(token.name) == "caption" ||
       toLower(token.name) == "col" || toLower(token.name) == "colgroup" ||
       toLower(token.name) == "html" || toLower(token.name) == "td" ||
       toLower(token.name) == "th" || toLower(token.name) == "tr")) {
    emitError("Unexpected end tag in table body", token);
    return;
  }

  processInTableInsertionMode(token);
}

inline void HtmlTreeBuilder::processInRowInsertionMode(const Token &token) {
  if (token.isStartTag() &&
      (toLower(token.name) == "td" || toLower(token.name) == "th")) {
    clearActiveFormattingElements();
    insertElement(token);
    insertionMode_ = InsertionMode::InCell;
    return;
  }

  if (token.isEndTag() && toLower(token.name) == "tr") {
    if (!hasElementInTableScope("tr")) {
      emitError("Unexpected tr end tag", token);
      return;
    }
    clearActiveFormattingElements();
    openElementsTo("tr");
    popStack();
    insertionMode_ = InsertionMode::InTableBody;
    return;
  }

  if ((token.isStartTag() &&
       (toLower(token.name) == "caption" || toLower(token.name) == "col" ||
        toLower(token.name) == "colgroup" || toLower(token.name) == "tbody" ||
        toLower(token.name) == "tfoot" || toLower(token.name) == "thead" ||
        toLower(token.name) == "tr")) ||
      (token.isEndTag() && toLower(token.name) == "table")) {
    if (!hasElementInTableScope("tr")) {
      emitError("Unexpected token in row", token);
      return;
    }
    Token endTr = Token::makeEndTag("tr");
    processInRowInsertionMode(endTr);
    processToken(token);
    return;
  }

  if (token.isEndTag() &&
      (toLower(token.name) == "tbody" || toLower(token.name) == "tfoot" ||
       toLower(token.name) == "thead")) {
    if (!hasElementInTableScope(token.name)) {
      emitError("Unexpected end tag in row", token);
      return;
    }
    if (!hasElementInTableScope("tr")) {
      emitError("Unexpected end tag in row", token);
      return;
    }
    Token endTr = Token::makeEndTag("tr");
    processInRowInsertionMode(endTr);
    processToken(token);
    return;
  }

  if (token.isEndTag() &&
      (toLower(token.name) == "body" || toLower(token.name) == "caption" ||
       toLower(token.name) == "col" || toLower(token.name) == "colgroup" ||
       toLower(token.name) == "html" || toLower(token.name) == "td" ||
       toLower(token.name) == "th")) {
    emitError("Unexpected end tag in row", token);
    return;
  }

  processInTableInsertionMode(token);
}

inline void HtmlTreeBuilder::processInCellInsertionMode(const Token &token) {
  if (token.isEndTag() &&
      (toLower(token.name) == "td" || toLower(token.name) == "th")) {
    if (!hasElementInTableScope(token.name)) {
      emitError("Unexpected end tag in cell", token);
      return;
    }
    generateImpliedEndTags();
    if (currentElement() &&
        toLower(currentElement()->localName()) != toLower(token.name)) {
      emitError("End tag mismatch", token);
    }
    openElementsTo(token.name);
    popStack();
    clearActiveFormattingElements();
    insertionMode_ = InsertionMode::InRow;
    return;
  }

  if (token.isStartTag() &&
      (toLower(token.name) == "caption" || toLower(token.name) == "col" ||
       toLower(token.name) == "colgroup" || toLower(token.name) == "tbody" ||
       toLower(token.name) == "td" || toLower(token.name) == "tfoot" ||
       toLower(token.name) == "th" || toLower(token.name) == "thead" ||
       toLower(token.name) == "tr")) {
    if (!hasElementInTableScope("td") && !hasElementInTableScope("th")) {
      emitError("Unexpected start tag in cell", token);
      return;
    }

    Token endCell =
        Token::makeEndTag(hasElementInTableScope("td") ? "td" : "th");
    processInCellInsertionMode(endCell);
    processToken(token);
    return;
  }

  if (token.isEndTag() &&
      (toLower(token.name) == "body" || toLower(token.name) == "caption" ||
       toLower(token.name) == "col" || toLower(token.name) == "colgroup" ||
       toLower(token.name) == "html")) {
    emitError("Unexpected end tag in cell", token);
    return;
  }

  if (token.isEndTag() &&
      (toLower(token.name) == "table" || toLower(token.name) == "tbody" ||
       toLower(token.name) == "tfoot" || toLower(token.name) == "thead" ||
       toLower(token.name) == "tr")) {
    if (!hasElementInTableScope(token.name)) {
      emitError("Unexpected end tag in cell", token);
      return;
    }
    Token endCell =
        Token::makeEndTag(hasElementInTableScope("td") ? "td" : "th");
    processInCellInsertionMode(endCell);
    processToken(token);
    return;
  }

  processInBodyInsertionMode(token);
}

inline void HtmlTreeBuilder::processInSelectInsertionMode(const Token &token) {
  if (token.isCharacter()) {
    insertCharacter(token);
    return;
  }

  if (token.isComment()) {
    insertComment(token);
    return;
  }

  if (token.isDoctype()) {
    emitError("Unexpected doctype", token);
    return;
  }

  if (token.isStartTag()) {
    std::string tagName = toLower(token.name);

    if (tagName == "html") {
      processInBodyInsertionMode(token);
      return;
    }

    if (tagName == "option") {
      if (currentElement() &&
          toLower(currentElement()->localName()) == "option") {
        Token endOption = Token::makeEndTag("option");
        processInSelectInsertionMode(endOption);
      }
      insertElement(token);
      return;
    }

    if (tagName == "optgroup") {
      if (currentElement() &&
          toLower(currentElement()->localName()) == "option") {
        Token endOption = Token::makeEndTag("option");
        processInSelectInsertionMode(endOption);
      }
      if (currentElement() &&
          toLower(currentElement()->localName()) == "optgroup") {
        Token endOptgroup = Token::makeEndTag("optgroup");
        processInSelectInsertionMode(endOptgroup);
      }
      insertElement(token);
      return;
    }

    if (tagName == "select") {
      emitError("Unexpected select start tag", token);
      Token endSelect = Token::makeEndTag("select");
      processInSelectInsertionMode(endSelect);
      return;
    }

    if (tagName == "input" || tagName == "keygen" || tagName == "textarea") {
      emitError("Unexpected input in select", token);
      if (hasElementInSelectScope("select")) {
        Token endSelect = Token::makeEndTag("select");
        processInSelectInsertionMode(endSelect);
        processToken(token);
      }
      return;
    }

    if (tagName == "script" || tagName == "template") {
      processInHeadInsertionMode(token);
      return;
    }
  }

  if (token.isEndTag()) {
    std::string tagName = toLower(token.name);

    if (tagName == "optgroup") {
      if (currentElement() &&
          toLower(currentElement()->localName()) == "option" &&
          stackSize() >= 2 &&
          toLower(stack_[stackSize() - 2].element->localName()) == "optgroup") {
        Token endOption = Token::makeEndTag("option");
        processInSelectInsertionMode(endOption);
      }
      if (currentElement() &&
          toLower(currentElement()->localName()) == "optgroup") {
        popStack();
      } else {
        emitError("Unexpected optgroup end tag", token);
      }
      return;
    }

    if (tagName == "option") {
      if (currentElement() &&
          toLower(currentElement()->localName()) == "option") {
        popStack();
      } else {
        emitError("Unexpected option end tag", token);
      }
      return;
    }

    if (tagName == "select") {
      if (!hasElementInSelectScope("select")) {
        emitError("Unexpected select end tag", token);
        return;
      }
      openElementsTo("select");
      popStack();
      insertionMode_ = InsertionMode::InBody;
      return;
    }

    if (tagName == "template") {
      processInHeadInsertionMode(token);
      return;
    }
  }

  if (token.isEndOfFile()) {
    processInBodyInsertionMode(token);
  }
}

inline void
HtmlTreeBuilder::processInSelectInTableInsertionMode(const Token &token) {
  if (token.isStartTag() &&
      (toLower(token.name) == "caption" || toLower(token.name) == "table" ||
       toLower(token.name) == "tbody" || toLower(token.name) == "tfoot" ||
       toLower(token.name) == "thead" || toLower(token.name) == "tr" ||
       toLower(token.name) == "td" || toLower(token.name) == "th")) {
    emitError("Unexpected table element in select", token);
    Token endSelect = Token::makeEndTag("select");
    processInSelectInTableInsertionMode(endSelect);
    processToken(token);
    return;
  }

  if (token.isEndTag() &&
      (toLower(token.name) == "caption" || toLower(token.name) == "table" ||
       toLower(token.name) == "tbody" || toLower(token.name) == "tfoot" ||
       toLower(token.name) == "thead" || toLower(token.name) == "tr" ||
       toLower(token.name) == "td" || toLower(token.name) == "th")) {
    emitError("Unexpected table end tag in select", token);
    if (hasElementInTableScope(token.name)) {
      Token endSelect = Token::makeEndTag("select");
      processInSelectInTableInsertionMode(endSelect);
      processToken(token);
    }
    return;
  }

  processInSelectInsertionMode(token);
}

inline void HtmlTreeBuilder::processAfterBodyInsertionMode(const Token &token) {
  if (token.isCharacter() && isWhitespace(token.data[0])) {
    processInBodyInsertionMode(token);
    return;
  }

  if (token.isComment()) {
    document_->appendChild(std::make_shared<CommentNode>(token.data));
    return;
  }

  if (token.isDoctype()) {
    emitError("Unexpected doctype", token);
    return;
  }

  if (token.isStartTag() && toLower(token.name) == "html") {
    processInBodyInsertionMode(token);
    return;
  }

  if (token.isEndTag() && toLower(token.name) == "html") {
    insertionMode_ = InsertionMode::AfterAfterBody;
    return;
  }

  if (token.isEndOfFile()) {
    stopParsing_ = true;
    return;
  }

  emitError("Unexpected token after body", token);
  insertionMode_ = InsertionMode::InBody;
  processToken(token);
}

inline void
HtmlTreeBuilder::processAfterAfterBodyInsertionMode(const Token &token) {
  if (token.isComment()) {
    document_->appendChild(std::make_shared<CommentNode>(token.data));
    return;
  }

  if (token.isDoctype() ||
      (token.isCharacter() && isWhitespace(token.data[0])) ||
      (token.isStartTag() && toLower(token.name) == "html")) {
    processInBodyInsertionMode(token);
    return;
  }

  if (token.isEndOfFile()) {
    stopParsing_ = true;
    return;
  }

  emitError("Unexpected token after after body", token);
  insertionMode_ = InsertionMode::InBody;
  processToken(token);
}

inline void HtmlTreeBuilder::insertDoctype(const Token &token) {
  auto doctype = std::make_shared<DocumentTypeNode>(
      token.name.empty() ? "html" : token.name, token.publicIdentifier,
      token.systemIdentifier);
  document_->appendChild(doctype);
  document_->setDoctypeName(token.name.empty() ? "html" : token.name);

  if (token.forceQuirks ||
      (!token.publicIdentifier.empty() &&
       token.publicIdentifier.find("-//W3O//DTD W3 HTML Strict 3.0//EN//") ==
           0) ||
      (!token.publicIdentifier.empty() &&
       token.publicIdentifier.find("-//W3C//DTD HTML 4.01 Frameset//") == 0) ||
      (!token.publicIdentifier.empty() &&
       token.publicIdentifier.find("-//W3C//DTD HTML 4.01 Transitional//") ==
           0)) {
    quirksMode_ = true;
    document_->setCompatMode("BackCompat");
  }
}

inline void HtmlTreeBuilder::insertComment(const Token &token) {
  auto comment = std::make_shared<CommentNode>(token.data);
  if (stack_.empty()) {
    document_->appendChild(comment);
  } else {
    currentElement()->appendChild(comment);
  }
}

inline void HtmlTreeBuilder::insertCharacter(const Token &token) {
  insertTextNode(token.data);
}

inline void HtmlTreeBuilder::insertElement(const Token &token) {
  auto element = createElement(token);

  if (stack_.empty()) {
    document_->appendChild(element);
  } else {
    currentElement()->appendChild(element);
  }

  pushStack(element, token);
}

inline void HtmlTreeBuilder::insertElement(const std::string &tagName) {
  insertElement(Token::makeStartTag(tagName));
}

inline void HtmlTreeBuilder::insertHtmlElement() { insertElement("html"); }

inline void HtmlTreeBuilder::insertHeadElement() {
  insertElement("head");
  headElement_ = currentElement();
}

inline void HtmlTreeBuilder::insertBodyElement() { insertElement("body"); }

inline void HtmlTreeBuilder::insertTextNode(const std::string &data) {
  if (data.empty())
    return;

  NodePtr current = currentElement();
  if (!current && stack_.empty()) {
    current = document_;
  }

  if (!current)
    return;

  auto lastChild = current->lastChild();
  if (lastChild && lastChild->nodeType() == NodeType::Text) {
    auto textNode = std::static_pointer_cast<TextNode>(lastChild);
    textNode->setData(textNode->data() + data);
  } else {
    current->appendChild(std::make_shared<TextNode>(data));
  }
}

inline ElementPtr HtmlTreeBuilder::createElement(const Token &token) {
  auto element = std::make_shared<Element>(toLower(token.name));
  for (const auto &attr : token.attributes) {
    element->setAttribute(attr.name, attr.value);
  }
  return element;
}

inline ElementPtr HtmlTreeBuilder::createElement(const std::string &tagName) {
  return std::make_shared<Element>(toLower(tagName));
}

inline void HtmlTreeBuilder::pushStack(const ElementPtr &element,
                                       const Token &token) {
  stack_.emplace_back(element, token);
}

inline void HtmlTreeBuilder::popStack() {
  if (!stack_.empty()) {
    stack_.pop_back();
  }
}

inline ElementPtr HtmlTreeBuilder::currentElement() const {
  if (stack_.empty())
    return nullptr;
  return stack_.back().element;
}

inline ElementPtr HtmlTreeBuilder::adjustedCurrentNode() const {
  if (stack_.empty())
    return nullptr;
  return stack_.back().element;
}

inline bool HtmlTreeBuilder::isStackEmpty() const { return stack_.empty(); }

inline size_t HtmlTreeBuilder::stackSize() const { return stack_.size(); }

inline void HtmlTreeBuilder::pushTemplateStack(const ElementPtr &element) {
  templateStack_.push_back(element);
}

inline void HtmlTreeBuilder::popTemplateStack() {
  if (!templateStack_.empty()) {
    templateStack_.pop_back();
  }
}

inline bool HtmlTreeBuilder::inTemplateStack() const {
  return !templateStack_.empty();
}

inline void HtmlTreeBuilder::openElementsTo(const std::string &tagName) {
  std::string lowerTag = toLower(tagName);
  while (!stack_.empty()) {
    auto elem = currentElement();
    if (toLower(elem->localName()) == lowerTag) {
      break;
    }
    popStack();
  }
}

inline void
HtmlTreeBuilder::openElementsToIncluding(const std::string &tagName) {
  std::string lowerTag = toLower(tagName);
  while (!stack_.empty()) {
    auto elem = currentElement();
    popStack();
    if (toLower(elem->localName()) == lowerTag) {
      break;
    }
  }
}

inline void
HtmlTreeBuilder::generateImpliedEndTags(const std::string &exclude) {
  static const std::unordered_set<std::string> impliedEndTags = {
      "dd", "dt", "li", "optgroup", "option", "p", "rb", "rp", "rt", "rtc"};

  while (!stack_.empty()) {
    auto elem = currentElement();
    std::string tagName = toLower(elem->localName());

    if (impliedEndTags.find(tagName) == impliedEndTags.end()) {
      break;
    }

    if (!exclude.empty() && tagName == toLower(exclude)) {
      break;
    }

    popStack();
  }
}

inline void HtmlTreeBuilder::generateAllImpliedEndTags() {
  static const std::unordered_set<std::string> impliedEndTags = {
      "caption", "colgroup", "dd",    "dt", "li",    "optgroup",
      "option",  "p",        "rb",    "rp", "rt",    "rtc",
      "tbody",   "td",       "tfoot", "th", "thead", "tr"};

  while (!stack_.empty()) {
    auto elem = currentElement();
    std::string tagName = toLower(elem->localName());

    if (impliedEndTags.find(tagName) == impliedEndTags.end()) {
      break;
    }

    popStack();
  }
}

inline bool HtmlTreeBuilder::isSpecialElement(const ElementPtr &element) const {
  return isSpecialTagName(element->localName());
}

inline bool
HtmlTreeBuilder::isFormattingElement(const ElementPtr &element) const {
  return isFormattingTagName(element->localName());
}

inline bool
HtmlTreeBuilder::isPhrasingContent(const ElementPtr &element) const {
  static const std::unordered_set<std::string> phrasingContent = {
      "a",      "abbr", "b",   "bdi",  "bdo", "br",   "cite",  "code",
      "data",   "del",  "dfn", "em",   "i",   "ins",  "kbd",   "mark",
      "q",      "rp",   "rt",  "ruby", "s",   "samp", "small", "span",
      "strong", "sub",  "sup", "time", "u",   "var",  "wbr"};
  return phrasingContent.find(toLower(element->localName())) !=
         phrasingContent.end();
}

inline bool
HtmlTreeBuilder::hasElementInScope(const std::string &tagName) const {
  std::string lowerTag = toLower(tagName);
  static const std::unordered_set<std::string> scopeStop = {
      "applet",  "caption", "html",     "table", "td", "th",
      "marquee", "object",  "template", "math",  "svg"};

  for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
    std::string elemTag = toLower(it->element->localName());
    if (elemTag == lowerTag)
      return true;
    if (scopeStop.find(elemTag) != scopeStop.end())
      return false;
  }
  return false;
}

inline bool
HtmlTreeBuilder::hasElementInListItemScope(const std::string &tagName) const {
  std::string lowerTag = toLower(tagName);
  static const std::unordered_set<std::string> scopeStop = {
      "applet", "caption",  "html", "table", "td", "th", "marquee",
      "object", "template", "math", "svg",   "ol", "ul"};

  for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
    std::string elemTag = toLower(it->element->localName());
    if (elemTag == lowerTag)
      return true;
    if (scopeStop.find(elemTag) != scopeStop.end())
      return false;
  }
  return false;
}

inline bool
HtmlTreeBuilder::hasElementInButtonScope(const std::string &tagName) const {
  std::string lowerTag = toLower(tagName);
  static const std::unordered_set<std::string> scopeStop = {
      "applet",  "caption", "html",     "table", "td",  "th",
      "marquee", "object",  "template", "math",  "svg", "button"};

  for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
    std::string elemTag = toLower(it->element->localName());
    if (elemTag == lowerTag)
      return true;
    if (scopeStop.find(elemTag) != scopeStop.end())
      return false;
  }
  return false;
}

inline bool
HtmlTreeBuilder::hasElementInTableScope(const std::string &tagName) const {
  std::string lowerTag = toLower(tagName);
  static const std::unordered_set<std::string> scopeStop = {"html", "table",
                                                            "template"};

  for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
    std::string elemTag = toLower(it->element->localName());
    if (elemTag == lowerTag)
      return true;
    if (scopeStop.find(elemTag) != scopeStop.end())
      return false;
  }
  return false;
}

inline bool
HtmlTreeBuilder::hasElementInSelectScope(const std::string &tagName) const {
  std::string lowerTag = toLower(tagName);
  static const std::unordered_set<std::string> scopeStop = {"optgroup",
                                                            "option"};

  for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
    std::string elemTag = toLower(it->element->localName());
    if (elemTag == lowerTag)
      return true;
    if (scopeStop.find(elemTag) == scopeStop.end())
      return false;
  }
  return false;
}

inline void HtmlTreeBuilder::reconstructActiveFormattingElements() {
  if (activeFormattingElements_.empty())
    return;

  auto last = activeFormattingElements_.back();
  if (!last)
    return;

  bool inStack = false;
  for (const auto &item : stack_) {
    if (item.element == last) {
      inStack = true;
      break;
    }
  }
  if (inStack)
    return;

  size_t index = activeFormattingElements_.size() - 1;
  while (index > 0) {
    index--;
    auto entry = activeFormattingElements_[index];
    if (!entry)
      break;

    bool found = false;
    for (const auto &item : stack_) {
      if (item.element == entry) {
        found = true;
        break;
      }
    }
    if (found) {
      index++;
      break;
    }
  }

  while (index < activeFormattingElements_.size()) {
    auto entry = activeFormattingElements_[index];
    if (!entry)
      break;

    auto clone = std::static_pointer_cast<Element>(entry->cloneNode(false));
    currentElement()->appendChild(clone);
    pushStack(clone, Token::makeStartTag(clone->localName()));
    activeFormattingElements_[index] = clone;

    index++;
  }
}

inline void
HtmlTreeBuilder::pushActiveFormattingElements(const ElementPtr &element) {
  size_t count = 0;
  for (const auto &elem : activeFormattingElements_) {
    if (elem && toLower(elem->localName()) == toLower(element->localName())) {
      count++;
    }
  }

  if (count >= 3) {
    for (auto it = activeFormattingElements_.begin();
         it != activeFormattingElements_.end();) {
      if (*it && toLower((*it)->localName()) == toLower(element->localName())) {
        it = activeFormattingElements_.erase(it);
        break;
      } else {
        ++it;
      }
    }
  }

  activeFormattingElements_.push_back(element);
}

inline void HtmlTreeBuilder::clearActiveFormattingElements() {
  while (!activeFormattingElements_.empty()) {
    auto last = activeFormattingElements_.back();
    activeFormattingElements_.pop_back();

    if (!last)
      break;

    bool inStack = false;
    for (const auto &item : stack_) {
      if (item.element == last) {
        inStack = true;
        break;
      }
    }
    if (inStack)
      break;
  }
}

inline bool HtmlTreeBuilder::isSpecialTagName(const std::string &tagName) {
  static const std::unordered_set<std::string> specialTags = {
      "address",    "applet",   "area",       "article",  "aside",  "base",
      "basefont",   "bgsound",  "blockquote", "body",     "br",     "button",
      "caption",    "center",   "col",        "colgroup", "dd",     "details",
      "dir",        "div",      "dl",         "dt",       "embed",  "fieldset",
      "figcaption", "figure",   "footer",     "form",     "frame",  "frameset",
      "h1",         "h2",       "h3",         "h4",       "h5",     "h6",
      "head",       "header",   "hgroup",     "hr",       "html",   "iframe",
      "img",        "input",    "keygen",     "li",       "link",   "listing",
      "main",       "marquee",  "menu",       "meta",     "nav",    "noembed",
      "noframes",   "noscript", "object",     "ol",       "p",      "param",
      "plaintext",  "pre",      "script",     "section",  "select", "source",
      "style",      "summary",  "table",      "tbody",    "td",     "template",
      "textarea",   "tfoot",    "th",         "thead",    "title",  "tr",
      "track",      "ul",       "wbr",        "xmp"};
  return specialTags.find(toLower(tagName)) != specialTags.end();
}

inline bool HtmlTreeBuilder::isFormattingTagName(const std::string &tagName) {
  static const std::unordered_set<std::string> formattingTags = {
      "a",    "b", "big",   "code",   "em",     "font", "i",
      "nobr", "s", "small", "strike", "strong", "tt",   "u"};
  return formattingTags.find(toLower(tagName)) != formattingTags.end();
}

inline bool HtmlTreeBuilder::isVoidTagName(const std::string &tagName) {
  return Element::isVoidElement(tagName);
}

inline bool HtmlTreeBuilder::isRawTextTagName(const std::string &tagName) {
  return Element::isRawTextElement(tagName);
}

inline bool
HtmlTreeBuilder::isEscapableRawTextTagName(const std::string &tagName) {
  return Element::isEscapableRawTextElement(tagName);
}

inline bool HtmlTreeBuilder::isBasicTagName(const std::string &tagName) {
  static const std::unordered_set<std::string> basicTags = {
      "b",    "big",    "blockquote", "body", "br",      "center", "code",
      "dd",   "div",    "dl",         "dt",   "em",      "embed",  "h1",
      "h2",   "h3",     "h4",         "h5",   "h6",      "head",   "hr",
      "html", "i",      "img",        "li",   "listing", "menu",   "meta",
      "nobr", "ol",     "p",          "pre",  "ruby",    "s",      "small",
      "span", "strike", "strong",     "sub",  "sup",     "table",  "td",
      "th",   "title",  "tr",         "tt",   "u",       "ul",     "var"};
  return basicTags.find(toLower(tagName)) != basicTags.end();
}

} // namespace dom
} // namespace xiaopeng
