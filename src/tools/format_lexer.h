#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tools::format {

enum class TokenKind {
    Word,
    Number,
    StringLiteral,
    CharLiteral,
    LineComment,
    BlockComment,
    Preprocessor,
    Symbol,
    Newline,
};

struct Token {
    TokenKind kind = TokenKind::Symbol;
    std::string text;
};

using TokenSpan = std::span<const Token>;

inline TokenSpan TokenSubspan(TokenSpan tokens, size_t begin, size_t end) {
    if (begin > tokens.size()) {
        begin = tokens.size();
    }
    if (end < begin) {
        end = begin;
    }
    if (end > tokens.size()) {
        end = tokens.size();
    }
    return tokens.subspan(begin, end - begin);
}

bool IsIdentifierStart(char ch);
bool IsIdentifierBody(char ch);
bool IsDigit(char ch);
bool IsHexDigit(char ch);
bool IsOctalDigit(char ch);
bool IsSpaceButNotNewline(char ch);
bool IsCommentOrNewline(const Token& token);

std::vector<Token> DropTrailingCommas(std::vector<Token> tokens);
std::vector<Token> TokenizeCharacterStream(std::string_view text);

}  // namespace tools::format
