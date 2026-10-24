#pragma once

#include "css_tokenizer.hpp"
#include "css_types.hpp"
#include <optional>
#include <string>
#include <vector>


namespace xiaopeng {
namespace css {

class CssParser {
public:
  CssParser(const std::string &input) : tokenizer_(input) {
    tokens_ = tokenizer_.tokenize();
    position_ = 0;
  }

  StyleSheet parse() {
    StyleSheet sheet;
    while (position_ < tokens_.size() && !peek().is(TokenType::EndOfFile)) {
      consumeWhitespace();
      if (peek().is(TokenType::EndOfFile))
        break;

      if (peek().is(TokenType::CDO) || peek().is(TokenType::CDC)) {
        consume(); // <!-- and --> are ignored at the top level
        continue;
      }
      if (peek().is(TokenType::AtKeyword)) {
        skipAtRule(); // At-rules are not applied yet
        continue;
      }

      auto rule = parseRule();
      if (rule) {
        sheet.rules.push_back(*rule);
      } else {
        // Error recovery: skip until }
        consumeUntil(TokenType::CloseCurly);
        consumeIgnoreEOF();
      }
    }
    return sheet;
  }

  // Parse a standalone selector list ("div > p, .item") as used by the DOM
  // querySelector API. Returns an empty vector for invalid input (callers
  // treat that as "no match" instead of throwing).
  static std::vector<Selector> parseSelectorList(const std::string &input) {
    CssParser parser(input);
    std::vector<Selector> result;
    while (true) {
      parser.consumeWhitespace();
      if (parser.peek().is(TokenType::EndOfFile))
        break;
      auto selector = parser.parseSelector();
      if (!selector)
        return {};
      result.push_back(*selector);
      parser.consumeWhitespace();
      if (parser.peek().is(TokenType::Comma)) {
        parser.consume();
        continue;
      }
      break;
    }
    return result;
  }

  // Specificity contribution of a functional pseudo-class, per CSS Selectors
  // Level 4: :where(...) is always zero; :is(...)/:not(...)/:has(...) weigh
  // their most specific argument. Returns nullopt when there is no special
  // rule (unknown function) or the argument list cannot be parsed — callers
  // then keep the plain (0,1,0) pseudo-class weight.
  static std::optional<Specificity>
  functionalPseudoSpecificity(const std::string &name,
                              const std::string &args) {
    const std::string funcName = asciiLower(name);
    if (funcName == "where")
      return Specificity{}; // always zero, even for unparseable arguments
    if (funcName != "is" && funcName != "not" && funcName != "has")
      return std::nullopt;

    std::string list = args;
    if (funcName == "has") {
      // Relative selector: strip a leading combinator (> + ~); the match
      // engine resolves it against :scope the same way (matchHasRelative).
      size_t i = list.find_first_not_of(" \t\r\n\f");
      if (i != std::string::npos &&
          (list[i] == '>' || list[i] == '+' || list[i] == '~'))
        list = list.substr(i + 1);
    }

    auto selectors = parseSelectorList(list);
    if (selectors.empty())
      return std::nullopt;
    Specificity max{};
    for (const auto &sel : selectors) {
      Specificity sp = sel.specificity();
      if (max < sp)
        max = sp;
    }
    return max;
  }

private:
  CssTokenizer tokenizer_;
  std::vector<Token> tokens_;
  size_t position_;

  // Format a number without excessive trailing zeros
  static std::string formatNumber(double n) {
    if (n == static_cast<int64_t>(n)) {
      return std::to_string(static_cast<int64_t>(n));
    }
    std::string s = std::to_string(n);
    // Remove trailing zeros
    s.erase(s.find_last_not_of('0') + 1, std::string::npos);
    if (s.back() == '.')
      s.pop_back();
    return s;
  }

  static std::string asciiLower(std::string s) {
    for (char &c : s) {
      if (c >= 'A' && c <= 'Z')
        c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
  }

  // Re-serialize a string token so that re-parsing yields the same string
  static std::string quoteString(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
      if (c == '"' || c == '\\')
        out += '\\';
      out += c;
    }
    out += '"';
    return out;
  }

  // Append one declaration-value token to its textual form. Function and
  // paren nesting is tracked so nested calls such as
  // var(--x, rgb(1, 2, 3)) keep every component.
  static void appendValueToken(std::string &out, const Token &t, int &depth) {
    if (t.is(TokenType::Whitespace)) {
      if (!out.empty() && out.back() != ' ')
        out += " ";
    } else if (t.is(TokenType::Function)) {
      out += t.value + "(";
      depth++;
    } else if (t.is(TokenType::OpenParen)) {
      out += "(";
      depth++;
    } else if (t.is(TokenType::CloseParen)) {
      out += ")";
      if (depth > 0)
        depth--;
    } else if (t.is(TokenType::String)) {
      out += quoteString(t.value);
    } else if (t.is(TokenType::Url)) {
      out += "url(" + t.value + ")";
    } else if (t.is(TokenType::Hash)) {
      out += "#" + t.value;
    } else if (t.is(TokenType::Dimension)) {
      out += formatNumber(t.numberValue) + t.unit;
    } else if (t.is(TokenType::Number)) {
      out += formatNumber(t.numberValue);
    } else if (t.is(TokenType::Percentage)) {
      out += formatNumber(t.numberValue) + "%";
    } else {
      out += t.value; // Ident, Delim, Comma, Colon, Semicolon, brackets
    }
  }

  Token peek() const {
    if (position_ >= tokens_.size())
      return {TokenType::EndOfFile};
    return tokens_[position_];
  }

  Token peekNext() const {
    if (position_ + 1 >= tokens_.size())
      return {TokenType::EndOfFile};
    return tokens_[position_ + 1];
  }

  Token consume() {
    if (position_ >= tokens_.size())
      return {TokenType::EndOfFile};
    return tokens_[position_++];
  }

  void consumeIgnoreEOF() {
    if (position_ < tokens_.size())
      position_++;
  }

  void consumeWhitespace() {
    while (peek().is(TokenType::Whitespace)) {
      consume();
    }
  }

  void consumeUntil(TokenType type) {
    while (position_ < tokens_.size() && !peek().is(TokenType::EndOfFile) &&
           !peek().is(type)) {
      consume();
    }
  }

  // Statement at-rules (@import, @charset) end at ';', block at-rules
  // (@media, @font-face) after their balanced {...} block — skipping only to
  // the first '}' would swallow the following rule or leak nested ones.
  void skipAtRule() {
    consume(); // at-keyword
    int depth = 0;
    while (position_ < tokens_.size() && !peek().is(TokenType::EndOfFile)) {
      Token t = consume();
      if (depth == 0 && t.is(TokenType::Semicolon))
        return;
      if (t.is(TokenType::OpenCurly)) {
        depth++;
      } else if (t.is(TokenType::CloseCurly)) {
        if (--depth <= 0)
          return;
      }
    }
  }

  std::optional<Rule> parseRule() {
    Rule rule;

    // Parse Selector List
    while (true) {
      auto selector = parseSelector();
      if (selector) {
        rule.selectors.push_back(*selector);
      } else {
        return std::nullopt;
      }

      consumeWhitespace();
      if (peek().is(TokenType::Comma)) {
        consume(); // ,
        consumeWhitespace();
        continue;
      } else if (peek().is(TokenType::OpenCurly)) {
        break;
      } else {
        // Unexpected token in selector list
        return std::nullopt;
      }
    }

    if (!peek().is(TokenType::OpenCurly))
      return std::nullopt;
    consume(); // {

    // Parse Declarations
    while (position_ < tokens_.size() && !peek().is(TokenType::EndOfFile) &&
           !peek().is(TokenType::CloseCurly)) {
      consumeWhitespace();
      if (peek().is(TokenType::CloseCurly))
        break;

      auto decl = parseDeclaration();
      if (decl) {
        rule.declarations.push_back(*decl);
      } else {
        while (position_ < tokens_.size() && !peek().is(TokenType::Semicolon) &&
               !peek().is(TokenType::CloseCurly)) {
          consume();
        }
        if (peek().is(TokenType::Semicolon)) {
          consume();
        } else if (peek().is(TokenType::CloseCurly)) {
          consume();
          break;
        }
      }
    }

    if (peek().is(TokenType::CloseCurly)) {
      consume(); // }
    }

    return rule;
  }

  std::optional<Selector> parseSelector() {
    Selector selector;
    consumeWhitespace();

    // Loop to parse compound selectors separated by combinators
    while (position_ < tokens_.size()) {
      if (peek().is(TokenType::Comma) || peek().is(TokenType::OpenCurly) ||
          peek().is(TokenType::EndOfFile)) {
        break;
      }

      std::optional<Combinator> combinator;
      bool hasCombinator = false;
      if (peek().is(TokenType::Whitespace)) {
        consumeWhitespace();
        if (peek().is(TokenType::Delim) && peek().value == ">") {
          combinator = Combinator::Child;
          consume();
          hasCombinator = true;
        } else if (peek().is(TokenType::Delim) && peek().value == "+") {
          combinator = Combinator::NextSibling;
          consume();
          hasCombinator = true;
        } else if (peek().is(TokenType::Delim) && peek().value == "~") {
          combinator = Combinator::SubsequentSibling;
          consume();
          hasCombinator = true;
        } else if (peek().is(TokenType::Comma) ||
                   peek().is(TokenType::OpenCurly)) {
          break;
        } else if (!peek().is(TokenType::EndOfFile)) {
          combinator = Combinator::Descendant;
          hasCombinator = true;
        }
      } else if (peek().is(TokenType::Delim) && peek().value == ">") {
        combinator = Combinator::Child;
        consume();
        hasCombinator = true;
      } else if (peek().is(TokenType::Delim) && peek().value == "+") {
        combinator = Combinator::NextSibling;
        consume();
        hasCombinator = true;
      } else if (peek().is(TokenType::Delim) && peek().value == "~") {
        combinator = Combinator::SubsequentSibling;
        consume();
        hasCombinator = true;
      }

      if (!selector.parts.empty() && hasCombinator && combinator.has_value()) {
        selector.combinators.push_back(*combinator);
      }

      consumeWhitespace();

      // Check if we hit end again after consuming combinator/whitespace
      if (peek().is(TokenType::Comma) || peek().is(TokenType::OpenCurly)) {
        break;
      }

      SimpleSelector simple;
      // Parse Simple Selector sequence (e.g. div.class#id)
      // Order doesn't strictly matter in CSS, but let's assume we handle one
      // part Actually selector parts are compounded. e.g. "div.foo" is one
      // compound selector. My SimpleSelector struct currently only holds ONE
      // type. I should have `Struct SelectorPart` which contains a list of
      // `SimpleSelectors`? "div.foo" -> Tag(div), Class(foo). My current
      // structure `Selector` -> vector<SimpleSelector> separated by Combinators
      // is flawed because "div.foo" has no combinator. I'll simplify: A
      // `SimpleSelector` in my `parts` vector will represent a single atomic
      // selector (Tag/Class/Id). If they are adjacent without whitespace, the
      // Combinator is effectively "None" or "Compound". I'll calculate standard
      // combinators (Descendant, Child) but adjacent selectors like `div.cls`
      // will be treated as separate parts with `Combinator::None`? Let's add
      // `Combinator::None` which implies "Same Element".

      // Wait, my css_types.hpp defines combinators list size = parts size - 1??
      // `combinators[i]` is between `parts[i]` and `parts[i+1]`.
      // So parsing `div.cls` -> Part(Tag div), Part(Class cls). Combinator?
      // Yes, I need a combinator for "same element" (compound selector).
      // Let's use `Combinator::None` for compound selectors.

      bool firstInCompound = true;
      while (true) {
        Token t = peek();
        SimpleSelector part;
        bool gotPart = false;

        if (t.is(TokenType::Ident)) {
          // Tag name
          if (!firstInCompound)
            break; // Tag name must be first in compound selector
          part.type = SelectorType::Tag;
          part.value = consume().value;
          gotPart = true;
        } else if (t.is(TokenType::Delim) && t.value == "*") {
          if (!firstInCompound)
            break;
          part.type = SelectorType::Universal;
          consume();
          gotPart = true;
        } else if (t.is(TokenType::Hash)) {
          part.type = SelectorType::Id;
          part.value = consume().value;
          gotPart = true;
        } else if (t.is(TokenType::Delim) && t.value == ".") {
          consume(); // .
          if (peek().is(TokenType::Ident)) {
            part.type = SelectorType::Class;
            part.value = consume().value;
            gotPart = true;
          } else {
            return std::nullopt; // Error
          }
        } else if (t.is(TokenType::Colon)) {
          consume(); // :
          // Pseudo class/element
          // Simplified interaction
          if (peek().is(TokenType::Colon)) {
            consume();
            part.type = SelectorType::PseudoElement;
          } else {
            part.type = SelectorType::PseudoClass;
          }
          if (peek().is(TokenType::Ident)) {
            part.value = consume().value;
            gotPart = true;
          } else if (peek().is(TokenType::Function)) {
            std::string funcName = consume().value;
            std::string args;

            // Collect function arguments (inline token reconstruction).
            // Nested functions/parens keep their own ')' so that arguments
            // such as :not(:nth-child(2)) survive intact.
            int depth = 0;
            while (position_ < tokens_.size() &&
                   !peek().is(TokenType::EndOfFile)) {
              if (depth == 0 && peek().is(TokenType::CloseParen))
                break;
              Token arg = consume();
              if (arg.is(TokenType::Whitespace)) {
                if (!args.empty() && args.back() != ' ')
                  args += " ";
              } else if (arg.is(TokenType::Function)) {
                args += arg.value + "(";
                depth++;
              } else if (arg.is(TokenType::OpenParen)) {
                args += "(";
                depth++;
              } else if (arg.is(TokenType::CloseParen)) {
                args += ")";
                depth--;
              } else if (arg.is(TokenType::Hash)) {
                args += "#" + arg.value;
              } else if (arg.is(TokenType::String)) {
                args += quoteString(arg.value);
              } else if (!arg.repr.empty()) {
                args += arg.repr; // numbers keep their sign: 2n+1, -n+3
              } else if (arg.is(TokenType::Number)) {
                args += formatNumber(arg.numberValue);
              } else if (arg.is(TokenType::Percentage)) {
                args += formatNumber(arg.numberValue) + "%";
              } else if (arg.is(TokenType::Dimension)) {
                args += formatNumber(arg.numberValue) + arg.unit;
              } else {
                args += arg.value;
              }
            }
            while (!args.empty() && args.back() == ' ')
              args.pop_back();
            if (peek().is(TokenType::CloseParen)) {
              consume(); // )
            }
            part.value = funcName + "(" + args + ")";
            // Precompute the specificity contribution (:is/:where/:has have
            // special rules; other functions keep the default (0,1,0)).
            part.functionSpecificity = functionalPseudoSpecificity(funcName, args);
            gotPart = true;
          }
        } else if (t.is(TokenType::OpenSquare)) {
          consume(); // [
          part.type = SelectorType::Attribute;
          consumeWhitespace();
          if (peek().is(TokenType::Ident)) {
            part.attributeName = consume().value;
            consumeWhitespace();

            // Parse optional operator: =, ~=, |=, ^=, $=, *=
            if (peek().is(TokenType::Delim)) {
              std::string op;
              std::string delim = peek().value;
              if (delim == "~" || delim == "|" || delim == "^" || delim == "$" ||
                  delim == "*") {
                op = consume().value;
                if (peek().is(TokenType::Delim) && peek().value == "=") {
                  op += consume().value; // e.g. "~="
                } else {
                  // Lone ~ | ^ $ * without = — treat as missing operator
                  op.clear();
                }
              } else if (delim == "=") {
                op = consume().value; // "="
              }
              if (!op.empty()) {
                part.attributeOperator = op;
                consumeWhitespace();
                // Parse the attribute value (Ident or String)
                if (peek().is(TokenType::Ident) ||
                    peek().is(TokenType::String)) {
                  part.attributeValue = consume().value;
                  consumeWhitespace();
                }
              }
            }
          }
          consumeUntil(TokenType::CloseSquare);
          if (peek().is(TokenType::CloseSquare))
            consume(); // ]
          gotPart = true;
        } else {
          break; // Not a selector part
        }

        if (gotPart) {
          if (!selector.parts.empty() && firstInCompound == false) {
            // This is a compound selector part, so stored combinator should
            // have been None? Actually, my structure is: [Part1] [Comb1]
            // [Part2] If I parsed Part1, now parsing Part2 without
            // whitespace/combinator, I need to push Comb::None But if this is
            // the VERY first part of the whole selector chain, no combinator
            // needed.

            // Logic:
            // parseSelector loop handles "Chains".
            // Inner loop handles "Compound".
            // If I am in inner loop (compound) and I parse a 2nd part, I must
            // push Combinator::None to previous list? No, my outer loop pushes
            // combinator BEFORE parsing the next chunk. So if I am in inner
            // loop, I am adding parts to the SAME chunk? "div.cls" is typically
            // one "Compound Selector". If my `Selector` struct flattens
            // everything into valid simple selectors + combinators: Part(div),
            // Comb(None), Part(.cls). Yes, `Combinator::None` implies
            // ADJACENT/SAME ELEMENT.

            selector.combinators.push_back(Combinator::None);
          }
          selector.parts.push_back(part);
          firstInCompound = false;
        }
      }

      if (firstInCompound) {
        // We entered the loop but didn't parse anything.
        // This means we hit something that isn't a selector part, e.g.
        // combinator char or EOF or comma.
        break;
      }
    }

    return selector.parts.empty() ? std::nullopt : std::make_optional(selector);
  }

  std::optional<Declaration> parseDeclaration() {
    Declaration decl;

    if (!peek().is(TokenType::Ident))
      return std::nullopt;
    decl.property = consume().value;
    // Property names are ASCII case-insensitive; custom properties are not
    if (decl.property.compare(0, 2, "--") != 0)
      decl.property = asciiLower(decl.property);

    consumeWhitespace();
    if (!peek().is(TokenType::Colon))
      return std::nullopt;
    consume(); // :
    consumeWhitespace();

    // Value parsing: consume until ; or } (a ';' inside a function such as an
    // unquoted data: URL does not end the declaration)
    int depth = 0;
    while (position_ < tokens_.size() && !peek().is(TokenType::EndOfFile) &&
           !peek().is(TokenType::CloseCurly) &&
           !(depth == 0 && peek().is(TokenType::Semicolon))) {
      // "!important" — whitespace may separate '!' from the keyword, and the
      // keyword is case-insensitive
      if (depth == 0 && peek().is(TokenType::Delim) && peek().value == "!") {
        size_t next = position_ + 1;
        while (next < tokens_.size() &&
               tokens_[next].is(TokenType::Whitespace))
          ++next;
        if (next < tokens_.size() && tokens_[next].is(TokenType::Ident) &&
            asciiLower(tokens_[next].value) == "important") {
          decl.important = true;
          position_ = next + 1;
          continue;
        }
      }

      appendValueToken(decl.value, consume(), depth);
    }

    // Whitespace before ';', '}' or '!important' is not part of the value;
    // keyword comparisons ("none", "block", ...) need the exact token
    while (!decl.value.empty() && decl.value.back() == ' ')
      decl.value.pop_back();

    if (peek().is(TokenType::Semicolon)) {
      consume();
    }

    return decl;
  }
};

} // namespace css
} // namespace xiaopeng
