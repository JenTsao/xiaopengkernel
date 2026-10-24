#pragma once

#include "html_types.hpp"
#include "../loader/error.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace xiaopeng {
namespace dom {

// U+FFFD, substituted for NUL and invalid code points
inline constexpr char kReplacementCharacter[] = "\xEF\xBF\xBD";

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
    void emitCurrentTagToken();
    
    // inAttribute enables the WHATWG "historical" rule for references that
    // lack a ';' and are followed by '=' or an alphanumeric
    std::string consumeCharacterReference(bool inAttribute);
    static bool isLegacyEntityName(const std::string& name);
    
    void handleDataState();
    void handleRcdataState();
    void handleRawtextState();
    void handlePlaintextState();
    void handleTextLessThanSignState(bool rcdata);
    void handleTextEndTagOpenState(bool rcdata);
    void handleTextEndTagNameState(bool rcdata);
    void handleCdataSectionState();
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
    
    void normalizeNewlines();
    static std::unordered_map<std::string, std::string>& getNamedEntities();
};

inline HtmlTokenizer::HtmlTokenizer(std::string_view input)
    : input_(input) {
    currentToken_ = Token::makeEndOfFile();
    if (input_.find('\r') != std::string_view::npos) {
        ownedInput_.assign(input_.data(), input_.size());
        normalizeNewlines();
    }
}

inline HtmlTokenizer::HtmlTokenizer(const loader::ByteBuffer& input)
    : ownedInput_(reinterpret_cast<const char*>(input.data()), input.size()),
      input_(ownedInput_) {
    currentToken_ = Token::makeEndOfFile();
    normalizeNewlines();
}

inline HtmlTokenizer::HtmlTokenizer(const std::string& input)
    : ownedInput_(input), input_(ownedInput_) {
    currentToken_ = Token::makeEndOfFile();
    normalizeNewlines();
}

// Input stream preprocessing: CRLF and lone CR become LF
inline void HtmlTokenizer::normalizeNewlines() {
    if (ownedInput_.find('\r') != std::string::npos) {
        std::string normalized;
        normalized.reserve(ownedInput_.size());
        for (size_t i = 0; i < ownedInput_.size(); ++i) {
            char c = ownedInput_[i];
            if (c == '\r') {
                normalized += '\n';
                if (i + 1 < ownedInput_.size() && ownedInput_[i + 1] == '\n')
                    ++i;
            } else {
                normalized += c;
            }
        }
        ownedInput_ = std::move(normalized);
    }
    input_ = ownedInput_;
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
        // The next call must reach end-of-file instead of re-entering the
        // state that hit EOF (which would flush its pending text forever)
        state_ = TokenizerState::Data;
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

// Emit the finished tag token. Start tags of raw-text elements switch the
// tokenizer the way the tree builder would (the whole input is tokenized
// before tree construction, so the builder cannot do it): their content up
// to the matching end tag is text, never markup.
inline void HtmlTokenizer::emitCurrentTagToken() {
    state_ = TokenizerState::Data;
    if (currentToken_.isStartTag()) {
        lastStartTag_ = currentToken_.name;
        const std::string& name = currentToken_.name;
        if (name == "title" || name == "textarea") {
            state_ = TokenizerState::Rcdata;
        } else if (name == "style" || name == "script" || name == "xmp" ||
                   name == "iframe" || name == "noembed" ||
                   name == "noframes") {
            state_ = TokenizerState::Rawtext;
        } else if (name == "plaintext") {
            state_ = TokenizerState::Plaintext;
        }
    }
    emitToken(currentToken_);
}

inline Token HtmlTokenizer::nextToken() {
    while (!hasEmittedToken_) {
        switch (state_) {
            case TokenizerState::Data:
                handleDataState();
                break;
            case TokenizerState::Rcdata:
                handleRcdataState();
                break;
            case TokenizerState::Rawtext:
                handleRawtextState();
                break;
            case TokenizerState::Plaintext:
                handlePlaintextState();
                break;
            case TokenizerState::RcdataLessThanSign:
            case TokenizerState::RawtextLessThanSign:
                handleTextLessThanSignState(state_ ==
                                            TokenizerState::RcdataLessThanSign);
                break;
            case TokenizerState::RcdataEndTagOpen:
            case TokenizerState::RawtextEndTagOpen:
                handleTextEndTagOpenState(state_ ==
                                          TokenizerState::RcdataEndTagOpen);
                break;
            case TokenizerState::RcdataEndTagName:
            case TokenizerState::RawtextEndTagName:
                handleTextEndTagNameState(state_ ==
                                          TokenizerState::RcdataEndTagName);
                break;
            case TokenizerState::CdataSection:
                handleCdataSectionState();
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
        std::string ref = consumeCharacterReference(false);
        if (!ref.empty()) {
            if (characterTokenBuffer_.empty()) {
                // Standalone entity: emit its own character token (existing
                // tokenizer contract, e.g. "&amp;" → token data "&")
                emitToken(Token::makeCharacter(ref));
            } else {
                // emitToken() has a single slot, so a second emit here would
                // overwrite the buffered text — merge instead
                characterTokenBuffer_ += ref;
            }
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
        // Tree construction ignores U+0000 in data, so drop it here
        emitError("Unexpected null character");
        return;
    }
    
    emitCharacterToken(c);
}

// RCDATA (<title>, <textarea>): text with character references, no markup
inline void HtmlTokenizer::handleRcdataState() {
    char c = consumeCharWithReconsume();
    
    if (c == '\0' && position_ >= input_.size()) {
        emitEofToken();
        return;
    }
    
    if (c == '&') {
        std::string ref = consumeCharacterReference(false);
        emitCharacterToken(ref.empty() ? std::string("&") : ref);
        return;
    }
    
    if (c == '<') {
        state_ = TokenizerState::RcdataLessThanSign;
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        emitCharacterToken(kReplacementCharacter);
        return;
    }
    
    emitCharacterToken(c);
}

// RAWTEXT (<style>, <script>, <xmp>, <iframe>, ...): text only
inline void HtmlTokenizer::handleRawtextState() {
    char c = consumeCharWithReconsume();
    
    if (c == '\0' && position_ >= input_.size()) {
        emitEofToken();
        return;
    }
    
    if (c == '<') {
        state_ = TokenizerState::RawtextLessThanSign;
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        emitCharacterToken(kReplacementCharacter);
        return;
    }
    
    emitCharacterToken(c);
}

inline void HtmlTokenizer::handlePlaintextState() {
    char c = consumeNextInputCharacter();
    
    if (c == '\0' && position_ >= input_.size()) {
        emitEofToken();
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        emitCharacterToken(kReplacementCharacter);
        return;
    }
    
    emitCharacterToken(c);
}

// Never set reconsume_ after reading EOF in the states below: nothing was
// consumed then, so stepping back would re-read the previous character.
inline void HtmlTokenizer::handleTextLessThanSignState(bool rcdata) {
    char c = consumeCharWithReconsume();
    
    if (c == '/') {
        tempBuffer_.clear();
        state_ = rcdata ? TokenizerState::RcdataEndTagOpen
                        : TokenizerState::RawtextEndTagOpen;
        return;
    }
    
    emitCharacterToken('<');
    if (c == '\0' && position_ >= input_.size()) {
        emitEofToken();
        return;
    }
    reconsume_ = true;
    state_ = rcdata ? TokenizerState::Rcdata : TokenizerState::Rawtext;
}

inline void HtmlTokenizer::handleTextEndTagOpenState(bool rcdata) {
    char c = consumeCharWithReconsume();
    
    if (isAlpha(c)) {
        createEndTagToken();
        reconsume_ = true;
        state_ = rcdata ? TokenizerState::RcdataEndTagName
                        : TokenizerState::RawtextEndTagName;
        return;
    }
    
    emitCharacterToken("</");
    if (c == '\0' && position_ >= input_.size()) {
        emitEofToken();
        return;
    }
    reconsume_ = true;
    state_ = rcdata ? TokenizerState::Rcdata : TokenizerState::Rawtext;
}

inline void HtmlTokenizer::handleTextEndTagNameState(bool rcdata) {
    char c = consumeCharWithReconsume();
    const bool atEof = c == '\0' && position_ >= input_.size();
    
    if (!atEof && (isWhitespace(c) || c == '/' || c == '>') &&
        isAppropriateEndTag()) {
        // The text before the end tag goes out first. emitToken() has a
        // single slot, so flush it now and see the terminator again next time.
        if (!characterTokenBuffer_.empty()) {
            emitToken(Token::makeCharacter(characterTokenBuffer_));
            characterTokenBuffer_.clear();
            reconsume_ = true;
            return;
        }
        if (isWhitespace(c)) {
            state_ = TokenizerState::BeforeAttributeName;
        } else if (c == '/') {
            state_ = TokenizerState::SelfClosingStartTag;
        } else {
            emitCurrentTagToken();
        }
        return;
    }
    
    if (!atEof && isAlpha(c)) {
        appendToTagName(toLower(c));
        tempBuffer_ += c;
        return;
    }
    
    // Not the matching end tag: "</name" was just text
    emitCharacterToken("</");
    emitCharacterToken(tempBuffer_);
    if (atEof) {
        emitEofToken();
        return;
    }
    reconsume_ = true;
    state_ = rcdata ? TokenizerState::Rcdata : TokenizerState::Rawtext;
}

// <![CDATA[ ... ]]> (only entered when allowCdata_ is set)
inline void HtmlTokenizer::handleCdataSectionState() {
    if (position_ >= input_.size()) {
        emitError("EOF in CDATA");
        emitEofToken();
        return;
    }
    if (input_.compare(position_, 3, "]]>") == 0) {
        for (int i = 0; i < 3; ++i) {
            consumeNextInputCharacter();
        }
        state_ = TokenizerState::Data;
        return;
    }
    emitCharacterToken(consumeNextInputCharacter());
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
        reconsume_ = true; // the '?' is part of the bogus comment's data
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
        emitCurrentTagToken();
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        currentToken_.name += kReplacementCharacter;
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
        currentAttributeName_ += kReplacementCharacter;
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
        currentAttributeName_ += kReplacementCharacter;
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
        emitCurrentTagToken();
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
        currentAttributeName_ += kReplacementCharacter;
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
        emitCurrentTagToken();
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
        currentAttributeValue_ += kReplacementCharacter;
        return;
    }
    
    if (c == '&') {
        std::string ref = consumeCharacterReference(true);
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
        currentAttributeValue_ += kReplacementCharacter;
        return;
    }
    
    if (c == '&') {
        std::string ref = consumeCharacterReference(true);
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
        emitCurrentTagToken();
        return;
    }
    
    if (c == '\0' && position_ >= input_.size()) {
        emitError("EOF in tag");
        emitEofToken();
        return;
    }
    
    if (c == '\0') {
        emitError("Unexpected null character");
        currentAttributeValue_ += kReplacementCharacter;
        return;
    }
    
    if (c == '"' || c == '\'' || c == '<' || c == '=' || c == '`') {
        emitError("Unexpected character in unquoted attribute value");
    }
    
    if (c == '&') {
        std::string ref = consumeCharacterReference(true);
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
        emitCurrentTagToken();
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
        emitCurrentTagToken();
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
        currentToken_.data += kReplacementCharacter;
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
    // Entered with reconsume_ set (from </ and <?), so honour it
    char c = consumeCharWithReconsume();
    
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
        currentToken_.data += kReplacementCharacter;
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
        currentToken_.name += kReplacementCharacter;
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
        currentToken_.name += kReplacementCharacter;
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
        currentToken_.publicIdentifier += kReplacementCharacter;
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
        currentToken_.publicIdentifier += kReplacementCharacter;
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
        currentToken_.systemIdentifier += kReplacementCharacter;
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
        currentToken_.systemIdentifier += kReplacementCharacter;
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

inline std::string HtmlTokenizer::consumeCharacterReference(bool inAttribute) {
    // Called just after '&'. On success the reference (and its ';') is
    // consumed and the decoded text returned. Otherwise "" is returned and
    // NOTHING is consumed, so the caller emits '&' and the following
    // characters are tokenized normally ("AT&T", "a &- b", "&#;").
    auto consumeUpTo = [this](size_t end) {
        while (position_ < end) {
            consumeNextInputCharacter();
        }
    };
    const size_t size = input_.size();
    if (position_ >= size) {
        return "";
    }
    
    if (input_[position_] == '#') {
        size_t p = position_ + 1;
        const bool hex = p < size && (input_[p] == 'x' || input_[p] == 'X');
        if (hex) {
            ++p;
        }
        // Accumulate with saturation: anything above U+10FFFF maps to
        // U+FFFD, and long digit runs must not overflow
        uint32_t code = 0;
        bool anyDigit = false;
        while (p < size && (hex ? isHexDigit(input_[p]) : isDigit(input_[p]))) {
            char d = input_[p];
            uint32_t digit = isDigit(d) ? static_cast<uint32_t>(d - '0')
                                        : static_cast<uint32_t>(toLower(d) - 'a' + 10);
            if (code <= 0x10FFFF) {
                code = code * (hex ? 16 : 10) + digit;
            }
            anyDigit = true;
            ++p;
        }
        
        if (!anyDigit) {
            emitError("Missing digits in numeric character reference");
            return "";
        }
        
        if (p < size && input_[p] == ';') {
            ++p;
        } else {
            emitError("Missing semicolon after numeric character reference");
        }
        consumeUpTo(p);
        
        if (code == 0) {
            emitError("Null character reference");
            code = 0xFFFD;
        } else if (code > 0x10FFFF) {
            emitError("Character reference outside unicode range");
            code = 0xFFFD;
        } else if (code >= 0xD800 && code <= 0xDFFF) {
            emitError("Surrogate character reference");
            code = 0xFFFD;
        } else if (code >= 0x80 && code <= 0x9F) {
            // WHATWG: C1 controls with a windows-1252 glyph are remapped;
            // the five undefined ones (0x81 0x8D 0x8F 0x90 0x9D) are kept
            emitError("C1 control character reference");
            static const uint32_t kC1Remap[32] = {
                0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020,
                0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,
                0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D,
                0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A,
                0x0153, 0, 0x017E, 0x0178};
            if (kC1Remap[code - 0x80] != 0) {
                code = kC1Remap[code - 0x80];
            }
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
    
    size_t runEnd = position_;
    while (runEnd < size && isAlphanumeric(input_[runEnd])) {
        ++runEnd;
    }
    if (runEnd == position_) {
        return "";
    }
    const std::string run(input_.substr(position_, runEnd - position_));
    const bool runHasSemicolon = runEnd < size && input_[runEnd] == ';';
    
    auto& entities = getNamedEntities();
    // Longest match: try the full alphanumeric run, then shorter prefixes
    // (so "&ampx" resolves "&amp" and leaves "x" in the data stream)
    for (size_t len = run.size(); len >= 1; --len) {
        const std::string name = run.substr(0, len);
        auto it = entities.find(name);
        if (it == entities.end()) {
            continue;
        }
        const bool semicolon = len == run.size() && runHasSemicolon;
        if (!semicolon) {
            // Only the legacy (HTML 4) names may omit the ';'
            if (!isLegacyEntityName(name)) {
                continue;
            }
            // Historical rule: in attribute values "&copy=2" or "&ampx"
            // stay literal text (common in URLs)
            if (inAttribute) {
                size_t next = position_ + len;
                if (next < size &&
                    (input_[next] == '=' || isAlphanumeric(input_[next]))) {
                    return "";
                }
            }
            emitError("Missing semicolon after character reference");
        }
        consumeUpTo(position_ + len + (semicolon ? 1 : 0));
        return it->second;
    }
    
    return "";
}

// Named references that may appear without a trailing ';' (WHATWG table)
inline bool HtmlTokenizer::isLegacyEntityName(const std::string& name) {
    static const std::unordered_set<std::string> kLegacy = {
        "AElig", "AMP", "Aacute", "Acirc", "Agrave", "Aring", "Atilde",
        "Auml", "COPY", "Ccedil", "ETH", "Eacute", "Ecirc", "Egrave", "Euml",
        "GT", "Iacute", "Icirc", "Igrave", "Iuml", "LT", "Ntilde", "Oacute",
        "Ocirc", "Ograve", "Oslash", "Otilde", "Ouml", "QUOT", "REG", "THORN",
        "Uacute", "Ucirc", "Ugrave", "Uuml", "Yacute", "aacute", "acirc",
        "acute", "aelig", "agrave", "amp", "aring", "atilde", "auml",
        "brvbar", "ccedil", "cedil", "cent", "copy", "curren", "deg",
        "divide", "eacute", "ecirc", "egrave", "eth", "euml", "frac12",
        "frac14", "frac34", "gt", "iacute", "icirc", "iexcl", "igrave",
        "iquest", "iuml", "laquo", "lt", "macr", "micro", "middot", "nbsp",
        "not", "ntilde", "oacute", "ocirc", "ograve", "ordf", "ordm",
        "oslash", "otilde", "ouml", "para", "plusmn", "pound", "quot",
        "raquo", "reg", "sect", "shy", "sup1", "sup2", "sup3", "szlig",
        "thorn", "times", "uacute", "ucirc", "ugrave", "uml", "uuml",
        "yacute", "yen", "yuml"};
    return kLegacy.count(name) > 0;
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
