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

      if (peek().is(TokenType::AtKeyword) || peek().is(TokenType::CDO) ||
          peek().is(TokenType::CDC)) {
        // Skip At-rules and CDO/CDC for MVP
        consumeUntil(TokenType::CloseCurly); // Very naive skipping
        consumeIgnoreEOF();                  // Consume the }
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

            // Collect function arguments (inline token reconstruction)
            while (position_ < tokens_.size() &&
                   !peek().is(TokenType::CloseParen) &&
                   !peek().is(TokenType::EndOfFile)) {
              Token arg = consume();
              if (arg.is(TokenType::Whitespace)) {
                if (!args.empty() && args.back() != ' ')
                  args += " ";
              } else if (arg.is(TokenType::Comma)) {
                args += ",";
              } else if (arg.is(TokenType::Ident) || arg.is(TokenType::Delim)) {
                args += arg.value;
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
            if (peek().is(TokenType::CloseParen)) {
              consume(); // )
            }
            part.value = funcName + "(" + args + ")";
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

    consumeWhitespace();
    if (!peek().is(TokenType::Colon))
      return std::nullopt;
    consume(); // :
    consumeWhitespace();

    // Value parsing: consume until ; or }
    while (position_ < tokens_.size() && !peek().is(TokenType::Semicolon) &&
           !peek().is(TokenType::CloseCurly)) {
      // Check for !important
      if (peek().is(TokenType::Delim) && peek().value == "!") {
        consume();
        if (peek().is(TokenType::Ident) && peek().value == "important") {
          decl.important = true;
          consume();
          continue;
        } else {
          decl.value += "!";
        }
      }

      Token t = consume();
      if (t.is(TokenType::Whitespace)) {
        if (!decl.value.empty() && decl.value.back() != ' ') {
          decl.value += " ";
        }
      } else if (t.is(TokenType::Function)) {
        decl.value += t.value + "(";
        while (position_ < tokens_.size() &&
               !peek().is(TokenType::CloseParen) &&
               !peek().is(TokenType::EndOfFile)) {
          Token arg = consume();
          if (arg.is(TokenType::Whitespace)) {
            if (!decl.value.empty() && decl.value.back() != ' ') {
              decl.value += " ";
            }
          } else if (arg.is(TokenType::Comma)) {
            decl.value += ",";
          } else if (arg.is(TokenType::Number)) {
            decl.value += formatNumber(arg.numberValue);
          } else if (arg.is(TokenType::Percentage)) {
            decl.value += formatNumber(arg.numberValue) + "%";
          } else if (arg.is(TokenType::Dimension)) {
            decl.value += formatNumber(arg.numberValue) + arg.unit;
          } else if (arg.is(TokenType::Ident)) {
            decl.value += arg.value;
          } else if (arg.is(TokenType::Delim)) {
            decl.value += arg.value;
          }
        }
        if (peek().is(TokenType::CloseParen)) {
          decl.value += ")";
          consume();
        }
      } else if (t.is(TokenType::String)) {
        decl.value += "\"" + t.value + "\"";
      } else if (t.is(TokenType::Url)) {
        decl.value += "url(" + t.value + ")";
      } else if (t.is(TokenType::Dimension)) {
        decl.value +=
            formatNumber(t.numberValue) + t.unit; // Rough reconstruction
      } else if (t.is(TokenType::Number)) {
        decl.value += formatNumber(t.numberValue);
      } else if (t.is(TokenType::Percentage)) {
        decl.value += formatNumber(t.numberValue) + "%";
      } else if (t.is(TokenType::Hash)) {
        decl.value += "#" + t.value;
      } else if (t.is(TokenType::Ident)) {
        decl.value += t.value;
      } else if (t.is(TokenType::Delim)) {
        decl.value += t.value;
      } else if (t.is(TokenType::Comma)) {
        decl.value += ",";
      }
      // ... incomplete value reconstruction but good enough for storage
    }

    if (peek().is(TokenType::Semicolon)) {
      consume();
    }

    return decl;
  }
};

} // namespace css
} // namespace xiaopeng
