#pragma once

#include "css_types.hpp"
#include <cctype>
#include <charconv>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace xiaopeng {
namespace css {

class CssTokenizer {
public:
  explicit CssTokenizer(std::string input)
      : input_(std::move(input)), position_(0), length_(input_.length()) {}

  std::vector<Token> tokenize() {
    std::vector<Token> tokens;
    tokens.reserve(std::min(length_ / 4 + 16, size_t(1024)));

    while (position_ < length_) {
      Token token = consumeToken();
      tokens.push_back(std::move(token));
    }
    tokens.emplace_back(TokenType::EndOfFile);
    return tokens;
  }

  size_t getPosition() const { return position_; }
  size_t getLength() const { return length_; }

private:
  std::string input_;
  size_t position_;
  size_t length_;

  static constexpr size_t MAX_STRING_LENGTH = 65536;
  static constexpr size_t MAX_NAME_LENGTH = 1024;

  bool hasMore() const { return position_ < length_; }

  char peek(size_t offset = 0) const {
    size_t idx = position_ + offset;
    return idx < length_ ? input_[idx] : '\0';
  }

  char peekNext() const { return peek(1); }

  char consume() { return position_ < length_ ? input_[position_++] : '\0'; }

  void consumeWhitespace() {
    while (position_ < length_ && isWhitespace(input_[position_])) {
      ++position_;
    }
  }

  static constexpr bool isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\0';
  }

  static constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }

  static constexpr bool isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  }

  static constexpr bool isNameStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           static_cast<unsigned char>(c) >= 128;
  }

  static constexpr bool isName(char c) {
    return isNameStart(c) || isDigit(c) || c == '-';
  }

  static constexpr char toLower(char c) {
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
  }

  static constexpr uint32_t hexToUint32(char h1) {
    if (h1 >= '0' && h1 <= '9')
      return static_cast<uint32_t>(h1 - '0');
    if (h1 >= 'a' && h1 <= 'f')
      return static_cast<uint32_t>(h1 - 'a' + 10);
    if (h1 >= 'A' && h1 <= 'F')
      return static_cast<uint32_t>(h1 - 'A' + 10);
    return 0;
  }

  static constexpr uint32_t hexToUint32(char h1, char h2) {
    return (hexToUint32(h1) << 4) | hexToUint32(h2);
  }

  static constexpr uint32_t hexToUint32(char h1, char h2, char h3, char h4) {
    return (hexToUint32(h1) << 12) | (hexToUint32(h2) << 8) |
           (hexToUint32(h3) << 4) | hexToUint32(h4);
  }

  static bool isValidUnicode(uint32_t cp) {
    return cp == 0 || (cp >= 0x20 && cp <= 0x10FFFF);
  }

  uint32_t decodeEscape() {
    if (position_ >= length_)
      return 0;

    char c = peek();
    if (!isHexDigit(c)) {
      if (c == '\n' || c == '\r' || c == '\f')
        return 0;
      consume();
      return static_cast<unsigned char>(c);
    }

    uint32_t codepoint = 0;
    int digits = 0;
    char hex[6] = {0};

    while (digits < 6 && position_ < length_ && isHexDigit(peek())) {
      hex[digits++] = consume();
    }

    if (digits < 6 && position_ < length_ && peek() == ' ') {
      consume();
    }

    if (digits == 0)
      return 0;

    codepoint = 0;
    for (int i = 0; i < digits; ++i) {
      codepoint = (codepoint << 4) | hexToUint32(hex[i]);
    }

    if (!isValidUnicode(codepoint))
      return 0xFFFD;

    return codepoint;
  }

  void appendUtf8(std::string &result, uint32_t codepoint) {
    if (codepoint < 0x80) {
      result += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
      result += static_cast<char>(0xC0 | (codepoint >> 6));
      result += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
      result += static_cast<char>(0xE0 | (codepoint >> 12));
      result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      result += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
      result += static_cast<char>(0xF0 | (codepoint >> 18));
      result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
      result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      result += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
  }

  void consumeComment() {
    if (position_ + 1 >= length_ || input_[position_] != '/' ||
        input_[position_ + 1] != '*') {
      return;
    }
    position_ += 2;

    while (position_ < length_) {
      if (input_[position_] == '*' && position_ + 1 < length_ &&
          input_[position_ + 1] == '/') {
        position_ += 2;
        return;
      }
      if (input_[position_] == '\r' && position_ + 1 < length_ &&
          input_[position_ + 1] == '\n') {
        position_ += 2;
      } else {
        ++position_;
      }
    }
  }

  std::optional<std::string> consumeString(char ending) {
    std::string value;
    value.reserve(32);

    if (position_ >= length_) {
      return std::nullopt;
    }

    consume();

    while (position_ < length_) {
      char c = input_[position_];

      if (c == ending) {
        ++position_;
        return value;
      }

      if (c == '\\') {
        ++position_;
        uint32_t decoded = decodeEscape();
        if (decoded != 0) {
          appendUtf8(value, decoded);
        }
      } else if (c == '\n' || c == '\r' || c == '\f') {
        return std::nullopt;
      } else if (static_cast<unsigned char>(c) < 0x1F) {
        ++position_;
      } else {
        value += c;
        ++position_;
      }

      if (value.length() > MAX_STRING_LENGTH) {
        return std::nullopt;
      }
    }

    return std::nullopt;
  }

  std::string consumeName() {
    std::string result;
    result.reserve(16);

    while (position_ < length_ && result.length() < MAX_NAME_LENGTH) {
      char c = input_[position_];

      if (isName(c)) {
        result += c;
        ++position_;
      } else if (c == '\\') {
        ++position_;
        uint32_t decoded = decodeEscape();
        if (decoded != 0) {
          appendUtf8(result, decoded);
        }
      } else {
        break;
      }
    }

    return result;
  }

  Token consumeNumeric() {
    const size_t start = position_;
    Token token = consumeNumericValue();
    token.repr = input_.substr(start, position_ - start);
    return token;
  }

  Token consumeNumericValue() {
    std::string buffer;
    buffer.reserve(16);

    char c = peek();
    if (c == '+' || c == '-') {
      buffer += consume();
    }

    while (isDigit(peek()) && buffer.length() < 16) {
      buffer += consume();
    }

    if (peek() == '.' && isDigit(peekNext())) {
      buffer += consume();
      while (isDigit(peek()) && buffer.length() < 16) {
        buffer += consume();
      }
    }

    if ((peek() == 'e' || peek() == 'E') && position_ + 1 < length_) {
      char next = peekNext();
      if (isDigit(next) ||
          ((next == '+' || next == '-') && isDigit(peek(2)))) {
        buffer += consume();
        c = peek();
        if (c == '+' || c == '-') {
          buffer += consume();
        }
        while (isDigit(peek()) && buffer.length() < 20) {
          buffer += consume();
        }
      }
    }

    double val = 0.0;
    if (!buffer.empty()) {
      auto [ptr, ec] =
          std::from_chars(buffer.data(), buffer.data() + buffer.size(), val);
      if (ec != std::errc{}) {
        if (buffer.find('.') == std::string::npos &&
            buffer.find('e') == std::string::npos &&
            buffer.find('E') == std::string::npos) {
          for (char ch : buffer) {
            if (ch >= '0' && ch <= '9') {
              val = val * 10 + (ch - '0');
            }
          }
          if (buffer[0] == '-')
            val = -val;
        }
      }
    }

    bool isInteger = buffer.find('.') == std::string::npos &&
                     buffer.find('e') == std::string::npos &&
                     buffer.find('E') == std::string::npos;

    if (peek() == '%') {
      consume();
      Token token{TokenType::Percentage};
      token.numberValue = val;
      token.flag = isInteger;
      return token;
    }

    if (isNameStart(peek())) {
      std::string unit = consumeName();
      Token token{TokenType::Dimension};
      token.numberValue = val;
      token.unit = unit;
      token.flag = isInteger;
      return token;
    }

    Token token{TokenType::Number};
    token.numberValue = val;
    token.flag = isInteger;
    return token;
  }

  // Unquoted url(...) per CSS Syntax §4.3.6: everything up to ')' is the URL
  // (paths like /a.png or data:...;base64 must not be split into tokens)
  Token consumeUrl() {
    std::string value;
    while (position_ < length_) {
      char c = consume();
      if (c == ')')
        return {TokenType::Url, value};
      if (isWhitespace(c)) {
        consumeWhitespace();
        if (position_ >= length_)
          return {TokenType::Url, value};
        if (peek() == ')') {
          consume();
          return {TokenType::Url, value};
        }
        return consumeBadUrlRemnants();
      }
      if (c == '"' || c == '\'' || c == '(')
        return consumeBadUrlRemnants();
      if (c == '\\') {
        uint32_t decoded = decodeEscape();
        if (decoded != 0)
          appendUtf8(value, decoded);
        continue;
      }
      value += c;
    }
    return {TokenType::Url, value};
  }

  Token consumeBadUrlRemnants() {
    while (position_ < length_) {
      char c = consume();
      if (c == ')')
        break;
      if (c == '\\' && position_ < length_)
        consume();
    }
    return {TokenType::BadUrl};
  }

  Token consumeIdentLike() {
    std::string name = consumeName();

    // Always advance: returning without consuming would make tokenize() spin
    if (name.empty())
      return {TokenType::Delim, std::string(1, consume())};

    if (peek() == '(') {
      consume();

      if (name == "url") {
        consumeWhitespace();
        if (peek() == '"' || peek() == '\'')
          return {TokenType::Function, name};
        return consumeUrl();
      }

      return {TokenType::Function, name};
    }

    if (peek() == ':' && (name == "progid" || name == "expression" ||
                          name == "-moz-binding")) {
      // consume the colon and optional function parens (IE legacy compat)
      consume(); // ':'
      if (peek() == '(') {
        consume(); // '('
        return {TokenType::Function, name};
      }
    }

    return {TokenType::Ident, name};
  }

  Token consumeToken() {
    if (position_ >= length_)
      return {TokenType::EndOfFile};

    char c = peek();

    // Generate a single Whitespace token for consecutive whitespace chars
    if (isWhitespace(c)) {
      consumeWhitespace();
      return {TokenType::Whitespace};
    }

    if (c == '/' && peekNext() == '*') {
      consumeComment();
      return consumeToken();
    }

    if (c == '"' || c == '\'') {
      auto result = consumeString(c);
      if (result) {
        return {TokenType::String, *result};
      }
      return {TokenType::BadString};
    }

    if (c == '#') {
      consume();
      if (position_ >= length_)
        return {TokenType::Delim, "#"};

      if (isName(peek()) || peek() == '\\') {
        bool isId = isNameStart(peek());
        std::string name = consumeName();
        Token token{TokenType::Hash};
        token.value = name;
        token.flag = isId;
        return token;
      }
      return {TokenType::Delim, "#"};
    }

    switch (c) {
    case '(':
      consume();
      return {TokenType::OpenParen, "("};
    case ')':
      consume();
      return {TokenType::CloseParen, ")"};
    case '[':
      consume();
      return {TokenType::OpenSquare, "["};
    case ']':
      consume();
      return {TokenType::CloseSquare, "]"};
    case '{':
      consume();
      return {TokenType::OpenCurly, "{"};
    case '}':
      consume();
      return {TokenType::CloseCurly, "}"};
    case ',':
      consume();
      return {TokenType::Comma, ","};
    case ':':
      consume();
      return {TokenType::Colon, ":"};
    case ';':
      consume();
      return {TokenType::Semicolon, ";"};
    case '<':
      consume();
      if (peek() == '!' && peekNext() == '-' && peek(2) == '-') {
        // "<!--": all three characters after '<' belong to the CDO token
        consume();
        consume();
        consume();
        return {TokenType::CDO};
      }
      return {TokenType::Delim, "<"};
    case '-':
      if (peekNext() == '-' && peek(2) == '>') {
        consume();
        consume();
        consume();
        return {TokenType::CDC};
      }
      if (isDigit(peekNext()) ||
          (peekNext() == '.' && isDigit(peek(2)))) {
        return consumeNumeric();
      }
      if (isNameStart(peekNext()) || peekNext() == '-') {
        return consumeIdentLike();
      }
      consume();
      return {TokenType::Delim, "-"};
    case '+':
      if (isDigit(peekNext()) ||
          (peekNext() == '.' && isDigit(peek(2)))) {
        return consumeNumeric();
      }
      consume();
      return {TokenType::Delim, "+"};
    case '.':
      if (isDigit(peekNext())) {
        return consumeNumeric();
      }
      consume();
      return {TokenType::Delim, "."};
    case '@':
      consume();
      if (position_ < length_ && isNameStart(peek())) {
        return {TokenType::AtKeyword, consumeName()};
      }
      return {TokenType::Delim, "@"};
    case '&':
    case '*':
    case '|':
    case '~':
    case '^':
    case '$':
    case '>':
      consume();
      return {TokenType::Delim, std::string(1, c)};
    default:
      break;
    }

    if (isDigit(c) || (c == '.' && isDigit(peekNext()))) {
      return consumeNumeric();
    }

    if (isNameStart(c)) {
      return consumeIdentLike();
    }

    consume();
    return {TokenType::Delim, std::string(1, c)};
  }
};

} // namespace css
} // namespace xiaopeng
