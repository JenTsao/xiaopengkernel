#pragma once

#include "../dom/dom.hpp"
#include "computed_style.hpp"
#include "css_parser.hpp"
#include "css_types.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace xiaopeng {
namespace css {

struct MatchResult {
  const Rule *rule;
  Selector::Specificity specificity;
  size_t position;
};

class StyleResolver {
public:
  StyleResolver() = default;

  void setRootFontSize(float px) { rootFontSize_ = px > 0.0f ? px : 16.0f; }

  ComputedStyle resolveStyle(dom::ElementPtr element, const StyleSheet &sheet) {
    return resolveStyle(element, sheet, nullptr);
  }

  // parentStyle enables CSS inheritance: properties that are inheritable and
  // not explicitly declared fall back to the parent's computed values.
  ComputedStyle resolveStyle(dom::ElementPtr element, const StyleSheet &sheet,
                             const ComputedStyle *parentStyle) {
    ComputedStyle style;

    // Custom properties inherit wholesale; the element's own declarations
    // (applied below) override the inherited ones.
    if (parentStyle) {
      style.customProperties = parentStyle->customProperties;
    }

    // 1. Collect matching rules
    std::vector<MatchResult> matches;
    size_t ruleIdx = 0;
    for (const auto &rule : sheet.rules) {
      for (const auto &selector : rule.selectors) {
        if (matchSelector(element, selector)) {
          matches.push_back({&rule, selector.specificity(), ruleIdx});
        }
      }
      ruleIdx++;
    }

    // 2. Sort by specificity and position
    std::sort(matches.begin(), matches.end(),
              [](const MatchResult &a, const MatchResult &b) {
                if (a.specificity != b.specificity) {
                  return a.specificity < b.specificity;
                }
                return a.position < b.position;
              });

    // Track explicitly declared properties (for inheritance semantics)
    std::unordered_set<std::string> declared;
    for (const auto &match : matches) {
      for (const auto &d : match.rule->declarations) {
        declared.insert(d.property);
      }
    }

    // Custom properties cascade before anything else so every var()
    // reference sees the winning value, whichever rule declares it
    applyCustomProperties(style, matches, parentStyle);

    // 3. Apply non-important declarations first (sorted by specificity)
    for (const auto &match : matches) {
      applyDeclarations(style, match.rule->declarations, false, parentStyle);
    }

    // 4. Apply !important declarations last (they always override non-important)
    for (const auto &match : matches) {
      applyDeclarations(style, match.rule->declarations, true, parentStyle);
    }

    // 5. Inheritable properties that were not declared come from the parent
    if (parentStyle) {
      for (const auto &name : inheritedProperties()) {
        if (declared.count(name) == 0) {
          copyProperty(style, *parentStyle, name);
        }
      }
    }

    // 6. Resolve relative units (em/rem) against the computed font sizes
    resolveRelativeUnits(style);

    return style;
  }

  // Match an element against a parsed complex selector (compound parts +
  // combinators). Public so the DOM querySelector engine can reuse it.
  bool matchSelector(dom::ElementPtr element, const Selector &selector) {
    if (selector.parts.empty() || !element)
      return false;

    // Start matching from the last part of the selector against the current
    // element
    return matchRecursively(element, selector,
                            static_cast<int>(selector.parts.size()) - 1);
  }

private:
  bool matchRecursively(dom::ElementPtr element, const Selector &selector,
                        int index) {
    if (index < 0)
      return true;
    if (!element)
      return false;

    // 1. Match the "Compound Selector" ending at 'index'
    // A compound selector consists of a chain of parts separated by
    // Combinator::None.
    int currentIndex = index;

    // Check parts belonging to the same element
    while (currentIndex >= 0) {
      // Check if current part matches
      if (!matchSimpleSelector(element, selector.parts[currentIndex])) {
        return false;
      }

      // Check if there is a 'None' combinator preceding this part
      if (currentIndex > 0 &&
          selector.combinators[currentIndex - 1] == Combinator::None) {
        currentIndex--;
      } else {
        break;
      }
    }

    // If we processed all parts and index became -1 (actually currentIndex will
    // be 0 if we consumed all), we need to check if we are done. If
    // currentIndex == 0, we consumed the whole selector chain successfully on
    // this element. BUT wait, a compound selector might be just "div".
    // currentIndex goes from 0 to 0. The loop breaks. If currentIndex == 0, we
    // are done!

    if (currentIndex == 0)
      return true;

    // 2. If not done, there must be a combinator at [currentIndex - 1]
    Combinator combinator = selector.combinators[currentIndex - 1];
    int nextIndex = currentIndex - 1;

    if (combinator == Combinator::Descendant) {
      dom::NodePtr parent = element->parentNode();
      while (parent && parent->nodeType() == dom::NodeType::Element) {
        auto parentElem = std::static_pointer_cast<dom::Element>(parent);
        if (matchRecursively(parentElem, selector, nextIndex)) {
          return true;
        }
        parent = parent->parentNode();
      }
      return false;
    } else if (combinator == Combinator::Child) {
      dom::NodePtr parent = element->parentNode();
      if (parent && parent->nodeType() == dom::NodeType::Element) {
        auto parentElem = std::static_pointer_cast<dom::Element>(parent);
        return matchRecursively(parentElem, selector, nextIndex);
      }
      return false;
    } else if (combinator == Combinator::NextSibling) {
      dom::ElementPtr prev = element->previousElementSibling();
      if (prev) {
        return matchRecursively(prev, selector, nextIndex);
      }
      return false;
    } else if (combinator == Combinator::SubsequentSibling) {
      dom::ElementPtr prev = element->previousElementSibling();
      while (prev) {
        if (matchRecursively(prev, selector, nextIndex)) {
          return true;
        }
        prev = prev->previousElementSibling();
      }
      return false;
    }

    return false;
  }

  bool matchSimpleSelector(dom::ElementPtr element,
                           const SimpleSelector &simple) {
    switch (simple.type) {
    case SelectorType::Tag:
      // Case-insensitive check generally appropriate for HTML
      // But my tokenizer might store simplified names.
      // dom::Element::tagName returns UPPERCASE. dom::Element::localName
      // returns original/lowercase. Let's assume lowercase for now as
      // normalized by tree builder.
      return dom::toLower(simple.value) == dom::toLower(element->localName());
    case SelectorType::Class:
      return element->hasClass(simple.value);
    case SelectorType::Id:
      return element->id() == simple.value;
    case SelectorType::Universal:
      return true;
    case SelectorType::Attribute:
      if (simple.attributeOperator.empty() && simple.attributeValue.empty()) {
        return element->hasAttribute(simple.attributeName);
      } else {
        auto val = element->getAttribute(simple.attributeName);
        if (!val.has_value()) return false;
        const std::string &op = simple.attributeOperator;
        const std::string &attrVal = simple.attributeValue;
        if (op == "=")
          return val.value() == attrVal;
        if (op == "~=") {
          // A value that is empty or contains whitespace never matches
          if (attrVal.empty() ||
              attrVal.find_first_of(" \t\n\r\f") != std::string::npos)
            return false;
          std::string haystack = val.value();
          size_t pos = 0;
          while (pos < haystack.size()) {
            size_t end = haystack.find_first_of(" \t\n", pos);
            if (end == std::string::npos) end = haystack.size();
            if (haystack.substr(pos, end - pos) == attrVal) return true;
            pos = end + 1;
          }
          return false;
        }
        if (op == "|=") {
          const std::string &v = val.value();
          return v == attrVal ||
                 (v.size() > attrVal.size() &&
                  v.substr(0, attrVal.size()) == attrVal &&
                  v[attrVal.size()] == '-');
        }
        // ^= $= *= with an empty value never match (Selectors §6.2)
        if (op == "^=") {
          return !attrVal.empty() && val.value().size() >= attrVal.size() &&
                 val.value().substr(0, attrVal.size()) == attrVal;
        }
        if (op == "$=") {
          return !attrVal.empty() && val.value().size() >= attrVal.size() &&
                 val.value().substr(val.value().size() - attrVal.size()) == attrVal;
        }
        if (op == "*=") {
          return !attrVal.empty() &&
                 val.value().find(attrVal) != std::string::npos;
        }
        return false;
      }
    case SelectorType::PseudoClass:
      return matchPseudoClass(element, simple.value);
    case SelectorType::PseudoElement:
      // No pseudo-element boxes are generated, so a rule such as
      // p::before { ... } must not style the <p> itself
      return false;
    default:
      return false;
    }
  }

  bool matchPseudoClass(dom::ElementPtr element, const std::string &name) {
    size_t paren = name.find('(');
    if (paren != std::string::npos) {
      std::string fn = dom::toLower(name.substr(0, paren));
      std::string args = pseudoFunctionArgs(name);
      if (fn == "nth-child")
        return evaluateNthFormula(element->getElementSiblingIndex(), args);
      if (fn == "nth-last-child")
        return evaluateNthFormula(element->getElementSiblingIndexFromEnd(),
                                  args);
      if (fn == "nth-of-type")
        return evaluateNthFormula(element->getElementSiblingIndexOfType(),
                                  args);
      if (fn == "nth-last-of-type")
        return evaluateNthFormula(siblingIndexOfTypeFromEnd(element), args);
      if (fn == "not")
        return matchNotSelectorList(element, args);
      // :where() matches like :is(); it only differs in specificity
      if (fn == "is" || fn == "where")
        return matchInnerSelectorList(element, args);
      if (fn == "has")
        return matchHasRelative(element, args);
      if (fn == "lang")
        return matchLang(element, args);
      return false;
    }

    std::string pc = dom::toLower(name);
    if (pc == "hover")
      return element->isHovered();
    if (pc == "active")
      return element->isActive();
    if (pc == "focus")
      return element->isFocused();
    if (pc == "first-child")
      return element->isFirstChild();
    if (pc == "last-child")
      return element->isLastChild();
    if (pc == "only-child")
      return element->isOnlyChild();
    if (pc == "empty")
      return element->isEmpty();
    if (pc == "root")
      return element->isDocumentRoot();
    if (pc == "checked")
      return element->isChecked();
    if (pc == "disabled")
      return element->isDisabled();
    if (pc == "enabled")
      return !element->isDisabled();
    if (pc == "required")
      return element->hasAttribute("required");
    if (pc == "optional")
      return !element->hasAttribute("required");
    if (pc == "link")
      return element->isLink();
    if (pc == "visited")
      return element->isVisited();
    if (pc == "first-of-type")
      return element->getElementSiblingIndexOfType() == 1;
    if (pc == "last-of-type")
      return siblingIndexOfTypeFromEnd(element) == 1;
    if (pc == "only-of-type")
      return element->getElementSiblingIndexOfType() == 1 &&
             siblingIndexOfTypeFromEnd(element) == 1;
    // :target needs URL fragment navigation, which the engine lacks
    return false;
  }

  static int siblingIndexOfTypeFromEnd(const dom::ElementPtr &element) {
    int index = 1;
    for (auto next = element->nextElementSibling(); next;
         next = next->nextElementSibling()) {
      if (next->localName() == element->localName())
        index++;
    }
    return index;
  }

  // An+B microsyntax: "odd", "even", "5", "n", "-n+3", "2n + 1", "+n-2"
  static bool parseAnPlusB(const std::string &formula, int &a, int &b) {
    std::string s;
    for (char c : formula) {
      if (!std::isspace(static_cast<unsigned char>(c)))
        s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (s == "odd") {
      a = 2;
      b = 1;
      return true;
    }
    if (s == "even") {
      a = 2;
      b = 0;
      return true;
    }
    auto parseInt = [](const std::string &t, int &out) {
      size_t i = (!t.empty() && (t[0] == '+' || t[0] == '-')) ? 1 : 0;
      if (i >= t.size() || t.size() - i > 9)
        return false;
      int v = 0;
      for (size_t k = i; k < t.size(); ++k) {
        if (t[k] < '0' || t[k] > '9')
          return false;
        v = v * 10 + (t[k] - '0');
      }
      out = (t[0] == '-') ? -v : v;
      return true;
    };
    size_t n = s.find('n');
    if (n == std::string::npos) {
      a = 0;
      return parseInt(s, b);
    }
    std::string aPart = s.substr(0, n);
    std::string bPart = s.substr(n + 1);
    if (aPart.empty() || aPart == "+")
      a = 1;
    else if (aPart == "-")
      a = -1;
    else if (!parseInt(aPart, a))
      return false;
    if (bPart.empty()) {
      b = 0;
      return true;
    }
    if (bPart[0] != '+' && bPart[0] != '-')
      return false;
    return parseInt(bPart, b);
  }

  // True when some n >= 0 satisfies a*n + b == index (index is 1-based)
  static bool evaluateNthFormula(int index, const std::string &formula) {
    int a = 0, b = 0;
    if (!parseAnPlusB(formula, a, b))
      return false;
    if (a == 0)
      return index == b;
    int diff = index - b;
    return diff % a == 0 && diff / a >= 0;
  }

  // :not(<selector-list>) — true when the element matches none of the
  // selectors; an unparseable argument makes the whole selector invalid
  bool matchNotSelectorList(dom::ElementPtr element, const std::string &list) {
    auto selectors = CssParser::parseSelectorList(list);
    if (selectors.empty())
      return false;
    for (const auto &selector : selectors) {
      if (matchSelector(element, selector))
        return false;
    }
    return true;
  }

  // :lang(xx) — case-insensitive match against the nearest lang attribute,
  // also accepting subtags ("en" matches "en-US")
  bool matchLang(dom::ElementPtr element, const std::string &arg) {
    std::string lang = dom::toLower(dom::trimWhitespace(arg));
    if (lang.size() >= 2 && (lang.front() == '"' || lang.front() == '\'') &&
        lang.back() == lang.front())
      lang = lang.substr(1, lang.size() - 2);
    if (lang.empty())
      return false;

    std::optional<std::string> val = element->getAttribute("lang");
    dom::NodePtr parent = element->parentNode();
    while (!val.has_value() && parent &&
           parent->nodeType() == dom::NodeType::Element) {
      val = std::static_pointer_cast<dom::Element>(parent)->getAttribute("lang");
      parent = parent->parentNode();
    }
    if (!val.has_value())
      return false;
    std::string v = dom::toLower(val.value());
    return v == lang || (v.size() > lang.size() &&
                         v.compare(0, lang.size(), lang) == 0 &&
                         v[lang.size()] == '-');
  }

  void applyDeclarations(ComputedStyle &style,
                         const std::vector<Declaration> &decls,
                         bool importantOnly,
                         const ComputedStyle *parentStyle = nullptr) {
    for (const auto &decl : decls) {
      // Skip declarations that don't match the important filter
      if (decl.important != importantOnly) continue;
      // Custom properties were already cascaded by applyCustomProperties
      if (isCustomPropertyName(decl.property)) continue;

      // Resolve var() references; one that cannot be resolved makes the
      // declaration invalid at computed-value time, i.e. it acts as 'unset'
      std::string resolvedValue =
          substituteVariables(decl.value, style, 0).value_or("unset");
      const std::string& value = resolvedValue;

      // CSS-wide keywords (inherit / initial / unset)
      if (value == "inherit" || value == "initial" || value == "unset") {
        if (value == "inherit") {
          if (parentStyle) {
            copyProperty(style, *parentStyle, decl.property);
          }
        } else if (value == "initial") {
          ComputedStyle defaultValue;
          copyProperty(style, defaultValue, decl.property);
        } else { // unset: inherit for inherited properties, else initial
          if (isInheritedProperty(decl.property) && parentStyle) {
            copyProperty(style, *parentStyle, decl.property);
          } else {
            ComputedStyle defaultValue;
            copyProperty(style, defaultValue, decl.property);
          }
        }
        continue;
      }

      if (decl.property == "display") {
        if (value == "none")
          style.display = Display::None;
        else if (value == "block")
          style.display = Display::Block;
        else if (value == "inline")
          style.display = Display::Inline;
        else if (value == "flex")
          style.display = Display::Flex;
        else if (value == "inline-block")
          style.display = Display::InlineBlock;
        else if (value == "grid")
          style.display = Display::Grid;
      } else if (decl.property == "width") {
        style.width = parseLength(value);
      } else if (decl.property == "height") {
        style.height = parseLength(value);
      } else if (decl.property == "color") {
        style.color = parseColor(value);
      } else if (decl.property == "background-color") {
        style.backgroundColor = parseColor(value);
      } else if (decl.property == "background") {
        // Shorthand: only the color component is extracted; other layers
        // (images, gradients) are kept as raw strings.
        if (looksLikeColor(value)) {
          style.backgroundColor = parseColor(value);
        } else {
          style.otherProperties["background"] = value;
        }
      } else if (decl.property == "margin") {
        // 1-4 value shorthand (TRBL)
        applyBoxLengths(style.marginTop, style.marginRight, style.marginBottom,
                        style.marginLeft, splitCssValues(value));
      } else if (decl.property == "padding") {
        applyBoxLengths(style.paddingTop, style.paddingRight,
                        style.paddingBottom, style.paddingLeft,
                        splitCssValues(value));
      } else if (decl.property == "margin-top") {
        style.marginTop = parseLength(value);
      } else if (decl.property == "margin-right") {
        style.marginRight = parseLength(value);
      } else if (decl.property == "margin-bottom") {
        style.marginBottom = parseLength(value);
      } else if (decl.property == "margin-left") {
        style.marginLeft = parseLength(value);
      } else if (decl.property == "padding-top") {
        style.paddingTop = parseLength(value);
      } else if (decl.property == "padding-right") {
        style.paddingRight = parseLength(value);
      } else if (decl.property == "padding-bottom") {
        style.paddingBottom = parseLength(value);
      } else if (decl.property == "padding-left") {
        style.paddingLeft = parseLength(value);
      } else if (decl.property == "font-size") {
        // em/% are relative to the parent's computed font size
        float parentPx =
            parentStyle ? computedFontSizePx(*parentStyle) : rootFontSize_;
        style.fontSize = parseFontLength(value, parentPx);
      } else if (decl.property == "font-family") {
        style.fontFamily = value;
      } else if (decl.property == "line-height") {
        style.lineHeight = parseLineHeight(value);
      } else if (decl.property == "text-indent") {
        style.textIndent = parseLength(value);
      } else if (decl.property == "white-space") {
        if (value == "normal")
          style.whiteSpace = WhiteSpace::Normal;
        else if (value == "pre")
          style.whiteSpace = WhiteSpace::Pre;
        else if (value == "nowrap")
          style.whiteSpace = WhiteSpace::NoWrap;
        else if (value == "pre-wrap")
          style.whiteSpace = WhiteSpace::PreWrap;
        else if (value == "pre-line")
          style.whiteSpace = WhiteSpace::PreLine;
      } else if (decl.property == "vertical-align") {
        if (value == "baseline")
          style.verticalAlign = VerticalAlign::Baseline;
        else if (value == "top")
          style.verticalAlign = VerticalAlign::Top;
        else if (value == "bottom")
          style.verticalAlign = VerticalAlign::Bottom;
        else if (value == "middle")
          style.verticalAlign = VerticalAlign::Middle;
        else if (value == "text-top")
          style.verticalAlign = VerticalAlign::TextTop;
        else if (value == "text-bottom")
          style.verticalAlign = VerticalAlign::TextBottom;
        else if (value == "sub")
          style.verticalAlign = VerticalAlign::Sub;
        else if (value == "super")
          style.verticalAlign = VerticalAlign::Super;
        else {
          // Try numeric: "2px", "0.5em", "10%"
          auto length = parseLength(value);
          if (length.unit != css::Length::Unit::Auto) {
            style.verticalAlign = VerticalAlign::Baseline;
            style.verticalAlignOffset = length.value;
          }
        }
      } else if (decl.property == "letter-spacing") {
        if (value == "normal") {
          style.letterSpacing = 0.0f;
        } else {
          auto l = parseLength(value);
          style.letterSpacing = (l.unit == Length::Unit::Em)
                                    ? l.value * computedFontSizePx(style)
                                    : l.value;
        }
      } else if (decl.property == "word-spacing") {
        if (value == "normal") {
          style.wordSpacing = 0.0f;
        } else {
          auto l = parseLength(value);
          style.wordSpacing = (l.unit == Length::Unit::Em)
                                  ? l.value * computedFontSizePx(style)
                                  : l.value;
        }
      } else if (decl.property == "text-decoration-line") {
        if (value == "underline")
          style.textDecorationLine = TextDecorationLine::Underline;
        else if (value == "overline")
          style.textDecorationLine = TextDecorationLine::Overline;
        else if (value == "line-through")
          style.textDecorationLine = TextDecorationLine::LineThrough;
        else
          style.textDecorationLine = TextDecorationLine::None;
      } else if (decl.property == "text-decoration") {
        if (value.find("underline") != std::string::npos)
          style.textDecorationLine = TextDecorationLine::Underline;
        else if (value.find("overline") != std::string::npos)
          style.textDecorationLine = TextDecorationLine::Overline;
        else if (value.find("line-through") != std::string::npos)
          style.textDecorationLine = TextDecorationLine::LineThrough;
        else
          style.textDecorationLine = TextDecorationLine::None;
      } else if (decl.property == "text-transform") {
        if (value == "uppercase")
          style.textTransform = TextTransform::Uppercase;
        else if (value == "lowercase")
          style.textTransform = TextTransform::Lowercase;
        else if (value == "capitalize")
          style.textTransform = TextTransform::Capitalize;
        else
          style.textTransform = TextTransform::None;
      } else if (decl.property == "flex-direction") {
        if (value == "row")
          style.flexDirection = FlexDirection::Row;
        else if (value == "row-reverse")
          style.flexDirection = FlexDirection::RowReverse;
        else if (value == "column")
          style.flexDirection = FlexDirection::Column;
        else if (value == "column-reverse")
          style.flexDirection = FlexDirection::ColumnReverse;
      } else if (decl.property == "flex-wrap") {
        if (value == "nowrap")
          style.flexWrap = FlexWrap::NoWrap;
        else if (value == "wrap")
          style.flexWrap = FlexWrap::Wrap;
        else if (value == "wrap-reverse")
          style.flexWrap = FlexWrap::WrapReverse;
      } else if (decl.property == "flex-grow") {
        float f = 0.0f;
        try {
          f = std::stof(value);
        } catch (...) {}
        style.flexGrow = f;
      } else if (decl.property == "flex-shrink") {
        float f = 1.0f;
        try {
          f = std::stof(value);
        } catch (...) {}
        style.flexShrink = f;
      } else if (decl.property == "flex-basis") {
        style.flexBasis = parseLength(value);
      } else if (decl.property == "flex") {
        // Shorthand: none | auto | [ <grow> <shrink>? || <basis> ]
        auto parts = splitCssValues(value);
        if (!parts.empty()) {
          if (parts.size() == 1 && parts[0] == "none") {
            style.flexGrow = 0.0f;
            style.flexShrink = 0.0f;
            style.flexBasis = Length::Auto();
          } else if (parts.size() == 1 && parts[0] == "auto") {
            style.flexGrow = 1.0f;
            style.flexShrink = 1.0f;
            style.flexBasis = Length::Auto();
          } else {
            // Omitted grow/shrink default to 1 and an omitted basis to 0%,
            // so "flex: 100px" means 1 1 100px (not grow = 100)
            float grow = 1.0f, shrink = 1.0f;
            Length basis = Length::Percent(0);
            int numbers = 0;
            for (const auto &part : parts) {
              float n = 0.0f;
              if (numbers < 2 && parsePlainNumber(part, n)) {
                (numbers == 0 ? grow : shrink) = n;
                numbers++;
              } else {
                basis = parseLength(part);
              }
            }
            style.flexGrow = grow;
            style.flexShrink = shrink;
            style.flexBasis = basis;
          }
        }
      } else if (decl.property == "border") {
        // <width> || <style> || <color>. Border styles are drawn as solid,
        // but 'none'/'hidden' compute the border width to 0.
        bool noStyle = false;
        for (const auto &part : splitCssValues(value)) {
          if (part == "none" || part == "hidden") {
            noStyle = true;
          } else if (part == "solid" || part == "dashed" || part == "dotted" ||
                     part == "double" || part == "groove" || part == "ridge" ||
                     part == "inset" || part == "outset") {
            // drawn as solid
          } else if (part == "thin" || part == "medium" || part == "thick") {
            auto l = Length::Px(part == "thin" ? 1.0f
                                : part == "medium" ? 3.0f
                                                   : 5.0f);
            style.borderTopWidth = style.borderRightWidth =
                style.borderBottomWidth = style.borderLeftWidth = l;
          } else if (std::isdigit(static_cast<unsigned char>(part[0])) ||
                     (part.size() > 1 && part[0] == '.' &&
                      std::isdigit(static_cast<unsigned char>(part[1])))) {
            auto l = parseLength(part);
            style.borderTopWidth = style.borderRightWidth =
                style.borderBottomWidth = style.borderLeftWidth = l;
          } else {
            // Assume color
            auto c = parseColor(part);
            style.borderTopColor = style.borderRightColor =
                style.borderBottomColor = style.borderLeftColor = c;
          }
        }
        if (noStyle) {
          style.borderTopWidth = style.borderRightWidth =
              style.borderBottomWidth = style.borderLeftWidth = Length::Px(0);
        }
      } else if (decl.property == "border-width") {
        applyBoxLengths(style.borderTopWidth, style.borderRightWidth,
                        style.borderBottomWidth, style.borderLeftWidth,
                        splitCssValues(value));
      } else if (decl.property == "border-color") {
        applyBoxColors(style.borderTopColor, style.borderRightColor,
                       style.borderBottomColor, style.borderLeftColor,
                       splitCssValues(value));
      } else if (decl.property == "border-left-width") {
        style.borderLeftWidth = parseLength(value);
      } else if (decl.property == "border-right-width") {
        style.borderRightWidth = parseLength(value);
      } else if (decl.property == "border-top-width") {
        style.borderTopWidth = parseLength(value);
      } else if (decl.property == "border-bottom-width") {
        style.borderBottomWidth = parseLength(value);
      } else if (decl.property == "border-left-color") {
        style.borderLeftColor = parseColor(value);
      } else if (decl.property == "border-right-color") {
        style.borderRightColor = parseColor(value);
      } else if (decl.property == "border-top-color") {
        style.borderTopColor = parseColor(value);
      } else if (decl.property == "border-bottom-color") {
        style.borderBottomColor = parseColor(value);
      } else if (decl.property == "box-sizing") {
        if (value == "border-box")
          style.boxSizing = BoxSizing::BorderBox;
        else
          style.boxSizing = BoxSizing::ContentBox;
      } else if (decl.property == "text-align") {
        if (value == "center")
          style.textAlign = TextAlign::Center;
        else if (value == "right")
          style.textAlign = TextAlign::Right;
        else if (value == "justify")
          style.textAlign = TextAlign::Justify;
        else
          style.textAlign = TextAlign::Left;
      } else if (decl.property == "overflow" || decl.property == "overflow-x" || decl.property == "overflow-y") {
        Overflow overflowVal = Overflow::Visible;
        if (value == "hidden")
          overflowVal = Overflow::Hidden;
        else if (value == "scroll")
          overflowVal = Overflow::Scroll;
        else if (value == "auto")
          overflowVal = Overflow::Auto;
        
        if (decl.property == "overflow") {
          // 1-2 value shorthand: <x> [ / <y> | <y> ]
          auto parts = splitCssValues(value);
          style.overflowX = parseOverflow(parts.empty() ? "visible" : parts[0]);
          style.overflowY = parseOverflow(
              parts.size() > 1 ? parts[1]
                               : (parts.empty() ? "visible" : parts[0]));
        } else if (decl.property == "overflow-x") {
          style.overflowX = overflowVal;
        } else {
          style.overflowY = overflowVal;
        }
      } else if (decl.property == "top") {
        style.top = parseLength(value);
      } else if (decl.property == "right") {
        style.right = parseLength(value);
      } else if (decl.property == "bottom") {
        style.bottom = parseLength(value);
      } else if (decl.property == "left") {
        style.left = parseLength(value);
      } else if (decl.property == "z-index") {
        try {
          style.zIndex = std::stoi(value);
        } catch (...) {}
      } else if (decl.property == "opacity") {
        // <number> | <percentage>, clamped to [0, 1]
        float v = 1.0f;
        try {
          v = std::stof(value);
        } catch (...) {
          v = 1.0f;
        }
        if (value.find('%') != std::string::npos)
          v /= 100.0f;
        style.opacity = std::clamp(v, 0.0f, 1.0f);
      } else if (decl.property == "transform") {
        if (value != "none") {
          style.transform = value;
        }
      } else if (decl.property == "filter") {
        if (value != "none") {
          style.filter = value;
        }
      } else if (decl.property == "perspective") {
        if (value != "none") {
          style.perspective = value;
        }
      } else if (decl.property == "will-change") {
        style.willChange = value;
      } else if (decl.property == "isolation") {
        if (value == "isolate") {
          style.isolation = Isolation::Isolate;
        } else {
          style.isolation = Isolation::Auto;
        }
      } else if (decl.property == "position") {
        if (value == "static")
          style.position = Position::Static;
        else if (value == "relative")
          style.position = Position::Relative;
        else if (value == "absolute")
          style.position = Position::Absolute;
        else if (value == "fixed")
          style.position = Position::Fixed;
        else if (value == "sticky")
          style.position = Position::Sticky;
      } else if (decl.property == "float") {
        if (value == "left")
          style.cssFloat = Float::Left;
        else if (value == "right")
          style.cssFloat = Float::Right;
        else
          style.cssFloat = Float::None;
      } else if (decl.property == "clear") {
        if (value == "left")
          style.clear = Clear::Left;
        else if (value == "right")
          style.clear = Clear::Right;
        else if (value == "both")
          style.clear = Clear::Both;
        else
          style.clear = Clear::None;
      }
      // --- Grid Layout Properties ---
      else if (decl.property == "grid-template-columns") {
        style.gridTemplateColumns = parseTrackSizeList(value);
      } else if (decl.property == "grid-template-rows") {
        style.gridTemplateRows = parseTrackSizeList(value);
      } else if (decl.property == "grid-auto-columns") {
        style.gridAutoColumns = parseTrackSizeList(value);
      } else if (decl.property == "grid-auto-rows") {
        style.gridAutoRows = parseTrackSizeList(value);
      } else if (decl.property == "grid-column-gap" || decl.property == "column-gap") {
        style.gridColumnGap = parseLength(value);
      } else if (decl.property == "grid-row-gap" || decl.property == "row-gap") {
        style.gridRowGap = parseLength(value);
      } else if (decl.property == "gap" || decl.property == "grid-gap") {
        // <row-gap> <column-gap>?
        auto parts = splitCssValues(value);
        if (!parts.empty()) {
          style.gridRowGap = parseLength(parts[0]);
          style.gridColumnGap = parseLength(parts.size() > 1 ? parts[1] : parts[0]);
        }
      } else if (decl.property == "grid-auto-flow") {
        if (value == "column")
          style.gridAutoFlow = GridAutoFlow::Column;
        else if (value == "row")
          style.gridAutoFlow = GridAutoFlow::Row;
        else if (value == "dense")
          style.gridAutoFlow = GridAutoFlow::RowDense;
        else if (value == "column dense")
          style.gridAutoFlow = GridAutoFlow::ColumnDense;
      } else if (decl.property == "justify-items") {
        if (value == "start")
          style.gridJustifyItems = GridJustifyItems::Start;
        else if (value == "end")
          style.gridJustifyItems = GridJustifyItems::End;
        else if (value == "center")
          style.gridJustifyItems = GridJustifyItems::Center;
        else if (value == "stretch")
          style.gridJustifyItems = GridJustifyItems::Stretch;
      } else if (decl.property == "align-items") {
        // Shared between flexbox and grid
        if (value == "stretch")
          style.alignItems = AlignItems::Stretch;
        else if (value == "flex-start" || value == "start")
          style.alignItems = AlignItems::FlexStart;
        else if (value == "flex-end" || value == "end")
          style.alignItems = AlignItems::FlexEnd;
        else if (value == "center")
          style.alignItems = AlignItems::Center;
        else if (value == "baseline")
          style.alignItems = AlignItems::Baseline;
      } else if (decl.property == "justify-content") {
        // Shared between flexbox and grid
        if (value == "flex-start" || value == "start")
          style.justifyContent = JustifyContent::FlexStart;
        else if (value == "flex-end" || value == "end")
          style.justifyContent = JustifyContent::FlexEnd;
        else if (value == "center")
          style.justifyContent = JustifyContent::Center;
        else if (value == "space-between")
          style.justifyContent = JustifyContent::SpaceBetween;
        else if (value == "space-around")
          style.justifyContent = JustifyContent::SpaceAround;
        else if (value == "space-evenly")
          style.justifyContent = JustifyContent::SpaceEvenly;
      } else if (decl.property == "align-content") {
        if (value == "start")
          style.gridAlignContent = GridAlignContent::Start;
        else if (value == "end")
          style.gridAlignContent = GridAlignContent::End;
        else if (value == "center")
          style.gridAlignContent = GridAlignContent::Center;
        else if (value == "stretch")
          style.gridAlignContent = GridAlignContent::Stretch;
        else if (value == "space-between")
          style.gridAlignContent = GridAlignContent::SpaceBetween;
        else if (value == "space-around")
          style.gridAlignContent = GridAlignContent::SpaceAround;
      }
      // --- Grid Item Properties ---
      else if (decl.property == "grid-column-start") {
        style.gridColumnStart = parseGridLine(value);
      } else if (decl.property == "grid-column-end") {
        style.gridColumnEnd = parseGridLine(value);
      } else if (decl.property == "grid-row-start") {
        style.gridRowStart = parseGridLine(value);
      } else if (decl.property == "grid-row-end") {
        style.gridRowEnd = parseGridLine(value);
      } else if (decl.property == "grid-column") {
        parseGridShorthand(value, style.gridColumnStart, style.gridColumnEnd);
      } else if (decl.property == "grid-row") {
        parseGridShorthand(value, style.gridRowStart, style.gridRowEnd);
      } else if (decl.property == "justify-self") {
        if (value == "start")
          style.gridJustifySelf = GridJustifySelf::Start;
        else if (value == "end")
          style.gridJustifySelf = GridJustifySelf::End;
        else if (value == "center")
          style.gridJustifySelf = GridJustifySelf::Center;
        else if (value == "stretch")
          style.gridJustifySelf = GridJustifySelf::Stretch;
        else if (value == "auto")
          style.gridJustifySelf = GridJustifySelf::Auto;
      } else if (decl.property == "align-self") {
        if (value == "auto")
          style.gridAlignSelf = GridAlignSelf::Auto;
        else if (value == "start")
          style.gridAlignSelf = GridAlignSelf::Start;
        else if (value == "end")
          style.gridAlignSelf = GridAlignSelf::End;
        else if (value == "center")
          style.gridAlignSelf = GridAlignSelf::Center;
        else if (value == "stretch")
          style.gridAlignSelf = GridAlignSelf::Stretch;
      }

      style.otherProperties[decl.property] = value;
    }
  }

private:
  float rootFontSize_ = 16.0f;

  // ── Inheritance / CSS-wide keyword helpers ──────────────────

  static const std::vector<std::string> &inheritedProperties() {
    static const std::vector<std::string> props = {
        "color",       "font-family",    "font-size",     "line-height",
        "text-indent", "white-space",    "letter-spacing", "word-spacing",
        "text-transform", "text-align"};
    return props;
  }

  static bool isInheritedProperty(const std::string &name) {
    auto &props = inheritedProperties();
    return std::find(props.begin(), props.end(), name) != props.end();
  }

  // Copy one property (longhand, recognized shorthand, or custom property)
  static void copyProperty(ComputedStyle &dst, const ComputedStyle &src,
                           const std::string &name) {
    if (name == "color") {
      dst.color = src.color;
    } else if (name == "font-family") {
      dst.fontFamily = src.fontFamily;
    } else if (name == "font-size") {
      dst.fontSize = src.fontSize;
    } else if (name == "line-height") {
      dst.lineHeight = src.lineHeight;
    } else if (name == "text-indent") {
      dst.textIndent = src.textIndent;
    } else if (name == "white-space") {
      dst.whiteSpace = src.whiteSpace;
    } else if (name == "letter-spacing") {
      dst.letterSpacing = src.letterSpacing;
    } else if (name == "word-spacing") {
      dst.wordSpacing = src.wordSpacing;
    } else if (name == "text-transform") {
      dst.textTransform = src.textTransform;
    } else if (name == "text-align") {
      dst.textAlign = src.textAlign;
    } else if (name == "margin") {
      dst.marginTop = src.marginTop;
      dst.marginRight = src.marginRight;
      dst.marginBottom = src.marginBottom;
      dst.marginLeft = src.marginLeft;
    } else if (name == "margin-top") {
      dst.marginTop = src.marginTop;
    } else if (name == "margin-right") {
      dst.marginRight = src.marginRight;
    } else if (name == "margin-bottom") {
      dst.marginBottom = src.marginBottom;
    } else if (name == "margin-left") {
      dst.marginLeft = src.marginLeft;
    } else if (name == "padding") {
      dst.paddingTop = src.paddingTop;
      dst.paddingRight = src.paddingRight;
      dst.paddingBottom = src.paddingBottom;
      dst.paddingLeft = src.paddingLeft;
    } else if (name == "padding-top") {
      dst.paddingTop = src.paddingTop;
    } else if (name == "padding-right") {
      dst.paddingRight = src.paddingRight;
    } else if (name == "padding-bottom") {
      dst.paddingBottom = src.paddingBottom;
    } else if (name == "padding-left") {
      dst.paddingLeft = src.paddingLeft;
    } else if (name == "border-width") {
      dst.borderTopWidth = src.borderTopWidth;
      dst.borderRightWidth = src.borderRightWidth;
      dst.borderBottomWidth = src.borderBottomWidth;
      dst.borderLeftWidth = src.borderLeftWidth;
    } else if (name == "border-top-width") {
      dst.borderTopWidth = src.borderTopWidth;
    } else if (name == "border-right-width") {
      dst.borderRightWidth = src.borderRightWidth;
    } else if (name == "border-bottom-width") {
      dst.borderBottomWidth = src.borderBottomWidth;
    } else if (name == "border-left-width") {
      dst.borderLeftWidth = src.borderLeftWidth;
    } else if (name == "border-color") {
      dst.borderTopColor = src.borderTopColor;
      dst.borderRightColor = src.borderRightColor;
      dst.borderBottomColor = src.borderBottomColor;
      dst.borderLeftColor = src.borderLeftColor;
    } else if (name == "border-top-color") {
      dst.borderTopColor = src.borderTopColor;
    } else if (name == "border-right-color") {
      dst.borderRightColor = src.borderRightColor;
    } else if (name == "border-bottom-color") {
      dst.borderBottomColor = src.borderBottomColor;
    } else if (name == "border-left-color") {
      dst.borderLeftColor = src.borderLeftColor;
    } else if (name == "overflow") {
      dst.overflowX = src.overflowX;
      dst.overflowY = src.overflowY;
    } else if (name == "overflow-x") {
      dst.overflowX = src.overflowX;
    } else if (name == "overflow-y") {
      dst.overflowY = src.overflowY;
    } else if (name.size() >= 2 && name[0] == '-' && name[1] == '-') {
      if (const auto *v = src.getCustomProperty(name)) {
        dst.setCustomProperty(name, *v);
      }
    }
  }

  // ── Shorthand helpers ───────────────────────────────────────

  // Split a declaration value on top-level whitespace (parens are respected,
  // so rgb(1, 2, 3) stays one token)
  static std::vector<std::string> splitCssValues(const std::string &value) {
    std::vector<std::string> parts;
    std::string current;
    int parenDepth = 0;
    for (char c : value) {
      if (c == '(') {
        parenDepth++;
      } else if (c == ')') {
        parenDepth--;
      }
      if (parenDepth == 0 &&
          (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '/')) {
        if (!current.empty()) {
          parts.push_back(current);
          current.clear();
        }
      } else {
        current += c;
      }
    }
    if (!current.empty()) {
      parts.push_back(current);
    }
    return parts;
  }

  void applyBoxLengths(Length &top, Length &right, Length &bottom,
                       Length &left, const std::vector<std::string> &parts) {
    if (parts.empty())
      return;
    if (parts.size() == 1) {
      top = right = bottom = left = parseLength(parts[0]);
    } else if (parts.size() == 2) {
      top = bottom = parseLength(parts[0]);
      right = left = parseLength(parts[1]);
    } else if (parts.size() == 3) {
      top = parseLength(parts[0]);
      right = left = parseLength(parts[1]);
      bottom = parseLength(parts[2]);
    } else {
      top = parseLength(parts[0]);
      right = parseLength(parts[1]);
      bottom = parseLength(parts[2]);
      left = parseLength(parts[3]);
    }
  }

  void applyBoxColors(Color &top, Color &right, Color &bottom, Color &left,
                      const std::vector<std::string> &parts) {
    if (parts.empty())
      return;
    if (parts.size() == 1) {
      top = right = bottom = left = parseColor(parts[0]);
    } else if (parts.size() == 2) {
      top = bottom = parseColor(parts[0]);
      right = left = parseColor(parts[1]);
    } else if (parts.size() == 3) {
      top = parseColor(parts[0]);
      right = left = parseColor(parts[1]);
      bottom = parseColor(parts[2]);
    } else {
      top = parseColor(parts[0]);
      right = parseColor(parts[1]);
      bottom = parseColor(parts[2]);
      left = parseColor(parts[3]);
    }
  }

  static Overflow parseOverflow(const std::string &value) {
    if (value == "hidden")
      return Overflow::Hidden;
    if (value == "scroll")
      return Overflow::Scroll;
    if (value == "auto")
      return Overflow::Auto;
    return Overflow::Visible;
  }

  static bool looksLikeColor(const std::string &value) {
    if (value.empty())
      return false;
    if (value[0] == '#')
      return true;
    if (value.compare(0, 4, "rgb(") == 0 || value.compare(0, 5, "rgba(") == 0 ||
        value.compare(0, 4, "hsl(") == 0 || value.compare(0, 5, "hsla(") == 0)
      return true;
    static const std::unordered_set<std::string> kNamed = {
        "red",    "green",  "blue",    "black",  "white",   "transparent",
        "gray",   "grey",   "cyan",    "magenta", "yellow", "orange",
        "purple", "pink",   "brown",   "navy",   "teal",    "olive",
        "silver", "lime",   "aqua",    "fuchsia", "maroon"};
    return kNamed.count(value) > 0;
  }

  // ── Relative unit helpers ───────────────────────────────────

  // Best-effort px value of a style's font-size (used as em base)
  static float computedFontSizePx(const ComputedStyle &style) {
    if (style.fontSize.unit == Length::Unit::Px)
      return style.fontSize.value;
    if (style.fontSize.unit == Length::Unit::Percent)
      return 16.0f * style.fontSize.value / 100.0f;
    return 16.0f;
  }

  // font-size parser: em/% are relative to the parent font size
  Length parseFontLength(const std::string &val, float parentPx) {
    if (val == "inherit" || val.empty())
      return Length::Px(16.0f);
    float f = 0.0f;
    try {
      f = std::stof(val);
    } catch (const std::exception &) {
      return Length::Px(16.0f);
    }
    if (val.find("%") != std::string::npos)
      return Length::Px(parentPx * f / 100.0f);
    if (val.find("rem") != std::string::npos)
      return Length::Px(rootFontSize_ * f);
    if (val.find("em") != std::string::npos)
      return Length::Px(parentPx * f);
    return Length::Px(f);
  }

  // Convert remaining em/rem lengths to px after declarations are applied
  void resolveRelativeUnits(ComputedStyle &style) {
    float fontPx = 16.0f;
    if (style.fontSize.unit == Length::Unit::Px) {
      fontPx = style.fontSize.value;
    } else if (style.fontSize.unit == Length::Unit::Em) {
      fontPx = style.fontSize.value * 16.0f;
      style.fontSize = Length::Px(fontPx);
    } else if (style.fontSize.unit == Length::Unit::Rem) {
      fontPx = style.fontSize.value * rootFontSize_;
      style.fontSize = Length::Px(fontPx);
    } else if (style.fontSize.unit == Length::Unit::Percent) {
      fontPx = 16.0f * style.fontSize.value / 100.0f;
      style.fontSize = Length::Px(fontPx);
    }

    Length *lengths[] = {
        &style.width,         &style.height,
        &style.minWidth,      &style.maxWidth,
        &style.minHeight,     &style.maxHeight,
        &style.marginTop,     &style.marginRight,
        &style.marginBottom,  &style.marginLeft,
        &style.paddingTop,    &style.paddingRight,
        &style.paddingBottom, &style.paddingLeft,
        &style.borderTopWidth,  &style.borderRightWidth,
        &style.borderBottomWidth, &style.borderLeftWidth,
        &style.top,           &style.right,
        &style.bottom,        &style.left,
        &style.textIndent,    &style.flexBasis,
        &style.gridColumnGap, &style.gridRowGap};
    for (Length *l : lengths) {
      if (l->unit == Length::Unit::Em) {
        *l = Length::Px(l->value * fontPx);
      } else if (l->unit == Length::Unit::Rem) {
        *l = Length::Px(l->value * rootFontSize_);
      }
    }
  }

  // ── :is() / :where() / :has() helpers ───────────────────────

  static std::string pseudoFunctionArgs(const std::string &name) {
    size_t open = name.find('(');
    size_t close = name.find_last_of(')');
    if (open == std::string::npos || close == std::string::npos ||
        close <= open + 1)
      return "";
    return name.substr(open + 1, close - open - 1);
  }

  bool matchInnerSelectorList(dom::ElementPtr element,
                              const std::string &list) {
    auto selectors = CssParser::parseSelectorList(list);
    if (selectors.empty())
      return false;
    for (const auto &selector : selectors) {
      if (matchSelector(element, selector))
        return true;
    }
    return false;
  }

  // :has(rel) with descendant (default), child (>), next-sibling (+) and
  // subsequent-sibling (~) relations
  bool matchHasRelative(dom::ElementPtr element, const std::string &relation) {
    std::string rel = dom::trimWhitespace(relation);
    if (rel.empty())
      return false;

    char combinator = ' ';
    std::string rest = rel;
    if (rel[0] == '>' || rel[0] == '+' || rel[0] == '~') {
      combinator = rel[0];
      rest = dom::trimWhitespace(rel.substr(1));
    }
    if (rest.empty())
      return false;

    auto selectors = CssParser::parseSelectorList(rest);
    if (selectors.empty())
      return false;

    auto matchesAny = [&](dom::ElementPtr candidate) {
      for (const auto &selector : selectors) {
        if (matchSelector(candidate, selector))
          return true;
      }
      return false;
    };

    if (combinator == '>') {
      for (const auto &child : element->childNodes()) {
        if (child->nodeType() == dom::NodeType::Element &&
            matchesAny(std::static_pointer_cast<dom::Element>(child))) {
          return true;
        }
      }
      return false;
    }
    if (combinator == '+') {
      auto next = element->nextElementSibling();
      return next && matchesAny(next);
    }
    if (combinator == '~') {
      auto sibling = element->nextElementSibling();
      while (sibling) {
        if (matchesAny(sibling))
          return true;
        sibling = sibling->nextElementSibling();
      }
      return false;
    }

    std::function<bool(dom::ElementPtr)> walk = [&](dom::ElementPtr node) {
      for (const auto &child : node->childNodes()) {
        if (child->nodeType() != dom::NodeType::Element)
          continue;
        auto childEl = std::static_pointer_cast<dom::Element>(child);
        if (matchesAny(childEl) || walk(childEl))
          return true;
      }
      return false;
    };
    return walk(element);
  }

  Length parseLength(const std::string &val) {
    if (val == "auto")
      return Length::Auto();

    float f = 0.0f;
    try {
      f = std::stof(val);
    } catch (const std::exception &) {
      // Fallback to 0 if parsing fails
      f = 0.0f;
    }

    if (val.find("%") != std::string::npos)
      return Length::Percent(f);
    // "rem" contains "em", so test it first
    if (val.find("rem") != std::string::npos)
      return Length{f, Length::Unit::Rem};
    if (val.find("em") != std::string::npos)
      return Length{f, Length::Unit::Em};
    return Length::Px(f);
  }

  /// Parse the CSS `line-height` value into a `LineHeight` descriptor.
  /// Supports: normal | <number> | <length>px | <percentage>%
  LineHeight parseLineHeight(const std::string &val) {
    if (val == "normal" || val.empty())
      return LineHeight::Normal();

    float f = 0.0f;
    try {
      f = std::stof(val);
    } catch (const std::exception &) {
      return LineHeight::Normal();
    }

    if (val.find("%") != std::string::npos)
      return LineHeight::Percent(f);
    if (val.find("px") != std::string::npos)
      return LineHeight::Length(f);
    // Unitless number — multiplied by font-size at use time.
    return LineHeight::Number(f);
  }

  Color parseColor(std::string val) {
    // Trim whitespace and potential semicolon
    size_t first = val.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
      return Color::Black();
    val.erase(0, first);
    val.erase(val.find_last_not_of(" \t\r\n;") + 1);

    if (val.empty())
      return Color::Black();

    // 1. Named Colors
    if (val == "red")
      return Color::Red();
    if (val == "blue")
      return Color::Blue();
    if (val == "green")
      return Color::Green();
    if (val == "black")
      return Color::Black();
    if (val == "white")
      return Color::White();
    if (val == "transparent")
      return Color::Transparent();
    if (val == "gray" || val == "grey")
      return {128, 128, 128, 255};
    if (val == "cyan")
      return {0, 255, 255, 255};
    if (val == "magenta")
      return {255, 0, 255, 255};
    if (val == "yellow")
      return {255, 255, 0, 255};
    if (val == "orange")
      return {255, 165, 0, 255};
    if (val == "purple")
      return {128, 0, 128, 255};
    if (val == "pink")
      return {255, 192, 203, 255};
    if (val == "brown")
      return {165, 42, 42, 255};
    if (val == "navy")
      return {0, 0, 128, 255};
    if (val == "teal")
      return {0, 128, 128, 255};
    if (val == "olive")
      return {128, 128, 0, 255};
    if (val == "silver")
      return {192, 192, 192, 255};
    if (val == "lime")
      return {0, 255, 0, 255};
    if (val == "aqua")
      return {0, 255, 255, 255};
    if (val == "maroon")
      return {128, 0, 0, 255};
    if (val == "fuchsia")
      return {255, 0, 255, 255};

    // 2. rgb() and rgba() functional notation
    if (val.find("rgb") != std::string::npos) {
      return parseFunctionalColor(val);
    }

    // 3. hsl() and hsla() functional notation
    if (val.find("hsl") != std::string::npos) {
      return parseHSLColor(val);
    }

    // 4. Hex Colors
    if (val[0] == '#') {
      std::string hex = val.substr(1);
      if (hex.length() == 3) {
        // #RGB -> #RRGGBB
        uint8_t r = hexCharToInt(hex[0]) * 17;
        uint8_t g = hexCharToInt(hex[1]) * 17;
        uint8_t b = hexCharToInt(hex[2]) * 17;
        return {r, g, b, 255};
      } else if (hex.length() == 4) {
        // #RGBA -> #RRGGBBAA
        uint8_t r = hexCharToInt(hex[0]) * 17;
        uint8_t g = hexCharToInt(hex[1]) * 17;
        uint8_t b = hexCharToInt(hex[2]) * 17;
        uint8_t a = hexCharToInt(hex[3]) * 17;
        return {r, g, b, a};
      } else if (hex.length() == 6 || hex.length() == 8) {
        // #RRGGBB or #RRGGBBAA
        uint8_t r = (hexCharToInt(hex[0]) << 4) | hexCharToInt(hex[1]);
        uint8_t g = (hexCharToInt(hex[2]) << 4) | hexCharToInt(hex[3]);
        uint8_t b = (hexCharToInt(hex[4]) << 4) | hexCharToInt(hex[5]);
        uint8_t a = 255;
        if (hex.length() == 8) {
          a = (hexCharToInt(hex[6]) << 4) | hexCharToInt(hex[7]);
        }
        return {r, g, b, a};
      } else {
        // Malformed hex color (not 3, 4, 6, or 8 chars) — fallback
        return Color::Black();
      }
    }

    // Default fallback
    return Color::Black();
  }

  Color parseFunctionalColor(const std::string &val) {
    size_t start = val.find('(');
    size_t end = val.find(')');
    if (start == std::string::npos || end == std::string::npos)
      return Color::Black();

    std::string content = val.substr(start + 1, end - start - 1);
    std::stringstream ss(content);
    std::string token;
    std::vector<float> values;

    while (std::getline(ss, token, ',')) {
      token.erase(remove_if(token.begin(), token.end(), isspace), token.end());
      float num = 0.0f;
      std::string percent;

      size_t percentPos = token.find('%');
      if (percentPos != std::string::npos) {
        percent = "%";
        token = token.substr(0, percentPos);
      }

      try {
        num = std::stof(token);
      } catch (...) {
        continue;
      }

      if (!percent.empty()) {
        num = num * 255.0f / 100.0f;
      }
      values.push_back(num);
    }

    if (values.size() >= 3) {
      uint8_t r = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, values[0])));
      uint8_t g = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, values[1])));
      uint8_t b = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, values[2])));
      uint8_t a = 255;
      if (values.size() >= 4) {
        if (values[3] <= 1.0f) {
          a = static_cast<uint8_t>(values[3] * 255.0f);
        } else {
          a = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, values[3])));
        }
      }
      return {r, g, b, a};
    }

    return Color::Black();
  }

  Color parseHSLColor(const std::string &val) {
    size_t start = val.find('(');
    size_t end = val.find(')');
    if (start == std::string::npos || end == std::string::npos)
      return Color::Black();

    std::string content = val.substr(start + 1, end - start - 1);
    std::stringstream ss(content);
    std::string token;
    std::vector<float> values;

    while (std::getline(ss, token, ',')) {
      token.erase(remove_if(token.begin(), token.end(), isspace), token.end());
      float num = 0.0f;
      std::string percent;

      size_t percentPos = token.find('%');
      if (percentPos != std::string::npos) {
        percent = "%";
        token = token.substr(0, percentPos);
      }

      try {
        num = std::stof(token);
      } catch (...) {
        continue;
      }

      // Note: HSL saturation and lightness are percentage values
      // that are divided by 100 below (not converted to 0-255 like RGB).
      values.push_back(num);
    }

    if (values.size() >= 3) {
      float h = values[0];
      float s = values.size() > 1 ? values[1] / 100.0f : 0.0f;
      float l = values.size() > 2 ? values[2] / 100.0f : 0.0f;

      if (h > 1) h = h / 360.0f;

      float r, g, b;
      if (s == 0) {
        r = g = b = l;
      } else {
        auto hue2rgb = [](float p, float q, float t) {
          if (t < 0) t += 1;
          if (t > 1) t -= 1;
          if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
          if (t < 1.0f/2.0f) return q;
          if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
          return p;
        };
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
        float p = 2.0f * l - q;
        r = hue2rgb(p, q, h + 1.0f/3.0f);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1.0f/3.0f);
      }

      uint8_t alpha = 255;
      if (values.size() >= 4) {
        if (values[3] <= 1.0f) {
          alpha = static_cast<uint8_t>(values[3] * 255.0f);
        } else {
          alpha = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, values[3])));
        }
      }

      return {
        static_cast<uint8_t>(r * 255),
        static_cast<uint8_t>(g * 255),
        static_cast<uint8_t>(b * 255),
        alpha
      };
    }

    return Color::Black();
  }

  // ── Custom properties / var() ───────────────────────────────

  static bool isCustomPropertyName(const std::string &name) {
    return name.size() > 2 && name[0] == '-' && name[1] == '-';
  }

  // Cascade the element's custom properties: the winning declaration per
  // name (important over normal, then specificity and order) with var()
  // references between them resolved. Values stay text so they substitute
  // verbatim ("1px solid red", "rgba(0, 0, 0, 0.5)", "2rem").
  void applyCustomProperties(ComputedStyle &style,
                             const std::vector<MatchResult> &matches,
                             const ComputedStyle *parentStyle) {
    std::unordered_map<std::string, std::string> winners;
    for (bool important : {false, true}) {
      for (const auto &match : matches) {
        for (const auto &decl : match.rule->declarations) {
          if (decl.important == important &&
              isCustomPropertyName(decl.property))
            winners[decl.property] = decl.value;
        }
      }
    }

    std::vector<std::string> pending;
    for (const auto &entry : winners) {
      const std::string &name = entry.first;
      const std::string &value = entry.second;
      if (value == "initial") {
        style.customProperties.erase(name);
      } else if (value == "inherit" || value == "unset") {
        const CustomPropertyValue *inherited =
            parentStyle ? parentStyle->getCustomProperty(name) : nullptr;
        if (inherited)
          style.setCustomProperty(name, *inherited);
        else
          style.customProperties.erase(name);
      } else {
        style.setCustomProperty(name, value);
        pending.push_back(name);
      }
    }

    // Resolve against the raw values first and publish afterwards, so the
    // outcome does not depend on the order of the declarations
    std::vector<std::pair<std::string, std::optional<std::string>>> resolved;
    for (const auto &name : pending)
      resolved.emplace_back(name, substituteVariables(winners[name], style, 0));
    for (const auto &entry : resolved) {
      if (entry.second)
        style.setCustomProperty(entry.first, *entry.second);
      else
        style.customProperties.erase(entry.first); // cyclic or unresolvable
    }
  }

  static std::string customPropertyText(const CustomPropertyValue &v) {
    if (const auto *text = std::get_if<std::string>(&v))
      return *text;
    if (const auto *len = std::get_if<Length>(&v)) {
      switch (len->unit) {
      case Length::Unit::Px:
        return std::to_string(len->value) + "px";
      case Length::Unit::Percent:
        return std::to_string(len->value) + "%";
      case Length::Unit::Em:
        return std::to_string(len->value) + "em";
      case Length::Unit::Rem:
        return std::to_string(len->value) + "rem";
      case Length::Unit::Auto:
        return "auto";
      }
      return "";
    }
    const Color &c = std::get<Color>(v);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
    return buf;
  }

  // Index of the ')' that closes the '(' at openPos, or npos
  static size_t findMatchingParen(const std::string &s, size_t openPos) {
    int depth = 0;
    char quote = 0;
    for (size_t i = openPos; i < s.size(); ++i) {
      char c = s[i];
      if (quote) {
        if (c == '\\')
          ++i;
        else if (c == quote)
          quote = 0;
      } else if (c == '"' || c == '\'') {
        quote = c;
      } else if (c == '(') {
        ++depth;
      } else if (c == ')' && --depth == 0) {
        return i;
      }
    }
    return std::string::npos;
  }

  static size_t findTopLevelComma(const std::string &s) {
    int depth = 0;
    char quote = 0;
    for (size_t i = 0; i < s.size(); ++i) {
      char c = s[i];
      if (quote) {
        if (c == '\\')
          ++i;
        else if (c == quote)
          quote = 0;
      } else if (c == '"' || c == '\'') {
        quote = c;
      } else if (c == '(') {
        ++depth;
      } else if (c == ')') {
        --depth;
      } else if (c == ',' && depth == 0) {
        return i;
      }
    }
    return std::string::npos;
  }

  static constexpr int kMaxVarDepth = 32;

  // Replace every var(--name[, fallback]) in value. Returns nullopt when a
  // reference has neither a value nor a fallback, or when references nest
  // too deeply (cycles).
  static std::optional<std::string>
  substituteVariables(const std::string &value, const ComputedStyle &style,
                      int depth) {
    if (value.find("var(") == std::string::npos)
      return value;
    if (depth > kMaxVarDepth)
      return std::nullopt;

    std::string result;
    size_t pos = 0;
    while (true) {
      size_t start = value.find("var(", pos);
      if (start == std::string::npos) {
        result += value.substr(pos);
        return result;
      }
      size_t close = findMatchingParen(value, start + 3);
      if (close == std::string::npos)
        return std::nullopt;
      result += value.substr(pos, start - pos);

      std::string inner = value.substr(start + 4, close - start - 4);
      size_t comma = findTopLevelComma(inner);
      std::string name = dom::trimWhitespace(inner.substr(0, comma));
      std::optional<std::string> replacement;
      if (const CustomPropertyValue *v = style.getCustomProperty(name))
        replacement =
            substituteVariables(customPropertyText(*v), style, depth + 1);
      if (!replacement && comma != std::string::npos)
        replacement = substituteVariables(
            dom::trimWhitespace(inner.substr(comma + 1)), style, depth + 1);
      if (!replacement)
        return std::nullopt;
      result += *replacement;
      pos = close + 1;
    }
  }

  // A bare <number> such as "1" or "0.5" (no unit)
  static bool parsePlainNumber(const std::string &s, float &out) {
    try {
      size_t idx = 0;
      out = std::stof(s, &idx);
      return idx == s.size();
    } catch (...) {
      return false;
    }
  }

  uint8_t hexCharToInt(char c) {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return 0;
  }

  // Parse a space-separated list of track sizes, e.g. "1fr 2fr 100px 50%"
  std::vector<TrackSize> parseTrackSizeList(const std::string &val) {
    std::vector<TrackSize> result;
    std::stringstream ss(val);
    std::string token;
    while (ss >> token) {
      TrackSize ts;
      if (token.find("fr") != std::string::npos) {
        ts.type = TrackSize::Type::Fr;
        try {
          ts.value = std::stof(token);
        } catch (...) {
          ts.value = 1.0f;
        }
      } else if (token.find('%') != std::string::npos) {
        ts.type = TrackSize::Type::Percent;
        try {
          ts.value = std::stof(token);
        } catch (...) {
          ts.value = 0.0f;
        }
      } else if (token == "auto") {
        ts.type = TrackSize::Type::Auto;
        ts.value = 0.0f;
      } else if (token == "min-content") {
        ts.type = TrackSize::Type::MinContent;
        ts.value = 0.0f;
      } else if (token == "max-content") {
        ts.type = TrackSize::Type::MaxContent;
        ts.value = 0.0f;
      } else {
        // Default: treat as px
        ts.type = TrackSize::Type::Px;
        try {
          ts.value = std::stof(token);
        } catch (...) {
          ts.value = 0.0f;
        }
      }
      result.push_back(ts);
    }
    return result;
  }

  // Parse a grid line number, e.g. "1", "2", "span 2"
  int parseGridLine(const std::string &val) {
    // Trim whitespace
    std::string v = val;
    size_t first = v.find_first_not_of(" \t\r\n");
    if (first != std::string::npos) v.erase(0, first);
    size_t last = v.find_last_not_of(" \t\r\n");
    if (last != std::string::npos) v.erase(last + 1);

    // Handle "span N"
    if (v.substr(0, 4) == "span") {
      std::string numStr = v.substr(4);
      try {
        return -std::stoi(numStr); // Negative = span
      } catch (...) {
        return -1;
      }
    }

    try {
      return std::stoi(v);
    } catch (...) {
      return 1;
    }
  }

  // Parse grid shorthand like "1 / 3" or "1 / span 2"
  void parseGridShorthand(const std::string &val, int &start, int &end) {
    size_t slashPos = val.find('/');
    if (slashPos != std::string::npos) {
      std::string startStr = val.substr(0, slashPos);
      std::string endStr = val.substr(slashPos + 1);
      start = parseGridLine(startStr);
      end = parseGridLine(endStr);
    } else {
      // Single value: set start, auto end
      start = parseGridLine(val);
      // end remains default (start + 1)
    }
  }
};

} // namespace css
} // namespace xiaopeng
