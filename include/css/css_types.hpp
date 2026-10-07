#pragma once

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace xiaopeng {
namespace css {

enum class TokenType {
  Ident,
  Function,
  AtKeyword,
  Hash,
  String,
  BadString,
  Url,
  BadUrl,
  Delim,
  Number,
  Percentage,
  Dimension,
  Whitespace,
  CDO,
  CDC,
  Colon,
  Semicolon,
  Comma,
  OpenSquare,
  CloseSquare,
  OpenParen,
  CloseParen,
  OpenCurly,
  CloseCurly,
  EndOfFile
};

struct Token {
  TokenType type = TokenType::EndOfFile;
  std::string value{};
  std::string unit{};       // For Dimension
  double numberValue = 0.0; // For Number, Percentage, Dimension
  bool flag = false; // "type flag" e.g. for Hash (id vs unrestricted), Number
                     // (integer vs number)

  bool is(TokenType t) const { return type == t; }

  // Debug helper
  std::string toString() const {
    switch (type) {
    case TokenType::Ident:
      return "Ident(" + value + ")";
    case TokenType::AtKeyword:
      return "AtKeyword(" + value + ")";
    case TokenType::String:
      return "String(\"" + value + "\")";
    case TokenType::Hash:
      return "Hash(" + value + ")";
    case TokenType::Number:
      return "Number(" + std::to_string(numberValue) + ")";
    case TokenType::Percentage:
      return "Percentage(" + std::to_string(numberValue) + "%)";
    case TokenType::Dimension:
      return "Dimension(" + std::to_string(numberValue) + unit + ")";
    case TokenType::Delim:
      return "Delim(" + value + ")";
    case TokenType::Whitespace:
      return "Whitespace";
    case TokenType::Colon:
      return "Colon";
    case TokenType::Semicolon:
      return "Semicolon";
    case TokenType::OpenCurly:
      return "{";
    case TokenType::CloseCurly:
      return "}";
    case TokenType::EndOfFile:
      return "EOF";
    default:
      return "Token";
    }
  }
};

enum class SelectorType {
  Tag,
  Id,
  Class,
  Universal,
  Attribute,
  PseudoClass,
  PseudoElement
};

enum class Combinator {
  None,
  Descendant,       // ' '
  Child,            // '>'
  NextSibling,      // '+'
  SubsequentSibling // '~'
};

// Specificity of a selector (a=IDs, b=classes/attributes/pseudo-classes,
// c=tags/pseudo-elements). At namespace scope so SimpleSelector can carry a
// precomputed contribution for functional pseudo-classes.
struct Specificity {
  uint32_t a = 0; // IDs
  uint32_t b = 0; // Classes, Attributes, Pseudo-classes
  uint32_t c = 0; // Tags, Pseudo-elements

  bool operator<(const Specificity &other) const {
    if (a != other.a)
      return a < other.a;
    if (b != other.b)
      return b < other.b;
    return c < other.c;
  }
  bool operator>(const Specificity &other) const { return other < *this; }
  bool operator==(const Specificity &other) const {
    return a == other.a && b == other.b && c == other.c;
  }
  bool operator!=(const Specificity &other) const {
    return !(*this == other);
  }
};

struct SimpleSelector {
  SelectorType type;
  std::string value; // tag name, class name, id, etc.
  // Attribute selector details could go here
  std::string attributeName;
  std::string attributeValue;
  std::string attributeOperator; // =, ~=, |=, etc.
  // Functional pseudo-classes (:is()/:where()/:has()) do not weigh as a
  // plain (0,1,0): :where() contributes zero and :is()/:has() contribute
  // their most specific argument (CSS Selectors Level 4). The parser
  // precomputes that contribution here; empty means "fall back to (0,1,0)"
  // (hand-built selectors or unparseable argument lists).
  std::optional<Specificity> functionSpecificity;

  bool match(const std::string &specificValue) const {
    // Basic match logic stub
    return value == specificValue;
  }
};

struct Selector {
  std::vector<SimpleSelector> parts;
  std::vector<Combinator>
      combinators; // combinators[i] is between parts[i] and parts[i+1]

  // Specificity type kept as a member alias for backwards compatibility
  using Specificity = ::xiaopeng::css::Specificity;

  // Calculate specificity
  Specificity specificity() const {
    Specificity s;

    for (const auto &part : parts) {
      switch (part.type) {
      case SelectorType::Id:
        s.a++;
        break;
      case SelectorType::Class:
      case SelectorType::Attribute:
        s.b++;
        break;
      case SelectorType::PseudoClass:
        // Functional pseudo-classes carry their precomputed contribution:
        // :where(...) weighs zero; :is(...)/:has(...) weigh their most
        // specific argument. Everything else stays (0,1,0).
        if (part.functionSpecificity) {
          s.a += part.functionSpecificity->a;
          s.b += part.functionSpecificity->b;
          s.c += part.functionSpecificity->c;
        } else {
          s.b++;
        }
        break;
      case SelectorType::Tag:
      case SelectorType::PseudoElement:
        s.c++;
        break;
      default:
        break;
      }
    }
    return s;
  }
};

struct Declaration {
  std::string property;
  std::string value; // Raw string for now, should parse into values later
  bool important = false;
};

struct Rule {
  std::vector<Selector> selectors;
  std::vector<Declaration> declarations;
};

struct StyleSheet {
  std::vector<Rule> rules;
};

} // namespace css
} // namespace xiaopeng
