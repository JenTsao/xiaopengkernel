#pragma once

#include "html_types.hpp"
#include "../loader/error.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <unordered_map>
#include <optional>

namespace xiaopeng {
namespace dom {

enum class TokenizerState : uint8_t {
    Data,
    TagOpen,
    EndTagOpen,
    TagName,
    BeforeAttributeName,
    AttributeName,
    AfterAttributeName,
    BeforeAttributeValue,
    AttributeValueDoubleQuoted,
    AttributeValueSingleQuoted,
    AttributeValueUnquoted,
    AfterAttributeValueQuoted,
    SelfClosingStartTag,
    BogusComment,
    MarkupDeclarationOpen,
    CommentStart,
    CommentStartDash,
    Comment,
    CommentEndDash,
    CommentEnd,
    CommentEndBang,
    Doctype,
    BeforeDoctypeName,
    DoctypeName,
    AfterDoctypeName,
    AfterDoctypePublicKeyword,
    BeforeDoctypePublicIdentifier,
    DoctypePublicIdentifierDoubleQuoted,
    DoctypePublicIdentifierSingleQuoted,
    AfterDoctypePublicIdentifier,
    BetweenDoctypePublicAndSystemIdentifiers,
    AfterDoctypeSystemKeyword,
    BeforeDoctypeSystemIdentifier,
    DoctypeSystemIdentifierDoubleQuoted,
    DoctypeSystemIdentifierSingleQuoted,
    AfterDoctypeSystemIdentifier,
    BogusDoctype,
    CdataSection,
    CdataSectionBracket,
    CdataSectionEnd,
    Rcdata,
    RcdataLessThanSign,
    RcdataEndTagOpen,
    RcdataEndTagName,
    Rawtext,
    RawtextLessThanSign,
    RawtextEndTagOpen,
    RawtextEndTagName,
    Plaintext
};

struct ParseError {
    std::string message;
    size_t position = 0;
    size_t line = 1;
    size_t column = 1;
    
    ParseError(std::string msg, size_t pos, size_t l, size_t c)
        : message(std::move(msg)), position(pos), line(l), column(c) {}
};

class HtmlTokenizer {
public:
    using ErrorCallback = std::function<void(const ParseError&)>;
    
    explicit HtmlTokenizer(std::string_view input);
    explicit HtmlTokenizer(const loader::ByteBuffer& input);
    explicit HtmlTokenizer(const std::string& input);
    
    Token nextToken();
    std::vector<Token> tokenize();
    
    bool hasError() const { return !errors_.empty(); }
    const std::vector<ParseError>& errors() const { return errors_; }
    void clearErrors() { errors_.clear(); }
    
    void setErrorCallback(ErrorCallback callback) { errorCallback_ = std::move(callback); }
    
    size_t position() const { return position_; }
    size_t line() const { return line_; }
    size_t column() const { return column_; }
    bool isEof() const { return position_ >= input_.size(); }
    
    TokenizerState state() const { return state_; }
    void setState(TokenizerState state) { state_ = state; }
    
    void setLastStartTag(const std::string& tagName) { lastStartTag_ = tagName; }
    const std::string& lastStartTag() const { return lastStartTag_; }
    
    void setAllowCdata(bool allow) { allowCdata_ = allow; }
    bool allowCdata() const { return allowCdata_; }

private:
    char consumeNextInputCharacter();
    char consumeCharWithReconsume();
    char currentInputCharacter() const;
    char peekNextCharacter(size_t offset = 1) const;
    
    void emitToken(Token token);
    void emitCharacterToken(char c);
    void emitCharacterToken(const std::string& s);
    void emitEofToken();
    void emitError(const std::string& message);
    
    void createStartTagToken();
    void createEndTagToken();
    void createCommentToken();
    void createDoctypeToken();
    
    void appendToTagName(char c);
    void appendToCommentData(char c);
    void appendToAttributeName(char c);
    void appendToAttributeValue(char c);
    void appendToPublicIdentifier(char c);
    void appendToSystemIdentifier(char c);
    
    void startNewAttribute();
    void finishAttribute();
    
    bool isAppropriateEndTag() const;
    
    std::string consumeCharacterReference();
    
    void handleDataState();
    void handleTagOpenState();
    void handleEndTagOpenState();
    void handleTagNameState();
    void handleBeforeAttributeNameState();
    void handleAttributeNameState();
    void handleAfterAttributeNameState();
    void handleBeforeAttributeValueState();
    void handleAttributeValueDoubleQuotedState();
    void handleAttributeValueSingleQuotedState();
    void handleAttributeValueUnquotedState();
    void handleAfterAttributeValueQuotedState();
    void handleSelfClosingStartTagState();
    void handleMarkupDeclarationOpenState();
    void handleCommentStartState();
    void handleCommentStartDashState();
    void handleCommentState();
    void handleCommentEndDashState();
    void handleCommentEndState();
    void handleCommentEndBangState();
    void handleBogusCommentState();
    void handleDoctypeState();
    void handleBeforeDoctypeNameState();
    void handleDoctypeNameState();
    void handleAfterDoctypeNameState();
    void handleAfterDoctypePublicKeywordState();
    void handleBeforeDoctypePublicIdentifierState();
    void handleDoctypePublicIdentifierDoubleQuotedState();
    void handleDoctypePublicIdentifierSingleQuotedState();
    void handleAfterDoctypePublicIdentifierState();
    void handleBetweenDoctypePublicAndSystemIdentifiersState();
    void handleAfterDoctypeSystemKeywordState();
    void handleBeforeDoctypeSystemIdentifierState();
    void handleDoctypeSystemIdentifierDoubleQuotedState();
    void handleDoctypeSystemIdentifierSingleQuotedState();
    void handleAfterDoctypeSystemIdentifierState();
    void handleBogusDoctypeState();
    
    // Keep owned input alive for constructors that receive temporary std::string
    // or ByteBuffer values. input_ points either at ownedInput_ or at an
    // externally-managed string_view.
    std::string ownedInput_;
    std::string_view input_;
    size_t position_ = 0;
    size_t line_ = 1;
    size_t column_ = 1;
    
    TokenizerState state_ = TokenizerState::Data;
    TokenizerState returnState_ = TokenizerState::Data;
    
    Token currentToken_;
    Token emittedToken_;
    bool hasEmittedToken_ = false;
    
    std::string currentAttributeName_;
    std::string currentAttributeValue_;
    std::string tempBuffer_;
    std::string lastStartTag_;
    std::string characterTokenBuffer_;
    
    std::vector<ParseError> errors_;
    ErrorCallback errorCallback_;
    
    bool reconsume_ = false;
    bool allowCdata_ = false;
    
    static std::unordered_map<std::string, std::string>& getNamedEntities();
};

inline HtmlTokenizer::HtmlTokenizer(std::string_view input)
    : input_(input) {
    currentToken_ = Token::makeEndOfFile();
}

inline HtmlTokenizer::HtmlTokenizer(const loader::ByteBuffer& input)
    : ownedInput_(reinterpret_cast<const char*>(input.data()), input.size()),
      input_(ownedInput_) {
    currentToken_ = Token::makeEndOfFile();
}

inline HtmlTokenizer::HtmlTokenizer(const std::string& input)
    : ownedInput_(input), input_(ownedInput_) {
    currentToken_ = Token::makeEndOfFile();
}

inline char HtmlTokenizer::currentInputCharacter() const {
    if (position_ >= input_.size()) {
        return '\0';
    }
    return input_[position_];
}

inline char HtmlTokenizer::peekNextCharacter(size_t offset) const {
    size_t pos = position_ + offset;
    if (pos >= input_.size()) {
        return '\0';
    }
    return input_[pos];
}

inline char HtmlTokenizer::consumeNextInputCharacter() {
    if (position_ >= input_.size()) {
        return '\0';
    }
    
    char c = input_[position_];
    
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    position_++;
    
    return c;
}

inline char HtmlTokenizer::consumeCharWithReconsume() {
    if (reconsume_) {
        reconsume_ = false;
        if (position_ > 0) {
            position_--;
        }
        return consumeNextInputCharacter();
    }
    return consumeNextInputCharacter();
}

inline void HtmlTokenizer::emitToken(Token token) {
    emittedToken_ = token;
    hasEmittedToken_ = true;
}

inline void HtmlTokenizer::emitCharacterToken(char c) {
    characterTokenBuffer_ += c;
}

inline void HtmlTokenizer::emitCharacterToken(const std::string& s) {
    characterTokenBuffer_ += s;
}

inline void HtmlTokenizer::emitEofToken() {
    if (!characterTokenBuffer_.empty()) {
        emitToken(Token::makeCharacter(characterTokenBuffer_));
        characterTokenBuffer_.clear();
        return;
    }
    emitToken(Token::makeEndOfFile());
}

inline void HtmlTokenizer::emitError(const std::string& message) {
    ParseError error(message, position_, line_, column_);
    errors_.push_back(error);
    if (errorCallback_) {
        errorCallback_(error);
    }
}

inline void HtmlTokenizer::createStartTagToken() {
    currentToken_ = Token();
    currentToken_.type = TokenType::StartTag;
    currentToken_.name.clear();
    currentToken_.attributes.clear();
    currentToken_.selfClosing = false;
}

inline void HtmlTokenizer::createEndTagToken() {
    currentToken_ = Token();
    currentToken_.type = TokenType::EndTag;
    currentToken_.name.clear();
    currentToken_.attributes.clear();
    currentToken_.selfClosing = false;
}

inline void HtmlTokenizer::createCommentToken() {
    currentToken_ = Token();
    currentToken_.type = TokenType::Comment;
    currentToken_.data.clear();
}

inline void HtmlTokenizer::createDoctypeToken() {
    currentToken_ = Token();
    currentToken_.type = TokenType::Doctype;
    currentToken_.name.clear();
    currentToken_.publicIdentifier.clear();
    currentToken_.systemIdentifier.clear();
    currentToken_.forceQuirks = false;
}

inline void HtmlTokenizer::appendToTagName(char c) {
    currentToken_.name += c;
}

inline void HtmlTokenizer::appendToCommentData(char c) {
    currentToken_.data += c;
}

inline void HtmlTokenizer::appendToAttributeName(char c) {
    currentAttributeName_ += c;
}

inline void HtmlTokenizer::appendToAttributeValue(char c) {
    currentAttributeValue_ += c;
}

inline void HtmlTokenizer::appendToPublicIdentifier(char c) {
    currentToken_.publicIdentifier += c;
}

inline void HtmlTokenizer::appendToSystemIdentifier(char c) {
    currentToken_.systemIdentifier += c;
}

inline void HtmlTokenizer::startNewAttribute() {
    currentAttributeName_.clear();
    currentAttributeValue_.clear();
}

inline void HtmlTokenizer::finishAttribute() {
    if (!currentAttributeName_.empty()) {
        bool found = false;
        for (const auto& attr : currentToken_.attributes) {
            if (attr.name == currentAttributeName_) {
                found = true;
                break;
            }
        }
        if (!found) {
            currentToken_.attributes.emplace_back(currentAttributeName_, currentAttributeValue_);
        }
    }
    currentAttributeName_.clear();
    currentAttributeValue_.clear();
}

inline bool HtmlTokenizer::isAppropriateEndTag() const {
    return currentToken_.isEndTag() && 
           toLower(currentToken_.name) == toLower(lastStartTag_);
}

inline Token HtmlTokenizer::nextToken() {
    while (!hasEmittedToken_) {
        switch (state_) {
            case TokenizerState::Data:
                handleDataState();
                break;
            case TokenizerState::TagOpen:
                handleTagOpenState();
                break;
            case TokenizerState::EndTagOpen:
                handleEndTagOpenState();
                break;
            case TokenizerState::TagName:
                handleTagNameState();
                break;
            case TokenizerState::BeforeAttributeName:
                handleBeforeAttributeNameState();
                break;
            case TokenizerState::AttributeName:
                handleAttributeNameState();
                break;
            case TokenizerState::AfterAttributeName:
                handleAfterAttributeNameState();
                break;
            case TokenizerState::BeforeAttributeValue:
                handleBeforeAttributeValueState();
                break;
            case TokenizerState::AttributeValueDoubleQuoted:
                handleAttributeValueDoubleQuotedState();
                break;
            case TokenizerState::AttributeValueSingleQuoted:
                handleAttributeValueSingleQuotedState();
                break;
            case TokenizerState::AttributeValueUnquoted:
                handleAttributeValueUnquotedState();
                break;
            case TokenizerState::AfterAttributeValueQuoted:
                handleAfterAttributeValueQuotedState();
                break;
            case TokenizerState::SelfClosingStartTag:
                handleSelfClosingStartTagState();
                break;
            case TokenizerState::MarkupDeclarationOpen:
                handleMarkupDeclarationOpenState();
                break;
            case TokenizerState::CommentStart:
                handleCommentStartState();
                break;
            case TokenizerState::CommentStartDash:
                handleCommentStartDashState();
                break;
            case TokenizerState::Comment:
                handleCommentState();
                break;
            case TokenizerState::CommentEndDash:
                handleCommentEndDashState();
                break;
            case TokenizerState::CommentEnd:
                handleCommentEndState();
                break;
            case TokenizerState::CommentEndBang:
                handleCommentEndBangState();
                break;
            case TokenizerState::BogusComment:
                handleBogusCommentState();
                break;
            case TokenizerState::Doctype:
                handleDoctypeState();
                break;
            case TokenizerState::BeforeDoctypeName:
                handleBeforeDoctypeNameState();
                break;
            case TokenizerState::DoctypeName:
                handleDoctypeNameState();
                break;
            case TokenizerState::AfterDoctypeName:
                handleAfterDoctypeNameState();
                break;
            case TokenizerState::AfterDoctypePublicKeyword:
                handleAfterDoctypePublicKeywordState();
                break;
            case TokenizerState::BeforeDoctypePublicIdentifier:
                handleBeforeDoctypePublicIdentifierState();
                break;
            case TokenizerState::DoctypePublicIdentifierDoubleQuoted:
                handleDoctypePublicIdentifierDoubleQuotedState();
                break;
            case TokenizerState::DoctypePublicIdentifierSingleQuoted:
                handleDoctypePublicIdentifierSingleQuotedState();
                break;
            case TokenizerState::AfterDoctypePublicIdentifier:
                handleAfterDoctypePublicIdentifierState();
                break;
            case TokenizerState::BetweenDoctypePublicAndSystemIdentifiers:
                handleBetweenDoctypePublicAndSystemIdentifiersState();
                break;
            case TokenizerState::AfterDoctypeSystemKeyword:
                handleAfterDoctypeSystemKeywordState();
                break;
            case TokenizerState::BeforeDoctypeSystemIdentifier:
                handleBeforeDoctypeSystemIdentifierState();
                break;
            case TokenizerState::DoctypeSystemIdentifierDoubleQuoted:
                handleDoctypeSystemIdentifierDoubleQuotedState();
                break;
            case TokenizerState::DoctypeSystemIdentifierSingleQuoted:
                handleDoctypeSystemIdentifierSingleQuotedState();
                break;
            case TokenizerState::AfterDoctypeSystemIdentifier:
                handleAfterDoctypeSystemIdentifierState();
                break;
            case TokenizerState::BogusDoctype:
                handleBogusDoctypeState();
                break;
            default:
                emitEofToken();
                break;
        }
    }
    
    hasEmittedToken_ = false;
    return emittedToken_;
}

inline std::vector<Token> HtmlTokenizer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token token = nextToken();
        tokens.push_back(token);
        if (token.isEndOfFile()) {
            break;
        }
    }
    return tokens;
}

inline void HtmlTokenizer::handleDataState() {
    char c = consumeCharWithReconsume();
    
    if (c == '\0' && position_ >= input_.size()) {
        emitEofToken();
        return;
    }
    
    if (c == '&') {
        returnState_ = TokenizerState::Data;
        std::string ref = consumeCharacterReference();
        if (!ref.empty()) {
            // Flush buffered text before the entity, then emit the entity
            // itself (dropping ref here would silently lose "&…;" after text)
            if (!characterTokenBuffer_.empty()) {
                emitToken(Token::makeCharacter(characterTokenBuffer_));
                characterTokenBuffer_.clear();
            }
            emitToken(Token::makeCharacter(ref));
            return;
        }
        emitCharacterToken('&');
        return;
    }
    
    if (c == '<') {
        if (!characterTokenBuffer_.empty()) {
            emitToken(Token::makeCharacter(characterTokenBuffer_));
            characterTokenBuffer_.clear();
            reconsume_ = true;
            return;
        }
        state_ = TokenizerState::TagOpen;
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        emitCharacterToken('\xFF');
        return;
    }
    
    emitCharacterToken(c);
}

inline void HtmlTokenizer::handleTagOpenState() {
    char c = consumeCharWithReconsume();
    
    if (c == '!') {
        state_ = TokenizerState::MarkupDeclarationOpen;
        return;
    }
    
    if (c == '/') {
        state_ = TokenizerState::EndTagOpen;
        return;
    }
    
    if (isAlpha(c)) {
        createStartTagToken();
        appendToTagName(toLower(c));
        state_ = TokenizerState::TagName;
        return;
    }
    
    if (c == '?') {
        emitError("Unexpected question mark instead of tag name");
        createCommentToken();
        state_ = TokenizerState::BogusComment;
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF before tag name");
        emitCharacterToken('<');
        emitEofToken();
        return;
    }
    
    emitError("Invalid first character of tag name");
    emitCharacterToken('<');
    reconsume_ = true;
    state_ = TokenizerState::Data;
}

inline void HtmlTokenizer::handleEndTagOpenState() {
    char c = consumeCharWithReconsume();
    
    if (isAlpha(c)) {
        createEndTagToken();
        appendToTagName(toLower(c));
        state_ = TokenizerState::TagName;
        return;
    }
    
    if (c == '>') {
        emitError("Missing end tag name");
        state_ = TokenizerState::Data;
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF before tag name");
        emitCharacterToken('<');
        emitCharacterToken('/');
        emitEofToken();
        return;
    }
    
    emitError("Invalid first character of tag name");
    createCommentToken();
    state_ = TokenizerState::BogusComment;
    reconsume_ = true;
}

inline void HtmlTokenizer::handleTagNameState() {
    char c = consumeCharWithReconsume();
    
    if (isWhitespace(c)) {
        state_ = TokenizerState::BeforeAttributeName;
        return;
    }
    
    if (c == '/') {
        state_ = TokenizerState::SelfClosingStartTag;
        return;
    }
    
    if (c == '>') {
        state_ = TokenizerState::Data;
        if (currentToken_.isStartTag()) {
            lastStartTag_ = currentToken_.name;
        }
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToTagName('\xFF');
        return;
    }
    
    if (isAlpha(c)) {
        appendToTagName(toLower(c));
    } else {
        appendToTagName(c);
    }
}

inline void HtmlTokenizer::handleBeforeAttributeNameState() {
    char c = consumeCharWithReconsume();
    
    if (isWhitespace(c)) {
        return;
    }
    
    if (c == '/' || c == '>') {
        reconsume_ = true;
        state_ = TokenizerState::AfterAttributeName;
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    if (c == '=' || c == '\'' || c == '"') {
        emitError("Unexpected character in attribute name");
    }
    
    startNewAttribute();
    if (isAlpha(c)) {
        appendToAttributeName(toLower(c));
    } else if (c == '\0') {
        emitError("Unexpected null character");
        appendToAttributeName('\xFF');
    } else {
        appendToAttributeName(c);
    }
    state_ = TokenizerState::AttributeName;
}

inline void HtmlTokenizer::handleAttributeNameState() {
    char c = consumeCharWithReconsume();
    
    if (isWhitespace(c) || c == '/' || c == '>') {
        reconsume_ = true;
        state_ = TokenizerState::AfterAttributeName;
        return;
    }
    
    if (c == '=') {
        state_ = TokenizerState::BeforeAttributeValue;
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToAttributeName('\xFF');
        return;
    }
    
    if (c == '\'' || c == '"') {
        emitError("Unexpected character in attribute name");
    }
    
    if (isAlpha(c)) {
        appendToAttributeName(toLower(c));
    } else {
        appendToAttributeName(c);
    }
}

inline void HtmlTokenizer::handleAfterAttributeNameState() {
    char c = consumeCharWithReconsume();
    
    if (isWhitespace(c)) {
        return;
    }
    
    if (c == '/') {
        finishAttribute();
        state_ = TokenizerState::SelfClosingStartTag;
        return;
    }
    
    if (c == '=') {
        state_ = TokenizerState::BeforeAttributeValue;
        return;
    }
    
    if (c == '>') {
        finishAttribute();
        state_ = TokenizerState::Data;
        if (currentToken_.isStartTag()) {
            lastStartTag_ = currentToken_.name;
        }
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    finishAttribute();
    startNewAttribute();
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToAttributeName('\xFF');
    } else if (isAlpha(c)) {
        appendToAttributeName(toLower(c));
    } else {
        appendToAttributeName(c);
    }
    state_ = TokenizerState::AttributeName;
}

inline void HtmlTokenizer::handleBeforeAttributeValueState() {
    char c = consumeCharWithReconsume();
    
    if (isWhitespace(c)) {
        return;
    }
    
    if (c == '"') {
        state_ = TokenizerState::AttributeValueDoubleQuoted;
        return;
    }
    
    if (c == '\'') {
        state_ = TokenizerState::AttributeValueSingleQuoted;
        return;
    }
    
    if (c == '>') {
        emitError("Missing attribute value");
        finishAttribute();
        state_ = TokenizerState::Data;
        if (currentToken_.isStartTag()) {
            lastStartTag_ = currentToken_.name;
        }
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    emitError("Unquoted attribute value");
    reconsume_ = true;
    state_ = TokenizerState::AttributeValueUnquoted;
}

inline void HtmlTokenizer::handleAttributeValueDoubleQuotedState() {
    char c = consumeNextInputCharacter();
    
    if (c == '"') {
        finishAttribute();
        state_ = TokenizerState::AfterAttributeValueQuoted;
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToAttributeValue('\xFF');
        return;
    }
    
    if (c == '&') {
        std::string ref = consumeCharacterReference();
        if (!ref.empty()) {
            currentAttributeValue_ += ref;
            return;
        }
    }
    
    appendToAttributeValue(c);
}

inline void HtmlTokenizer::handleAttributeValueSingleQuotedState() {
    char c = consumeNextInputCharacter();
    
    if (c == '\'') {
        finishAttribute();
        state_ = TokenizerState::AfterAttributeValueQuoted;
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToAttributeValue('\xFF');
        return;
    }
    
    if (c == '&') {
        std::string ref = consumeCharacterReference();
        if (!ref.empty()) {
            currentAttributeValue_ += ref;
            return;
        }
    }
    
    appendToAttributeValue(c);
}

inline void HtmlTokenizer::handleAttributeValueUnquotedState() {
    char c = consumeCharWithReconsume();
    
    if (isWhitespace(c)) {
        finishAttribute();
        state_ = TokenizerState::BeforeAttributeName;
        return;
    }
    
    if (c == '>') {
        finishAttribute();
        state_ = TokenizerState::Data;
        if (currentToken_.isStartTag()) {
            lastStartTag_ = currentToken_.name;
        }
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToAttributeValue('\xFF');
        return;
    }
    
    if (c == '"' || c == '\'' || c == '<' || c == '=' || c == '`') {
        emitError("Unexpected character in unquoted attribute value");
    }
    
    if (c == '&') {
        std::string ref = consumeCharacterReference();
        if (!ref.empty()) {
            currentAttributeValue_ += ref;
            return;
        }
    }
    
    appendToAttributeValue(c);
}

inline void HtmlTokenizer::handleAfterAttributeValueQuotedState() {
    char c = consumeCharWithReconsume();
    
    if (isWhitespace(c)) {
        state_ = TokenizerState::BeforeAttributeName;
        return;
    }
    
    if (c == '/') {
        state_ = TokenizerState::SelfClosingStartTag;
        return;
    }
    
    if (c == '>') {
        state_ = TokenizerState::Data;
        if (currentToken_.isStartTag()) {
            lastStartTag_ = currentToken_.name;
        }
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    emitError("Missing whitespace between attributes");
    reconsume_ = true;
    state_ = TokenizerState::BeforeAttributeName;
}

inline void HtmlTokenizer::handleSelfClosingStartTagState() {
    char c = consumeNextInputCharacter();
    
    if (c == '>') {
        currentToken_.selfClosing = true;
        state_ = TokenizerState::Data;
        if (currentToken_.isStartTag()) {
            lastStartTag_ = currentToken_.name;
        }
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    emitError("Missing '>' after '/'");
    reconsume_ = true;
    state_ = TokenizerState::BeforeAttributeName;
}

inline void HtmlTokenizer::handleMarkupDeclarationOpenState() {
    if (position_ + 1 < input_.size() &&
        input_[position_] == '-' && input_[position_ + 1] == '-') {
        consumeNextInputCharacter();
        consumeNextInputCharacter();
        createCommentToken();
        state_ = TokenizerState::CommentStart;
        return;
    }
    
    if (position_ + 6 < input_.size() &&
        toLower(input_[position_]) == 'd' &&
        toLower(input_[position_ + 1]) == 'o' &&
        toLower(input_[position_ + 2]) == 'c' &&
        toLower(input_[position_ + 3]) == 't' &&
        toLower(input_[position_ + 4]) == 'y' &&
        toLower(input_[position_ + 5]) == 'p' &&
        toLower(input_[position_ + 6]) == 'e') {
        for (int i = 0; i < 7; i++) {
            consumeNextInputCharacter();
        }
        state_ = TokenizerState::Doctype;
        return;
    }
    
    if (allowCdata_ && position_ + 6 < input_.size() &&
        input_[position_] == '[' &&
        input_[position_ + 1] == 'C' &&
        input_[position_ + 2] == 'D' &&
        input_[position_ + 3] == 'A' &&
        input_[position_ + 4] == 'T' &&
        input_[position_ + 5] == 'A' &&
        input_[position_ + 6] == '[') {
        for (int i = 0; i < 7; i++) {
            consumeNextInputCharacter();
        }
        state_ = TokenizerState::CdataSection;
        return;
    }
    
    emitError("Incorrectly opened comment");
    createCommentToken();
    state_ = TokenizerState::BogusComment;
}

inline void HtmlTokenizer::handleCommentStartState() {
    char c = consumeNextInputCharacter();
    
    if (c == '-') {
        state_ = TokenizerState::CommentStartDash;
        return;
    }
    
    if (c == '>') {
        emitError("Abrupt closing of empty comment");
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in comment");
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    appendToCommentData(c);
    state_ = TokenizerState::Comment;
}

inline void HtmlTokenizer::handleCommentStartDashState() {
    char c = consumeNextInputCharacter();
    
    if (c == '-') {
        state_ = TokenizerState::CommentEnd;
        return;
    }
    
    if (c == '>') {
        emitError("Abrupt closing of empty comment");
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in comment");
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    appendToCommentData('-');
    appendToCommentData(c);
    state_ = TokenizerState::Comment;
}

inline void HtmlTokenizer::handleCommentState() {
    char c = consumeNextInputCharacter();
    
    if (c == '-') {
        state_ = TokenizerState::CommentEndDash;
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in comment");
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToCommentData('\xFF');
        return;
    }
    
    appendToCommentData(c);
}

inline void HtmlTokenizer::handleCommentEndDashState() {
    char c = consumeNextInputCharacter();
    
    if (c == '-') {
        state_ = TokenizerState::CommentEnd;
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in comment");
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    appendToCommentData('-');
    appendToCommentData(c);
    state_ = TokenizerState::Comment;
}

inline void HtmlTokenizer::handleCommentEndState() {
    char c = consumeNextInputCharacter();
    
    if (c == '>') {
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '!') {
        state_ = TokenizerState::CommentEndBang;
        return;
    }
    
    if (c == '-') {
        appendToCommentData('-');
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in comment");
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    appendToCommentData('-');
    appendToCommentData('-');
    appendToCommentData(c);
    state_ = TokenizerState::Comment;
}

inline void HtmlTokenizer::handleCommentEndBangState() {
    char c = consumeNextInputCharacter();
    
    if (c == '-') {
        appendToCommentData('-');
        appendToCommentData('-');
        appendToCommentData('!');
        state_ = TokenizerState::CommentEndDash;
        return;
    }
    
    if (c == '>') {
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in comment");
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    appendToCommentData('-');
    appendToCommentData('-');
    appendToCommentData('!');
    appendToCommentData(c);
    state_ = TokenizerState::Comment;
}

inline void HtmlTokenizer::handleBogusCommentState() {
    char c = consumeNextInputCharacter();
    
    if (c == '>') {
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0') {
        appendToCommentData('\xFF');
        return;
    }
    
    appendToCommentData(c);
}

inline void HtmlTokenizer::handleDoctypeState() {
    char c = consumeCharWithReconsume();
    
    if (isWhitespace(c)) {
        state_ = TokenizerState::BeforeDoctypeName;
        return;
    }
    
    if (c == '>') {
        reconsume_ = true;
        state_ = TokenizerState::BeforeDoctypeName;
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        createDoctypeToken();
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    emitError("Missing whitespace before doctype name");
    reconsume_ = true;
    state_ = TokenizerState::BeforeDoctypeName;
}

inline void HtmlTokenizer::handleBeforeDoctypeNameState() {
    char c = consumeCharWithReconsume();
    
    if (isWhitespace(c)) {
        return;
    }
    
    if (c == '>') {
        emitError("Missing doctype name");
        createDoctypeToken();
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        createDoctypeToken();
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    createDoctypeToken();
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToTagName('\xFF');
    } else if (isAlpha(c)) {
        appendToTagName(toLower(c));
    } else {
        appendToTagName(c);
    }
    state_ = TokenizerState::DoctypeName;
}

inline void HtmlTokenizer::handleDoctypeNameState() {
    char c = consumeNextInputCharacter();
    
    if (isWhitespace(c)) {
        state_ = TokenizerState::AfterDoctypeName;
        return;
    }
    
    if (c == '>') {
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToTagName('\xFF');
        return;
    }
    
    if (isAlpha(c)) {
        appendToTagName(toLower(c));
    } else {
        appendToTagName(c);
    }
}

inline void HtmlTokenizer::handleAfterDoctypeNameState() {
    char c = consumeNextInputCharacter();
    
    if (isWhitespace(c)) {
        return;
    }
    
    if (c == '>') {
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (toLower(c) == 'p' && position_ + 5 < input_.size() &&
        toLower(input_[position_]) == 'u' &&
        toLower(input_[position_ + 1]) == 'b' &&
        toLower(input_[position_ + 2]) == 'l' &&
        toLower(input_[position_ + 3]) == 'i' &&
        toLower(input_[position_ + 4]) == 'c') {
        for (int i = 0; i < 5; i++) {
            consumeNextInputCharacter();
        }
        state_ = TokenizerState::AfterDoctypePublicKeyword;
        return;
    }
    
    if (toLower(c) == 's' && position_ + 5 < input_.size() &&
        toLower(input_[position_]) == 'y' &&
        toLower(input_[position_ + 1]) == 's' &&
        toLower(input_[position_ + 2]) == 't' &&
        toLower(input_[position_ + 3]) == 'e' &&
        toLower(input_[position_ + 4]) == 'm') {
        for (int i = 0; i < 5; i++) {
            consumeNextInputCharacter();
        }
        state_ = TokenizerState::AfterDoctypeSystemKeyword;
        return;
    }
    
    emitError("Invalid character sequence after doctype name");
    currentToken_.forceQuirks = true;
    state_ = TokenizerState::BogusDoctype;
}

inline void HtmlTokenizer::handleAfterDoctypePublicKeywordState() {
    char c = consumeNextInputCharacter();
    
    if (isWhitespace(c)) {
        state_ = TokenizerState::BeforeDoctypePublicIdentifier;
        return;
    }
    
    if (c == '"') {
        emitError("Missing whitespace after doctype public keyword");
        state_ = TokenizerState::DoctypePublicIdentifierDoubleQuoted;
        return;
    }
    
    if (c == '\'') {
        emitError("Missing whitespace after doctype public keyword");
        state_ = TokenizerState::DoctypePublicIdentifierSingleQuoted;
        return;
    }
    
    if (c == '>') {
        emitError("Missing doctype public identifier");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    emitError("Missing quote before doctype public identifier");
    currentToken_.forceQuirks = true;
    state_ = TokenizerState::BogusDoctype;
}

inline void HtmlTokenizer::handleBeforeDoctypePublicIdentifierState() {
    char c = consumeNextInputCharacter();
    
    if (isWhitespace(c)) {
        return;
    }
    
    if (c == '"') {
        state_ = TokenizerState::DoctypePublicIdentifierDoubleQuoted;
        return;
    }
    
    if (c == '\'') {
        state_ = TokenizerState::DoctypePublicIdentifierSingleQuoted;
        return;
    }
    
    if (c == '>') {
        emitError("Missing doctype public identifier");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    emitError("Missing quote before doctype public identifier");
    currentToken_.forceQuirks = true;
    state_ = TokenizerState::BogusDoctype;
}

inline void HtmlTokenizer::handleDoctypePublicIdentifierDoubleQuotedState() {
    char c = consumeNextInputCharacter();
    
    if (c == '"') {
        state_ = TokenizerState::AfterDoctypePublicIdentifier;
        return;
    }
    
    if (c == '>') {
        emitError("Abrupt doctype public identifier");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToPublicIdentifier('\xFF');
        return;
    }
    
    appendToPublicIdentifier(c);
}

inline void HtmlTokenizer::handleDoctypePublicIdentifierSingleQuotedState() {
    char c = consumeNextInputCharacter();
    
    if (c == '\'') {
        state_ = TokenizerState::AfterDoctypePublicIdentifier;
        return;
    }
    
    if (c == '>') {
        emitError("Abrupt doctype public identifier");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToPublicIdentifier('\xFF');
        return;
    }
    
    appendToPublicIdentifier(c);
}

inline void HtmlTokenizer::handleAfterDoctypePublicIdentifierState() {
    char c = consumeNextInputCharacter();
    
    if (isWhitespace(c)) {
        state_ = TokenizerState::BetweenDoctypePublicAndSystemIdentifiers;
        return;
    }
    
    if (c == '>') {
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '"') {
        emitError("Missing whitespace between doctype public and system identifiers");
        state_ = TokenizerState::DoctypeSystemIdentifierDoubleQuoted;
        return;
    }
    
    if (c == '\'') {
        emitError("Missing whitespace between doctype public and system identifiers");
        state_ = TokenizerState::DoctypeSystemIdentifierSingleQuoted;
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    emitError("Missing quote before doctype system identifier");
    currentToken_.forceQuirks = true;
    state_ = TokenizerState::BogusDoctype;
}

inline void HtmlTokenizer::handleBetweenDoctypePublicAndSystemIdentifiersState() {
    char c = consumeNextInputCharacter();
    
    if (isWhitespace(c)) {
        return;
    }
    
    if (c == '>') {
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '"') {
        state_ = TokenizerState::DoctypeSystemIdentifierDoubleQuoted;
        return;
    }
    
    if (c == '\'') {
        state_ = TokenizerState::DoctypeSystemIdentifierSingleQuoted;
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    emitError("Missing quote before doctype system identifier");
    currentToken_.forceQuirks = true;
    state_ = TokenizerState::BogusDoctype;
}

inline void HtmlTokenizer::handleAfterDoctypeSystemKeywordState() {
    char c = consumeNextInputCharacter();
    
    if (isWhitespace(c)) {
        state_ = TokenizerState::BeforeDoctypeSystemIdentifier;
        return;
    }
    
    if (c == '"') {
        emitError("Missing whitespace after doctype system keyword");
        state_ = TokenizerState::DoctypeSystemIdentifierDoubleQuoted;
        return;
    }
    
    if (c == '\'') {
        emitError("Missing whitespace after doctype system keyword");
        state_ = TokenizerState::DoctypeSystemIdentifierSingleQuoted;
        return;
    }
    
    if (c == '>') {
        emitError("Missing doctype system identifier");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    emitError("Missing quote before doctype system identifier");
    currentToken_.forceQuirks = true;
    state_ = TokenizerState::BogusDoctype;
}

inline void HtmlTokenizer::handleBeforeDoctypeSystemIdentifierState() {
    char c = consumeNextInputCharacter();
    
    if (isWhitespace(c)) {
        return;
    }
    
    if (c == '"') {
        state_ = TokenizerState::DoctypeSystemIdentifierDoubleQuoted;
        return;
    }
    
    if (c == '\'') {
        state_ = TokenizerState::DoctypeSystemIdentifierSingleQuoted;
        return;
    }
    
    if (c == '>') {
        emitError("Missing doctype system identifier");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    emitError("Missing quote before doctype system identifier");
    currentToken_.forceQuirks = true;
    state_ = TokenizerState::BogusDoctype;
}

inline void HtmlTokenizer::handleDoctypeSystemIdentifierDoubleQuotedState() {
    char c = consumeNextInputCharacter();
    
    if (c == '"') {
        state_ = TokenizerState::AfterDoctypeSystemIdentifier;
        return;
    }
    
    if (c == '>') {
        emitError("Abrupt doctype system identifier");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToSystemIdentifier('\xFF');
        return;
    }
    
    appendToSystemIdentifier(c);
}

inline void HtmlTokenizer::handleDoctypeSystemIdentifierSingleQuotedState() {
    char c = consumeNextInputCharacter();
    
    if (c == '\'') {
        state_ = TokenizerState::AfterDoctypeSystemIdentifier;
        return;
    }
    
    if (c == '>') {
        emitError("Abrupt doctype system identifier");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        appendToSystemIdentifier('\xFF');
        return;
    }
    
    appendToSystemIdentifier(c);
}

inline void HtmlTokenizer::handleAfterDoctypeSystemIdentifierState() {
    char c = consumeNextInputCharacter();
    
    if (isWhitespace(c)) {
        return;
    }
    
    if (c == '>') {
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in doctype");
        currentToken_.forceQuirks = true;
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    emitError("Unexpected character after doctype system identifier");
    state_ = TokenizerState::BogusDoctype;
}

inline void HtmlTokenizer::handleBogusDoctypeState() {
    char c = consumeNextInputCharacter();
    
    if (c == '>') {
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        state_ = TokenizerState::Data;
        emitToken(currentToken_);
        return;
    }
}

inline std::string HtmlTokenizer::consumeCharacterReference() {
    if (position_ >= input_.size()) {
        return "";
    }
    
    char c = input_[position_];
    
    if (isWhitespace(c) || c == '<' || c == '&' || c == '\0') {
        return "";
    }
    
    consumeNextInputCharacter();
    
    if (c == '#') {
        int code = 0;
        bool valid = false;
        
        if (position_ < input_.size()) {
            char next = input_[position_];
            if (next == 'x' || next == 'X') {
                consumeNextInputCharacter();
                while (position_ < input_.size()) {
                    char hex = input_[position_];
                    if (isHexDigit(hex)) {
                        code *= 16;
                        if (isDigit(hex)) {
                            code += hex - '0';
                        } else if (hex >= 'a' && hex <= 'f') {
                            code += hex - 'a' + 10;
                        } else {
                            code += hex - 'A' + 10;
                        }
                        valid = true;
                        consumeNextInputCharacter();
                    } else {
                        break;
                    }
                }
            } else {
                while (position_ < input_.size()) {
                    char dec = input_[position_];
                    if (isDigit(dec)) {
                        code *= 10;
                        code += dec - '0';
                        valid = true;
                        consumeNextInputCharacter();
                    } else {
                        break;
                    }
                }
            }
        }
        
        if (!valid) {
            emitError("Missing digits in numeric character reference");
            return "";
        }
        
        if (position_ < input_.size() && input_[position_] == ';') {
            consumeNextInputCharacter();
        } else {
            emitError("Missing semicolon after numeric character reference");
        }
        
        if (code == 0) {
            emitError("Null character reference");
            code = 0xFFFD;
        } else if (code > 0x10FFFF) {
            emitError("Character reference outside unicode range");
            code = 0xFFFD;
        } else if ((code >= 0xD800 && code <= 0xDFFF) || code == 0xFFFE || code == 0xFFFF) {
            emitError("Surrogate character reference");
            code = 0xFFFD;
        } else if (code >= 0x80 && code <= 0x9F) {
            // WHATWG: numeric references to C1 controls map to
            // windows-1252 glyphs (or U+FFFD where undefined)
            emitError("C1 control character reference");
            static const int kC1Remap[32] = {
                0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020,
                0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD,
                0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D,
                0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A,
                0x0153, 0xFFFD, 0x017E, 0x0178};
            code = kC1Remap[code - 0x80];
        }
        
        std::string result;
        if (code < 0x80) {
            result = static_cast<char>(code);
        } else if (code < 0x800) {
            result += static_cast<char>(0xC0 | (code >> 6));
            result += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            result += static_cast<char>(0xE0 | (code >> 12));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (code >> 18));
            result += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (code & 0x3F));
        }
        
        return result;
    }
    
    std::string entityName(1, c);
    size_t runStart = position_;
    while (position_ < input_.size()) {
        char next = input_[position_];
        if (isAlphanumeric(next)) {
            entityName += next;
            consumeNextInputCharacter();
        } else {
            break;
        }
    }

    auto& entities = getNamedEntities();
    // Longest match: try the full alphanumeric run, then shorter prefixes
    // (so "&ampx" resolves "&amp" and leaves "x" in the data stream)
    for (size_t len = entityName.size(); len >= 1; --len) {
        auto it = entities.find(entityName.substr(0, len));
        if (it == entities.end()) {
            continue;
        }
        // Rewind the input to just after the matched name
        position_ = runStart + len - 1;
        if (position_ < input_.size() && input_[position_] == ';') {
            consumeNextInputCharacter();
        }
        return it->second;
    }

    return "";
}

inline std::unordered_map<std::string, std::string>& HtmlTokenizer::getNamedEntities() {
    // Common named character references (WHATWG named entities subset:
    // legacy entities + frequently used symbols, Greek, arrows, math).
    static std::unordered_map<std::string, std::string> namedEntities = {
        // Latin-1 punctuation & symbols
        {"nbsp", "\u00A0"}, {"iexcl", "\u00A1"}, {"cent", "\u00A2"},
        {"pound", "\u00A3"}, {"curren", "\u00A4"}, {"yen", "\u00A5"},
        {"brvbar", "\u00A6"}, {"sect", "\u00A7"}, {"uml", "\u00A8"},
        {"copy", "\u00A9"}, {"ordf", "\u00AA"}, {"laquo", "\u00AB"},
        {"not", "\u00AC"}, {"shy", "\u00AD"}, {"reg", "\u00AE"},
        {"macr", "\u00AF"}, {"deg", "\u00B0"}, {"plusmn", "\u00B1"},
        {"sup2", "\u00B2"}, {"sup3", "\u00B3"}, {"acute", "\u00B4"},
        {"micro", "\u00B5"}, {"para", "\u00B6"}, {"middot", "\u00B7"},
        {"cedil", "\u00B8"}, {"sup1", "\u00B9"}, {"ordm", "\u00BA"},
        {"raquo", "\u00BB"}, {"frac14", "\u00BC"}, {"frac12", "\u00BD"},
        {"frac34", "\u00BE"}, {"iquest", "\u00BF"}, {"times", "\u00D7"},
        {"divide", "\u00F7"}, {"quot", "\""}, {"apos", "'"}, {"amp", "&"},
        {"lt", "<"}, {"gt", ">"},
        // Latin-1 letters
        {"Agrave", "\u00C0"}, {"Aacute", "\u00C1"}, {"Acirc", "\u00C2"},
        {"Atilde", "\u00C3"}, {"Auml", "\u00C4"}, {"Aring", "\u00C5"},
        {"AElig", "\u00C6"}, {"Ccedil", "\u00C7"}, {"Egrave", "\u00C8"},
        {"Eacute", "\u00C9"}, {"Ecirc", "\u00CA"}, {"Euml", "\u00CB"},
        {"Igrave", "\u00CC"}, {"Iacute", "\u00CD"}, {"Icirc", "\u00CE"},
        {"Iuml", "\u00CF"}, {"ETH", "\u00D0"}, {"Ntilde", "\u00D1"},
        {"Ograve", "\u00D2"}, {"Oacute", "\u00D3"}, {"Ocirc", "\u00D4"},
        {"Otilde", "\u00D5"}, {"Ouml", "\u00D6"}, {"Oslash", "\u00D8"},
        {"Ugrave", "\u00D9"}, {"Uacute", "\u00DA"}, {"Ucirc", "\u00DB"},
        {"Uuml", "\u00DC"}, {"Yacute", "\u00DD"}, {"THORN", "\u00DE"},
        {"szlig", "\u00DF"}, {"agrave", "\u00E0"}, {"aacute", "\u00E1"},
        {"acirc", "\u00E2"}, {"atilde", "\u00E3"}, {"auml", "\u00E4"},
        {"aring", "\u00E5"}, {"aelig", "\u00E6"}, {"ccedil", "\u00E7"},
        {"egrave", "\u00E8"}, {"eacute", "\u00E9"}, {"ecirc", "\u00EA"},
        {"euml", "\u00EB"}, {"igrave", "\u00EC"}, {"iacute", "\u00ED"},
        {"icirc", "\u00EE"}, {"iuml", "\u00EF"}, {"eth", "\u00F0"},
        {"ntilde", "\u00F1"}, {"ograve", "\u00F2"}, {"oacute", "\u00F3"},
        {"ocirc", "\u00F4"}, {"otilde", "\u00F5"}, {"ouml", "\u00F6"},
        {"oslash", "\u00F8"}, {"ugrave", "\u00F9"}, {"uacute", "\u00FA"},
        {"ucirc", "\u00FB"}, {"uuml", "\u00FC"}, {"yacute", "\u00FD"},
        {"thorn", "\u00FE"}, {"yuml", "\u00FF"},
        // Latin extended
        {"OElig", "\u0152"}, {"oelig", "\u0153"}, {"Scaron", "\u0160"},
        {"scaron", "\u0161"}, {"Yuml", "\u0178"}, {"fnof", "\u0192"},
        {"circ", "\u02C6"}, {"tilde", "\u02DC"},
        // General punctuation & spaces
        {"ensp", "\u2002"}, {"emsp", "\u2003"}, {"thinsp", "\u2009"},
        {"zwnj", "\u200C"}, {"zwj", "\u200D"}, {"lrm", "\u200E"},
        {"rlm", "\u200F"}, {"ndash", "\u2013"}, {"mdash", "\u2014"},
        {"lsquo", "\u2018"}, {"rsquo", "\u2019"}, {"sbquo", "\u201A"},
        {"ldquo", "\u201C"}, {"rdquo", "\u201D"}, {"bdquo", "\u201E"},
        {"dagger", "\u2020"}, {"Dagger", "\u2021"}, {"bull", "\u2022"},
        {"hellip", "\u2026"}, {"permil", "\u2030"}, {"prime", "\u2032"},
        {"Prime", "\u2033"}, {"lsaquo", "\u2039"}, {"rsaquo", "\u203A"},
        {"oline", "\u203E"}, {"frasl", "\u2044"}, {"euro", "\u20AC"},
        {"trade", "\u2122"},
        // Letterlike symbols
        {"image", "\u2111"}, {"weierp", "\u2118"}, {"real", "\u211C"},
        {"alefsym", "\u2135"},
        // Arrows
        {"larr", "\u2190"}, {"uarr", "\u2191"}, {"rarr", "\u2192"},
        {"darr", "\u2193"}, {"harr", "\u2194"}, {"crarr", "\u21B5"},
        {"lArr", "\u21D0"}, {"uArr", "\u21D1"}, {"rArr", "\u21D2"},
        {"dArr", "\u21D3"}, {"hArr", "\u21D4"},
        // Mathematical operators
        {"forall", "\u2200"}, {"part", "\u2202"}, {"exist", "\u2203"},
        {"empty", "\u2205"}, {"nabla", "\u2207"}, {"isin", "\u2208"},
        {"notin", "\u2209"}, {"ni", "\u220B"}, {"prod", "\u220F"},
        {"sum", "\u2211"}, {"minus", "\u2212"}, {"lowast", "\u2217"},
        {"radic", "\u221A"}, {"prop", "\u221D"}, {"infin", "\u221E"},
        {"ang", "\u2220"}, {"and", "\u2227"}, {"or", "\u2228"},
        {"cap", "\u2229"}, {"cup", "\u222A"}, {"int", "\u222B"},
        {"there4", "\u2234"}, {"sim", "\u223C"}, {"cong", "\u2245"},
        {"asymp", "\u2248"}, {"ne", "\u2260"}, {"equiv", "\u2261"},
        {"le", "\u2264"}, {"ge", "\u2265"}, {"sub", "\u2282"},
        {"sup", "\u2283"}, {"nsub", "\u2284"}, {"sube", "\u2286"},
        {"supe", "\u2287"}, {"oplus", "\u2295"}, {"otimes", "\u2297"},
        {"perp", "\u22A5"}, {"sdot", "\u22C5"},
        // Technical
        {"lceil", "\u2308"}, {"rceil", "\u2309"}, {"lfloor", "\u230A"},
        {"rfloor", "\u230B"}, {"lang", "\u27E8"}, {"rang", "\u27E9"},
        // Shapes & card suits
        {"loz", "\u25CA"}, {"spades", "\u2660"}, {"clubs", "\u2663"},
        {"hearts", "\u2665"}, {"diams", "\u2666"},
        // Greek
        {"Alpha", "\u0391"}, {"Beta", "\u0392"}, {"Gamma", "\u0393"},
        {"Delta", "\u0394"}, {"Epsilon", "\u0395"}, {"Zeta", "\u0396"},
        {"Eta", "\u0397"}, {"Theta", "\u0398"}, {"Iota", "\u0399"},
        {"Kappa", "\u039A"}, {"Lambda", "\u039B"}, {"Mu", "\u039C"},
        {"Nu", "\u039D"}, {"Xi", "\u039E"}, {"Omicron", "\u039F"},
        {"Pi", "\u03A0"}, {"Rho", "\u03A1"}, {"Sigma", "\u03A3"},
        {"Tau", "\u03A4"}, {"Upsilon", "\u03A5"}, {"Phi", "\u03A6"},
        {"Chi", "\u03A7"}, {"Psi", "\u03A8"}, {"Omega", "\u03A9"},
        {"alpha", "\u03B1"}, {"beta", "\u03B2"}, {"gamma", "\u03B3"},
        {"delta", "\u03B4"}, {"epsilon", "\u03B5"}, {"zeta", "\u03B6"},
        {"eta", "\u03B7"}, {"theta", "\u03B8"}, {"iota", "\u03B9"},
        {"kappa", "\u03BA"}, {"lambda", "\u03BB"}, {"mu", "\u03BC"},
        {"nu", "\u03BD"}, {"xi", "\u03BE"}, {"omicron", "\u03BF"},
        {"pi", "\u03C0"}, {"rho", "\u03C1"}, {"sigmaf", "\u03C2"},
        {"sigma", "\u03C3"}, {"tau", "\u03C4"}, {"upsilon", "\u03C5"},
        {"phi", "\u03C6"}, {"chi", "\u03C7"}, {"psi", "\u03C8"},
        {"omega", "\u03C9"}, {"thetasym", "\u03D1"}, {"upsih", "\u03D2"},
        {"piv", "\u03D6"}
    };
    return namedEntities;
}

}
}
