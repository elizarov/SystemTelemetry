#include "tools/impl/format_lexer.h"

#include <optional>
#include <utility>

namespace {

bool IsGroupClose(std::string_view text) {
    return text == ")" || text == "]" || text == "}";
}

bool IsAtPreprocessorStart(std::string_view text, size_t index) {
    if (index > text.size() || index != 0 && text[index - 1] != '\n') {
        return false;
    }
    while (index < text.size() && IsSpaceButNotNewline(text[index])) {
        ++index;
    }
    return index < text.size() && text[index] == '#';
}

std::string ReadPreprocessor(std::string_view text, size_t& index) {
    const size_t start = index;
    while (index < text.size()) {
        const size_t lineStart = index;
        while (index < text.size() && text[index] != '\n') {
            ++index;
        }
        size_t lineEnd = index;
        while (lineEnd > lineStart && IsSpaceButNotNewline(text[lineEnd - 1])) {
            --lineEnd;
        }
        const bool continued = lineEnd > lineStart && text[lineEnd - 1] == '\\';
        if (index < text.size() && text[index] == '\n') {
            ++index;
        }
        if (!continued) {
            break;
        }
    }
    return std::string(text.substr(start, index - start));
}

size_t SkipWhitespaceAndComments(std::string_view text, size_t index) {
    while (index < text.size()) {
        if (IsSpaceButNotNewline(text[index]) || text[index] == '\n') {
            ++index;
            continue;
        }
        if (index + 1 < text.size() && text[index] == '/' && text[index + 1] == '/') {
            index += 2;
            while (index < text.size() && text[index] != '\n') {
                ++index;
            }
            continue;
        }
        if (index + 1 < text.size() && text[index] == '/' && text[index + 1] == '*') {
            index += 2;
            while (index + 1 < text.size() && (text[index] != '*' || text[index + 1] != '/')) {
                ++index;
            }
            if (index + 1 < text.size()) {
                index += 2;
            }
            continue;
        }
        break;
    }
    return index;
}

bool IsTrailingComma(std::string_view text, size_t commaIndex) {
    if (commaIndex >= text.size() || text[commaIndex] != ',') {
        return false;
    }
    const size_t next = SkipWhitespaceAndComments(text, commaIndex + 1);
    return next < text.size() && IsGroupClose(std::string_view(text).substr(next, 1));
}

std::string ReadQuoted(std::string_view text, size_t& index, char quote) {
    const size_t start = index;
    ++index;
    while (index < text.size()) {
        const char ch = text[index];
        if (ch == '\\' && index + 1 < text.size()) {
            index += 2;
            continue;
        }
        ++index;
        if (ch == quote) {
            break;
        }
    }
    return std::string(text.substr(start, index - start));
}

std::optional<std::string> TryReadRawString(std::string_view text, size_t& index) {
    if (index + 1 >= text.size() || text[index] != 'R' || text[index + 1] != '"') {
        return std::nullopt;
    }
    size_t delimiterStart = index + 2;
    size_t openParen = delimiterStart;
    while (openParen < text.size() && text[openParen] != '(' && text[openParen] != '\n') {
        ++openParen;
    }
    if (openParen >= text.size() || text[openParen] != '(') {
        return std::nullopt;
    }
    const std::string delimiter(text.substr(delimiterStart, openParen - delimiterStart));
    const std::string terminator = ")" + delimiter + "\"";
    const size_t end = text.find(terminator, openParen + 1);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    const size_t start = index;
    index = end + terminator.size();
    return std::string(text.substr(start, index - start));
}

}  // namespace

bool IsIdentifierStart(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

bool IsIdentifierBody(char ch) {
    return IsIdentifierStart(ch) || (ch >= '0' && ch <= '9');
}

bool IsDigit(char ch) {
    return ch >= '0' && ch <= '9';
}

bool IsHexDigit(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
}

bool IsOctalDigit(char ch) {
    return ch >= '0' && ch <= '7';
}

bool IsSpaceButNotNewline(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v';
}

bool IsCommentOrNewline(const Token& token) {
    return token.kind == TokenKind::Newline ||
        token.kind == TokenKind::LineComment ||
        token.kind == TokenKind::BlockComment;
}

std::vector<Token> TokenizeCharacterStream(std::string_view text) {
    std::vector<Token> tokens;
    size_t index = 0;
    while (index < text.size()) {
        const char ch = text[index];
        if (IsAtPreprocessorStart(text, index)) {
            tokens.push_back({TokenKind::Preprocessor, ReadPreprocessor(text, index)});
            continue;
        }
        if (ch == '\n') {
            tokens.push_back({TokenKind::Newline, "\n"});
            ++index;
            continue;
        }
        if (IsSpaceButNotNewline(ch)) {
            ++index;
            continue;
        }
        if (ch == '/' && index + 1 < text.size() && text[index + 1] == '/') {
            const size_t start = index;
            index += 2;
            while (index < text.size() && text[index] != '\n') {
                ++index;
            }
            tokens.push_back({TokenKind::LineComment, std::string(text.substr(start, index - start))});
            continue;
        }
        if (ch == '/' && index + 1 < text.size() && text[index + 1] == '*') {
            const size_t start = index;
            index += 2;
            while (index + 1 < text.size() && !(text[index] == '*' && text[index + 1] == '/')) {
                ++index;
            }
            if (index + 1 < text.size()) {
                index += 2;
            }
            tokens.push_back({TokenKind::BlockComment, std::string(text.substr(start, index - start))});
            continue;
        }
        if (ch == 'R') {
            if (std::optional<std::string> raw = TryReadRawString(text, index)) {
                tokens.push_back({TokenKind::StringLiteral, *raw});
                continue;
            }
        }
        if (ch == '"') {
            tokens.push_back({TokenKind::StringLiteral, ReadQuoted(text, index, '"')});
            continue;
        }
        if (ch == '\'') {
            tokens.push_back({TokenKind::CharLiteral, ReadQuoted(text, index, '\'')});
            continue;
        }
        if (IsIdentifierStart(ch)) {
            const size_t start = index;
            ++index;
            while (index < text.size() && IsIdentifierBody(text[index])) {
                ++index;
            }
            tokens.push_back({TokenKind::Word, std::string(text.substr(start, index - start))});
            continue;
        }
        if (IsDigit(ch)) {
            const size_t start = index;
            ++index;
            while (index < text.size() && (IsIdentifierBody(text[index]) || text[index] == '.')) {
                ++index;
            }
            tokens.push_back({TokenKind::Number, std::string(text.substr(start, index - start))});
            continue;
        }
        if (ch == ',' && IsTrailingComma(text, index)) {
            ++index;
            continue;
        }
        static constexpr std::string_view kThreeCharOps[] = {"<<=", ">>=", "<=>", "...", "->*"};
        bool matched = false;
        for (std::string_view op : kThreeCharOps) {
            if (index + op.size() <= text.size() && text.substr(index, op.size()) == op) {
                tokens.push_back({TokenKind::Symbol, std::string(op)});
                index += op.size();
                matched = true;
                break;
            }
        }
        if (matched) {
            continue;
        }
        static constexpr std::string_view kTwoCharOps[] = {
            "::",
            "->",
            "++",
            "--",
            "&&",
            "||",
            "==",
            "!=",
            "<=",
            ">=",
            "+=",
            "-=",
            "*=",
            "/=",
            "%=",
            "&=",
            "|=",
            "^=",
            "<<",
            ">>",
            "##",
            ".*"
        };
        for (std::string_view op : kTwoCharOps) {
            if (index + op.size() <= text.size() && text.substr(index, op.size()) == op) {
                tokens.push_back({TokenKind::Symbol, std::string(op)});
                index += op.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            tokens.push_back({TokenKind::Symbol, std::string(1, ch)});
            ++index;
        }
    }
    return tokens;
}
