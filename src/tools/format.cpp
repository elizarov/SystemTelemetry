#include "tools/format.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <io.h>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <tree_sitter_cpp.h>
#include <vector>

#include "tools/format_args.h"
#include "tools/format_config.h"
#include "tools/impl/lint_common.h"

namespace {

using tools::format::FormatMode;
using tools::format::FormatOptions;
using tools::format::FormatterConfig;

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

struct FileFormatResult {
    bool ok = true;
    bool changed = false;
    bool parseHadErrors = false;
    std::string parseErrorNodeType;
    int parseErrorLine = 0;
    int parseErrorColumn = 0;
    std::string parseErrorSnippet;
    std::string formatted;
    std::string error;
};

struct ParseResult {
    bool ok = false;
    bool hasErrors = false;
    std::string errorNodeType;
    int errorLine = 0;
    int errorColumn = 0;
    std::string errorSnippet;
};

std::string FormatElapsed(std::chrono::steady_clock::duration elapsed) {
    const double seconds = std::chrono::duration<double>(elapsed).count();
    char buffer[64] = {};
    if (seconds < 1.0) {
        std::snprintf(buffer, sizeof(buffer), "%dms", static_cast<int>(seconds * 1000.0 + 0.5));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.3fs", seconds);
    }
    return buffer;
}

std::string NormalizeLineEndings(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
            result.push_back('\n');
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

std::string ToFileLineEndings(std::string_view text) {
    std::string result;
    result.reserve(text.size() + text.size() / 24);
    for (char ch : text) {
        if (ch == '\n') {
            result += "\r\n";
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

std::string Spaces(int count) {
    return std::string(static_cast<size_t>(std::max(0, count)), ' ');
}

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

bool IsGroupClose(std::string_view text) {
    return text == ")" || text == "]" || text == "}";
}

bool IsTrailingComma(const std::vector<Token>& tokens, size_t index) {
    if (index >= tokens.size() || tokens[index].text != ",") {
        return false;
    }
    for (size_t next = index + 1; next < tokens.size(); ++next) {
        if (IsCommentOrNewline(tokens[next])) {
            continue;
        }
        return IsGroupClose(tokens[next].text);
    }
    return false;
}

std::vector<Token> DropTrailingCommas(std::vector<Token> tokens) {
    std::vector<Token> result;
    result.reserve(tokens.size());
    for (size_t index = 0; index < tokens.size(); ++index) {
        if (!IsTrailingComma(tokens, index)) {
            result.push_back(std::move(tokens[index]));
        }
    }
    return result;
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

std::vector<Token> Tokenize(std::string_view text) {
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
    return DropTrailingCommas(std::move(tokens));
}

bool IsControlBraceGroupOpen(std::string_view text) {
    return text == "(" || text == "[" || text == "{";
}

std::string ControlBraceMatchingClose(std::string_view text) {
    if (text == "(") {
        return ")";
    }
    if (text == "[") {
        return "]";
    }
    return "}";
}

size_t NextCodeIndex(const std::vector<Token>& tokens, size_t index, size_t end) {
    while (index < end && IsCommentOrNewline(tokens[index])) {
        ++index;
    }
    return index;
}

std::optional<size_t> FindControlBraceMatchingClose(const std::vector<Token>& tokens, size_t openIndex, size_t end) {
    if (openIndex >= end || !IsControlBraceGroupOpen(tokens[openIndex].text)) {
        return std::nullopt;
    }
    const std::string close = ControlBraceMatchingClose(tokens[openIndex].text);
    int depth = 0;
    for (size_t index = openIndex; index < end; ++index) {
        if (tokens[index].text == tokens[openIndex].text) {
            ++depth;
        } else if (tokens[index].text == close) {
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }
    return std::nullopt;
}

void AppendTokenRange(const std::vector<Token>& tokens, size_t begin, size_t end, std::vector<Token>& output) {
    if (begin >= end) {
        return;
    }
    output.insert(
        output.end(),
        tokens.begin() + static_cast<std::ptrdiff_t>(begin),
        tokens.begin() + static_cast<std::ptrdiff_t>(end)
    );
}

bool IsBraceRequiredControlToken(const Token& token) {
    return token.kind == TokenKind::Word && (
        token.text == "if" ||
        token.text == "else" ||
        token.text == "for" ||
        token.text == "while" ||
        token.text == "do" ||
        token.text == "switch"
    );
}

std::optional<size_t> FindControlHeaderEnd(const std::vector<Token>& tokens, size_t controlIndex, size_t end) {
    for (size_t index = controlIndex + 1; index < end; ++index) {
        if (tokens[index].text != "(") {
            continue;
        }
        if (std::optional<size_t> close = FindControlBraceMatchingClose(tokens, index, end)) {
            return *close + 1;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

void RewriteControlBracesRange(const std::vector<Token>& tokens, size_t begin, size_t end, std::vector<Token>& output);

size_t RewriteControlBracesStatement(
    const std::vector<Token>& tokens,
    size_t begin,
    size_t end,
    std::vector<Token>& output
);

size_t RewriteIfControlStatement(
    const std::vector<Token>& tokens,
    size_t ifIndex,
    size_t end,
    std::vector<Token>& output
);

size_t FindControlBracesStatementEnd(const std::vector<Token>& tokens, size_t begin, size_t end);

size_t RewriteBraceBlock(const std::vector<Token>& tokens, size_t openIndex, size_t end, std::vector<Token>& output) {
    const std::optional<size_t> close = FindControlBraceMatchingClose(tokens, openIndex, end);
    if (!close) {
        output.push_back(tokens[openIndex]);
        return openIndex + 1;
    }
    output.push_back(tokens[openIndex]);
    RewriteControlBracesRange(tokens, openIndex + 1, *close, output);
    output.push_back(tokens[*close]);
    return *close + 1;
}

size_t FindSimpleControlBracesStatementEnd(const std::vector<Token>& tokens, size_t begin, size_t end) {
    for (size_t index = begin; index < end; ++index) {
        if (IsControlBraceGroupOpen(tokens[index].text)) {
            if (std::optional<size_t> close = FindControlBraceMatchingClose(tokens, index, end)) {
                index = *close;
                continue;
            }
        }
        if (tokens[index].text == ";") {
            return index + 1;
        }
    }
    return end;
}

size_t FindIfControlBracesStatementEnd(const std::vector<Token>& tokens, size_t begin, size_t end) {
    const std::optional<size_t> headerEnd = FindControlHeaderEnd(tokens, begin, end);
    if (!headerEnd) {
        return FindSimpleControlBracesStatementEnd(tokens, begin, end);
    }
    size_t statementEnd = FindControlBracesStatementEnd(tokens, *headerEnd, end);
    const size_t elseIndex = NextCodeIndex(tokens, statementEnd, end);
    if (elseIndex < end && tokens[elseIndex].text == "else") {
        statementEnd = FindControlBracesStatementEnd(tokens, elseIndex, end);
    }
    return statementEnd;
}

size_t FindElseControlBracesStatementEnd(const std::vector<Token>& tokens, size_t begin, size_t end) {
    return FindControlBracesStatementEnd(tokens, begin + 1, end);
}

bool ContainsOnlyNewlines(const std::vector<Token>& tokens, size_t begin, size_t end) {
    for (size_t index = begin; index < end; ++index) {
        if (tokens[index].kind != TokenKind::Newline) {
            return false;
        }
    }
    return true;
}

size_t FindHeaderBodyControlBracesStatementEnd(const std::vector<Token>& tokens, size_t begin, size_t end) {
    const std::optional<size_t> headerEnd = FindControlHeaderEnd(tokens, begin, end);
    if (!headerEnd) {
        return FindSimpleControlBracesStatementEnd(tokens, begin, end);
    }
    return FindControlBracesStatementEnd(tokens, *headerEnd, end);
}

size_t FindDoControlBracesStatementEnd(const std::vector<Token>& tokens, size_t begin, size_t end) {
    const size_t bodyEnd = FindControlBracesStatementEnd(tokens, begin + 1, end);
    const size_t whileIndex = NextCodeIndex(tokens, bodyEnd, end);
    if (whileIndex >= end || tokens[whileIndex].text != "while") {
        return bodyEnd;
    }
    const std::optional<size_t> whileHeaderEnd = FindControlHeaderEnd(tokens, whileIndex, end);
    if (!whileHeaderEnd) {
        return FindSimpleControlBracesStatementEnd(tokens, whileIndex, end);
    }
    const size_t semicolon = NextCodeIndex(tokens, *whileHeaderEnd, end);
    return semicolon < end && tokens[semicolon].text == ";" ? semicolon + 1 : *whileHeaderEnd;
}

size_t FindControlBracesStatementEnd(const std::vector<Token>& tokens, size_t begin, size_t end) {
    const size_t statementStart = NextCodeIndex(tokens, begin, end);
    if (statementStart >= end) {
        return end;
    }
    if (tokens[statementStart].text == "{") {
        if (std::optional<size_t> close = FindControlBraceMatchingClose(tokens, statementStart, end)) {
            return *close + 1;
        }
        return end;
    }
    if (tokens[statementStart].text == "if") {
        return FindIfControlBracesStatementEnd(tokens, statementStart, end);
    }
    if (tokens[statementStart].text == "else") {
        return FindElseControlBracesStatementEnd(tokens, statementStart, end);
    }
    if (tokens[statementStart].text == "do") {
        return FindDoControlBracesStatementEnd(tokens, statementStart, end);
    }
    if (
        tokens[statementStart].text == "for" ||
        tokens[statementStart].text == "while" ||
        tokens[statementStart].text == "switch"
    ) {
        return FindHeaderBodyControlBracesStatementEnd(tokens, statementStart, end);
    }
    return FindSimpleControlBracesStatementEnd(tokens, statementStart, end);
}

void AppendRewrittenControlBody(
    const std::vector<Token>& tokens,
    size_t bodyBegin,
    size_t bodyEnd,
    std::vector<Token>& output
) {
    output.push_back({TokenKind::Symbol, "{"});
    RewriteControlBracesStatement(tokens, bodyBegin, bodyEnd, output);
    output.push_back({TokenKind::Symbol, "}"});
}

size_t RewriteHeaderBodyControlStatement(
    const std::vector<Token>& tokens,
    size_t controlIndex,
    size_t end,
    std::vector<Token>& output
) {
    const std::optional<size_t> headerEnd = FindControlHeaderEnd(tokens, controlIndex, end);
    if (!headerEnd) {
        output.push_back(tokens[controlIndex]);
        return controlIndex + 1;
    }
    AppendTokenRange(tokens, controlIndex, *headerEnd, output);
    const size_t bodyBegin = *headerEnd;
    const size_t bodyStart = NextCodeIndex(tokens, bodyBegin, end);
    if (bodyStart < end && tokens[bodyStart].text == "{") {
        return RewriteControlBracesStatement(tokens, bodyBegin, end, output);
    }
    const size_t bodyEnd = FindControlBracesStatementEnd(tokens, bodyBegin, end);
    AppendRewrittenControlBody(tokens, bodyBegin, bodyEnd, output);
    return bodyEnd;
}

size_t RewriteElseControlStatement(
    const std::vector<Token>& tokens,
    size_t elseIndex,
    size_t end,
    std::vector<Token>& output
) {
    output.push_back(tokens[elseIndex]);
    const size_t bodyBegin = elseIndex + 1;
    const size_t bodyStart = NextCodeIndex(tokens, bodyBegin, end);
    if (bodyStart < end && tokens[bodyStart].text == "if") {
        AppendTokenRange(tokens, bodyBegin, bodyStart, output);
        return RewriteIfControlStatement(tokens, bodyStart, end, output);
    }
    if (bodyStart < end && tokens[bodyStart].text == "{") {
        const std::optional<size_t> bodyClose = FindControlBraceMatchingClose(tokens, bodyStart, end);
        if (bodyClose && ContainsOnlyNewlines(tokens, bodyBegin, bodyStart)) {
            const size_t innerStart = NextCodeIndex(tokens, bodyStart + 1, *bodyClose);
            if (innerStart < *bodyClose && tokens[innerStart].text == "if") {
                const size_t innerEnd = FindIfControlBracesStatementEnd(tokens, innerStart, *bodyClose);
                if (
                    innerEnd <= *bodyClose &&
                    ContainsOnlyNewlines(tokens, bodyStart + 1, innerStart) &&
                    ContainsOnlyNewlines(tokens, innerEnd, *bodyClose)
                ) {
                    RewriteIfControlStatement(tokens, innerStart, *bodyClose, output);
                    return *bodyClose + 1;
                }
            }
        }
        return RewriteControlBracesStatement(tokens, bodyBegin, end, output);
    }
    const size_t bodyEnd = FindControlBracesStatementEnd(tokens, bodyBegin, end);
    AppendRewrittenControlBody(tokens, bodyBegin, bodyEnd, output);
    return bodyEnd;
}

size_t RewriteIfControlStatement(
    const std::vector<Token>& tokens,
    size_t ifIndex,
    size_t end,
    std::vector<Token>& output
) {
    size_t next = RewriteHeaderBodyControlStatement(tokens, ifIndex, end, output);
    const size_t elseIndex = NextCodeIndex(tokens, next, end);
    if (elseIndex < end && tokens[elseIndex].text == "else") {
        AppendTokenRange(tokens, next, elseIndex, output);
        next = RewriteElseControlStatement(tokens, elseIndex, end, output);
    }
    return next;
}

size_t RewriteDoControlStatement(
    const std::vector<Token>& tokens,
    size_t doIndex,
    size_t end,
    std::vector<Token>& output
) {
    output.push_back(tokens[doIndex]);
    const size_t bodyBegin = doIndex + 1;
    const size_t bodyStart = NextCodeIndex(tokens, bodyBegin, end);
    size_t bodyEnd = bodyBegin;
    if (bodyStart < end && tokens[bodyStart].text == "{") {
        bodyEnd = RewriteControlBracesStatement(tokens, bodyBegin, end, output);
    } else {
        bodyEnd = FindControlBracesStatementEnd(tokens, bodyBegin, end);
        AppendRewrittenControlBody(tokens, bodyBegin, bodyEnd, output);
    }
    const size_t whileIndex = NextCodeIndex(tokens, bodyEnd, end);
    if (whileIndex < end && tokens[whileIndex].text == "while") {
        AppendTokenRange(tokens, bodyEnd, FindDoControlBracesStatementEnd(tokens, doIndex, end), output);
        return FindDoControlBracesStatementEnd(tokens, doIndex, end);
    }
    return bodyEnd;
}

size_t RewriteControlBracesStatement(
    const std::vector<Token>& tokens,
    size_t begin,
    size_t end,
    std::vector<Token>& output
) {
    const size_t statementStart = NextCodeIndex(tokens, begin, end);
    AppendTokenRange(tokens, begin, statementStart, output);
    if (statementStart >= end) {
        return end;
    }
    if (tokens[statementStart].text == "{") {
        return RewriteBraceBlock(tokens, statementStart, end, output);
    }
    if (tokens[statementStart].text == "if") {
        return RewriteIfControlStatement(tokens, statementStart, end, output);
    }
    if (tokens[statementStart].text == "else") {
        return RewriteElseControlStatement(tokens, statementStart, end, output);
    }
    if (tokens[statementStart].text == "do") {
        return RewriteDoControlStatement(tokens, statementStart, end, output);
    }
    if (
        tokens[statementStart].text == "for" ||
        tokens[statementStart].text == "while" ||
        tokens[statementStart].text == "switch"
    ) {
        return RewriteHeaderBodyControlStatement(tokens, statementStart, end, output);
    }
    const size_t statementEnd = FindSimpleControlBracesStatementEnd(tokens, statementStart, end);
    RewriteControlBracesRange(tokens, statementStart, statementEnd, output);
    return statementEnd;
}

void RewriteControlBracesRange(const std::vector<Token>& tokens, size_t begin, size_t end, std::vector<Token>& output) {
    size_t index = begin;
    while (index < end) {
        const size_t codeIndex = NextCodeIndex(tokens, index, end);
        AppendTokenRange(tokens, index, codeIndex, output);
        if (codeIndex >= end) {
            return;
        }
        if (IsBraceRequiredControlToken(tokens[codeIndex])) {
            index = RewriteControlBracesStatement(tokens, codeIndex, end, output);
            continue;
        }
        if (tokens[codeIndex].text == "{") {
            index = RewriteBraceBlock(tokens, codeIndex, end, output);
            continue;
        }
        output.push_back(tokens[codeIndex]);
        index = codeIndex + 1;
    }
}

std::vector<Token> AddRequiredControlBraces(const std::vector<Token>& tokens) {
    std::vector<Token> output;
    output.reserve(tokens.size());
    RewriteControlBracesRange(tokens, 0, tokens.size(), output);
    return output;
}

bool IsWordLike(const Token& token) {
    return token.kind == TokenKind::Word ||
        token.kind == TokenKind::Number ||
        token.kind == TokenKind::StringLiteral ||
        token.kind == TokenKind::CharLiteral;
}

bool IsControlKeyword(std::string_view text) {
    return text == "if" || text == "for" || text == "while" || text == "switch" || text == "catch";
}

bool IsBinaryOperator(std::string_view text) {
    static constexpr std::string_view kOperators[] = {
        "=",
        "+",
        "-",
        "&",
        "/",
        "%",
        "==",
        "!=",
        "<=",
        ">=",
        "&&",
        "||",
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
        "<<=",
        ">>=",
        "|",
        "^",
        "?",
        ":"
    };
    return std::find(std::begin(kOperators), std::end(kOperators), text) != std::end(kOperators);
}

bool IsPrefixOperator(std::string_view text) {
    return text == "!" || text == "~" || text == "++" || text == "--";
}

bool IsNoSpaceBefore(std::string_view text) {
    return text == ")" ||
        text == "]" ||
        text == ";" ||
        text == "," ||
        text == "." ||
        text == "->" ||
        text == ".*" ||
        text == "->*" ||
        text == "::" ||
        text == "++" ||
        text == "--";
}

bool IsNoSpaceAfter(std::string_view text) {
    return text == "(" ||
        text == "[" ||
        text == "{" ||
        text == "." ||
        text == "->" ||
        text == ".*" ||
        text == "->*" ||
        text == "::" ||
        text == "~" ||
        text == "!";
}

bool IsStringOrCharacterLiteralPrefix(std::string_view text) {
    return text == "L" || text == "u" || text == "U" || text == "u8";
}

std::string TrimRight(std::string value) {
    while (!value.empty() && IsSpaceButNotNewline(value.back())) {
        value.pop_back();
    }
    return value;
}

std::string TrimLeft(std::string_view value) {
    size_t index = 0;
    while (index < value.size() && IsSpaceButNotNewline(value[index])) {
        ++index;
    }
    return std::string(value.substr(index));
}

std::string SingleLineSnippet(std::string_view text, uint32_t startByte, uint32_t endByte) {
    if (startByte >= text.size()) {
        return {};
    }
    if (endByte <= startByte || endByte > text.size()) {
        endByte = static_cast<uint32_t>(std::min(text.size(), static_cast<size_t>(startByte) + 120));
    }
    std::string snippet(text.substr(startByte, std::min<uint32_t>(endByte - startByte, 120)));
    for (char& ch : snippet) {
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            ch = ' ';
        }
    }
    return tools::lint::Trim(snippet);
}

TSNode FindFirstErrorNode(TSNode node) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        if (ts_node_has_error(child)) {
            return FindFirstErrorNode(child);
        }
    }
    if (std::strcmp(ts_node_type(node), "ERROR") == 0 || ts_node_is_missing(node)) {
        return node;
    }
    return node;
}

class PrettyFormatter {
public:
    explicit PrettyFormatter(
        const FormatterConfig& config,
        int initialIndentLevel = 0,
        bool executableBodyContext = false
    ) : config_(config), indentLevel_(initialIndentLevel) {
        if (executableBodyContext) {
            blockStack_.push_back({BlockKind::FunctionDefinition, true, DeclarationKind::None, false, -1});
        }
    }

    std::string Format(const std::vector<Token>& tokens) {
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            switch (token.kind) {
                case TokenKind::Newline:
                    HandleOriginalNewline(tokens, index);
                    break;
                case TokenKind::Preprocessor:
                    EmitPreprocessor(token.text);
                    break;
                case TokenKind::LineComment:
                    EmitLineComment(tokens, index);
                    break;
                case TokenKind::BlockComment:
                    EmitBlockComment(token.text);
                    break;
                default:
                    EmitCodeToken(tokens, index);
                    break;
            }
        }
        FlushPending();
        while (!outputLines_.empty() && outputLines_.back().empty()) {
            outputLines_.pop_back();
        }
        return JoinOutput();
    }

private:
    enum class BlockKind {
        Other,
        CaseScope,
        SwitchStatement,
        DoStatement,
        NamespaceDeclaration,
        EnumDeclaration,
        TypeDeclaration,
        FunctionDefinition,
    };

    enum class DeclarationKind {
        None,
        Field,
        MacroDefinition,
        Method,
        NamespaceDeclaration,
        TypeDeclaration,
    };

    struct RequiresClauseParts {
        std::vector<Token> condition;
        std::vector<Token> declaration;
    };

    struct BlockState {
        BlockKind kind = BlockKind::Other;
        bool indentsBody = true;
        DeclarationKind previousDeclarationKind = DeclarationKind::None;
        bool previousDeclarationBreaksSiblingGroup = false;
        int previousCaseBodyIndentLevel = -1;
    };

    void EmitLine(std::string text) {
        outputLines_.push_back(TrimRight(std::move(text)));
        allowOriginalBlank_ = true;
    }

    void HandleOriginalNewline(const std::vector<Token>& tokens, size_t& index) {
        size_t newlineCount = 1;
        while (index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::Newline) {
            ++index;
            ++newlineCount;
        }
        if (ShouldForwardOriginalBlankSeparatorToPending(newlineCount)) {
            pendingTokens_.push_back({TokenKind::Newline, "\n"});
            pendingTokens_.push_back({TokenKind::Newline, "\n"});
            return;
        }
        if (ShouldForwardStandaloneLineCommentToPending(tokens, index, newlineCount)) {
            pendingTokens_.push_back({TokenKind::Newline, "\n"});
            if (newlineCount > 1) {
                pendingTokens_.push_back({TokenKind::Newline, "\n"});
            }
            return;
        }
        if (ShouldPreserveOriginalBlankSeparator(tokens, index, newlineCount)) {
            EmitOriginalBlankSeparator();
        }
    }

    bool ShouldForwardOriginalBlankSeparatorToPending(size_t newlineCount) const {
        if (newlineCount <= 1 || pendingTokens_.empty()) {
            return false;
        }
        return IsInsidePendingLambdaBody();
    }

    bool ShouldForwardStandaloneLineCommentToPending(
        const std::vector<Token>& tokens,
        size_t newlineIndex,
        size_t newlineCount
    ) const {
        if (newlineCount == 0 || pendingTokens_.empty()) {
            return false;
        }
        return newlineIndex + 1 < tokens.size() && tokens[newlineIndex + 1].kind == TokenKind::LineComment;
    }

    bool IsInsidePendingLambdaBody() const {
        std::vector<size_t> openGroups;
        for (size_t index = 0; index < pendingTokens_.size(); ++index) {
            const Token& token = pendingTokens_[index];
            if (IsGroupOpen(token.text)) {
                openGroups.push_back(index);
            } else if (token.text == ")" || token.text == "]" || token.text == "}") {
                if (!openGroups.empty()) {
                    openGroups.pop_back();
                }
            }
        }
        return std::any_of(openGroups.begin(), openGroups.end(), [this](size_t index) {
            return pendingTokens_[index].text == "{" && IsLambdaBodyOpenToken(pendingTokens_, index);
        });
    }

    bool ShouldPreserveOriginalBlankSeparator(
        const std::vector<Token>& tokens,
        size_t newlineIndex,
        size_t newlineCount
    ) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, newlineIndex);
        const bool hasBlankSeparator =
            newlineCount > 1 || (previous && tokens[*previous].kind == TokenKind::Preprocessor);
        if (!hasBlankSeparator) {
            return false;
        }
        if (!pendingTokens_.empty() || !pendingPrefix_.empty() || groupDepth_ != 0) {
            return false;
        }
        const size_t next = NextSignificantIndex(tokens, newlineIndex + 1);
        if (next >= tokens.size() || tokens[next].text == "}") {
            return false;
        }
        if (outputLines_.empty() || outputLines_.back().empty()) {
            return false;
        }
        const std::string trimmed = tools::lint::Trim(outputLines_.back());
        if (trimmed.empty()) {
            return false;
        }
        if (trimmed.back() == '{' || trimmed.back() == ':') {
            return false;
        }
        return true;
    }

    void EmitOriginalBlankSeparator() {
        if (outputLines_.empty() || outputLines_.back().empty()) {
            return;
        }
        outputLines_.push_back({});
        allowOriginalBlank_ = false;
    }

    void EmitCodeToken(const std::vector<Token>& tokens, size_t& index) {
        const Token& token = tokens[index];
        EmitPendingPreprocessorBlankBefore(token);
        EmitPendingLogicalBlankBefore(token);
        if (token.text == "}") {
            if (groupDepth_ == 0 && !IsClosingCaseScopeBrace()) {
                CloseCaseBodyIfNeeded();
            }
            if (groupDepth_ > 0) {
                --groupDepth_;
                pendingTokens_.push_back(token);
            } else {
                EmitCloseBrace(tokens, index);
            }
            return;
        }
        if (token.text == "{") {
            if (groupDepth_ == 0 && pendingTokens_.empty() && justEmittedCaseLabel_) {
                EmitCaseScopeOpenBrace();
                return;
            }
            if (groupDepth_ == 0 && pendingTokens_.empty()) {
                justEmittedCaseLabel_ = false;
                EmitStandaloneBlockOpenBrace(tokens, index);
                return;
            }
            if (groupDepth_ == 0 && IsCodeBlockOpen()) {
                justEmittedCaseLabel_ = false;
                EmitOpenBrace(tokens, index);
            } else {
                justEmittedCaseLabel_ = false;
                ++groupDepth_;
                pendingTokens_.push_back(token);
            }
            return;
        }
        justEmittedCaseLabel_ = false;
        if (token.text == "(" || token.text == "[") {
            ++groupDepth_;
            pendingTokens_.push_back(token);
            return;
        }
        if (token.text == ")" || token.text == "]") {
            groupDepth_ = std::max(0, groupDepth_ - 1);
            pendingTokens_.push_back(token);
            return;
        }
        if (token.text == ";") {
            pendingTokens_.push_back(token);
            if (groupDepth_ == 0 && !NextTokenIsSameLineComment(tokens, index)) {
                const DeclarationKind declarationKind = ClassifySemicolonDeclaration(pendingTokens_);
                EmitPendingDeclaration(declarationKind, false);
            }
            return;
        }
        if (token.text == ":" && groupDepth_ == 0 && IsLabelColon()) {
            pendingTokens_.push_back(token);
            EmitLabel();
            return;
        }
        pendingTokens_.push_back(token);
    }

    void EmitStandaloneBlockOpenBrace(const std::vector<Token>& tokens, size_t& index) {
        const size_t closeIndex = NextSignificantIndex(tokens, index + 1);
        if (closeIndex < tokens.size() && tokens[closeIndex].text == "}") {
            EmitLine(Indent() + "{}");
            index = closeIndex;
            return;
        }
        EmitLine(Indent() + "{");
        blockStack_.push_back({
            BlockKind::Other,
            true,
            previousDeclarationKind_,
            previousDeclarationBreaksSiblingGroup_,
            caseBodyIndentLevel_
        });
        previousDeclarationKind_ = DeclarationKind::None;
        previousDeclarationBreaksSiblingGroup_ = false;
        ++indentLevel_;
    }

    bool NextTokenIsSameLineComment(const std::vector<Token>& tokens, size_t index) const {
        return index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LineComment;
    }

    void EmitOpenBrace(const std::vector<Token>& tokens, size_t& index) {
        const BlockKind blockKind = ClassifyBlock(pendingTokens_);
        const DeclarationKind declarationKind = DeclarationKindForBlock(blockKind);
        const size_t closeIndex = NextSignificantIndex(tokens, index + 1);
        if (closeIndex < tokens.size() && tokens[closeIndex].text == "}") {
            EmitBlankBeforeDeclarationKind(declarationKind, blockKind != BlockKind::NamespaceDeclaration);
            std::string suffix = " {}";
            const size_t afterClose = NextSignificantIndex(tokens, closeIndex + 1);
            if (afterClose < tokens.size() && tokens[afterClose].text == ";") {
                suffix += ";";
                index = afterClose;
            } else {
                index = closeIndex;
            }
            EmitFormatted(pendingTokens_, suffix);
            ClearPending();
            NoteDeclarationKind(declarationKind);
            if (
                blockKind == BlockKind::EnumDeclaration ||
                blockKind == BlockKind::TypeDeclaration ||
                blockKind == BlockKind::FunctionDefinition ||
                blockKind == BlockKind::NamespaceDeclaration
            ) {
                pendingLogicalBlank_ = true;
            }
            return;
        }
        EmitBlankBeforeDeclarationKind(declarationKind, true);
        EmitFormatted(pendingTokens_, " {");
        ClearPending();
        NoteDeclarationKind(declarationKind);
        const bool indentsBody = blockKind != BlockKind::NamespaceDeclaration;
        blockStack_.push_back({
            blockKind,
            indentsBody,
            previousDeclarationKind_,
            previousDeclarationBreaksSiblingGroup_,
            caseBodyIndentLevel_
        });
        if (blockKind == BlockKind::SwitchStatement) {
            caseBodyIndentLevel_ = -1;
        }
        previousDeclarationKind_ = DeclarationKind::None;
        previousDeclarationBreaksSiblingGroup_ = false;
        if (indentsBody) {
            ++indentLevel_;
        } else {
            pendingLogicalBlank_ = true;
        }
    }

    void EmitCloseBrace(const std::vector<Token>& tokens, size_t& index) {
        FlushPending();
        const BlockState closedBlock = PopBlockState();
        const bool closedCaseScope = closedBlock.kind == BlockKind::CaseScope;
        previousDeclarationKind_ = closedBlock.previousDeclarationKind;
        previousDeclarationBreaksSiblingGroup_ = closedBlock.previousDeclarationBreaksSiblingGroup;
        if (closedBlock.indentsBody) {
            indentLevel_ = std::max(0, indentLevel_ - 1);
        } else {
            EmitRequiredBlankLine();
        }
        if (closedBlock.kind == BlockKind::SwitchStatement) {
            caseBodyIndentLevel_ = closedBlock.previousCaseBodyIndentLevel;
        }
        const size_t next = NextSignificantIndex(tokens, index + 1);
        if (next < tokens.size() && tokens[next].text == ";") {
            EmitLine(Indent() + "};");
            index = next;
            if (closedBlock.kind == BlockKind::EnumDeclaration || closedBlock.kind == BlockKind::TypeDeclaration) {
                pendingLogicalBlank_ = true;
            }
            return;
        }
        if (IsTypeDeclarationTrailingDeclarator(closedBlock.kind, tokens, next)) {
            pendingPrefix_ = "} ";
            return;
        }
        if (next < tokens.size() && tokens[next].kind == TokenKind::Word && (
            tokens[next].text == "else" ||
            tokens[next].text == "catch" ||
            tokens[next].text == "finally" ||
            (tokens[next].text == "while" && closedBlock.kind == BlockKind::DoStatement)
        )) {
            pendingPrefix_ = "} ";
            return;
        }
        EmitLine(Indent() + "}");
        if (closedCaseScope) {
            indentLevel_ = caseBodyIndentLevel_;
            return;
        }
        if (closedBlock.kind == BlockKind::NamespaceDeclaration || closedBlock.kind == BlockKind::FunctionDefinition) {
            pendingLogicalBlank_ = true;
        }
    }

    void EmitLineComment(const std::vector<Token>& tokens, size_t& index) {
        Token comment{TokenKind::LineComment, TrimRight(TrimLeft(tokens[index].text))};
        if (
            pendingTokens_.empty() &&
            pendingPrefix_.empty() &&
            IsTrailingCommentAfterEmittedClose(tokens, index) &&
            !outputLines_.empty()
        ) {
            outputLines_.back() = TrimRight(std::move(outputLines_.back())) + "  " + comment.text;
            return;
        }
        EmitPendingPreprocessorBlankBefore(tokens[index]);
        EmitPendingLogicalBlankBefore(tokens[index]);
        if (pendingTokens_.empty() && pendingPrefix_.empty()) {
            EmitLine(Indent() + comment.text);
            return;
        }
        pendingTokens_.push_back(std::move(comment));
        if (groupDepth_ == 0 && HasTopLevelStatementTerminator(pendingTokens_)) {
            const DeclarationKind declarationKind = ClassifySemicolonDeclaration(pendingTokens_);
            EmitPendingDeclaration(declarationKind, false);
        }
    }

    void EmitBlockComment(std::string_view text) {
        EmitPendingPreprocessorBlankBefore({});
        EmitPendingLogicalBlankBefore({});
        if (!pendingTokens_.empty()) {
            pendingTokens_.push_back({TokenKind::BlockComment, std::string(text)});
            return;
        }
        if (text.find('\n') == std::string_view::npos) {
            pendingTokens_.push_back({TokenKind::BlockComment, std::string(text)});
            FlushPending();
            return;
        }
        std::vector<std::string> lines = tools::lint::SplitLines(text);
        for (std::string& line : lines) {
            EmitLine(Indent() + TrimRight(TrimLeft(line)));
        }
    }

    bool IsTrailingCommentAfterEmittedClose(const std::vector<Token>& tokens, size_t index) const {
        if (index == 0 || tokens[index - 1].kind == TokenKind::Newline) {
            return false;
        }
        const std::string& previous = tokens[index - 1].text;
        return previous == "}" || previous == ";";
    }

    void EmitPreprocessor(std::string_view text) {
        FlushPending();
        EmitPendingLogicalBlankBefore({});
        std::vector<std::string> lines = tools::lint::SplitLines(text);
        if (pendingUndefBlank_) {
            pendingUndefBlank_ = false;
            EmitRequiredBlankLine();
        }
        if (pendingPragmaOnceBlank_) {
            pendingPragmaOnceBlank_ = false;
            EmitRequiredBlankLine();
        }
        const bool isMacroDefinition = IsDefineDirective(lines);
        const bool isUndefDirective = IsUndefDirective(lines);
        if (isUndefDirective) {
            EmitRequiredBlankLine();
        }
        if (isMacroDefinition) {
            EmitBlankBeforeDeclarationKind(DeclarationKind::MacroDefinition, false);
        }
        if (ShouldFormatDefineContinuation(lines)) {
            EmitFormattedDefine(lines);
            NoteDeclarationKind(DeclarationKind::MacroDefinition);
            return;
        }
        for (size_t index = 0; index < lines.size(); ++index) {
            std::string line = TrimRight(TrimLeft(lines[index]));
            if (index > 0 && !line.empty()) {
                line = Spaces(config_.indentWidth) + line;
            }
            EmitLine(std::move(line));
        }
        if (isMacroDefinition) {
            NoteDeclarationKind(DeclarationKind::MacroDefinition);
        }
        if (IsPragmaOnceDirective(lines)) {
            pendingPragmaOnceBlank_ = true;
        }
        if (isUndefDirective) {
            pendingUndefBlank_ = true;
        }
        pendingPreprocessorBlank_ = true;
    }

    bool IsPragmaOnceDirective(const std::vector<std::string>& lines) const {
        return !lines.empty() && tools::lint::Trim(lines.front()) == "#pragma once";
    }

    bool IsDefineDirective(const std::vector<std::string>& lines) const {
        if (lines.empty()) {
            return false;
        }
        return tools::lint::StartsWith(TrimRight(TrimLeft(lines.front())), "#define ");
    }

    bool IsUndefDirective(const std::vector<std::string>& lines) const {
        if (lines.empty()) {
            return false;
        }
        const std::string line = TrimRight(TrimLeft(lines.front()));
        return line == "#undef" || tools::lint::StartsWith(line, "#undef ");
    }

    bool ShouldFormatDefineContinuation(const std::vector<std::string>& lines) const {
        return lines.size() > 1 && IsDefineDirective(lines);
    }

    void EmitFormattedDefine(const std::vector<std::string>& lines) {
        std::string defineLine = RemoveContinuationBackslash(TrimRight(TrimLeft(lines.front())));
        std::string replacement;
        for (size_t index = 1; index < lines.size(); ++index) {
            std::string part = RemoveContinuationBackslash(TrimRight(TrimLeft(lines[index])));
            if (part.empty()) {
                continue;
            }
            if (!replacement.empty()) {
                replacement.push_back(' ');
            }
            replacement += part;
        }
        if (replacement.empty()) {
            EmitLine(std::move(defineLine));
            pendingPreprocessorBlank_ = true;
            return;
        }
        std::vector<Token> replacementTokens = Tokenize(replacement);
        if (
            std::vector<std::vector<Token>> statements =
                SplitStatementLikeMacroReplacement(defineLine, replacementTokens);
            statements.size() > 1
        ) {
            EmitLine(defineLine + " \\");
            std::vector<std::string> statementLines;
            for (const std::vector<Token>& statement : statements) {
                std::vector<std::string> formattedStatement = FormatRange(statement, 1, {}, {});
                statementLines.insert(statementLines.end(), formattedStatement.begin(), formattedStatement.end());
            }
            EmitDefineReplacementLines(statementLines);
            pendingPreprocessorBlank_ = true;
            return;
        }
        if (std::optional<std::vector<std::string>> structuredLines = FormatStructuredMacroReplacement(
            replacementTokens
        )) {
            EmitLine(defineLine + " \\");
            EmitDefineReplacementLines(*structuredLines);
            pendingPreprocessorBlank_ = true;
            return;
        }
        const std::string normalizedReplacement = FormatInline(Tokenize(replacement));
        if (FitsRawLine(defineLine + " " + normalizedReplacement)) {
            EmitLine(defineLine + " " + normalizedReplacement);
            pendingPreprocessorBlank_ = true;
            return;
        }
        EmitLine(defineLine + " \\");
        std::vector<std::string> replacementLines = FormatRange(Tokenize(normalizedReplacement), 1, {}, {});
        EmitDefineReplacementLines(replacementLines);
        pendingPreprocessorBlank_ = true;
    }

    void EmitDefineReplacementLines(const std::vector<std::string>& replacementLines) {
        for (size_t index = 0; index < replacementLines.size(); ++index) {
            std::string line = replacementLines[index];
            if (index + 1 < replacementLines.size()) {
                line += " \\";
            }
            EmitLine(std::move(line));
        }
    }

    std::optional<std::vector<std::string>> FormatStructuredMacroReplacement(
        const std::vector<Token>& replacementTokens
    ) const {
        if (!IsStructuredMacroReplacement(replacementTokens)) {
            return std::nullopt;
        }
        PrettyFormatter formatter(config_, 1);
        std::vector<std::string> lines = tools::lint::SplitLines(formatter.Format(replacementTokens));
        lines.erase(std::remove(lines.begin(), lines.end(), std::string{}), lines.end());
        if (lines.empty()) {
            return std::nullopt;
        }
        return lines;
    }

    bool IsStructuredMacroReplacement(const std::vector<Token>& replacementTokens) const {
        return ContainsTopLevelToken(replacementTokens, ";") && (
            ContainsWord(replacementTokens, "class") ||
            ContainsWord(replacementTokens, "enum") ||
            ContainsWord(replacementTokens, "struct") ||
            ContainsWord(replacementTokens, "template")
        );
    }

    std::vector<std::vector<Token>> SplitStatementLikeMacroReplacement(
        std::string_view defineLine,
        const std::vector<Token>& replacementTokens
    ) const {
        std::vector<std::string> activeParameters = StatementLikeParametersForDefine(defineLine);
        if (activeParameters.empty()) {
            return {};
        }
        std::vector<std::vector<Token>> statements;
        size_t index = 0;
        while (index < replacementTokens.size()) {
            while (index < replacementTokens.size() && replacementTokens[index].kind == TokenKind::Newline) {
                ++index;
            }
            if (index >= replacementTokens.size()) {
                break;
            }
            if (!IsStatementLikeMacroInvocation(replacementTokens, index, activeParameters)) {
                return {};
            }
            const size_t open = NextSignificantIndex(replacementTokens, index + 1);
            const std::optional<size_t> close = FindMatchingClose(replacementTokens, open);
            if (!close) {
                return {};
            }
            statements.emplace_back(
                replacementTokens.begin() + static_cast<std::ptrdiff_t>(index),
                replacementTokens.begin() + static_cast<std::ptrdiff_t>(*close + 1)
            );
            index = *close + 1;
        }
        return statements;
    }

    std::vector<std::string> StatementLikeParametersForDefine(std::string_view defineLine) const {
        std::vector<std::string> defineParameters = ParseDefineParameters(defineLine);
        std::vector<std::string> activeParameters;
        for (const std::string& parameter : defineParameters) {
            if (IsConfiguredStatementLikeMacroParameter(parameter)) {
                activeParameters.push_back(parameter);
            }
        }
        return activeParameters;
    }

    std::vector<std::string> ParseDefineParameters(std::string_view defineLine) const {
        static constexpr std::string_view kDefinePrefix = "#define";
        std::string text = TrimLeft(defineLine);
        if (!tools::lint::StartsWith(text, kDefinePrefix)) {
            return {};
        }
        text.erase(0, kDefinePrefix.size());
        text = TrimLeft(text);
        size_t nameEnd = 0;
        while (nameEnd < text.size() && IsIdentifierBody(text[nameEnd])) {
            ++nameEnd;
        }
        if (nameEnd == 0 || nameEnd >= text.size() || text[nameEnd] != '(') {
            return {};
        }
        const size_t close = text.find(')', nameEnd + 1);
        if (close == std::string_view::npos) {
            return {};
        }
        std::vector<std::string> parameters;
        std::string_view parameterText = std::string_view(text).substr(nameEnd + 1, close - nameEnd - 1);
        while (!parameterText.empty()) {
            const size_t comma = parameterText.find(',');
            std::string parameter = std::string(TrimRight(TrimLeft(parameterText.substr(0, comma))));
            if (!parameter.empty()) {
                parameters.push_back(std::move(parameter));
            }
            if (comma == std::string_view::npos) {
                break;
            }
            parameterText.remove_prefix(comma + 1);
        }
        return parameters;
    }

    bool IsConfiguredStatementLikeMacroParameter(std::string_view parameter) const {
        return std::find(
            config_.statementLikeMacroParameters.begin(),
            config_.statementLikeMacroParameters.end(),
            parameter
        ) != config_.statementLikeMacroParameters.end();
    }

    bool IsStatementLikeMacroInvocation(
        const std::vector<Token>& tokens,
        size_t index,
        const std::vector<std::string>& activeParameters
    ) const {
        if (index >= tokens.size() || tokens[index].kind != TokenKind::Word) {
            return false;
        }
        if (std::find(activeParameters.begin(), activeParameters.end(), tokens[index].text) == activeParameters.end()) {
            return false;
        }
        const size_t open = NextSignificantIndex(tokens, index + 1);
        return open < tokens.size() && tokens[open].text == "(";
    }

    bool FitsRawLine(std::string_view text) const {
        return static_cast<int>(text.size()) <= config_.columnLimit;
    }

    std::string RemoveContinuationBackslash(std::string value) const {
        value = TrimRight(std::move(value));
        if (!value.empty() && value.back() == '\\') {
            value.pop_back();
            value = TrimRight(std::move(value));
        }
        return value;
    }

    void FlushPending() {
        if (pendingTokens_.empty()) {
            return;
        }
        if (IsInsideEnumDeclaration()) {
            EmitEnumEnumerators(pendingTokens_);
            ClearPending();
            return;
        }
        EmitFormatted(pendingTokens_, {});
        ClearPending();
    }

    void EmitPendingDeclaration(DeclarationKind declarationKind, bool separateSameKind) {
        if (pendingTokens_.empty()) {
            return;
        }
        std::vector<std::string> lines = FormatPendingLines({});
        const bool fieldBreaksSiblingGroup =
            declarationKind == DeclarationKind::Field && lines.size() > 1 && FieldDeclarationBreaksSiblingGroup(lines);
        EmitBlankBeforeDeclarationKind(declarationKind, separateSameKind, fieldBreaksSiblingGroup);
        for (std::string& line : lines) {
            EmitLine(std::move(line));
        }
        ClearPending();
        NoteDeclarationKind(declarationKind, fieldBreaksSiblingGroup);
    }

    bool FieldDeclarationBreaksSiblingGroup(const std::vector<std::string>& lines) const {
        return FieldHasSameIndentDelimiterClose(lines);
    }

    bool FieldHasSameIndentDelimiterClose(const std::vector<std::string>& lines) const {
        if (lines.size() < 3) {
            return false;
        }
        const std::string declarationIndent = LeadingWhitespace(lines.front());
        for (size_t index = 1; index < lines.size(); ++index) {
            if (tools::lint::Trim(lines[index]).empty()) {
                continue;
            }
            if (LeadingWhitespace(lines[index]) != declarationIndent) {
                continue;
            }
            const std::string trimmed = TrimLeft(lines[index]);
            if (trimmed.empty()) {
                continue;
            }
            if (trimmed.front() == '}' || trimmed.front() == ')' || trimmed.front() == ']') {
                return true;
            }
        }
        return false;
    }

    std::string LeadingWhitespace(std::string_view value) const {
        size_t end = 0;
        while (end < value.size() && IsSpaceButNotNewline(value[end])) {
            ++end;
        }
        return std::string(value.substr(0, end));
    }

    bool IsInsideEnumDeclaration() const {
        return !blockStack_.empty() && blockStack_.back().kind == BlockKind::EnumDeclaration;
    }

    void EmitEnumEnumerators(const std::vector<Token>& tokens) {
        std::vector<std::vector<Token>> elements = SplitTopLevel(tokens, ',');
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(tokens, ',')) {
            EmitFormatted(tokens, ",");
            return;
        }
        bool emittedElement = false;
        for (size_t index = 0; index < elements.size(); ++index) {
            std::vector<std::string> elementLines =
                FormatDelimitedElement(elements[index], indentLevel_, ",", false, true, false, emittedElement);
            if (elementLines.empty()) {
                continue;
            }
            for (std::string& line : elementLines) {
                EmitLine(std::move(line));
            }
            emittedElement = true;
        }
    }

    void EmitFormatted(const std::vector<Token>& tokens, std::string_view suffix) {
        EmitFormattedAtIndent(tokens, indentLevel_, suffix);
    }

    void EmitFormattedAtIndent(const std::vector<Token>& tokens, int indentLevel, std::string_view suffix) {
        for (std::string& line : FormatRange(tokens, indentLevel, pendingPrefix_, std::string(suffix))) {
            EmitLine(std::move(line));
        }
    }

    std::vector<std::string> FormatPendingLines(std::string_view suffix) const {
        return FormatRange(pendingTokens_, indentLevel_, pendingPrefix_, std::string(suffix));
    }

    void EmitLabel() {
        if (IsCaseOrDefaultLabel()) {
            CloseCaseBodyIfNeeded();
            EmitFormattedAtIndent(pendingTokens_, indentLevel_, {});
            ClearPending();
            ++indentLevel_;
            caseBodyIndentLevel_ = indentLevel_;
            justEmittedCaseLabel_ = true;
            return;
        }
        const int labelIndent = IsAccessSpecifierLabel() ? std::max(0, indentLevel_ - 1) : indentLevel_;
        EmitFormattedAtIndent(pendingTokens_, labelIndent, {});
        if (IsAccessSpecifierLabel()) {
            previousDeclarationKind_ = DeclarationKind::None;
            previousDeclarationBreaksSiblingGroup_ = false;
        }
        ClearPending();
    }

    void EmitCaseScopeOpenBrace() {
        if (!outputLines_.empty()) {
            outputLines_.back() = TrimRight(std::move(outputLines_.back())) + " {";
        }
        blockStack_.push_back({
            BlockKind::CaseScope,
            true,
            previousDeclarationKind_,
            previousDeclarationBreaksSiblingGroup_,
            caseBodyIndentLevel_
        });
        previousDeclarationKind_ = DeclarationKind::None;
        previousDeclarationBreaksSiblingGroup_ = false;
        justEmittedCaseLabel_ = false;
    }

    bool IsClosingCaseScopeBrace() const {
        return !blockStack_.empty() && blockStack_.back().kind == BlockKind::CaseScope;
    }

    void CloseCaseBodyIfNeeded() {
        if (caseBodyIndentLevel_ >= 0 && indentLevel_ == caseBodyIndentLevel_) {
            indentLevel_ = caseBodyIndentLevel_ - 1;
            caseBodyIndentLevel_ = -1;
        }
    }

    void EmitPendingLogicalBlankBefore(const Token& token) {
        if (!pendingLogicalBlank_) {
            return;
        }
        pendingLogicalBlank_ = false;
        if (token.text == "}") {
            return;
        }
        if (!outputLines_.empty() && !outputLines_.back().empty()) {
            EmitRequiredBlankLine();
        }
    }

    void EmitPendingPreprocessorBlankBefore(const Token& token) {
        const bool requiresUndefBlank = pendingUndefBlank_;
        if (!pendingPreprocessorBlank_ && !requiresUndefBlank) {
            return;
        }
        pendingPreprocessorBlank_ = false;
        pendingUndefBlank_ = false;
        if (token.text == "}" && !requiresUndefBlank) {
            return;
        }
        if (!outputLines_.empty() && !outputLines_.back().empty()) {
            EmitRequiredBlankLine();
        }
    }

    void EmitBlankBeforeDeclarationKind(
        DeclarationKind declarationKind,
        bool separateSameKind,
        bool currentDeclarationBreaksSiblingGroup = false
    ) {
        if (declarationKind == DeclarationKind::None || !IsDeclarationContext()) {
            return;
        }
        if (previousDeclarationKind_ == DeclarationKind::None) {
            return;
        }
        if (
            previousDeclarationKind_ == declarationKind &&
            !separateSameKind &&
            !previousDeclarationBreaksSiblingGroup_ &&
            !currentDeclarationBreaksSiblingGroup
        ) {
            return;
        }
        EmitBlankBeforeSiblingGroupIfNeeded();
    }

    void NoteDeclarationKind(DeclarationKind declarationKind, bool breaksSiblingGroup = false) {
        if (declarationKind == DeclarationKind::None || !IsDeclarationContext()) {
            return;
        }
        previousDeclarationKind_ = declarationKind;
        previousDeclarationBreaksSiblingGroup_ = declarationKind == DeclarationKind::Field && breaksSiblingGroup;
    }

    void EmitBlankBeforeSiblingGroupIfNeeded() {
        if (outputLines_.empty() || outputLines_.back().empty()) {
            return;
        }
        const std::string trimmed = tools::lint::Trim(outputLines_.back());
        if (!trimmed.empty() && trimmed.back() == '{') {
            return;
        }
        if (tools::lint::StartsWith(trimmed, "//") || tools::lint::StartsWith(trimmed, "/*")) {
            return;
        }
        outputLines_.push_back({});
        allowOriginalBlank_ = false;
    }

    void EmitRequiredBlankLine() {
        if (outputLines_.empty() || outputLines_.back().empty()) {
            return;
        }
        outputLines_.push_back({});
        allowOriginalBlank_ = false;
    }

    void ClearPending() {
        pendingTokens_.clear();
        pendingPrefix_.clear();
        groupDepth_ = 0;
    }

    struct GroupPair {
        size_t open = 0;
        size_t close = 0;
    };

    enum class ChainKind {
        None,
        Ternary,
        Logical,
        Bitwise,
        Equality,
        Relational,
        Shift,
        Additive,
        Multiplicative,
    };

    enum class LayoutNodeKind {
        TemplateDeclaration,
        Assignment,
        DeclarationValue,
        Lambda,
        ConstructorInitializer,
        OperatorChain,
        StringLiteralSequence,
        Group,
    };

    struct LayoutContext {
        int indentLevel = 0;
        std::string prefix;
        std::string suffix;
        bool indentSplitChains = false;
        bool indentLogicalSplitChains = false;
    };

    struct LayoutNode {
        LayoutNodeKind kind = LayoutNodeKind::Group;
        size_t index = 0;
        GroupPair group{};
        int depth = 0;
        size_t order = 0;
    };

    struct LayoutTree {
        std::vector<LayoutNode> breakNodes;
    };

    struct LayoutResult {
        std::vector<std::string> lines;
        bool fits = false;
        int maxOverflow = 0;
        size_t lineCount = 0;
        int deepestRenderedIndent = 0;
        int deepestBreakDepth = -1;
        size_t order = 0;
        bool compact = false;
    };

    struct TernaryExpressionParts {
        std::vector<Token> condition;
        std::vector<Token> trueBranch;
        std::vector<Token> falseBranch;
    };

    std::vector<std::string> FormatRange(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains = false,
        bool indentLogicalSplitChains = false
    ) const {
        return FormatRangeResult(
            tokens,
            indentLevel,
            std::move(prefix),
            std::move(suffix),
            indentSplitChains,
            indentLogicalSplitChains
        ).lines;
    }

    LayoutResult FormatRangeResult(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains = false,
        bool indentLogicalSplitChains = false
    ) const {
        if (tokens.empty()) {
            if (!prefix.empty() || !suffix.empty()) {
                return ScoreLayoutCandidate({Indent(indentLevel) + prefix + suffix}, true, 0, 0);
            }
            return ScoreLayoutCandidate({}, true, 0, 0);
        }
        const LayoutContext
            context{indentLevel, std::move(prefix), std::move(suffix), indentSplitChains, indentLogicalSplitChains};
        return FormatLayoutTreeResult(tokens, context);
    }

    std::vector<std::string> FormatLayoutTree(const std::vector<Token>& tokens, const LayoutContext& context) const {
        return FormatLayoutTreeResult(tokens, context).lines;
    }

    LayoutResult FormatLayoutTreeResult(const std::vector<Token>& tokens, const LayoutContext& context) const {
        const std::string cacheKey = LayoutCacheKey(tokens, context);
        if (const auto cached = layoutCache_.find(cacheKey); cached != layoutCache_.end()) {
            return cached->second;
        }
        LayoutResult best;
        bool hasBest = false;
        const bool compactAllowed = !HasOriginalBlankSeparator(tokens) &&
            !ShouldForceSplit(tokens) &&
            !IsTemplateDeclarationPrefix(tokens) &&
            !IsRequiresClausePrefix(tokens);
        if (compactAllowed) {
            std::string inlineText = context.prefix +
                FormatInline(tokens, InlineBudget(context.indentLevel, context.prefix, context.suffix));
            AppendSuffix(inlineText, context.suffix);
            best = ScoreLayoutCandidate(
                {Indent(context.indentLevel) + inlineText},
                true,
                static_cast<int>(tokens.size()) + 1,
                0
            );
            hasBest = true;
        }
        LayoutTree tree = BuildLayoutTree(tokens);
        for (const LayoutNode& node : tree.breakNodes) {
            std::optional<LayoutResult> candidate = FormatLayoutNodeResult(tokens, context, node);
            if (!candidate) {
                continue;
            }
            OffsetBreakDepth(*candidate, node.depth);
            candidate->order = node.order;
            if (!hasBest || IsBetterLayout(*candidate, best)) {
                best = std::move(*candidate);
                hasBest = true;
            }
        }
        if (!hasBest) {
            std::string inlineText = context.prefix + FormatInline(tokens);
            AppendSuffix(inlineText, context.suffix);
            best = ScoreLayoutCandidate({Indent(context.indentLevel) + inlineText}, true, 0, 0);
        }
        layoutCache_[cacheKey] = best;
        return best;
    }

    LayoutTree BuildLayoutTree(const std::vector<Token>& tokens) const {
        LayoutTree tree;
        size_t order = 0;
        const auto addNode = [&](LayoutNode node) {
            for (const LayoutNode& existing : tree.breakNodes) {
                if (
                    existing.kind == node.kind &&
                    existing.index == node.index &&
                    existing.group.open == node.group.open &&
                    existing.group.close == node.group.close
                ) {
                    return;
                }
            }
            node.order = order++;
            tree.breakNodes.push_back(node);
        };
        if (IsTemplateDeclarationPrefix(tokens)) {
            addNode({LayoutNodeKind::TemplateDeclaration, 0, {}, 0, 0});
            return tree;
        }
        if (IsRequiresClausePrefix(tokens)) {
            addNode({LayoutNodeKind::TemplateDeclaration, 0, {}, 0, 0});
            return tree;
        }
        if (std::optional<size_t> topLevelLambdaBody = FindTopLevelLambdaBodyOpen(tokens)) {
            addNode({LayoutNodeKind::Lambda, *topLevelLambdaBody, {}, 0, 0});
            return tree;
        }
        if (std::optional<GroupPair> group = FindFirstWrappableGroupPairWithTopLevelSeparatorAndLambda(tokens, ',')) {
            addNode({LayoutNodeKind::Group, 0, *group, 0, 0});
        }
        if (std::optional<size_t> initializerColon = FindConstructorInitializerColon(tokens)) {
            addNode({LayoutNodeKind::ConstructorInitializer, *initializerColon, {}, 0, 0});
        }
        if (std::optional<GroupPair> controlHeader = FindLeadingControlHeaderGroup(tokens)) {
            addNode({LayoutNodeKind::Group, 0, *controlHeader, 0, 0});
        }
        if (
            std::optional<size_t> assignment = FindTopLevelAssignment(tokens);
            assignment && !IsDefaultedDeletedOrPureVirtualMethodDeclaration(tokens)
        ) {
            addNode({LayoutNodeKind::Assignment, *assignment, {}, 0, 0});
            CollectLayoutGroupNodes(tokens, 0, *assignment, 0, tree.breakNodes, order);
            return tree;
        }
        const ChainKind chainKind = SelectChainKind(tokens);
        if (chainKind != ChainKind::None) {
            addNode({LayoutNodeKind::OperatorChain, 0, {}, 0, 0});
            if (chainKind == ChainKind::Shift || IsTrueOperatorChain(tokens, chainKind)) {
                return tree;
            }
        }
        if (IsStringLiteralSequence(tokens)) {
            addNode({LayoutNodeKind::StringLiteralSequence, 0, {}, 0, 0});
            return tree;
        }
        if (std::optional<GroupPair> initializerList = FindTopLevelInitializerListDeclarationGroup(tokens)) {
            if (std::optional<size_t> declarator = InitializerListDeclarationDeclarator(tokens, *initializerList)) {
                addNode({LayoutNodeKind::DeclarationValue, *declarator, {}, 0, 0});
            }
            addNode({LayoutNodeKind::Group, 0, *initializerList, 0, 0});
        }
        if (std::optional<size_t> nestedLambdaBody = FindLambdaBodyOpen(tokens)) {
            addNode({LayoutNodeKind::Lambda, *nestedLambdaBody, {}, 1, 0});
            return tree;
        }
        CollectLayoutGroupNodes(tokens, 0, tokens.size(), chainKind == ChainKind::None ? 0 : 1, tree.breakNodes, order);
        return tree;
    }

    void CollectLayoutGroupNodes(
        const std::vector<Token>& tokens,
        size_t begin,
        size_t end,
        int depth,
        std::vector<LayoutNode>& nodes,
        size_t& order
    ) const {
        for (size_t index = begin; index < end; ++index) {
            if (!IsWrappableGroupOpen(tokens, index)) {
                continue;
            }
            const std::optional<size_t> close = FindWrappableGroupClose(tokens, index);
            if (!close || *close >= end) {
                continue;
            }
            const bool wrappable = !IsEmptyGroupPair(tokens, index, *close) &&
                !IsNonWrappablePrefixGroup(tokens, index, *close) &&
                !IsFunctionPointerDeclaratorGroupOpen(tokens, index);
            if (wrappable) {
                nodes.push_back({LayoutNodeKind::Group, 0, GroupPair{index, *close}, depth, order++});
            }
            if (!GroupOwnsNestedBreaks(tokens, index, *close)) {
                CollectLayoutGroupNodes(tokens, index + 1, *close, depth + 1, nodes, order);
            }
            index = *close;
        }
    }

    bool GroupOwnsNestedBreaks(const std::vector<Token>& tokens, size_t open, size_t close) const {
        if (open >= tokens.size() || close > tokens.size()) {
            return false;
        }
        if (IsLambdaBodyOpenToken(tokens, open)) {
            return true;
        }
        if (tokens[open].text == "{") {
            return true;
        }
        std::vector<Token> inner(
            tokens.begin() + static_cast<std::ptrdiff_t>(open + 1),
            tokens.begin() + static_cast<std::ptrdiff_t>(close)
        );
        if (HasNestedWrappableGroupWithNonClosingTail(tokens, open + 1, close)) {
            return true;
        }
        return ContainsTopLevelSeparator(inner, ',') || ContainsTopLevelSeparator(inner, ';');
    }

    bool HasNestedWrappableGroupWithNonClosingTail(const std::vector<Token>& tokens, size_t begin, size_t end) const {
        for (size_t index = begin; index < end; ++index) {
            if (!IsWrappableGroupOpen(tokens, index)) {
                continue;
            }
            const std::optional<size_t> close = FindWrappableGroupClose(tokens, index);
            if (!close || *close >= end) {
                continue;
            }
            if (
                !IsEmptyGroupPair(tokens, index, *close) &&
                !IsNonWrappablePrefixGroup(tokens, index, *close) &&
                !IsFunctionPointerDeclaratorGroupOpen(tokens, index) &&
                HasNonClosingTokenBefore(tokens, *close + 1, end)
            ) {
                return true;
            }
            index = *close;
        }
        return false;
    }

    bool HasNonClosingTokenBefore(const std::vector<Token>& tokens, size_t begin, size_t end) const {
        for (size_t index = begin; index < end; ++index) {
            if (tokens[index].kind == TokenKind::Newline) {
                continue;
            }
            if (!IsStackedClosingToken(tokens, index)) {
                return true;
            }
        }
        return false;
    }

    bool IsStackedClosingToken(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size()) {
            return false;
        }
        return tokens[index].text == ")" ||
            tokens[index].text == "]" ||
            tokens[index].text == "}" ||
            IsTemplateAngleCloseToken(tokens, index);
    }

    std::optional<GroupPair> FindLeadingControlHeaderGroup(const std::vector<Token>& tokens) const {
        const size_t keyword = NextSignificantIndex(tokens, 0);
        if (keyword >= tokens.size() || !IsControlKeyword(tokens[keyword].text)) {
            return std::nullopt;
        }
        const size_t open = NextSignificantIndex(tokens, keyword + 1);
        if (open >= tokens.size() || tokens[open].text != "(") {
            return std::nullopt;
        }
        const std::optional<size_t> close = FindMatchingClose(tokens, open);
        if (!close || IsEmptyGroupPair(tokens, open, *close)) {
            return std::nullopt;
        }
        return GroupPair{open, *close};
    }

    std::optional<std::vector<std::string>> FormatLayoutNode(
        const std::vector<Token>& tokens,
        const LayoutContext& context,
        const LayoutNode& node
    ) const {
        std::optional<LayoutResult> result = FormatLayoutNodeResult(tokens, context, node);
        if (!result) {
            return std::nullopt;
        }
        return std::move(result->lines);
    }

    std::optional<LayoutResult> FormatLayoutNodeResult(
        const std::vector<Token>& tokens,
        const LayoutContext& context,
        const LayoutNode& node
    ) const {
        switch (node.kind) {
            case LayoutNodeKind::TemplateDeclaration:
                if (!IsRequiresClausePrefix(tokens) && !IsTemplateDeclarationPrefix(tokens)) {
                    return std::nullopt;
                }
                return ScoreLayoutCandidate(
                    IsRequiresClausePrefix(tokens) ?
                        FormatRequiresClause(tokens, context.indentLevel, context.prefix, context.suffix) :
                        FormatTemplateDeclaration(tokens, context.indentLevel, context.prefix, context.suffix),
                    false,
                    0,
                    0
                );
            case LayoutNodeKind::Assignment:
                return FormatAssignmentResult(
                    tokens,
                    node.index,
                    context.indentLevel,
                    context.prefix,
                    context.suffix,
                    context.indentSplitChains,
                    context.indentLogicalSplitChains
                );
            case LayoutNodeKind::DeclarationValue:
                return FormatDeclarationValueBreak(
                    tokens,
                    node.index,
                    context.indentLevel,
                    context.prefix,
                    context.suffix
                );
            case LayoutNodeKind::Lambda:
                return ScoreLayoutCandidate(
                    FormatSplitLambda(tokens, node.index, context.indentLevel, context.prefix, context.suffix),
                    false,
                    0,
                    0
                );
            case LayoutNodeKind::ConstructorInitializer:
                return ScoreLayoutCandidate(
                    FormatConstructorInitializerList(
                        tokens,
                        node.index,
                        context.indentLevel,
                        context.prefix,
                        context.suffix
                    ),
                    false,
                    0,
                    0
                );
            case LayoutNodeKind::OperatorChain:
                return FormatOperatorChainResult(
                    tokens,
                    context.indentLevel,
                    context.prefix,
                    context.suffix,
                    context.indentSplitChains,
                    context.indentLogicalSplitChains
                );
            case LayoutNodeKind::StringLiteralSequence:
                return ScoreLayoutCandidate(
                    FormatStringLiteralSequence(tokens, context.indentLevel, context.prefix, context.suffix),
                    false,
                    0,
                    0
                );
            case LayoutNodeKind::Group:
                return FormatSplitGroupResult(tokens, node.group, context.indentLevel, context.prefix, context.suffix);
        }
        return std::nullopt;
    }

    LayoutResult ScoreLayoutCandidate(
        std::vector<std::string> lines,
        bool compact,
        int breakDepth,
        size_t order
    ) const {
        int maxOverflow = 0;
        for (const std::string& line : lines) {
            maxOverflow = std::max(maxOverflow, static_cast<int>(line.size()) - config_.columnLimit);
        }
        maxOverflow = std::max(0, maxOverflow);
        const size_t lineCount = lines.size();
        int deepestRenderedIndent = 0;
        for (const std::string& line : lines) {
            deepestRenderedIndent = std::max(deepestRenderedIndent, LeadingIndentLevel(line));
        }
        const int deepestBreakDepth = compact ? -1 : breakDepth;
        return LayoutResult{
            std::move(lines),
            maxOverflow == 0,
            maxOverflow,
            lineCount,
            deepestRenderedIndent,
            deepestBreakDepth,
            order,
            compact
        };
    }

    bool IsBetterLayout(const LayoutResult& candidate, const LayoutResult& current) const {
        if (candidate.fits != current.fits) {
            return candidate.fits;
        }
        if (!candidate.fits && candidate.maxOverflow != current.maxOverflow) {
            return candidate.maxOverflow < current.maxOverflow;
        }
        if (candidate.lineCount != current.lineCount) {
            return candidate.lineCount < current.lineCount;
        }
        if (candidate.deepestRenderedIndent != current.deepestRenderedIndent) {
            return candidate.deepestRenderedIndent < current.deepestRenderedIndent;
        }
        if (candidate.deepestBreakDepth != current.deepestBreakDepth) {
            return candidate.deepestBreakDepth < current.deepestBreakDepth;
        }
        if (candidate.order != current.order) {
            return candidate.order < current.order;
        }
        return candidate.compact && !current.compact;
    }

    void MergeBreakDepth(LayoutResult& target, const LayoutResult& nested, int depthOffset) const {
        if (nested.deepestBreakDepth < 0) {
            return;
        }
        target.deepestBreakDepth = std::max(target.deepestBreakDepth, nested.deepestBreakDepth + depthOffset);
        target.compact = false;
    }

    void OffsetBreakDepth(LayoutResult& result, int depthOffset) const {
        if (result.deepestBreakDepth >= 0) {
            result.deepestBreakDepth += depthOffset;
        }
    }

    int LeadingIndentLevel(std::string_view line) const {
        size_t spaces = 0;
        while (spaces < line.size() && line[spaces] == ' ') {
            ++spaces;
        }
        return static_cast<int>(spaces / static_cast<size_t>(config_.indentWidth));
    }

    std::string LayoutCacheKey(const std::vector<Token>& tokens, const LayoutContext& context) const {
        std::string key = std::to_string(context.indentLevel) +
            "|" +
            context.prefix +
            "|" +
            context.suffix +
            "|" +
            (context.indentSplitChains ? "1" : "0") +
            "|" +
            (context.indentLogicalSplitChains ? "1" : "0");
        for (const Token& token : tokens) {
            key.push_back('\x1f');
            key += std::to_string(static_cast<int>(token.kind));
            key.push_back(':');
            key += token.text;
        }
        return key;
    }

    bool IsTemplateDeclarationPrefix(const std::vector<Token>& tokens) const {
        if (tokens.empty() || tokens.front().text != "template") {
            return false;
        }
        const size_t open = NextSignificantIndex(tokens, 1);
        return open < tokens.size() && tokens[open].text == "<" && IsTemplateAngleOpen(tokens, open);
    }

    std::vector<std::string> FormatTemplateDeclaration(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        const size_t open = NextSignificantIndex(tokens, 1);
        const std::optional<size_t> close = FindTemplateAngleClose(tokens, open);
        if (!close) {
            std::string inlineText = prefix + FormatInline(tokens);
            AppendSuffix(inlineText, suffix);
            return {Indent(indentLevel) + inlineText};
        }
        std::vector<Token> templatePrefix(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(*close + 1));
        std::vector<Token> declaration(tokens.begin() + static_cast<std::ptrdiff_t>(*close + 1), tokens.end());
        std::vector<std::string> lines;
        std::string templatePrefixText = prefix + FormatInline(templatePrefix);
        if (Fits(indentLevel, templatePrefixText)) {
            lines.push_back(Indent(indentLevel) + templatePrefixText);
        } else {
            lines = FormatSplitGroup(templatePrefix, GroupPair{open, *close}, indentLevel, std::move(prefix), {});
        }
        if (declaration.empty()) {
            if (!suffix.empty()) {
                AppendSuffix(lines.back(), suffix);
            }
            return lines;
        }
        std::vector<std::string> declarationLines;
        if (std::optional<RequiresClauseParts> requiresClause = SplitRequiresClause(declaration)) {
            const std::string inlineRequiresClause = FormatInlineRequiresClause(requiresClause->condition);
            if (lines.size() == 1 && Fits(indentLevel, templatePrefixText + " " + inlineRequiresClause)) {
                lines.back() = Indent(indentLevel) + templatePrefixText + " " + inlineRequiresClause;
                declarationLines = FormatRange(requiresClause->declaration, indentLevel, {}, std::move(suffix));
            } else {
                declarationLines =
                    FormatRequiresClause(declaration, indentLevel + 1, {}, std::move(suffix), indentLevel);
            }
        } else {
            declarationLines = FormatRange(declaration, indentLevel, {}, std::move(suffix));
        }
        lines.insert(lines.end(), declarationLines.begin(), declarationLines.end());
        return lines;
    }

    bool IsRequiresClausePrefix(const std::vector<Token>& tokens) const {
        return SplitRequiresClause(tokens).has_value();
    }

    std::optional<RequiresClauseParts> SplitRequiresClause(const std::vector<Token>& tokens) const {
        const size_t first = NextSignificantIndex(tokens, 0);
        if (first >= tokens.size() || tokens[first].text != "requires") {
            return std::nullopt;
        }
        const size_t open = NextSignificantIndex(tokens, first + 1);
        if (open >= tokens.size() || tokens[open].text != "(") {
            return std::nullopt;
        }
        const std::optional<size_t> close = FindMatchingClose(tokens, open);
        if (!close) {
            return std::nullopt;
        }
        const size_t afterClose = NextSignificantIndex(tokens, *close + 1);
        if (afterClose < tokens.size() && tokens[afterClose].text == "{") {
            return std::nullopt;
        }
        return RequiresClauseParts{
            std::vector<Token>(
                tokens.begin() + static_cast<std::ptrdiff_t>(open + 1),
                tokens.begin() + static_cast<std::ptrdiff_t>(*close)
            ),
            std::vector<Token>(tokens.begin() + static_cast<std::ptrdiff_t>(*close + 1), tokens.end())
        };
    }

    std::string FormatInlineRequiresClause(const std::vector<Token>& condition) const {
        return "requires(" + FormatInline(condition) + ")";
    }

    std::vector<std::string> FormatRequiresClause(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        std::optional<int> declarationIndentLevel = std::nullopt
    ) const {
        const std::optional<RequiresClauseParts> parts = SplitRequiresClause(tokens);
        if (!parts) {
            std::string inlineText = prefix + FormatInline(tokens);
            AppendSuffix(inlineText, suffix);
            return {Indent(indentLevel) + inlineText};
        }
        std::vector<std::string> lines;
        std::string requiresLine = prefix + FormatInlineRequiresClause(parts->condition);
        if (Fits(indentLevel, requiresLine)) {
            lines.push_back(Indent(indentLevel) + std::move(requiresLine));
        } else {
            lines.push_back(Indent(indentLevel) + prefix + "requires(");
            std::vector<std::string> conditionLines = FormatRange(parts->condition, indentLevel + 1, {}, {}, true);
            lines.insert(lines.end(), conditionLines.begin(), conditionLines.end());
            lines.push_back(Indent(indentLevel) + ")");
        }
        if (parts->declaration.empty()) {
            if (!suffix.empty()) {
                AppendSuffix(lines.back(), suffix);
            }
            return lines;
        }
        const int trailingDeclarationIndentLevel = declarationIndentLevel.value_or(indentLevel);
        std::vector<std::string> declarationLines =
            FormatRange(parts->declaration, trailingDeclarationIndentLevel, {}, std::move(suffix));
        lines.insert(lines.end(), declarationLines.begin(), declarationLines.end());
        return lines;
    }

    std::vector<std::string> FormatAssignment(
        const std::vector<Token>& tokens,
        size_t assignment,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains,
        bool indentLogicalSplitChains
    ) const {
        return FormatAssignmentResult(
            tokens,
            assignment,
            indentLevel,
            std::move(prefix),
            std::move(suffix),
            indentSplitChains,
            indentLogicalSplitChains
        ).lines;
    }

    LayoutResult FormatAssignmentResult(
        const std::vector<Token>& tokens,
        size_t assignment,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains,
        bool indentLogicalSplitChains
    ) const {
        if (StartsWithInitializerList(tokens, assignment + 1)) {
            return ScoreLayoutCandidate(
                FormatInitializerAssignment(tokens, assignment, indentLevel, std::move(prefix), std::move(suffix)),
                false,
                0,
                0
            );
        }
        std::vector<std::string> lines;
        std::vector<Token> lhs(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(assignment + 1));
        std::vector<Token> rhs(tokens.begin() + static_cast<std::ptrdiff_t>(assignment + 1), tokens.end());
        lines.push_back(Indent(indentLevel) + prefix + FormatInline(lhs));
        LayoutResult rhsResult =
            FormatRangeResult(rhs, indentLevel + 1, {}, suffix, indentSplitChains, indentLogicalSplitChains);
        lines.insert(lines.end(), rhsResult.lines.begin(), rhsResult.lines.end());

        LayoutResult best = ScoreLayoutCandidate(std::move(lines), false, 0, 0);
        MergeBreakDepth(best, rhsResult, 1);
        std::string attachedPrefix = prefix + FormatInline(lhs) + " ";
        LayoutResult attachedRhs = FormatRangeResult(
            rhs,
            indentLevel,
            std::move(attachedPrefix),
            suffix,
            indentSplitChains,
            indentLogicalSplitChains
        );
        LayoutResult attached =
            ScoreLayoutCandidate(std::move(attachedRhs.lines), attachedRhs.deepestBreakDepth < 0, 0, 1);
        MergeBreakDepth(attached, attachedRhs, 1);
        if (IsBetterLayout(attached, best)) {
            best = std::move(attached);
        }

        std::vector<Token> lhsValue(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(assignment));
        if (!lhsValue.empty()) {
            LayoutResult lhsResult = FormatRangeResult(lhsValue, indentLevel, prefix, " =", true);
            if (!lhsResult.lines.empty()) {
                LayoutResult splitRhs =
                    FormatRangeResult(rhs, indentLevel + 1, {}, suffix, indentSplitChains, indentLogicalSplitChains);
                std::vector<std::string> lhsLines = std::move(lhsResult.lines);
                lhsLines.insert(lhsLines.end(), splitRhs.lines.begin(), splitRhs.lines.end());
                LayoutResult splitLhs = ScoreLayoutCandidate(std::move(lhsLines), false, 0, 2);
                MergeBreakDepth(splitLhs, lhsResult, 1);
                MergeBreakDepth(splitLhs, splitRhs, 1);
                if (IsBetterLayout(splitLhs, best)) {
                    best = std::move(splitLhs);
                }
            }
        }
        return best;
    }

    std::optional<LayoutResult> FormatDeclarationValueBreak(
        const std::vector<Token>& tokens,
        size_t declarator,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        if (declarator == 0 || declarator >= tokens.size()) {
            return std::nullopt;
        }
        std::vector<Token> typeTokens(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(declarator));
        std::vector<Token> valueTokens(tokens.begin() + static_cast<std::ptrdiff_t>(declarator), tokens.end());
        std::string typeLine = prefix + FormatInline(typeTokens);
        if (typeLine.empty()) {
            return std::nullopt;
        }

        LayoutResult valueResult = FormatRangeResult(valueTokens, indentLevel + 1, {}, std::move(suffix), true);
        std::vector<std::string> lines{Indent(indentLevel) + std::move(typeLine)};
        lines.insert(lines.end(), valueResult.lines.begin(), valueResult.lines.end());
        LayoutResult result = ScoreLayoutCandidate(std::move(lines), false, 0, 0);
        MergeBreakDepth(result, valueResult, 1);
        return result;
    }

    std::string FormatGroupOpeningLine(const std::vector<Token>& tokens, GroupPair group) const {
        if (group.open < tokens.size() && tokens[group.open].text == "<" && IsTemplateAngleOpen(tokens, group.open)) {
            std::vector<Token> beforeOpen(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(group.open));
            std::string beforeOpenText = FormatInline(beforeOpen);
            if (!beforeOpen.empty() && beforeOpen.back().text == "template") {
                beforeOpenText += " ";
            }
            return beforeOpenText + "<";
        }
        std::vector<Token> firstLineTokens(
            tokens.begin(),
            tokens.begin() + static_cast<std::ptrdiff_t>(group.open + 1)
        );
        return FormatInline(firstLineTokens);
    }

    std::vector<std::string> FormatInitializerAssignment(
        const std::vector<Token>& tokens,
        size_t assignment,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        const size_t open = NextSignificantIndex(tokens, assignment + 1);
        const std::optional<size_t> close = FindMatchingClose(tokens, open);
        if (!close) {
            std::string inlineText = prefix + FormatInline(tokens);
            AppendSuffix(inlineText, suffix);
            return {Indent(indentLevel) + inlineText};
        }
        return FormatSplitGroup(tokens, GroupPair{open, *close}, indentLevel, std::move(prefix), std::move(suffix));
    }

    std::vector<std::string> FormatSplitLambda(
        const std::vector<Token>& tokens,
        size_t bodyOpen,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        const std::optional<size_t> bodyClose = FindMatchingClose(tokens, bodyOpen);
        if (!bodyClose) {
            std::string inlineText = prefix + FormatInline(tokens);
            AppendSuffix(inlineText, suffix);
            return {Indent(indentLevel) + inlineText};
        }
        std::vector<Token> header(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(bodyOpen));
        std::vector<Token> body(
            tokens.begin() + static_cast<std::ptrdiff_t>(bodyOpen + 1),
            tokens.begin() + static_cast<std::ptrdiff_t>(*bodyClose)
        );
        std::vector<Token> after(tokens.begin() + static_cast<std::ptrdiff_t>(*bodyClose + 1), tokens.end());
        std::vector<std::string> lines = FormatLambdaHeaderWithLeadingTokens(header, indentLevel, std::move(prefix));
        PrettyFormatter bodyFormatter(config_, indentLevel + 1, true);
        std::vector<std::string> bodyLines = tools::lint::SplitLines(bodyFormatter.Format(body));
        while (!bodyLines.empty() && bodyLines.back().empty()) {
            bodyLines.pop_back();
        }
        lines.insert(lines.end(), bodyLines.begin(), bodyLines.end());
        std::string closeLine = "}" + FormatInline(after);
        AppendSuffix(closeLine, suffix);
        lines.push_back(Indent(indentLevel) + closeLine);
        return lines;
    }

    std::vector<std::string> FormatLambdaHeaderWithLeadingTokens(
        const std::vector<Token>& header,
        int indentLevel,
        std::string prefix
    ) const {
        const std::optional<size_t> captureOpen = FindLambdaCaptureOpenInHeader(header);
        if (!captureOpen || *captureOpen == 0) {
            return FormatLambdaHeader(header, indentLevel, std::move(prefix));
        }
        std::vector<Token> leading(header.begin(), header.begin() + static_cast<std::ptrdiff_t>(*captureOpen));
        std::vector<Token> lambdaHeader(header.begin() + static_cast<std::ptrdiff_t>(*captureOpen), header.end());
        prefix += FormatInline(leading);
        if (!prefix.empty() && prefix.back() != ' ' && prefix.back() != '(' && prefix.back() != '[') {
            prefix.push_back(' ');
        }
        return FormatLambdaHeader(lambdaHeader, indentLevel, std::move(prefix));
    }

    std::optional<size_t> FindLambdaCaptureOpenInHeader(const std::vector<Token>& header) const {
        for (size_t cursor = header.size(); cursor > 0; --cursor) {
            const size_t index = cursor - 1;
            if (header[index].text == "{" || header[index].text == "}" || header[index].text == ";") {
                return std::nullopt;
            }
            if (!IsLambdaIntroducerClose(header, index)) {
                continue;
            }
            const std::optional<size_t> open = FindMatchingOpen(header, index);
            if (open) {
                return open;
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> FormatLambdaHeader(
        const std::vector<Token>& header,
        int indentLevel,
        std::string prefix
    ) const {
        LayoutResult best;
        bool hasBest = false;
        size_t order = 0;
        const auto consider = [&](std::vector<std::string> lines, int breakDepth) {
            LayoutResult candidate = ScoreLayoutCandidate(std::move(lines), false, breakDepth, order++);
            if (!hasBest || IsBetterLayout(candidate, best)) {
                best = std::move(candidate);
                hasBest = true;
            }
        };

        const std::string inlineHeader = prefix + FormatInline(header) + " {";
        consider({Indent(indentLevel) + inlineHeader}, static_cast<int>(header.size()) + 1);
        if (
            std::optional<std::vector<std::string>> detachedParameters =
                FormatDetachedLambdaParameterLine(header, indentLevel, prefix)
        ) {
            consider(*detachedParameters, 0);
        }
        if (
            std::optional<std::vector<std::string>> splitParameters =
                FormatLambdaParameterGroup(header, indentLevel, prefix)
        ) {
            consider(*splitParameters, 0);
        }
        if (
            std::optional<std::vector<std::string>> splitCaptures =
                FormatLambdaCaptureGroup(header, indentLevel, prefix)
        ) {
            consider(*splitCaptures, 0);
        }
        return best.lines;
    }

    std::optional<std::vector<std::string>> FormatDetachedLambdaParameterLine(
        const std::vector<Token>& header,
        int indentLevel,
        std::string_view prefix
    ) const {
        if (header.empty() || header.front().text != "[") {
            return std::nullopt;
        }
        const std::optional<size_t> captureClose = FindMatchingClose(header, 0);
        if (!captureClose) {
            return std::nullopt;
        }
        const size_t restStart = NextSignificantIndex(header, *captureClose + 1);
        if (restStart >= header.size() || header[restStart].text != "(") {
            return std::nullopt;
        }
        std::vector<Token> captures(header.begin(), header.begin() + static_cast<std::ptrdiff_t>(*captureClose + 1));
        const std::string captureLine = std::string(prefix) + FormatInline(captures);
        const std::string restLine = FormatLambdaHeaderAfterCaptures(header, captures);
        return std::vector<std::string>{
            Indent(indentLevel) + captureLine,
            Indent(indentLevel + 1) + restLine,
            Indent(indentLevel) + "{"
        };
    }

    std::string FormatLambdaHeaderAfterCaptures(
        const std::vector<Token>& header,
        const std::vector<Token>& captures
    ) const {
        const std::string fullHeader = FormatInline(header);
        const std::string captureText = FormatInline(captures);
        if (fullHeader.size() >= captureText.size() && fullHeader.compare(0, captureText.size(), captureText) == 0) {
            return fullHeader.substr(captureText.size());
        }
        std::vector<Token> rest(header.begin() + static_cast<std::ptrdiff_t>(captures.size()), header.end());
        return FormatInline(rest);
    }

    std::optional<std::vector<std::string>> FormatLambdaParameterGroup(
        const std::vector<Token>& header,
        int indentLevel,
        std::string_view prefix
    ) const {
        const std::optional<GroupPair> group = FindLambdaParameterGroup(header);
        if (!group) {
            return std::nullopt;
        }
        std::vector<Token> inner(
            header.begin() + static_cast<std::ptrdiff_t>(group->open + 1),
            header.begin() + static_cast<std::ptrdiff_t>(group->close)
        );
        if (!ContainsTopLevelSeparator(inner, ',')) {
            return std::nullopt;
        }
        std::vector<Token> firstLineTokens(
            header.begin(),
            header.begin() + static_cast<std::ptrdiff_t>(group->open + 1)
        );
        const std::string firstLine = std::string(prefix) + FormatInline(firstLineTokens);
        std::vector<std::string> lines;
        lines.push_back(Indent(indentLevel) + firstLine);
        AppendLambdaParameterLines(lines, inner, *group, header, indentLevel);
        AppendLambdaBodyOpen(lines);
        return lines;
    }

    std::optional<std::vector<std::string>> FormatLambdaCaptureGroup(
        const std::vector<Token>& header,
        int indentLevel,
        std::string_view prefix
    ) const {
        if (header.empty() || header.front().text != "[") {
            return std::nullopt;
        }
        const std::optional<size_t> captureClose = FindMatchingClose(header, 0);
        if (!captureClose) {
            return std::nullopt;
        }
        std::vector<Token> captureInner(
            header.begin() + 1,
            header.begin() + static_cast<std::ptrdiff_t>(*captureClose)
        );
        if (!ContainsTopLevelSeparator(captureInner, ',')) {
            return std::nullopt;
        }
        std::vector<std::string> lines;
        lines.push_back(Indent(indentLevel) + std::string(prefix) + "[");
        std::vector<std::vector<Token>> captures = SplitTopLevel(captureInner, ',');
        for (size_t index = 0; index < captures.size(); ++index) {
            std::string elementSuffix;
            if (index + 1 < captures.size()) {
                elementSuffix = ",";
            }
            std::vector<std::string> captureLines =
                FormatRange(captures[index], indentLevel + 1, {}, std::move(elementSuffix), true);
            lines.insert(lines.end(), captureLines.begin(), captureLines.end());
        }
        const std::optional<GroupPair> parameterGroup = FindLambdaParameterGroup(header);
        if (parameterGroup) {
            std::vector<Token> rest(header.begin() + static_cast<std::ptrdiff_t>(*captureClose), header.end());
            const std::string restLine = FormatInline(rest);
            if (Fits(indentLevel, restLine)) {
                lines.push_back(Indent(indentLevel) + restLine + " {");
                return lines;
            }
            std::vector<Token> inner(
                header.begin() + static_cast<std::ptrdiff_t>(parameterGroup->open + 1),
                header.begin() + static_cast<std::ptrdiff_t>(parameterGroup->close)
            );
            if (ContainsTopLevelSeparator(inner, ',')) {
                std::vector<Token> firstLineTokens(
                    header.begin() + static_cast<std::ptrdiff_t>(*captureClose),
                    header.begin() + static_cast<std::ptrdiff_t>(parameterGroup->open + 1)
                );
                lines.push_back(Indent(indentLevel) + FormatInline(firstLineTokens));
                AppendLambdaParameterLines(lines, inner, *parameterGroup, header, indentLevel);
                AppendLambdaBodyOpen(lines);
                return lines;
            }
        }
        std::vector<Token> rest(header.begin() + static_cast<std::ptrdiff_t>(*captureClose), header.end());
        lines.push_back(Indent(indentLevel) + FormatInline(rest) + " {");
        return lines;
    }

    static void AppendLambdaBodyOpen(std::vector<std::string>& lines) {
        if (!lines.empty()) {
            lines.back() += " {";
        }
    }

    void AppendLambdaParameterLines(
        std::vector<std::string>& lines,
        const std::vector<Token>& inner,
        GroupPair group,
        const std::vector<Token>& header,
        int indentLevel
    ) const {
        std::vector<std::vector<Token>> elements = SplitTopLevel(inner, ',');
        for (size_t index = 0; index < elements.size(); ++index) {
            if (elements[index].empty()) {
                continue;
            }
            std::string elementSuffix;
            if (index + 1 < elements.size()) {
                elementSuffix = ",";
            }
            std::vector<std::string> elementLines =
                FormatRange(elements[index], indentLevel + 1, {}, elementSuffix, true);
            AppendSplitElementLines(lines, elementLines, false);
        }
        std::vector<Token> afterTokens(header.begin() + static_cast<std::ptrdiff_t>(group.close + 1), header.end());
        std::string closeLine = ")";
        std::string afterText = FormatInline(afterTokens);
        if (!afterText.empty()) {
            closeLine += " ";
            closeLine += afterText;
        }
        lines.push_back(Indent(indentLevel) + closeLine);
    }

    std::optional<GroupPair> FindLambdaParameterGroup(const std::vector<Token>& header) const {
        if (header.empty() || header.front().text != "[") {
            return std::nullopt;
        }
        const std::optional<size_t> captureClose = FindMatchingClose(header, 0);
        if (!captureClose) {
            return std::nullopt;
        }
        const size_t open = NextSignificantIndex(header, *captureClose + 1);
        if (open >= header.size() || header[open].text != "(") {
            return std::nullopt;
        }
        const std::optional<size_t> close = FindMatchingClose(header, open);
        if (!close || IsEmptyGroupPair(header, open, *close)) {
            return std::nullopt;
        }
        return GroupPair{open, *close};
    }

    std::vector<std::string> FormatConstructorInitializerList(
        const std::vector<Token>& tokens,
        size_t initializerColon,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        std::vector<Token> header(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(initializerColon));
        std::vector<Token> initializers(
            tokens.begin() + static_cast<std::ptrdiff_t>(initializerColon + 1),
            tokens.end()
        );
        std::vector<std::string> lines = FormatConstructorInitializerHeader(header, indentLevel, std::move(prefix));
        std::vector<std::vector<Token>> elements = SplitTopLevel(initializers, ',');
        const bool separateBodyOpen = suffix == " {";
        for (size_t index = 0; index < elements.size(); ++index) {
            if (elements[index].empty()) {
                continue;
            }
            std::string elementSuffix;
            if (index + 1 < elements.size()) {
                elementSuffix = ",";
            } else if (!separateBodyOpen) {
                elementSuffix = suffix;
            }
            std::vector<std::string> elementLines =
                FormatRange(elements[index], indentLevel + 1, {}, elementSuffix, true);
            lines.insert(lines.end(), elementLines.begin(), elementLines.end());
        }
        if (separateBodyOpen) {
            lines.push_back(Indent(indentLevel) + "{");
        }
        return lines;
    }

    std::vector<std::string> FormatConstructorInitializerHeader(
        const std::vector<Token>& header,
        int indentLevel,
        std::string prefix
    ) const {
        if (std::optional<GroupPair> group = FindFirstWrappableGroupPair(header)) {
            std::vector<Token> inner(
                header.begin() + static_cast<std::ptrdiff_t>(group->open + 1),
                header.begin() + static_cast<std::ptrdiff_t>(group->close)
            );
            if (header[group->open].text == "(" && ContainsTopLevelSeparator(inner, ',')) {
                return FormatSplitGroup(header, *group, indentLevel, std::move(prefix), " :");
            }
        }
        return FormatRange(header, indentLevel, std::move(prefix), " :");
    }

    std::vector<std::string> FormatSplitGroup(
        const std::vector<Token>& tokens,
        GroupPair group,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        return FormatSplitGroupResult(tokens, group, indentLevel, std::move(prefix), std::move(suffix)).lines;
    }

    LayoutResult FormatSplitGroupResult(
        const std::vector<Token>& tokens,
        GroupPair group,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        std::optional<LayoutResult> stacked =
            FormatStackedNestedGroupResult(tokens, group, indentLevel, prefix, suffix);
        std::vector<Token> firstLineTokens(
            tokens.begin(),
            tokens.begin() + static_cast<std::ptrdiff_t>(group.open + 1)
        );
        std::vector<Token> inner(
            tokens.begin() + static_cast<std::ptrdiff_t>(group.open + 1),
            tokens.begin() + static_cast<std::ptrdiff_t>(group.close)
        );
        std::vector<Token> suffixTokens(tokens.begin() + static_cast<std::ptrdiff_t>(group.close), tokens.end());
        std::vector<std::string> lines;
        lines.push_back(Indent(indentLevel) + prefix + FormatGroupOpeningLine(tokens, group));
        const bool startsWithControlFor = StartsWithControlFor(firstLineTokens);
        const bool splitForHeader =
            startsWithControlFor || (StartsWithControlHeader(firstLineTokens) && ContainsTopLevelSeparator(inner, ';'));
        const bool indentElementChains = startsWithControlFor || !StartsWithControlHeader(firstLineTokens);
        const char separator = splitForHeader ? ';' : ',';
        std::vector<std::vector<Token>> elements = SplitTopLevel(inner, separator);
        std::vector<LayoutResult> nestedResults;
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(inner, separator)) {
            const bool indentSingleExpressionChains =
                !IsPlainParenthesizedExpressionGroup(tokens, group, firstLineTokens);
            LayoutResult childResult = FormatRangeResult(inner, indentLevel + 1, {}, {}, indentSingleExpressionChains);
            lines.insert(lines.end(), childResult.lines.begin(), childResult.lines.end());
            nestedResults.push_back(std::move(childResult));
        } else {
            bool emittedElement = false;
            for (size_t index = 0; index < elements.size(); ++index) {
                std::string elementSuffix;
                if (index + 1 < elements.size()) {
                    elementSuffix = std::string(1, separator);
                }
                const bool isInitializerElement = tokens[group.open].text == "{" && separator == ',';
                LayoutResult elementResult = FormatDelimitedElementResult(
                    elements[index],
                    indentLevel + 1,
                    elementSuffix,
                    isInitializerElement,
                    indentElementChains,
                    startsWithControlFor,
                    emittedElement
                );
                if (elementResult.lines.empty()) {
                    continue;
                }
                AppendSplitElementLines(lines, elementResult.lines, isInitializerElement);
                nestedResults.push_back(std::move(elementResult));
                emittedElement = true;
            }
        }
        std::string closeLine = FormatGroupClosingLine(tokens, group, suffixTokens);
        AppendSuffix(closeLine, suffix);
        lines.push_back(Indent(indentLevel) + closeLine);
        LayoutResult result = ScoreLayoutCandidate(std::move(lines), false, 0, 0);
        for (const LayoutResult& nestedResult : nestedResults) {
            MergeBreakDepth(result, nestedResult, 1);
        }
        if (stacked) {
            stacked->order = 1;
            if (IsBetterLayout(*stacked, result)) {
                return *stacked;
            }
        }
        return result;
    }

    std::optional<std::vector<std::string>> FormatStackedNestedGroup(
        const std::vector<Token>& tokens,
        GroupPair group,
        int indentLevel,
        std::string_view prefix,
        std::string_view suffix
    ) const {
        std::optional<LayoutResult> result = FormatStackedNestedGroupResult(tokens, group, indentLevel, prefix, suffix);
        if (!result) {
            return std::nullopt;
        }
        return std::move(result->lines);
    }

    std::optional<LayoutResult> FormatStackedNestedGroupResult(
        const std::vector<Token>& tokens,
        GroupPair group,
        int indentLevel,
        std::string_view prefix,
        std::string_view suffix
    ) const {
        const std::optional<GroupPair> nestedGroup = FindStackableNestedGroup(tokens, group);
        if (!nestedGroup) {
            return std::nullopt;
        }
        std::vector<Token> firstLineTokens(
            tokens.begin(),
            tokens.begin() + static_cast<std::ptrdiff_t>(nestedGroup->open + 1)
        );
        std::string firstLine = std::string(prefix) + FormatInline(firstLineTokens);
        if (!Fits(indentLevel, firstLine)) {
            return std::nullopt;
        }

        std::vector<Token> nestedInner(
            tokens.begin() + static_cast<std::ptrdiff_t>(nestedGroup->open + 1),
            tokens.begin() + static_cast<std::ptrdiff_t>(nestedGroup->close)
        );
        std::vector<std::string> lines{Indent(indentLevel) + std::move(firstLine)};
        std::vector<std::vector<Token>> elements = SplitTopLevel(nestedInner, ',');
        std::vector<LayoutResult> nestedResults;
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(nestedInner, ',')) {
            LayoutResult childResult = FormatRangeResult(nestedInner, indentLevel + 1, {}, {}, true);
            lines.insert(lines.end(), childResult.lines.begin(), childResult.lines.end());
            nestedResults.push_back(std::move(childResult));
        } else {
            bool emittedElement = false;
            for (size_t index = 0; index < elements.size(); ++index) {
                std::string elementSuffix;
                if (index + 1 < elements.size()) {
                    elementSuffix = ",";
                }
                LayoutResult elementResult = FormatDelimitedElementResult(
                    elements[index],
                    indentLevel + 1,
                    elementSuffix,
                    true,
                    true,
                    false,
                    emittedElement
                );
                if (elementResult.lines.empty()) {
                    continue;
                }
                AppendSplitElementLines(lines, elementResult.lines, tokens[nestedGroup->open].text == "{");
                nestedResults.push_back(std::move(elementResult));
                emittedElement = true;
            }
        }

        std::vector<Token> closingTokens(
            tokens.begin() + static_cast<std::ptrdiff_t>(nestedGroup->close),
            tokens.end()
        );
        std::string closeLine = FormatInline(closingTokens);
        AppendSuffix(closeLine, suffix);
        lines.push_back(Indent(indentLevel) + closeLine);
        const int nestedBreakDepth = NestedGroupBreakDepth(tokens, group, *nestedGroup);
        LayoutResult result = ScoreLayoutCandidate(std::move(lines), false, nestedBreakDepth, 0);
        for (const LayoutResult& nestedResult : nestedResults) {
            MergeBreakDepth(result, nestedResult, nestedBreakDepth + 1);
        }
        return result;
    }

    int NestedGroupBreakDepth(const std::vector<Token>& tokens, GroupPair outer, GroupPair nested) const {
        int depth = 0;
        for (size_t index = outer.open + 1; index < nested.open && index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
        }
        return depth;
    }

    std::optional<GroupPair> FindStackableNestedGroup(const std::vector<Token>& tokens, GroupPair group) const {
        if (group.open >= tokens.size() || group.close > tokens.size()) {
            return std::nullopt;
        }
        std::vector<Token> inner(
            tokens.begin() + static_cast<std::ptrdiff_t>(group.open + 1),
            tokens.begin() + static_cast<std::ptrdiff_t>(group.close)
        );
        if (ContainsTopLevelSeparator(inner, ',') || ContainsTopLevelSeparator(inner, ';')) {
            return std::nullopt;
        }
        for (size_t index = group.open + 1; index < group.close; ++index) {
            if (!IsWrappableGroupOpen(tokens, index)) {
                continue;
            }
            const std::optional<size_t> close = FindWrappableGroupClose(tokens, index);
            if (!close || *close >= group.close) {
                continue;
            }
            if (
                IsEmptyGroupPair(tokens, index, *close) ||
                IsNonWrappablePrefixGroup(tokens, index, *close) ||
                IsFunctionPointerDeclaratorGroupOpen(tokens, index)
            ) {
                index = close.value_or(index);
                continue;
            }
            if (!HasNonClosingTokenBefore(tokens, *close + 1, group.close)) {
                return GroupPair{index, *close};
            }
            index = *close;
        }
        return std::nullopt;
    }

    std::string FormatGroupClosingLine(
        const std::vector<Token>& tokens,
        GroupPair group,
        const std::vector<Token>& suffixTokens
    ) const {
        if (!IsTemplateAngleGroup(tokens, group) || suffixTokens.empty()) {
            return FormatInline(suffixTokens);
        }
        std::string closeLine = suffixTokens.front().text;
        std::vector<Token> afterClose(suffixTokens.begin() + 1, suffixTokens.end());
        std::string afterText = FormatInline(afterClose);
        if (!afterText.empty()) {
            if (NeedsSpaceAfterSplitTemplateClose(afterText)) {
                closeLine.push_back(' ');
            }
            closeLine += afterText;
        }
        return closeLine;
    }

    static bool NeedsSpaceAfterSplitTemplateClose(std::string_view afterText) {
        if (afterText.empty()) {
            return false;
        }
        if (afterText.rfind("::", 0) == 0 || afterText.rfind("->", 0) == 0) {
            return false;
        }
        const char first = afterText.front();
        return first != '(' &&
            first != '[' &&
            first != ';' &&
            first != ',' &&
            first != ')' &&
            first != ']' &&
            first != '}';
    }

    bool IsPlainParenthesizedExpressionGroup(
        const std::vector<Token>& tokens,
        GroupPair group,
        const std::vector<Token>& firstLineTokens
    ) const {
        if (group.open >= tokens.size() || tokens[group.open].text != "(") {
            return false;
        }
        if (StartsWithControlHeader(firstLineTokens) || IsFunctionPointerDeclaratorGroupOpen(tokens, group.open)) {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, group.open);
        if (!previous) {
            return true;
        }
        const Token& token = tokens[*previous];
        if (token.kind == TokenKind::Word) {
            return IsNonFunctionGroupOwnerWord(token.text) || IsGlobalQualifierPrefixWord(token.text);
        }
        if (token.text == ")" || token.text == "]" || IsTemplateAngleCloseToken(tokens, *previous)) {
            return false;
        }
        return !IsOperatorFunctionNameToken(tokens, *previous) && !IsOperatorCallNameClose(tokens, *previous);
    }

    std::optional<GroupPair> FindTopLevelInitializerListDeclarationGroup(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (depth == 0 && tokens[index].text == "{") {
                const std::optional<size_t> close = FindMatchingClose(tokens, index);
                if (!close) {
                    return std::nullopt;
                }
                const size_t afterClose = NextSignificantIndex(tokens, *close + 1);
                if (afterClose >= tokens.size() || tokens[afterClose].text != ";") {
                    index = *close;
                    continue;
                }
                const size_t afterSemicolon = NextSignificantIndex(tokens, afterClose + 1);
                if (afterSemicolon < tokens.size()) {
                    index = *close;
                    continue;
                }
                std::vector<Token> inner(
                    tokens.begin() + static_cast<std::ptrdiff_t>(index + 1),
                    tokens.begin() + static_cast<std::ptrdiff_t>(*close)
                );
                if (!ContainsTopLevelSeparator(inner, ',')) {
                    index = *close;
                    continue;
                }
                const std::optional<size_t> declarator = PreviousNonNewlineIndex(tokens, index);
                if (declarator && tokens[*declarator].kind == TokenKind::Word && IsLikelyDeclarationTypeBeforeName(
                    tokens,
                    *declarator
                )) {
                    return GroupPair{index, *close};
                }
                index = *close;
                continue;
            }
            UpdateDepth(tokens, index, depth);
        }
        return std::nullopt;
    }

    std::optional<size_t> InitializerListDeclarationDeclarator(
        const std::vector<Token>& tokens,
        GroupPair initializerList
    ) const {
        if (initializerList.open >= tokens.size()) {
            return std::nullopt;
        }
        const std::optional<size_t> declarator = PreviousNonNewlineIndex(tokens, initializerList.open);
        if (!declarator || tokens[*declarator].kind != TokenKind::Word) {
            return std::nullopt;
        }
        if (!IsLikelyDeclarationTypeBeforeName(tokens, *declarator)) {
            return std::nullopt;
        }
        return declarator;
    }

    bool IsLikelyDeclarationTypeBeforeName(const std::vector<Token>& tokens, size_t nameIndex) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, nameIndex);
        if (!previous) {
            return false;
        }
        if (IsPointerOrReferenceDeclaratorToken(tokens[*previous].text)) {
            return IsPointerOrReferenceDeclarator(tokens, *previous) && IsLikelyTypeBeforePointer(tokens, *previous);
        }
        return IsLikelyTypeNameToken(tokens, *previous);
    }

    bool IsStringLiteralSequence(const std::vector<Token>& tokens) const {
        int literalCount = 0;
        bool sawTrailingSemicolon = false;
        for (const Token& token : tokens) {
            if (token.kind == TokenKind::Newline) {
                continue;
            }
            if (token.text == ";" && literalCount > 0 && !sawTrailingSemicolon) {
                sawTrailingSemicolon = true;
                continue;
            }
            if (token.kind != TokenKind::StringLiteral || sawTrailingSemicolon) {
                return false;
            }
            ++literalCount;
        }
        return literalCount > 1;
    }

    std::vector<std::string> FormatStringLiteralSequence(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        std::vector<std::string> lines;
        std::optional<size_t> lastStringLiteral;
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (tokens[index].kind == TokenKind::StringLiteral) {
                lastStringLiteral = index;
            }
        }
        if (
            std::optional<size_t> last = PreviousNonNewlineIndex(tokens, tokens.size());
            last && tokens[*last].text == ";"
        ) {
            suffix = ";" + suffix;
        }
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (token.kind != TokenKind::StringLiteral) {
                continue;
            }
            std::string line = lines.empty() ? prefix + token.text : token.text;
            if (lastStringLiteral && index == *lastStringLiteral) {
                AppendSuffix(line, suffix);
            }
            const int lineIndent = lines.empty() ? indentLevel : indentLevel + 1;
            lines.push_back(Indent(lineIndent) + line);
        }
        return lines;
    }

    std::vector<std::string> FormatOperatorChain(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains,
        bool indentLogicalSplitChains
    ) const {
        return FormatOperatorChainResult(
            tokens,
            indentLevel,
            std::move(prefix),
            std::move(suffix),
            indentSplitChains,
            indentLogicalSplitChains
        ).lines;
    }

    LayoutResult FormatOperatorChainResult(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains,
        bool indentLogicalSplitChains
    ) const {
        const ChainKind chainKind = SelectChainKind(tokens);
        if (chainKind == ChainKind::Ternary) {
            return ScoreLayoutCandidate(
                FormatTernaryChain(tokens, indentLevel, std::move(prefix), std::move(suffix), indentSplitChains),
                false,
                0,
                0
            );
        }
        if (chainKind == ChainKind::Shift) {
            return ScoreLayoutCandidate(
                FormatShiftOperatorChain(tokens, indentLevel, std::move(prefix), std::move(suffix)),
                false,
                0,
                0
            );
        }
        std::vector<std::vector<Token>> parts;
        std::vector<Token> current;
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            UpdateDepth(token, depth);
            current.push_back(token);
            if (depth == 0 && IsChainBreakOperator(tokens, index, chainKind)) {
                if (index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LineComment) {
                    current.push_back(tokens[index + 1]);
                    ++index;
                }
                parts.push_back(current);
                current.clear();
            }
        }
        if (!current.empty()) {
            parts.push_back(current);
        }
        std::vector<std::string> lines;
        std::vector<LayoutResult> nestedResults;
        const bool indentSplitChainContinuation =
            indentSplitChains && (chainKind != ChainKind::Logical || indentLogicalSplitChains);
        const bool indentContinuation =
            indentSplitChainContinuation || !prefix.empty() || (!tokens.empty() && tokens.front().text == "return");
        for (size_t index = 0; index < parts.size(); ++index) {
            std::string partPrefix = index == 0 ? prefix : std::string{};
            std::string partSuffix;
            if (index + 1 == parts.size()) {
                partSuffix = suffix;
            }
            const int partIndent = indentContinuation && index > 0 ? indentLevel + 1 : indentLevel;
            LayoutResult partResult =
                FormatChainPartResult(parts[index], partIndent, std::move(partPrefix), std::move(partSuffix));
            lines.insert(lines.end(), partResult.lines.begin(), partResult.lines.end());
            nestedResults.push_back(std::move(partResult));
        }
        LayoutResult result = ScoreLayoutCandidate(std::move(lines), false, 0, 0);
        for (const LayoutResult& nestedResult : nestedResults) {
            MergeBreakDepth(result, nestedResult, 1);
        }
        return result;
    }

    std::vector<std::string> FormatShiftOperatorChain(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        std::vector<Token> receiver;
        std::vector<std::vector<Token>> segments;
        std::vector<Token> current;
        bool sawShiftOperator = false;
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            UpdateDepth(token, depth);
            if (depth == 0 && IsChainBreakOperator(tokens, index, ChainKind::Shift)) {
                if (!sawShiftOperator) {
                    receiver = current;
                    sawShiftOperator = true;
                } else {
                    segments.push_back(current);
                }
                current.clear();
                current.push_back(token);
                continue;
            }
            current.push_back(token);
        }
        if (!sawShiftOperator) {
            return FormatChainPart(tokens, indentLevel, std::move(prefix), std::move(suffix));
        }
        if (!current.empty()) {
            segments.push_back(current);
        }
        std::vector<std::string> lines = FormatRange(receiver, indentLevel, std::move(prefix), {}, true);
        if (std::optional<std::string> compactTail = CompactShiftTail(segments, indentLevel + 1, suffix)) {
            lines.push_back(Indent(indentLevel + 1) + *compactTail);
            return lines;
        }
        segments = GroupShiftSegmentsByBreakRules(segments);
        for (size_t index = 0; index < segments.size();) {
            std::string segmentSuffix;
            if (index + 1 == segments.size()) {
                segmentSuffix = suffix;
            }
            std::vector<std::string> segmentLines =
                FormatChainPart(segments[index], indentLevel + 1, {}, std::move(segmentSuffix));
            lines.insert(lines.end(), segmentLines.begin(), segmentLines.end());
            ++index;
        }
        return lines;
    }

    std::optional<std::string> CompactShiftTail(
        const std::vector<std::vector<Token>>& segments,
        int indentLevel,
        std::string_view suffix
    ) const {
        std::vector<Token> combined;
        for (const std::vector<Token>& segment : segments) {
            combined.insert(combined.end(), segment.begin(), segment.end());
        }
        if (combined.empty() || HasOriginalBlankSeparator(combined) || ShouldForceSplit(combined)) {
            return std::nullopt;
        }
        std::string line = FormatInline(combined);
        AppendSuffix(line, suffix);
        if (!Fits(indentLevel, line)) {
            return std::nullopt;
        }
        return line;
    }

    std::vector<std::vector<Token>> GroupShiftSegmentsByBreakRules(
        const std::vector<std::vector<Token>>& segments
    ) const {
        std::vector<std::vector<Token>> grouped;
        std::vector<Token> pendingConfiguration;
        for (const std::vector<Token>& segment : segments) {
            if (IsStreamShiftConfigurationSegment(segment)) {
                pendingConfiguration.insert(pendingConfiguration.end(), segment.begin(), segment.end());
                continue;
            }
            if (!pendingConfiguration.empty()) {
                pendingConfiguration.insert(pendingConfiguration.end(), segment.begin(), segment.end());
                grouped.push_back(std::move(pendingConfiguration));
                pendingConfiguration.clear();
                continue;
            }
            grouped.push_back(segment);
        }
        if (!pendingConfiguration.empty()) {
            grouped.push_back(std::move(pendingConfiguration));
        }
        return grouped;
    }

    bool IsStreamShiftConfigurationSegment(const std::vector<Token>& segment) const {
        std::optional<std::string> methodName = StreamShiftConfigurationMethodName(segment);
        return methodName && IsConfiguredStreamShiftConfigurationMethod(*methodName);
    }

    std::optional<std::string> StreamShiftConfigurationMethodName(const std::vector<Token>& segment) const {
        size_t index = NextSignificantIndex(segment, 0);
        if (index >= segment.size() || (segment[index].text != "<<" && segment[index].text != ">>")) {
            return std::nullopt;
        }
        index = NextSignificantIndex(segment, index + 1);
        if (index >= segment.size() || segment[index].kind != TokenKind::Word) {
            return std::nullopt;
        }
        std::string name = segment[index].text;
        index = NextSignificantIndex(segment, index + 1);
        while (index < segment.size() && segment[index].text == "::") {
            const size_t namePart = NextSignificantIndex(segment, index + 1);
            if (namePart >= segment.size() || segment[namePart].kind != TokenKind::Word) {
                return std::nullopt;
            }
            name += "::";
            name += segment[namePart].text;
            index = NextSignificantIndex(segment, namePart + 1);
        }
        if (index < segment.size() && segment[index].text == "(") {
            const std::optional<size_t> close = FindMatchingClose(segment, index);
            if (!close) {
                return std::nullopt;
            }
            index = NextSignificantIndex(segment, *close + 1);
        }
        if (index != segment.size()) {
            return std::nullopt;
        }
        return name;
    }

    bool IsConfiguredStreamShiftConfigurationMethod(std::string_view methodName) const {
        return std::find(
            config_.streamShiftConfigurationMethods.begin(),
            config_.streamShiftConfigurationMethods.end(),
            methodName
        ) != config_.streamShiftConfigurationMethods.end();
    }

    std::vector<std::string> FormatTernaryChain(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains
    ) const {
        if (std::optional<TernaryExpressionParts> single = SplitSingleTopLevelTernary(tokens)) {
            return FormatSingleTernaryExpression(
                *single,
                indentLevel,
                std::move(prefix),
                std::move(suffix),
                indentSplitChains
            );
        }

        std::vector<std::vector<Token>> parts;
        std::vector<Token> current;
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            UpdateDepth(token, depth);
            current.push_back(token);
            if (depth == 0 && token.text == ":") {
                if (index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LineComment) {
                    current.push_back(tokens[index + 1]);
                    ++index;
                }
                parts.push_back(current);
                current.clear();
            }
        }
        if (!current.empty()) {
            parts.push_back(current);
        }
        std::vector<std::string> lines;
        const bool indentContinuation =
            indentSplitChains || !prefix.empty() || (!tokens.empty() && tokens.front().text == "return");
        for (size_t index = 0; index < parts.size(); ++index) {
            std::string partPrefix = index == 0 ? prefix : std::string{};
            std::string partSuffix;
            if (index + 1 == parts.size()) {
                partSuffix = suffix;
            }
            const int partIndent = indentContinuation && index > 0 ? indentLevel + 1 : indentLevel;
            std::vector<std::string> partLines =
                FormatTernaryChainPart(parts[index], partIndent, std::move(partPrefix), std::move(partSuffix));
            lines.insert(lines.end(), partLines.begin(), partLines.end());
        }
        return lines;
    }

    std::vector<std::string> FormatSingleTernaryExpression(
        const TernaryExpressionParts& parts,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains
    ) const {
        LayoutResult best;
        bool hasBest = false;
        size_t order = 0;
        const auto consider = [&](std::vector<std::string> lines, int breakDepth) {
            LayoutResult candidate = ScoreLayoutCandidate(std::move(lines), false, breakDepth, order++);
            if (!hasBest || IsBetterLayout(candidate, best)) {
                best = std::move(candidate);
                hasBest = true;
            }
        };

        const std::string conditionText = FormatInline(parts.condition);
        const std::string trueText = FormatInline(parts.trueBranch);
        std::string falseText = FormatInline(parts.falseBranch);
        AppendSuffix(falseText, suffix);
        const std::string conditionPrefix = prefix + conditionText + " ? ";
        const std::string trueSuffix = " : " + falseText;
        consider(FormatRange(parts.trueBranch, indentLevel, conditionPrefix, trueSuffix, true), 1);

        std::string conditionSuffix = " ? " + trueText + " : " + falseText;
        consider(FormatRange(parts.condition, indentLevel, prefix, conditionSuffix, true), 1);

        consider(FormatRange(parts.falseBranch, indentLevel, conditionPrefix + trueText + " : ", suffix, true), 1);

        std::vector<std::string> questionBreak =
            FormatRange(parts.condition, indentLevel, prefix, " ?", indentSplitChains);
        std::vector<std::string> questionTrueLines =
            FormatRange(parts.trueBranch, indentLevel + 1, {}, trueSuffix, true);
        questionBreak.insert(questionBreak.end(), questionTrueLines.begin(), questionTrueLines.end());
        consider(std::move(questionBreak), 0);

        std::vector<std::string> colonBreak = FormatRange(parts.trueBranch, indentLevel, conditionPrefix, " :", true);
        std::vector<std::string> colonFalseLines = FormatRange(parts.falseBranch, indentLevel + 1, {}, suffix, true);
        colonBreak.insert(colonBreak.end(), colonFalseLines.begin(), colonFalseLines.end());
        consider(std::move(colonBreak), 0);

        std::vector<std::string> bothBreaks =
            FormatRange(parts.condition, indentLevel, prefix, " ?", indentSplitChains);
        std::vector<std::string> bothTrueLines = FormatRange(parts.trueBranch, indentLevel + 1, {}, " :", true);
        std::vector<std::string> bothFalseLines = FormatRange(parts.falseBranch, indentLevel + 1, {}, suffix, true);
        bothBreaks.insert(bothBreaks.end(), bothTrueLines.begin(), bothTrueLines.end());
        bothBreaks.insert(bothBreaks.end(), bothFalseLines.begin(), bothFalseLines.end());
        consider(std::move(bothBreaks), 0);

        return best.lines;
    }

    std::vector<std::string> FormatTernaryChainPart(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        std::string inlineText = prefix + FormatInline(tokens);
        AppendSuffix(inlineText, suffix);
        if (Fits(indentLevel, inlineText)) {
            return {Indent(indentLevel) + inlineText};
        }
        if (
            std::optional<std::vector<std::string>> splitPart =
                FormatTernaryChainConditionPart(tokens, indentLevel, prefix, suffix)
        ) {
            return *splitPart;
        }
        return FormatChainPart(tokens, indentLevel, std::move(prefix), std::move(suffix));
    }

    std::optional<std::vector<std::string>> FormatTernaryChainConditionPart(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string_view prefix,
        std::string_view suffix
    ) const {
        const std::optional<size_t> question = FindTopLevelToken(tokens, "?");
        if (!question || FindMatchingTernaryColon(tokens, *question)) {
            return std::nullopt;
        }
        size_t valueEnd = tokens.size();
        std::string valueSuffix(suffix);
        const std::optional<size_t> last = PreviousNonNewlineIndex(tokens, tokens.size());
        if (last && tokens[*last].text == ":") {
            valueEnd = *last;
            valueSuffix.insert(0, " :");
        }
        if (*question + 1 >= valueEnd) {
            return std::nullopt;
        }
        std::vector<Token> condition(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(*question));
        std::vector<Token> value(
            tokens.begin() + static_cast<std::ptrdiff_t>(*question + 1),
            tokens.begin() + static_cast<std::ptrdiff_t>(valueEnd)
        );
        std::string valuePrefix = std::string(prefix) + FormatInline(condition) + " ? ";
        return FormatRange(value, indentLevel, std::move(valuePrefix), std::move(valueSuffix), true);
    }

    std::optional<TernaryExpressionParts> SplitSingleTopLevelTernary(const std::vector<Token>& tokens) const {
        const std::optional<size_t> question = FindTopLevelToken(tokens, "?");
        if (!question) {
            return std::nullopt;
        }
        const std::optional<size_t> colon = FindMatchingTernaryColon(tokens, *question);
        if (!colon || *question == 0 || *question + 1 >= *colon || *colon + 1 >= tokens.size()) {
            return std::nullopt;
        }
        std::vector<Token> falseBranch(tokens.begin() + static_cast<std::ptrdiff_t>(*colon + 1), tokens.end());
        if (FindTopLevelToken(falseBranch, "?")) {
            return std::nullopt;
        }
        return TernaryExpressionParts{
            std::vector<Token>(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(*question)),
            std::vector<Token>(
                tokens.begin() + static_cast<std::ptrdiff_t>(*question + 1),
                tokens.begin() + static_cast<std::ptrdiff_t>(*colon)
            ),
            std::move(falseBranch)
        };
    }

    std::optional<size_t> FindMatchingTernaryColon(const std::vector<Token>& tokens, size_t question) const {
        int depth = 0;
        int nestedTernaryDepth = 0;
        for (size_t index = question + 1; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth != 0) {
                continue;
            }
            if (tokens[index].text == "?") {
                ++nestedTernaryDepth;
                continue;
            }
            if (tokens[index].text != ":") {
                continue;
            }
            if (nestedTernaryDepth == 0) {
                return index;
            }
            --nestedTernaryDepth;
        }
        return std::nullopt;
    }

    static bool StartsWithGroupClose(std::string_view text) {
        return !text.empty() && (text.front() == ')' || text.front() == ']' || text.front() == '}');
    }

    static bool EndsWithColon(std::string_view text) {
        return !text.empty() && text.back() == ':';
    }

    std::vector<std::string> FormatChainPart(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        return FormatChainPartResult(tokens, indentLevel, std::move(prefix), std::move(suffix)).lines;
    }

    LayoutResult FormatChainPartResult(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        std::string inlineText = prefix + FormatInline(tokens);
        AppendSuffix(inlineText, suffix);
        if (Fits(indentLevel, inlineText)) {
            return ScoreLayoutCandidate({Indent(indentLevel) + inlineText}, true, 0, 0);
        }
        const ChainKind chainKind = SelectChainKind(tokens);
        const std::optional<size_t> trailingOperator = PreviousNonNewlineIndex(tokens, tokens.size());
        if (
            chainKind != ChainKind::None &&
            trailingOperator &&
            *trailingOperator + 1 == tokens.size() &&
            IsChainBreakOperator(tokens, *trailingOperator, chainKind)
        ) {
            std::vector<Token> value(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(*trailingOperator));
            if (!value.empty()) {
                std::string operatorSuffix = " " + tokens[*trailingOperator].text;
                AppendSuffix(operatorSuffix, suffix);
                return FormatRangeResult(value, indentLevel, std::move(prefix), std::move(operatorSuffix), true);
            }
        }
        return FormatRangeResult(tokens, indentLevel, std::move(prefix), std::move(suffix), true);
    }

    std::string JoinOutput() const {
        std::string result;
        for (const std::string& line : outputLines_) {
            result += line;
            result.push_back('\n');
        }
        return result;
    }

    std::string FormatInline(const std::vector<Token>& tokens, std::optional<size_t> maxLength = std::nullopt) const {
        std::string result;
        std::optional<Token> previous;
        std::optional<std::string> trailingStringLiteral;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (token.kind == TokenKind::Newline) {
                continue;
            }
            if (token.kind == TokenKind::LineComment) {
                result = TrimRight(result);
                if (!result.empty()) {
                    result += "  ";
                }
                result += TrimRight(TrimLeft(token.text));
                previous = token;
                trailingStringLiteral.reset();
                continue;
            }
            if (
                token.kind == TokenKind::StringLiteral &&
                previous &&
                previous->kind == TokenKind::StringLiteral &&
                trailingStringLiteral &&
                TryAppendConcatenatedStringLiteral(result, *trailingStringLiteral, token.text, tokens, index, maxLength)
            ) {
                previous = Token{TokenKind::StringLiteral, *trailingStringLiteral};
                continue;
            }
            if (previous && NeedsSpaceBefore(tokens, index, *previous)) {
                result.push_back(' ');
            }
            result += token.text;
            previous = token;
            if (token.kind == TokenKind::StringLiteral) {
                trailingStringLiteral = token.text;
            } else {
                trailingStringLiteral.reset();
            }
        }
        return result;
    }

    bool TryAppendConcatenatedStringLiteral(
        std::string& result,
        std::string& trailingLiteral,
        std::string_view currentLiteral,
        const std::vector<Token>& tokens,
        size_t index,
        std::optional<size_t> maxLength
    ) const {
        if (HasUserDefinedLiteralSuffix(tokens, index)) {
            return false;
        }
        std::optional<std::string> currentTail = ConcatenatedStringLiteralTail(trailingLiteral, currentLiteral);
        if (!currentTail) {
            return false;
        }
        std::string candidate = result;
        candidate.pop_back();
        candidate.append(*currentTail);
        if (maxLength && candidate.size() > *maxLength) {
            return false;
        }
        result = std::move(candidate);
        trailingLiteral.pop_back();
        trailingLiteral.append(*currentTail);
        return true;
    }

    std::optional<std::string> ConcatenatedStringLiteralTail(
        std::string_view previousLiteral,
        std::string_view currentLiteral
    ) const {
        if (!IsOrdinaryStringLiteralText(previousLiteral) || !IsOrdinaryStringLiteralText(currentLiteral)) {
            return std::nullopt;
        }
        const std::string_view previousContent = StringLiteralContent(previousLiteral);
        const std::string_view currentContent = StringLiteralContent(currentLiteral);
        if (StringLiteralContentEndsWithEscapedNewline(previousContent)) {
            return std::nullopt;
        }
        if (currentContent.empty()) {
            return std::string(currentLiteral.substr(1));
        }
        if (!HasTrailingExpandableEscape(previousContent, currentContent.front())) {
            return std::string(currentLiteral.substr(1));
        }
        return std::nullopt;
    }

    static bool IsOrdinaryStringLiteralText(std::string_view literal) {
        return literal.size() >= 2 && literal.front() == '"' && literal.back() == '"';
    }

    static std::string_view StringLiteralContent(std::string_view literal) {
        return literal.substr(1, literal.size() - 2);
    }

    static bool StringLiteralContentEndsWithEscapedNewline(std::string_view content) {
        if (content.empty() || content.back() != 'n') {
            return false;
        }
        size_t slashCount = 0;
        for (size_t index = content.size() - 1; index > 0 && content[index - 1] == '\\'; --index) {
            ++slashCount;
        }
        return slashCount % 2 == 1;
    }

    bool StringLiteralEndsWithEscapedNewline(std::string_view literal) const {
        const size_t firstQuote = literal.find('"');
        const size_t lastQuote = literal.rfind('"');
        if (firstQuote == std::string_view::npos || lastQuote <= firstQuote) {
            return false;
        }
        if (firstQuote > 0 && literal[firstQuote - 1] == 'R') {
            return false;
        }
        return StringLiteralContentEndsWithEscapedNewline(literal.substr(firstQuote + 1, lastQuote - firstQuote - 1));
    }

    bool HasTrailingExpandableEscape(std::string_view content, char nextChar) const {
        for (size_t index = 0; index < content.size();) {
            if (content[index] != '\\') {
                ++index;
                continue;
            }
            const size_t escapeStart = index;
            ++index;
            if (index >= content.size()) {
                return true;
            }
            if (content[index] == 'x') {
                ++index;
                while (index < content.size() && IsHexDigit(content[index])) {
                    ++index;
                }
                if (index == content.size()) {
                    return IsHexDigit(nextChar);
                }
                continue;
            }
            if (IsOctalDigit(content[index])) {
                int digitCount = 0;
                while (index < content.size() && digitCount < 3 && IsOctalDigit(content[index])) {
                    ++index;
                    ++digitCount;
                }
                if (index == content.size()) {
                    return digitCount < 3 && IsOctalDigit(nextChar);
                }
                continue;
            }
            ++index;
            if (index == content.size() && escapeStart + 1 == content.size()) {
                return true;
            }
        }
        return false;
    }

    bool HasUserDefinedLiteralSuffix(const std::vector<Token>& tokens, size_t stringLiteralIndex) const {
        const size_t next = NextSignificantIndex(tokens, stringLiteralIndex + 1);
        return next < tokens.size() && tokens[next].kind == TokenKind::Word;
    }

    bool NeedsSpaceBefore(const std::vector<Token>& tokens, size_t index, const Token& previous) const {
        const Token& token = tokens[index];
        const std::string_view current = token.text;
        const std::string_view prev = previous.text;
        const std::optional<size_t> previousIndex = PreviousNonNewlineIndex(tokens, index);
        const size_t prevIndex = previousIndex.value_or(index);
        if (IsOperatorFunctionNameToken(tokens, index)) {
            return false;
        }
        if (current == "(" && IsOperatorFunctionNameToken(tokens, prevIndex)) {
            return false;
        }
        if (current == "(" && IsFunctionPointerDeclaratorGroupOpen(tokens, index)) {
            return IsFunctionPointerDeclaratorContextBeforeGroup(tokens, index);
        }
        if (current == "(" && IsUnaryPrefixOperator(tokens, prevIndex)) {
            return false;
        }
        if (prev == "return" && current != ";") {
            return true;
        }
        if (current == "(" && previous.kind == TokenKind::Word) {
            return IsControlKeyword(prev);
        }
        if (
            (token.kind == TokenKind::StringLiteral || token.kind == TokenKind::CharLiteral) &&
            previous.kind == TokenKind::Word &&
            IsStringOrCharacterLiteralPrefix(prev)
        ) {
            return false;
        }
        if (current == "~" && previous.kind == TokenKind::Word && IsDestructorNameToken(tokens, index)) {
            return true;
        }
        if (token.kind == TokenKind::Word && prev == "...") {
            return true;
        }
        if (current == ":" && IsLabelColonToken(tokens, index)) {
            return false;
        }
        if (current == "<" && prev == "template") {
            return true;
        }
        if (current == "<" && IsTemplateAngleOpen(tokens, index)) {
            return false;
        }
        if (IsTemplateAngleCloseToken(tokens, index)) {
            return false;
        }
        if (prev == "<" && IsTemplateAngleOpen(tokens, prevIndex)) {
            return false;
        }
        if (IsTemplateAngleCloseToken(tokens, prevIndex)) {
            if (current == "(" || IsNoSpaceBefore(current)) {
                return false;
            }
            if (IsPointerOrReferenceDeclaratorToken(current) && IsPointerOrReferenceDeclarator(tokens, index)) {
                return false;
            }
        }
        if (current == "{" && IsAssignmentOperator(prev)) {
            return true;
        }
        if (current == "{" && prev == ":") {
            return true;
        }
        if (IsLambdaReturnArrowToken(tokens, index) || IsLambdaReturnArrowToken(tokens, prevIndex)) {
            return true;
        }
        if (current == "{" && IsLambdaBodyOpenToken(tokens, index)) {
            return true;
        }
        if (prev == "{" && current != "}" && IsLambdaBodyOpenToken(tokens, prevIndex)) {
            return true;
        }
        if ((prev == "," || prev == ";") && current != ")" && current != "]" && current != ";") {
            return true;
        }
        if ((current == "++" || current == "--") && prev == ")") {
            return true;
        }
        if (
            (current == "++" || current == "--") && IsUnaryPrefixOperator(tokens, index) && IsBinaryOperatorLike(prev)
        ) {
            return true;
        }
        if (current == "[" && prev == "auto") {
            return true;
        }
        if (current == "::" && NeedsSpaceBeforeLeadingGlobalQualifier(tokens, prevIndex)) {
            return true;
        }
        if (current == "{" || IsNoSpaceBefore(current) || IsNoSpaceAfter(prev)) {
            return false;
        }
        if (IsPointerOrReferenceDeclaratorToken(current)) {
            if (IsPointerOrReferenceDeclarator(tokens, index)) {
                return false;
            }
        }
        if (IsUnaryPrefixOperator(tokens, index) && IsUnaryPrefixOperator(tokens, prevIndex)) {
            return false;
        }
        if (IsPointerOrReferenceDeclaratorToken(prev) && token.kind == TokenKind::Word) {
            if (IsPointerOrReferenceDeclarator(tokens, prevIndex)) {
                return true;
            }
            return !IsUnaryPrefixOperator(tokens, prevIndex);
        }
        if ((prev == "+" || prev == "-") && IsUnaryPrefixOperator(tokens, prevIndex)) {
            return false;
        }
        if (IsBinaryOperatorLike(current) || IsBinaryOperatorLike(prev)) {
            return true;
        }
        if (prev == ")" && IsCStyleCastCloseBeforeExpression(tokens, prevIndex)) {
            return false;
        }
        if ((prev == ")" || prev == "]") && (token.kind == TokenKind::Word || token.kind == TokenKind::StringLiteral)) {
            return true;
        }
        return IsWordLike(previous) && IsWordLike(token);
    }

    bool NeedsSpaceBeforeLeadingGlobalQualifier(const std::vector<Token>& tokens, size_t prevIndex) const {
        const Token& previous = tokens[prevIndex];
        if (previous.kind == TokenKind::Word) {
            return IsGlobalQualifierPrefixWord(previous.text);
        }
        if (!IsBinaryOperatorLike(previous.text)) {
            return false;
        }
        if (IsUnaryPrefixOperator(tokens, prevIndex) || IsTemplateAngleCloseToken(tokens, prevIndex)) {
            return false;
        }
        if (IsPointerOrReferenceDeclaratorToken(previous.text) && IsPointerOrReferenceDeclarator(tokens, prevIndex)) {
            return false;
        }
        return true;
    }

    bool IsGlobalQualifierPrefixWord(std::string_view text) const {
        static constexpr std::string_view kWords[] = {
            "alignas",
            "case",
            "co_return",
            "consteval",
            "constexpr",
            "constinit",
            "delete",
            "extern",
            "friend",
            "inline",
            "new",
            "return",
            "throw"
        };
        return IsTypeContextWord(text) || std::find(std::begin(kWords), std::end(kWords), text) != std::end(kWords);
    }

    bool IsOperatorFunctionNameToken(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].kind != TokenKind::Symbol) {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        return previous && tokens[*previous].text == "operator";
    }

    bool IsDestructorNameToken(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != "~") {
            return false;
        }
        const size_t nameIndex = NextSignificantIndex(tokens, index + 1);
        if (nameIndex >= tokens.size() || tokens[nameIndex].kind != TokenKind::Word) {
            return false;
        }
        const size_t afterName = NextSignificantIndex(tokens, nameIndex + 1);
        return afterName < tokens.size() && tokens[afterName].text == "(";
    }

    std::optional<size_t> PreviousNonNewlineIndex(const std::vector<Token>& tokens, size_t index) const {
        while (index > 0) {
            --index;
            if (tokens[index].kind != TokenKind::Newline) {
                return index;
            }
        }
        return std::nullopt;
    }

    bool IsUnaryPrefixOperator(const std::vector<Token>& tokens, size_t index) const {
        const std::string& text = tokens[index].text;
        if (
            text != "*" &&
            text != "&" &&
            text != "+" &&
            text != "-" &&
            text != "++" &&
            text != "--" &&
            text != "!" &&
            text != "~"
        ) {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        if (!previous) {
            return true;
        }
        if (IsTemplateAngleCloseToken(tokens, *previous)) {
            return false;
        }
        const std::string& prev = tokens[*previous].text;
        return prev == "(" ||
            prev == "[" ||
            prev == "{" ||
            prev == "," ||
            prev == ";" ||
            prev == "=" ||
            prev == "?" ||
            prev == ":" ||
            IsBinaryOperatorLike(prev) ||
            prev == "return";
    }

    bool IsCStyleCastCloseBeforeExpression(const std::vector<Token>& tokens, size_t close) const {
        if (close >= tokens.size() || tokens[close].text != ")") {
            return false;
        }
        const std::optional<size_t> open = FindMatchingOpen(tokens, close);
        if (!open || !IsLikelyCStyleCastType(tokens, *open, close)) {
            return false;
        }
        const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
        if (!beforeOpen) {
            return true;
        }
        const std::string& before = tokens[*beforeOpen].text;
        if (before == "alignas") {
            return false;
        }
        return IsCStyleCastContextBeforeOpen(tokens, *beforeOpen);
    }

    bool IsCStyleCastContextBeforeOpen(const std::vector<Token>& tokens, size_t beforeOpen) const {
        const std::string& before = tokens[beforeOpen].text;
        if (tokens[beforeOpen].kind == TokenKind::Word) {
            return before == "return" || before == "co_return" || before == "throw";
        }
        return before == "(" ||
            before == "[" ||
            before == "{" ||
            before == "," ||
            before == ";" ||
            before == "=" ||
            before == "?" ||
            before == ":" ||
            IsBinaryOperatorLike(before);
    }

    bool IsLikelyCStyleCastType(const std::vector<Token>& tokens, size_t open, size_t close) const {
        if (open + 1 >= close) {
            return false;
        }
        size_t last = open;
        for (size_t index = open + 1; index < close; ++index) {
            const Token& token = tokens[index];
            if (token.kind == TokenKind::Newline) {
                continue;
            }
            if (
                token.kind != TokenKind::Word &&
                token.text != "::" &&
                token.text != "*" &&
                token.text != "&" &&
                token.text != "&&" &&
                token.text != "^" &&
                token.text != "%" &&
                token.text != "<" &&
                token.text != ">" &&
                token.text != ","
            ) {
                return false;
            }
            last = index;
        }
        if (last == open) {
            return false;
        }
        return IsLikelyTypeNameToken(tokens, last) ||
            (IsPointerOrReferenceDeclaratorToken(tokens[last].text) && IsLikelyTypeBeforePointer(tokens, last));
    }

    bool IsLikelyTypeBeforePointer(const std::vector<Token>& tokens, size_t index) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        if (!previous) {
            return false;
        }
        if (IsPointerOrReferenceDeclaratorToken(tokens[*previous].text) && IsPointerOrReferenceDeclarator(
            tokens,
            *previous
        )) {
            return true;
        }
        return IsLikelyTypeNameToken(tokens, *previous);
    }

    bool IsLikelyTypeNameToken(const std::vector<Token>& tokens, size_t index) const {
        const Token& token = tokens[index];
        if (IsTemplateAngleCloseToken(tokens, index)) {
            return IsLikelyTemplateTypeClose(tokens, index);
        }
        if (token.text == ")") {
            return IsDecltypeCloseBeforePointer(tokens, index);
        }
        if (token.text == "]") {
            return false;
        }
        if (token.kind != TokenKind::Word) {
            return false;
        }
        if (IsCallingConventionToken(token.text)) {
            return true;
        }
        static constexpr std::string_view kTypeWords[] = {
            "auto",
            "bool",
            "char",
            "const",
            "double",
            "float",
            "int",
            "long",
            "short",
            "signed",
            "size_t",
            "std",
            "unsigned",
            "void",
            "wchar_t"
        };
        if (std::find(std::begin(kTypeWords), std::end(kTypeWords), token.text) != std::end(kTypeWords)) {
            return true;
        }
        if (IsTypedefStyleTypeName(token.text)) {
            return true;
        }
        if (!token.text.empty() && token.text.front() >= 'A' && token.text.front() <= 'Z') {
            return true;
        }
        const std::optional<size_t> beforeType = PreviousNonNewlineIndex(tokens, index);
        if (beforeType && tokens[*beforeType].kind == TokenKind::Word && IsTypeContextWord(tokens[*beforeType].text)) {
            return true;
        }
        return beforeType && tokens[*beforeType].text == "::";
    }

    static bool IsTypedefStyleTypeName(std::string_view text) {
        return text.size() > 2 && text.ends_with("_t");
    }

    static bool IsCallingConventionToken(std::string_view text) {
        static constexpr std::string_view kCallingConventionTokens[] = {
            "__cdecl",
            "__clrcall",
            "__fastcall",
            "__stdcall",
            "__thiscall",
            "__vectorcall"
        };
        return std::find(std::begin(kCallingConventionTokens), std::end(kCallingConventionTokens), text) !=
            std::end(kCallingConventionTokens);
    }

    bool IsDecltypeCloseBeforePointer(const std::vector<Token>& tokens, size_t closeIndex) const {
        const std::optional<size_t> open = FindMatchingOpen(tokens, closeIndex);
        if (!open || *open == 0) {
            return false;
        }
        const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
        return beforeOpen && tokens[*beforeOpen].text == "decltype";
    }

    bool IsLikelyTemplateTypeClose(const std::vector<Token>& tokens, size_t closeIndex) const {
        const std::optional<size_t> open = FindTemplateAngleOpen(tokens, closeIndex);
        if (!open) {
            return true;
        }
        const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
        if (!beforeOpen || tokens[*beforeOpen].kind != TokenKind::Word) {
            return true;
        }
        return !tools::lint::EndsWith(tokens[*beforeOpen].text, "_v");
    }

    bool IsLikelyDeclaratorContextBeforePointer(const std::vector<Token>& tokens, size_t index) const {
        const std::optional<size_t> typeStart = TypeNameStartBeforePointer(tokens, index);
        if (!typeStart) {
            return false;
        }
        const std::optional<size_t> beforeType = PreviousNonNewlineIndex(tokens, *typeStart);
        if (!beforeType) {
            return true;
        }
        const Token& token = tokens[*beforeType];
        if (token.kind == TokenKind::Word) {
            return IsTypeContextWord(token.text);
        }
        const std::string& text = token.text;
        if (text == "=") {
            return HasWordBefore(tokens, *beforeType, "using");
        }
        if (text == "::") {
            return IsTypeQualifierStartInDeclaratorContext(tokens, *beforeType);
        }
        return text == "(" ||
            text == "[" ||
            text == "{" ||
            text == "," ||
            text == "<" ||
            IsPointerOrReferenceDeclaratorToken(text) ||
            text == ":";
    }

    bool IsTypeQualifierStartInDeclaratorContext(const std::vector<Token>& tokens, size_t qualifier) const {
        const std::optional<size_t> beforeQualifier = PreviousNonNewlineIndex(tokens, qualifier);
        if (!beforeQualifier) {
            return true;
        }
        const std::string& text = tokens[*beforeQualifier].text;
        if (tokens[*beforeQualifier].kind == TokenKind::Word) {
            return IsTypeContextWord(text);
        }
        return text == "(" ||
            text == "[" ||
            text == "{" ||
            text == "," ||
            text == "<" ||
            text == "=" ||
            IsPointerOrReferenceDeclaratorToken(text) ||
            text == ":";
    }

    bool HasWordBefore(const std::vector<Token>& tokens, size_t before, std::string_view word) const {
        for (size_t index = 0; index < before; ++index) {
            if (tokens[index].kind == TokenKind::Word && tokens[index].text == word) {
                return true;
            }
        }
        return false;
    }

    std::optional<size_t> TypeNameStartBeforePointer(const std::vector<Token>& tokens, size_t index) const {
        std::optional<size_t> start = PreviousNonNewlineIndex(tokens, index);
        if (!start) {
            return std::nullopt;
        }
        start = UnwrapTemplateTypeNameStart(tokens, *start);
        if (!start) {
            return std::nullopt;
        }
        while (IsPointerOrReferenceDeclaratorToken(tokens[*start].text)) {
            if (!IsPointerOrReferenceDeclarator(tokens, *start)) {
                return std::nullopt;
            }
            start = PreviousNonNewlineIndex(tokens, *start);
            if (!start) {
                return std::nullopt;
            }
            start = UnwrapTemplateTypeNameStart(tokens, *start);
            if (!start) {
                return std::nullopt;
            }
        }
        while (*start > 1) {
            const std::optional<size_t> before = PreviousNonNewlineIndex(tokens, *start);
            if (!before || tokens[*before].text != "::") {
                break;
            }
            const std::optional<size_t> qualifier = PreviousNonNewlineIndex(tokens, *before);
            if (!qualifier || (tokens[*qualifier].kind != TokenKind::Word && tokens[*qualifier].text != ">")) {
                break;
            }
            start = UnwrapTemplateTypeNameStart(tokens, *qualifier);
            if (!start) {
                return std::nullopt;
            }
        }
        return start;
    }

    std::optional<size_t> UnwrapTemplateTypeNameStart(const std::vector<Token>& tokens, size_t index) const {
        if (!IsTemplateAngleCloseToken(tokens, index)) {
            return index;
        }
        const std::optional<size_t> open = FindTemplateAngleOpen(tokens, index);
        if (!open) {
            return index;
        }
        const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
        if (!beforeOpen) {
            return std::nullopt;
        }
        return beforeOpen;
    }

    bool IsTypeContextWord(std::string_view text) const {
        static constexpr std::string_view kWords[] = {
            "class",
            "const",
            "enum",
            "long",
            "mutable",
            "short",
            "signed",
            "static",
            "struct",
            "typename",
            "unsigned",
            "virtual",
            "volatile"
        };
        return std::find(std::begin(kWords), std::end(kWords), text) != std::end(kWords);
    }

    bool Fits(int indentLevel, std::string_view text) const {
        return static_cast<int>(indentLevel * config_.indentWidth + text.size()) <= config_.columnLimit;
    }

    std::optional<size_t> InlineBudget(int indentLevel, std::string_view prefix, std::string_view suffix) const {
        const int usedWidth =
            indentLevel * config_.indentWidth + static_cast<int>(prefix.size()) + static_cast<int>(suffix.size());
        if (usedWidth >= config_.columnLimit) {
            return 0;
        }
        return static_cast<size_t>(config_.columnLimit - usedWidth);
    }

    std::string Indent() const {
        return Indent(indentLevel_);
    }

    std::string Indent(int indentLevel) const {
        return Spaces(indentLevel * config_.indentWidth);
    }

    void AppendSuffix(std::string& line, std::string_view suffix) const {
        if (suffix.empty()) {
            return;
        }
        const size_t comment = line.find("  //");
        if (comment == std::string::npos) {
            line += suffix;
        } else {
            line.insert(comment, suffix);
        }
    }

    std::vector<std::string> FormatDelimitedElement(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string_view suffix,
        bool isInitializerElement,
        bool indentSplitChains,
        bool indentLogicalSplitChains,
        bool preserveLeadingBlankSeparator
    ) const {
        return FormatDelimitedElementResult(
            tokens,
            indentLevel,
            suffix,
            isInitializerElement,
            indentSplitChains,
            indentLogicalSplitChains,
            preserveLeadingBlankSeparator
        ).lines;
    }

    LayoutResult FormatDelimitedElementResult(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string_view suffix,
        bool isInitializerElement,
        bool indentSplitChains,
        bool indentLogicalSplitChains,
        bool preserveLeadingBlankSeparator
    ) const {
        std::vector<std::string> lines;
        size_t begin = 0;
        bool preserveBlankSeparator = preserveLeadingBlankSeparator;
        while (begin < tokens.size()) {
            size_t newlineCount = 0;
            while (begin < tokens.size() && tokens[begin].kind == TokenKind::Newline) {
                ++newlineCount;
                ++begin;
            }
            if (begin >= tokens.size()) {
                return ScoreLayoutCandidate(std::move(lines), true, 0, 0);
            }
            const bool hasBlankSeparator = newlineCount > 1;
            if (tokens[begin].kind == TokenKind::LineComment) {
                if (hasBlankSeparator && preserveBlankSeparator) {
                    lines.push_back({});
                }
                lines.push_back(Indent(indentLevel) + TrimRight(TrimLeft(tokens[begin].text)));
                ++begin;
                preserveBlankSeparator = true;
                continue;
            }
            if (hasBlankSeparator && preserveBlankSeparator) {
                lines.push_back({});
            }
            break;
        }
        std::vector<Token> remaining(tokens.begin() + static_cast<std::ptrdiff_t>(begin), tokens.end());
        if (remaining.empty() || ContainsOnlyNewlines(remaining, 0, remaining.size())) {
            return ScoreLayoutCandidate(std::move(lines), true, 0, 0);
        }
        LayoutResult remainingResult;
        if (isInitializerElement) {
            remainingResult = FormatRangeResult(remaining, indentLevel, {}, std::string(suffix), true);
        } else {
            remainingResult = FormatRangeResult(
                remaining,
                indentLevel,
                {},
                std::string(suffix),
                indentSplitChains,
                indentLogicalSplitChains
            );
        }
        lines.insert(lines.end(), remainingResult.lines.begin(), remainingResult.lines.end());
        LayoutResult result = ScoreLayoutCandidate(std::move(lines), remainingResult.deepestBreakDepth < 0, 0, 0);
        MergeBreakDepth(result, remainingResult, 0);
        return result;
    }

    void AppendSplitElementLines(
        std::vector<std::string>& lines,
        const std::vector<std::string>& elementLines,
        bool combineNestedInitializerBoundary
    ) const {
        if (elementLines.empty()) {
            return;
        }
        size_t firstElementLine = 0;
        if (
            combineNestedInitializerBoundary &&
            !lines.empty() &&
            tools::lint::Trim(lines.back()) == "}," &&
            tools::lint::Trim(elementLines.front()) == "{"
        ) {
            lines.back() = TrimRight(lines.back()) + " " + TrimLeft(elementLines.front());
            firstElementLine = 1;
        }
        lines.insert(
            lines.end(),
            elementLines.begin() + static_cast<std::ptrdiff_t>(firstElementLine),
            elementLines.end()
        );
    }

    bool ShouldForceSplit(const std::vector<Token>& tokens) const {
        if (HasMultiStatementLambdaBody(tokens)) {
            return true;
        }
        if (HasLineTerminatedStringLiteralSequence(tokens)) {
            return true;
        }
        if (HasExpandableEscapeStringLiteralSequence(tokens)) {
            return true;
        }
        if (!HasLineComment(tokens)) {
            return false;
        }
        if (!LineCommentBeforeTopLevelStatementTerminator(tokens)) {
            return false;
        }
        return FindFirstWrappableGroupPair(tokens).has_value() || CanSplitOperatorChain(tokens);
    }

    bool HasLineTerminatedStringLiteralSequence(const std::vector<Token>& tokens) const {
        const Token* previousStringLiteral = nullptr;
        for (const Token& token : tokens) {
            if (token.kind == TokenKind::Newline) {
                continue;
            }
            if (token.kind != TokenKind::StringLiteral) {
                previousStringLiteral = nullptr;
                continue;
            }
            if (previousStringLiteral != nullptr && StringLiteralEndsWithEscapedNewline(previousStringLiteral->text)) {
                return true;
            }
            previousStringLiteral = &token;
        }
        return false;
    }

    bool HasExpandableEscapeStringLiteralSequence(const std::vector<Token>& tokens) const {
        const Token* previousStringLiteral = nullptr;
        for (const Token& token : tokens) {
            if (token.kind == TokenKind::Newline) {
                continue;
            }
            if (token.kind != TokenKind::StringLiteral) {
                previousStringLiteral = nullptr;
                continue;
            }
            if (previousStringLiteral != nullptr && HasExpandableEscapeStringLiteralBoundary(
                previousStringLiteral->text,
                token.text
            )) {
                return true;
            }
            previousStringLiteral = &token;
        }
        return false;
    }

    bool HasExpandableEscapeStringLiteralBoundary(
        std::string_view previousLiteral,
        std::string_view currentLiteral
    ) const {
        if (!IsOrdinaryStringLiteralText(previousLiteral) || !IsOrdinaryStringLiteralText(currentLiteral)) {
            return false;
        }
        const std::string_view currentContent = StringLiteralContent(currentLiteral);
        return !currentContent.empty() &&
            HasTrailingExpandableEscape(StringLiteralContent(previousLiteral), currentContent.front());
    }

    bool HasMultiStatementLambdaBody(const std::vector<Token>& tokens) const {
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (!IsLambdaBodyOpenToken(tokens, index)) {
                continue;
            }
            const std::optional<size_t> close = FindMatchingClose(tokens, index);
            if (!close) {
                continue;
            }
            if (CountTopLevelLambdaBodyStatements(tokens, index + 1, *close) > 1) {
                return true;
            }
            index = *close;
        }
        return false;
    }

    size_t CountTopLevelLambdaBodyStatements(const std::vector<Token>& tokens, size_t begin, size_t end) const {
        int depth = 0;
        size_t count = 0;
        for (size_t index = begin; index < end; ++index) {
            if (tokens[index].kind == TokenKind::Newline) {
                continue;
            }
            if (depth == 0) {
                if (tokens[index].text == ";") {
                    ++count;
                } else if (tokens[index].text == "{" && IsTopLevelLambdaStatementBlockOpen(tokens, index, begin)) {
                    ++count;
                }
            }
            UpdateDepth(tokens[index], depth);
        }
        return count;
    }

    bool IsTopLevelLambdaStatementBlockOpen(const std::vector<Token>& tokens, size_t open, size_t bodyBegin) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, open);
        if (!previous || *previous < bodyBegin) {
            return true;
        }
        const std::string& previousText = tokens[*previous].text;
        if (previousText == "else" || previousText == "try" || previousText == "finally" || previousText == "do") {
            return true;
        }
        if (previousText != ")") {
            return false;
        }
        const std::optional<size_t> groupOpen = FindMatchingOpen(tokens, *previous);
        if (!groupOpen || *groupOpen < bodyBegin) {
            return false;
        }
        const std::optional<size_t> beforeGroup = PreviousNonNewlineIndex(tokens, *groupOpen);
        return beforeGroup && *beforeGroup >= bodyBegin && IsControlKeyword(tokens[*beforeGroup].text);
    }

    bool HasOriginalBlankSeparator(const std::vector<Token>& tokens) const {
        for (size_t index = 1; index < tokens.size(); ++index) {
            if (tokens[index - 1].kind == TokenKind::Newline && tokens[index].kind == TokenKind::Newline) {
                return true;
            }
        }
        return false;
    }

    bool HasLineComment(const std::vector<Token>& tokens) const {
        return std::any_of(tokens.begin(), tokens.end(), [](const Token& token) {
            return token.kind == TokenKind::LineComment;
        });
    }

    bool LineCommentBeforeTopLevelStatementTerminator(const std::vector<Token>& tokens) const {
        int depth = 0;
        bool sawLineComment = false;
        for (const Token& token : tokens) {
            if (depth == 0 && token.text == ";") {
                return sawLineComment;
            }
            if (token.kind == TokenKind::LineComment) {
                sawLineComment = true;
            }
            UpdateDepth(token, depth);
        }
        return sawLineComment;
    }

    std::optional<size_t> FindTopLevelAssignment(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (depth == 0 && IsAssignmentOperator(token.text) && !IsOperatorFunctionNameToken(tokens, index)) {
                return index;
            }
            UpdateDepth(token, depth);
        }
        return std::nullopt;
    }

    std::optional<GroupPair> FindFirstWrappableGroupPair(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (IsWrappableGroupOpen(tokens, index) && depth == 0) {
                if (std::optional<size_t> close = FindWrappableGroupClose(tokens, index)) {
                    if (
                        !IsEmptyGroupPair(tokens, index, *close) &&
                        !IsNonWrappablePrefixGroup(tokens, index, *close) &&
                        !IsFunctionPointerDeclaratorGroupOpen(tokens, index)
                    ) {
                        return GroupPair{index, *close};
                    }
                }
            }
            UpdateDepth(tokens, index, depth);
        }
        return std::nullopt;
    }

    std::optional<GroupPair> FindFirstWrappableGroupPairWithTopLevelSeparatorAndLambda(
        const std::vector<Token>& tokens,
        char separator
    ) const {
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (IsGroupOpen(tokens[index].text)) {
                if (std::optional<size_t> close = FindWrappableGroupClose(tokens, index)) {
                    if (
                        !IsEmptyGroupPair(tokens, index, *close) &&
                        !IsNonWrappablePrefixGroup(tokens, index, *close) &&
                        !IsFunctionPointerDeclaratorGroupOpen(tokens, index)
                    ) {
                        std::vector<Token> inner(
                            tokens.begin() + static_cast<std::ptrdiff_t>(index + 1),
                            tokens.begin() + static_cast<std::ptrdiff_t>(*close)
                        );
                        if (ContainsTopLevelSeparator(inner, separator) && FindTopLevelLambdaBodyOpen(inner)) {
                            return GroupPair{index, *close};
                        }
                    }
                }
            }
        }
        return std::nullopt;
    }

    bool IsWrappableGroupOpen(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size()) {
            return false;
        }
        return IsGroupOpen(tokens[index].text) || IsWrappableTemplateAngleOpen(tokens, index);
    }

    bool IsWrappableTemplateAngleOpen(const std::vector<Token>& tokens, size_t index) const {
        if (!IsTemplateAngleOpen(tokens, index)) {
            return false;
        }
        const std::optional<size_t> close = FindTemplateAngleClose(tokens, index);
        if (!close || tokens[*close].text != ">") {
            return false;
        }
        std::vector<Token> inner(
            tokens.begin() + static_cast<std::ptrdiff_t>(index + 1),
            tokens.begin() + static_cast<std::ptrdiff_t>(*close)
        );
        return ContainsTopLevelSeparator(inner, ',');
    }

    bool IsTemplateAngleGroup(const std::vector<Token>& tokens, GroupPair group) const {
        return group.open < tokens.size() &&
            group.close < tokens.size() &&
            tokens[group.open].text == "<" &&
            IsTemplateAngleOpen(tokens, group.open);
    }

    std::optional<size_t> FindWrappableGroupClose(const std::vector<Token>& tokens, size_t open) const {
        if (open >= tokens.size()) {
            return std::nullopt;
        }
        if (IsTemplateAngleOpen(tokens, open)) {
            const std::optional<size_t> close = FindTemplateAngleClose(tokens, open);
            if (close && tokens[*close].text == ">") {
                return close;
            }
            return std::nullopt;
        }
        return FindMatchingClose(tokens, open);
    }

    bool IsNonWrappablePrefixGroup(const std::vector<Token>& tokens, size_t open, size_t close) const {
        return IsParenthesizedCalleeGroup(tokens, open, close) || IsDeclspecGroup(tokens, open);
    }

    bool IsParenthesizedCalleeGroup(const std::vector<Token>& tokens, size_t open, size_t close) const {
        if (open >= tokens.size() || tokens[open].text != "(") {
            return false;
        }
        if (IsFunctionPointerDeclaratorGroupOpen(tokens, open)) {
            return false;
        }
        const size_t next = NextSignificantIndex(tokens, close + 1);
        return next < tokens.size() && tokens[next].text == "(";
    }

    bool IsDeclspecGroup(const std::vector<Token>& tokens, size_t open) const {
        if (open >= tokens.size() || tokens[open].text != "(") {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, open);
        return previous && tokens[*previous].text == "__declspec";
    }

    bool IsEmptyGroupPair(const std::vector<Token>& tokens, size_t open, size_t close) const {
        for (size_t index = open + 1; index < close; ++index) {
            if (tokens[index].kind != TokenKind::Newline) {
                return false;
            }
        }
        return true;
    }

    std::optional<size_t> FindMatchingClose(const std::vector<Token>& tokens, size_t openIndex) const {
        const std::string close = MatchingClose(tokens[openIndex].text);
        int depth = 0;
        for (size_t index = openIndex; index < tokens.size(); ++index) {
            if (tokens[index].text == tokens[openIndex].text) {
                ++depth;
            } else if (tokens[index].text == close) {
                --depth;
                if (depth == 0) {
                    return index;
                }
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> FindMatchingOpen(const std::vector<Token>& tokens, size_t closeIndex) const {
        const std::string& close = tokens[closeIndex].text;
        std::string open;
        if (close == ")") {
            open = "(";
        } else if (close == "]") {
            open = "[";
        } else if (close == "}") {
            open = "{";
        } else {
            return std::nullopt;
        }
        int depth = 0;
        for (size_t index = closeIndex + 1; index > 0; --index) {
            const size_t current = index - 1;
            if (tokens[current].text == close) {
                ++depth;
            } else if (tokens[current].text == open) {
                --depth;
                if (depth == 0) {
                    return current;
                }
            }
        }
        return std::nullopt;
    }

    std::vector<std::vector<Token>> SplitTopLevel(const std::vector<Token>& tokens, char separator) const {
        std::vector<std::vector<Token>> result;
        std::vector<Token> current;
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (depth == 0 && token.text.size() == 1 && token.text[0] == separator) {
                if (index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LineComment) {
                    current.push_back(tokens[index + 1]);
                    ++index;
                }
                result.push_back(current);
                current.clear();
                continue;
            }
            current.push_back(token);
            UpdateDepth(tokens, index, depth);
        }
        result.push_back(current);
        return result;
    }

    bool ContainsTopLevelSeparator(const std::vector<Token>& tokens, char separator) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (depth == 0 && token.text.size() == 1 && token.text[0] == separator) {
                return true;
            }
            UpdateDepth(tokens, index, depth);
        }
        return false;
    }

    bool HasTopLevelStatementTerminator(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (const Token& token : tokens) {
            if (depth == 0 && token.text == ";") {
                return true;
            }
            UpdateDepth(token, depth);
        }
        return false;
    }

    bool CanSplitOperatorChain(const std::vector<Token>& tokens) const {
        return SelectChainKind(tokens) != ChainKind::None;
    }

    bool IsTrueOperatorChain(const std::vector<Token>& tokens, ChainKind chainKind) const {
        if (chainKind == ChainKind::None) {
            return false;
        }
        if (chainKind == ChainKind::Ternary) {
            return !SplitSingleTopLevelTernary(tokens).has_value();
        }
        return CountTopLevelChainBreakOperators(tokens, chainKind) > 1;
    }

    size_t CountTopLevelChainBreakOperators(const std::vector<Token>& tokens, ChainKind chainKind) const {
        size_t count = 0;
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth == 0 && IsChainBreakOperator(tokens, index, chainKind)) {
                ++count;
            }
        }
        return count;
    }

    ChainKind SelectChainKind(const std::vector<Token>& tokens) const {
        int depth = 0;
        bool hasTernary = false;
        bool hasLogical = false;
        bool hasBitwise = false;
        bool hasEquality = false;
        bool hasRelational = false;
        bool hasShift = false;
        bool hasPlus = false;
        bool hasMinus = false;
        bool hasStar = false;
        bool hasDivisionOrModulo = false;
        for (size_t index = 0; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth != 0 || IsTemplateAngleToken(tokens, index)) {
                continue;
            }
            const std::string& text = tokens[index].text;
            if (text == "?") {
                hasTernary = true;
            } else if (IsPointerOrReferenceDeclaratorToken(text) && IsPointerOrReferenceDeclarator(tokens, index)) {
                continue;
            } else if (text == "&&" || text == "||") {
                hasLogical = true;
            } else if (text == "|" || text == "^") {
                hasBitwise = true;
            } else if (text == "==" || text == "!=") {
                hasEquality = true;
            } else if (text == "<" || text == ">" || text == "<=" || text == ">=") {
                hasRelational = true;
            } else if (text == "<<" || text == ">>") {
                hasShift = true;
            } else if ((text == "+" || text == "-") && IsUnaryPrefixOperator(tokens, index)) {
                continue;
            } else if (text == "+") {
                hasPlus = true;
            } else if (text == "-") {
                hasMinus = true;
            } else if ((text == "*" || text == "&") && IsPointerOrReferenceDeclarator(tokens, index)) {
                continue;
            } else if (text == "*") {
                hasStar = true;
            } else if (text == "/" || text == "%") {
                hasDivisionOrModulo = true;
            }
        }
        if (hasTernary) {
            return ChainKind::Ternary;
        }
        if (hasLogical) {
            return ChainKind::Logical;
        }
        if (hasBitwise) {
            return ChainKind::Bitwise;
        }
        if (hasEquality) {
            return ChainKind::Equality;
        }
        if (hasRelational) {
            return ChainKind::Relational;
        }
        if (hasShift) {
            return ChainKind::Shift;
        }
        if (hasPlus && !hasMinus) {
            return ChainKind::Additive;
        }
        if (hasStar && !hasDivisionOrModulo) {
            return ChainKind::Multiplicative;
        }
        return ChainKind::None;
    }

    bool IsChainBreakOperator(const std::vector<Token>& tokens, size_t index, ChainKind chainKind) const {
        if (chainKind == ChainKind::None || IsTemplateAngleToken(tokens, index)) {
            return false;
        }
        if (IsPointerOrReferenceDeclaratorToken(tokens[index].text) && IsPointerOrReferenceDeclarator(tokens, index)) {
            return false;
        }
        if ((tokens[index].text == "+" || tokens[index].text == "-") && IsUnaryPrefixOperator(tokens, index)) {
            return false;
        }
        const std::string& text = tokens[index].text;
        switch (chainKind) {
            case ChainKind::Ternary:
                return text == "?" || text == ":";
            case ChainKind::Logical:
                return text == "&&" || text == "||";
            case ChainKind::Bitwise:
                return text == "|" || text == "^";
            case ChainKind::Equality:
                return text == "==" || text == "!=";
            case ChainKind::Relational:
                return text == "<" || text == ">" || text == "<=" || text == ">=";
            case ChainKind::Shift:
                return text == "<<" || text == ">>";
            case ChainKind::Additive:
                return text == "+";
            case ChainKind::Multiplicative:
                return text == "*";
            case ChainKind::None:
                return false;
        }
        return false;
    }

    bool StartsWithInitializerList(const std::vector<Token>& tokens, size_t index) const {
        const size_t first = NextSignificantIndex(tokens, index);
        return first < tokens.size() && tokens[first].text == "{";
    }

    std::optional<size_t> FindConstructorInitializerColon(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (depth == 0 && tokens[index].text == ":" && IsConstructorInitializerColon(tokens, index)) {
                return index;
            }
            UpdateDepth(tokens[index], depth);
        }
        return std::nullopt;
    }

    bool IsConstructorInitializerColon(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != ":") {
            return false;
        }
        if (HasTopLevelTokenBefore(tokens, index, "?")) {
            return false;
        }
        const std::optional<size_t> parametersClose = FindConstructorParameterListCloseBeforeColon(tokens, index);
        if (!parametersClose) {
            return false;
        }
        const std::optional<size_t> open = FindMatchingOpen(tokens, *parametersClose);
        if (!open) {
            return false;
        }
        const std::optional<size_t> functionName = PreviousNonNewlineIndex(tokens, *open);
        if (!functionName || tokens[*functionName].kind != TokenKind::Word) {
            return false;
        }
        const size_t afterColon = NextSignificantIndex(tokens, index + 1);
        return afterColon < tokens.size();
    }

    std::optional<size_t> FindConstructorParameterListCloseBeforeColon(
        const std::vector<Token>& tokens,
        size_t colon
    ) const {
        size_t cursor = colon;
        while (std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, cursor)) {
            if (tokens[*previous].text == ")") {
                const std::optional<size_t> open = FindMatchingOpen(tokens, *previous);
                if (!open) {
                    return std::nullopt;
                }
                const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
                if (beforeOpen && tokens[*beforeOpen].kind == TokenKind::Word && IsConstructorTrailingQualifierGroup(
                    tokens[*beforeOpen].text
                )) {
                    cursor = *beforeOpen;
                    continue;
                }
                return *previous;
            }
            if (
                tokens[*previous].kind == TokenKind::Word && IsConstructorTrailingQualifierWord(tokens[*previous].text)
            ) {
                cursor = *previous;
                continue;
            }
            break;
        }
        return std::nullopt;
    }

    static bool IsConstructorTrailingQualifierWord(std::string_view text) {
        return text == "noexcept";
    }

    static bool IsConstructorTrailingQualifierGroup(std::string_view text) {
        return text == "noexcept" || text == "requires";
    }

    bool HasTopLevelTokenBefore(const std::vector<Token>& tokens, size_t before, std::string_view text) const {
        int depth = 0;
        for (size_t index = 0; index < before; ++index) {
            if (depth == 0 && tokens[index].text == text) {
                return true;
            }
            UpdateDepth(tokens[index], depth);
        }
        return false;
    }

    std::optional<size_t> FindLambdaBodyOpen(const std::vector<Token>& tokens) const {
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (!IsLambdaBodyOpenToken(tokens, index)) {
                continue;
            }
            const std::optional<size_t> close = FindMatchingClose(tokens, index);
            if (close && IsEmptyGroupPair(tokens, index, *close)) {
                continue;
            }
            if (IsLambdaBodyOpenToken(tokens, index)) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> FindTopLevelLambdaBodyOpen(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (depth == 0 && IsLambdaBodyOpenToken(tokens, index)) {
                const std::optional<size_t> close = FindMatchingClose(tokens, index);
                if (!close || !IsEmptyGroupPair(tokens, index, *close)) {
                    return index;
                }
            }
            UpdateDepth(tokens, index, depth);
        }
        return std::nullopt;
    }

    bool IsPointerOrReferenceDeclarator(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size() || !IsPointerOrReferenceDeclaratorToken(tokens[index].text)) {
            return false;
        }
        const size_t nextIndex = NextSignificantIndex(tokens, index + 1);
        const Token* next = nextIndex < tokens.size() ? &tokens[nextIndex] : nullptr;
        const bool beforeDeclaratorName = next != nullptr && next->kind == TokenKind::Word;
        const bool beforeTemplateClose = next != nullptr && IsTemplateAngleCloseToken(tokens, nextIndex);
        const bool beforeStructuredBinding = next != nullptr && tokens[index].text != "*" && next->text == "[";
        const bool beforeUnnamedDeclaratorEnd =
            next == nullptr || next->text == ")" || next->text == "," || next->text == "=" || next->text == ";";
        const bool beforePointerOrReferenceDeclarator =
            next != nullptr && IsPointerOrReferenceDeclaratorToken(next->text);
        const bool beforeFunctionPointerDeclarator =
            next != nullptr && tokens[index].text == "*" && IsFunctionPointerDeclaratorGroupOpen(tokens, nextIndex);
        const bool beforeDeclarator = beforeDeclaratorName ||
            beforeTemplateClose ||
            beforeStructuredBinding ||
            beforeUnnamedDeclaratorEnd ||
            beforePointerOrReferenceDeclarator ||
            beforeFunctionPointerDeclarator;
        return beforeDeclarator &&
            IsLikelyTypeBeforePointer(tokens, index) &&
            IsLikelyDeclaratorContextBeforePointer(tokens, index);
    }

    static bool IsPointerOrReferenceDeclaratorToken(std::string_view text) {
        return text == "*" || text == "&" || text == "&&" || text == "^" || text == "%";
    }

    bool IsFunctionPointerDeclaratorGroupOpen(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != "(") {
            return false;
        }
        const std::optional<size_t> close = FindMatchingClose(tokens, index);
        if (!close) {
            return false;
        }
        const size_t afterClose = NextSignificantIndex(tokens, *close + 1);
        if (afterClose >= tokens.size() || tokens[afterClose].text != "(") {
            return false;
        }
        bool sawPointer = false;
        bool sawSignificant = false;
        for (size_t inner = index + 1; inner < *close; ++inner) {
            if (tokens[inner].kind == TokenKind::Newline) {
                continue;
            }
            sawSignificant = true;
            if (
                tokens[inner].kind != TokenKind::Word &&
                tokens[inner].text != "::" &&
                !IsPointerOrReferenceDeclaratorToken(tokens[inner].text)
            ) {
                return false;
            }
            if (IsPointerOrReferenceDeclaratorToken(tokens[inner].text)) {
                sawPointer = true;
            }
        }
        return sawSignificant && sawPointer;
    }

    bool IsFunctionPointerDeclaratorContextBeforeGroup(const std::vector<Token>& tokens, size_t index) const {
        if (FunctionPointerDeclaratorGroupStartsWithCallingConvention(tokens, index)) {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        if (!previous) {
            return false;
        }
        if (IsPointerOrReferenceDeclaratorToken(tokens[*previous].text) && IsPointerOrReferenceDeclarator(
            tokens,
            *previous
        )) {
            return true;
        }
        return IsLikelyTypeBeforePointer(tokens, index) && IsLikelyDeclaratorContextBeforePointer(tokens, index);
    }

    bool FunctionPointerDeclaratorGroupStartsWithCallingConvention(
        const std::vector<Token>& tokens,
        size_t index
    ) const {
        if (index >= tokens.size() || tokens[index].text != "(") {
            return false;
        }
        const size_t first = NextSignificantIndex(tokens, index + 1);
        return first < tokens.size() &&
            tokens[first].kind == TokenKind::Word &&
            IsCallingConventionToken(tokens[first].text);
    }

    bool IsCodeBlockOpen() const {
        if (pendingTokens_.empty()) {
            return false;
        }
        const std::string first = pendingTokens_.front().text;
        if (
            first == "namespace" ||
            first == "class" ||
            first == "struct" ||
            first == "enum" ||
            first == "if" ||
            first == "for" ||
            first == "while" ||
            first == "switch" ||
            first == "catch" ||
            first == "try" ||
            first == "finally" ||
            first == "do" ||
            first == "else"
        ) {
            return true;
        }
        if (ContainsWord(pendingTokens_, "class") || ContainsWord(pendingTokens_, "struct") || ContainsWord(
            pendingTokens_,
            "enum"
        )) {
            return true;
        }
        if (IsBracedConstructorExpressionOpen()) {
            return false;
        }
        if (ContainsTopLevelAssignment(pendingTokens_)) {
            return false;
        }
        return IsFunctionDefinitionBlock(pendingTokens_);
    }

    bool IsBracedConstructorExpressionOpen() const {
        if (pendingTokens_.empty()) {
            return false;
        }
        const std::optional<size_t> last = PreviousNonNewlineIndex(pendingTokens_, pendingTokens_.size());
        if (!last || !IsLikelyTypeNameToken(pendingTokens_, *last)) {
            return false;
        }
        const std::string& first = pendingTokens_.front().text;
        if (first == "return" || first == "co_return" || first == "throw") {
            return true;
        }
        const std::optional<size_t> beforeType = PreviousNonNewlineIndex(pendingTokens_, *last);
        if (!beforeType) {
            return false;
        }
        const std::string& previous = pendingTokens_[*beforeType].text;
        return previous == "?" ||
            previous == ":" ||
            previous == "(" ||
            previous == "[" ||
            previous == "{" ||
            previous == "," ||
            previous == "=" ||
            IsBinaryOperatorLike(previous);
    }

    BlockKind ClassifyBlock(const std::vector<Token>& tokens) const {
        if (!tokens.empty() && tokens.front().text == "namespace") {
            return BlockKind::NamespaceDeclaration;
        }
        if (!tokens.empty() && tokens.front().text == "switch") {
            return BlockKind::SwitchStatement;
        }
        if (!tokens.empty() && tokens.front().text == "do") {
            return BlockKind::DoStatement;
        }
        const std::optional<size_t> typeDeclarationKeyword = FindTypeDeclarationKeyword(tokens);
        if (typeDeclarationKeyword && tokens[*typeDeclarationKeyword].text == "enum") {
            return BlockKind::EnumDeclaration;
        }
        if (typeDeclarationKeyword) {
            return BlockKind::TypeDeclaration;
        }
        if (IsFunctionDefinitionBlock(tokens)) {
            return BlockKind::FunctionDefinition;
        }
        return BlockKind::Other;
    }

    DeclarationKind DeclarationKindForBlock(BlockKind blockKind) const {
        switch (blockKind) {
            case BlockKind::NamespaceDeclaration:
                return DeclarationKind::NamespaceDeclaration;
            case BlockKind::CaseScope:
            case BlockKind::SwitchStatement:
            case BlockKind::DoStatement:
                return DeclarationKind::None;
            case BlockKind::EnumDeclaration:
            case BlockKind::TypeDeclaration:
                return DeclarationKind::TypeDeclaration;
            case BlockKind::FunctionDefinition:
                return DeclarationKind::Method;
            case BlockKind::Other:
                return DeclarationKind::None;
        }
        return DeclarationKind::None;
    }

    bool IsTypeDeclarationTrailingDeclarator(BlockKind blockKind, const std::vector<Token>& tokens, size_t next) const {
        if (blockKind != BlockKind::TypeDeclaration && blockKind != BlockKind::EnumDeclaration) {
            return false;
        }
        if (next >= tokens.size()) {
            return false;
        }
        const Token& token = tokens[next];
        return token.kind == TokenKind::Word || IsPointerOrReferenceDeclaratorToken(token.text);
    }

    DeclarationKind ClassifySemicolonDeclaration(const std::vector<Token>& tokens) const {
        if (!IsDeclarationContext() || tokens.empty()) {
            return DeclarationKind::None;
        }
        const std::string& first = tokens.front().text;
        if (first == "using" || first == "typedef" || first == "static_assert") {
            return DeclarationKind::Field;
        }
        if (FindTypeDeclarationKeyword(tokens)) {
            return DeclarationKind::TypeDeclaration;
        }
        if (
            (!ContainsTopLevelAssignment(tokens) || IsDefaultedDeletedOrPureVirtualMethodDeclaration(tokens)) &&
            ContainsTopLevelToken(tokens, ")")
        ) {
            return DeclarationKind::Method;
        }
        return DeclarationKind::Field;
    }

    bool IsDefaultedDeletedOrPureVirtualMethodDeclaration(const std::vector<Token>& tokens) const {
        const std::optional<size_t> assignment = FindTopLevelAssignment(tokens);
        if (!assignment) {
            return false;
        }
        const size_t marker = NextSignificantIndex(tokens, *assignment + 1);
        if (marker >= tokens.size()) {
            return false;
        }
        return tokens[marker].text == "default" || tokens[marker].text == "delete" || tokens[marker].text == "0";
    }

    static bool IsTypeDeclarationLeadingSpecifierWord(std::string_view text) {
        static constexpr std::string_view kWords[] = {
            "const",
            "consteval",
            "constexpr",
            "constinit",
            "extern",
            "inline",
            "mutable",
            "static",
            "typedef",
            "volatile"
        };
        return std::find(std::begin(kWords), std::end(kWords), text) != std::end(kWords);
    }

    std::optional<size_t> FindTypeDeclarationKeyword(const std::vector<Token>& tokens) const {
        size_t first = NextSignificantIndex(tokens, 0);
        if (first >= tokens.size()) {
            return std::nullopt;
        }
        if (tokens[first].text == "template") {
            const size_t groupOpen = NextSignificantIndex(tokens, first + 1);
            if (groupOpen >= tokens.size() || tokens[groupOpen].text != "<") {
                return std::nullopt;
            }
            const std::optional<size_t> groupClose = FindTemplateAngleClose(tokens, groupOpen);
            if (!groupClose) {
                return std::nullopt;
            }
            first = NextSignificantIndex(tokens, *groupClose + 1);
            if (first >= tokens.size()) {
                return std::nullopt;
            }
        }
        while (first < tokens.size() && tokens[first].kind == TokenKind::Word) {
            if (tokens[first].text == "class" || tokens[first].text == "struct" || tokens[first].text == "enum") {
                return first;
            }
            if (!IsTypeDeclarationLeadingSpecifierWord(tokens[first].text)) {
                return std::nullopt;
            }
            first = NextSignificantIndex(tokens, first + 1);
        }
        return std::nullopt;
    }

    bool IsDeclarationContext() const {
        return std::none_of(blockStack_.begin(), blockStack_.end(), [](const BlockState& block) {
            return block.kind == BlockKind::FunctionDefinition || block.kind == BlockKind::Other;
        });
    }

    bool IsFunctionDefinitionBlock(const std::vector<Token>& tokens) const {
        if (tokens.empty() || ContainsTopLevelAssignment(tokens)) {
            return false;
        }
        const std::string& first = tokens.front().text;
        if (
            first == "namespace" ||
            first == "class" ||
            first == "struct" ||
            first == "enum" ||
            first == "if" ||
            first == "for" ||
            first == "while" ||
            first == "switch" ||
            first == "catch" ||
            first == "try" ||
            first == "finally" ||
            first == "do" ||
            first == "else"
        ) {
            return false;
        }
        return ContainsTopLevelFunctionParameterList(tokens);
    }

    BlockState PopBlockState() {
        if (blockStack_.empty()) {
            return {};
        }
        const BlockState result = blockStack_.back();
        blockStack_.pop_back();
        return result;
    }

    bool ContainsTopLevelAssignment(const std::vector<Token>& tokens) const {
        return FindTopLevelAssignment(tokens).has_value();
    }

    bool ContainsTopLevelToken(const std::vector<Token>& tokens, std::string_view tokenText) const {
        return FindTopLevelToken(tokens, tokenText).has_value();
    }

    bool ContainsTopLevelFunctionParameterList(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (depth == 0 && IsTemplateAngleOpen(tokens, index)) {
                if (std::optional<size_t> close = FindTemplateAngleClose(tokens, index)) {
                    index = *close;
                    continue;
                }
            }
            if (depth == 0 && tokens[index].text == "(" && IsFunctionParameterListOpen(tokens, index)) {
                return true;
            }
            UpdateDepth(tokens[index], depth);
        }
        return false;
    }

    bool IsFunctionParameterListOpen(const std::vector<Token>& tokens, size_t open) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, open);
        if (!previous) {
            return false;
        }
        if (IsFunctionPointerDeclaratorGroupOpen(tokens, open)) {
            return false;
        }
        const Token& token = tokens[*previous];
        if (token.kind == TokenKind::Word) {
            return !IsNonFunctionGroupOwnerWord(token.text);
        }
        return IsOperatorFunctionNameToken(tokens, *previous) || IsOperatorCallNameClose(tokens, *previous);
    }

    static bool IsNonFunctionGroupOwnerWord(std::string_view text) {
        static constexpr std::string_view kWords[] = {
            "__declspec",
            "__uuidof",
            "alignas",
            "alignof",
            "decltype",
            "requires",
            "sizeof",
            "typeid"
        };
        return std::find(std::begin(kWords), std::end(kWords), text) != std::end(kWords);
    }

    bool IsOperatorCallNameClose(const std::vector<Token>& tokens, size_t close) const {
        if (close >= tokens.size() || tokens[close].text != ")") {
            return false;
        }
        const std::optional<size_t> open = FindMatchingOpen(tokens, close);
        if (!open || *open == 0) {
            return false;
        }
        const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
        return beforeOpen && tokens[*beforeOpen].text == "operator";
    }

    std::optional<size_t> FindTopLevelToken(const std::vector<Token>& tokens, std::string_view tokenText) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth == 0 && tokens[index].text == tokenText) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> FindTopLevelTokenAfter(
        const std::vector<Token>& tokens,
        std::string_view tokenText,
        size_t begin
    ) const {
        int depth = 0;
        for (size_t index = begin; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth == 0 && tokens[index].text == tokenText) {
                return index;
            }
        }
        return std::nullopt;
    }

    bool ContainsTokenAfter(const std::vector<Token>& tokens, size_t begin, std::string_view tokenText) const {
        for (size_t index = begin; index < tokens.size(); ++index) {
            if (tokens[index].text == tokenText) {
                return true;
            }
        }
        return false;
    }

    bool ContainsWord(const std::vector<Token>& tokens, std::string_view text) const {
        return std::any_of(tokens.begin(), tokens.end(), [text](const Token& token) {
            return token.kind == TokenKind::Word && token.text == text;
        });
    }

    bool IsLabelColon() const {
        if (pendingTokens_.empty()) {
            return false;
        }
        const std::string& previous = pendingTokens_.back().text;
        if (previous == "public" || previous == "private" || previous == "protected" || previous == "default") {
            return true;
        }
        const std::string trimmed = tools::lint::Trim(FormatInline(pendingTokens_));
        return tools::lint::StartsWith(trimmed, "case ");
    }

    bool IsAccessSpecifierLabel() const {
        if (pendingTokens_.size() < 2) {
            return false;
        }
        const std::string& previous = pendingTokens_[pendingTokens_.size() - 2].text;
        return previous == "public" || previous == "private" || previous == "protected";
    }

    bool IsCaseOrDefaultLabel() const {
        if (pendingTokens_.size() < 2) {
            return false;
        }
        const std::string& previous = pendingTokens_[pendingTokens_.size() - 2].text;
        if (previous == "default") {
            return true;
        }
        const std::string trimmed = tools::lint::Trim(FormatInline(pendingTokens_));
        return tools::lint::StartsWith(trimmed, "case ");
    }

    bool IsLabelColonToken(const std::vector<Token>& tokens, size_t index) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        if (!previous) {
            return false;
        }
        const std::string& prev = tokens[*previous].text;
        if (prev == "public" || prev == "private" || prev == "protected" || prev == "default") {
            return true;
        }
        for (size_t current = *previous + 1; current > 0; --current) {
            const size_t candidate = current - 1;
            if (tokens[candidate].kind == TokenKind::Newline) {
                continue;
            }
            if (tokens[candidate].text == "case") {
                return true;
            }
            if (tokens[candidate].text == ";" || tokens[candidate].text == "{" || tokens[candidate].text == "}") {
                return false;
            }
        }
        return false;
    }

    bool IsLambdaReturnArrowToken(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != "->") {
            return false;
        }
        for (size_t current = index; current > 0; --current) {
            const size_t candidate = current - 1;
            if (tokens[candidate].text == "{" || tokens[candidate].text == "}" || tokens[candidate].text == ";") {
                return false;
            }
            if (IsLambdaIntroducerClose(tokens, candidate)) {
                return true;
            }
        }
        return false;
    }

    bool IsLambdaBodyOpenToken(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != "{") {
            return false;
        }
        for (size_t current = index; current > 0; --current) {
            const size_t candidate = current - 1;
            if (tokens[candidate].text == "{" || tokens[candidate].text == "}" || tokens[candidate].text == ";") {
                return false;
            }
            if (IsLambdaIntroducerClose(tokens, candidate)) {
                return true;
            }
        }
        return false;
    }

    bool IsLambdaIntroducerClose(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != "]") {
            return false;
        }
        const std::optional<size_t> open = FindMatchingOpen(tokens, index);
        if (!open) {
            const size_t afterClose = NextSignificantIndex(tokens, index + 1);
            return index == 0 && afterClose < tokens.size() && tokens[afterClose].text == "(";
        }
        if (std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open)) {
            if (IsWordLike(tokens[*beforeOpen]) || tokens[*beforeOpen].text == ")" || tokens[*beforeOpen].text == "]") {
                return false;
            }
        }
        const size_t afterClose = NextSignificantIndex(tokens, index + 1);
        if (afterClose >= tokens.size()) {
            return false;
        }
        const std::string& next = tokens[afterClose].text;
        return next == "(" || next == "{" || next == "mutable" || next == "noexcept" || next == "->";
    }

    bool IsTemplateAngleToken(const std::vector<Token>& tokens, size_t index) const {
        return IsTemplateAngleOpen(tokens, index) || IsTemplateAngleClose(tokens, index);
    }

    bool IsTemplateAngleCloseToken(const std::vector<Token>& tokens, size_t index) const {
        return index < tokens.size() &&
            (tokens[index].text == ">" || tokens[index].text == ">>") &&
            IsTemplateAngleClose(tokens, index);
    }

    bool IsTemplateAngleOpen(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != "<") {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        if (!previous) {
            return false;
        }
        const Token& before = tokens[*previous];
        if (before.text != "template" && before.kind != TokenKind::Word && before.text != ">") {
            return false;
        }
        return FindTemplateAngleClose(tokens, index).has_value();
    }

    bool IsTemplateAngleClose(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size() || (tokens[index].text != ">" && tokens[index].text != ">>")) {
            return false;
        }
        return FindTemplateAngleOpen(tokens, index).has_value();
    }

    int TemplateCloseWidth(std::string_view text) const {
        if (text == ">>") {
            return 2;
        }
        if (text == ">") {
            return 1;
        }
        return 0;
    }

    std::optional<size_t> FindTemplateAngleOpen(const std::vector<Token>& tokens, size_t close) const {
        if (close >= tokens.size()) {
            return std::nullopt;
        }
        const int closeWidth = TemplateCloseWidth(tokens[close].text);
        if (closeWidth == 0) {
            return std::nullopt;
        }
        int depth = closeWidth - 1;
        for (size_t current = close; current > 0; --current) {
            const size_t candidate = current - 1;
            const int candidateCloseWidth = TemplateCloseWidth(tokens[candidate].text);
            if (candidateCloseWidth > 0) {
                depth += candidateCloseWidth;
            } else if (tokens[candidate].text == "<" && IsTemplateAngleOpen(tokens, candidate)) {
                if (depth == 0) {
                    return candidate;
                }
                --depth;
            } else if (depth == 0 && IsTemplateArgumentReferenceToken(tokens, candidate)) {
                continue;
            } else if (depth == 0 && IsTemplateScanBoundary(tokens[candidate].text)) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> FindTemplateAngleClose(const std::vector<Token>& tokens, size_t open) const {
        int depth = 0;
        for (size_t index = open + 1; index < tokens.size(); ++index) {
            const std::string& text = tokens[index].text;
            if (text == "<") {
                ++depth;
            } else if (text == ">") {
                if (depth == 0) {
                    return index;
                }
                --depth;
            } else if (text == ">>") {
                if (depth <= 1) {
                    return index;
                }
                depth -= 2;
            } else if (depth == 0 && IsTemplateArgumentReferenceToken(tokens, index)) {
                continue;
            } else if (depth == 0 && IsTemplateScanBoundary(text)) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    bool IsTemplateArgumentReferenceToken(const std::vector<Token>& tokens, size_t index) const {
        if (tokens[index].text != "&&") {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        const size_t next = NextSignificantIndex(tokens, index + 1);
        if (!previous || next >= tokens.size()) {
            return false;
        }
        const std::string& before = tokens[*previous].text;
        const std::string& after = tokens[next].text;
        return (tokens[*previous].kind == TokenKind::Word || before == ">" || before == ">>") &&
            (after == "," || after == ">" || after == ">>");
    }

    bool IsTemplateScanBoundary(std::string_view text) const {
        return text == ";" ||
            text == "{" ||
            text == "}" ||
            text == "=" ||
            text == "?" ||
            text == ":" ||
            text == "&&" ||
            text == "||";
    }

    bool StartsWithControlFor(const std::vector<Token>& tokens) const {
        return FirstControlHeaderToken(tokens) && FirstControlHeaderToken(tokens)->text == "for";
    }

    bool StartsWithControlHeader(const std::vector<Token>& tokens) const {
        return FirstControlHeaderToken(tokens).has_value();
    }

    std::optional<Token> FirstControlHeaderToken(const std::vector<Token>& tokens) const {
        if (tokens.empty()) {
            return std::nullopt;
        }
        size_t index = 0;
        if (tokens[index].text == "else") {
            index = NextSignificantIndex(tokens, index + 1);
        }
        if (index >= tokens.size()) {
            return std::nullopt;
        }
        if (tokens[index].kind == TokenKind::Word && IsControlKeyword(tokens[index].text)) {
            return tokens[index];
        }
        return std::nullopt;
    }

    const Token* NextNonNewline(const std::vector<Token>& tokens, size_t index) const {
        while (index < tokens.size()) {
            if (tokens[index].kind != TokenKind::Newline) {
                return &tokens[index];
            }
            ++index;
        }
        return nullptr;
    }

    Token NextSignificant(const std::vector<Token>& tokens, size_t index) const {
        const size_t found = NextSignificantIndex(tokens, index);
        if (found < tokens.size()) {
            return tokens[found];
        }
        return {};
    }

    size_t NextSignificantIndex(const std::vector<Token>& tokens, size_t index) const {
        while (index < tokens.size() && tokens[index].kind == TokenKind::Newline) {
            ++index;
        }
        return index;
    }

    void UpdateDepth(const std::vector<Token>& tokens, size_t index, int& depth) const {
        if (index >= tokens.size()) {
            return;
        }
        const Token& token = tokens[index];
        if (IsGroupOpen(token.text)) {
            ++depth;
        } else if (token.text == ")" || token.text == "]" || token.text == "}") {
            depth = std::max(0, depth - 1);
        } else if (IsTemplateAngleOpen(tokens, index)) {
            ++depth;
        } else if (IsTemplateAngleCloseToken(tokens, index)) {
            depth = std::max(0, depth - TemplateCloseWidth(token.text));
        }
    }

    static bool IsGroupOpen(std::string_view text) {
        return text == "(" || text == "[" || text == "{";
    }

    static std::string MatchingClose(std::string_view text) {
        if (text == "(") {
            return ")";
        }
        if (text == "[") {
            return "]";
        }
        if (text == "{") {
            return "}";
        }
        return "}";
    }

    static void UpdateDepth(const Token& token, int& depth) {
        if (IsGroupOpen(token.text)) {
            ++depth;
        } else if (token.text == ")" || token.text == "]" || token.text == "}") {
            depth = std::max(0, depth - 1);
        }
    }

    static bool IsAssignmentOperator(std::string_view text) {
        static constexpr std::string_view kOperators[] = {
            "=",
            "+=",
            "-=",
            "*=",
            "/=",
            "%=",
            "&=",
            "|=",
            "^=",
            "<<=",
            ">>="
        };
        return std::find(std::begin(kOperators), std::end(kOperators), text) != std::end(kOperators);
    }

    static bool IsBinaryOperatorLike(std::string_view text) {
        static constexpr std::string_view kOperators[] = {
            "=",
            "+",
            "-",
            "*",
            "/",
            "%",
            "==",
            "!=",
            "<",
            ">",
            "<=",
            ">=",
            "&&",
            "||",
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
            "<<=",
            ">>=",
            "&",
            "|",
            "^",
            "?",
            ":"
        };
        return std::find(std::begin(kOperators), std::end(kOperators), text) != std::end(kOperators);
    }

    std::vector<std::string> outputLines_;
    std::vector<Token> pendingTokens_;
    std::string pendingPrefix_;
    std::vector<BlockState> blockStack_;
    mutable std::map<std::string, LayoutResult> layoutCache_;
    const FormatterConfig& config_;
    int indentLevel_ = 0;
    int groupDepth_ = 0;
    int caseBodyIndentLevel_ = -1;
    DeclarationKind previousDeclarationKind_ = DeclarationKind::None;
    bool previousDeclarationBreaksSiblingGroup_ = false;
    bool pendingLogicalBlank_ = false;
    bool pendingPreprocessorBlank_ = false;
    bool pendingPragmaOnceBlank_ = false;
    bool pendingUndefBlank_ = false;
    bool allowOriginalBlank_ = false;
    bool justEmittedCaseLabel_ = false;
};

struct IncludeLine {
    std::string line;
    std::string spelling;
    bool quoted = false;
    int group = 0;
};

std::optional<IncludeLine> ParseIncludeLine(std::string_view line) {
    const std::string trimmed = tools::lint::Trim(line);
    constexpr std::string_view prefix = "#include";
    if (!tools::lint::StartsWith(trimmed, prefix)) {
        return std::nullopt;
    }
    size_t index = prefix.size();
    while (index < trimmed.size() && IsSpaceButNotNewline(trimmed[index])) {
        ++index;
    }
    if (index >= trimmed.size()) {
        return std::nullopt;
    }
    const char open = trimmed[index];
    const char close = open == '"' ? '"' : open == '<' ? '>' : '\0';
    if (close == '\0') {
        return std::nullopt;
    }
    const size_t end = trimmed.find(close, index + 1);
    if (end == std::string::npos) {
        return std::nullopt;
    }
    if (!tools::lint::Trim(std::string_view(trimmed).substr(end + 1)).empty()) {
        return std::nullopt;
    }
    IncludeLine include;
    include.line = "#include " + trimmed.substr(index, end - index + 1);
    include.spelling = trimmed.substr(index, end - index + 1);
    include.quoted = open == '"';
    return include;
}

std::string Stem(std::string_view path) {
    const std::string normalized = tools::lint::NormalizeSeparators(std::string(path));
    const size_t slash = normalized.find_last_of('/');
    const size_t start = slash == std::string::npos ? 0 : slash + 1;
    const size_t dot = normalized.find_last_of('.');
    if (dot == std::string::npos || dot < start) {
        return normalized.substr(start);
    }
    return normalized.substr(start, dot - start);
}

bool IsMainInclude(const IncludeLine& include, const FormatterConfig& config, std::string_view sourcePath) {
    if (!include.quoted || !config.mainIncludeQuote) {
        return false;
    }
    std::string includeText = include.spelling.substr(1, include.spelling.size() - 2);
    if (tools::lint::Extension(includeText) != ".h") {
        return false;
    }
    const std::string includeStem = Stem(includeText);
    const std::string sourceStem = Stem(sourcePath);
    if (tools::lint::StartsWith(sourceStem, includeStem + ".")) {
        return true;
    }
    if (!tools::lint::StartsWith(includeStem, sourceStem)) {
        return false;
    }
    const std::string suffix = includeStem.substr(sourceStem.size());
    return std::regex_match(suffix, std::regex(config.mainIncludeRegex));
}

int IncludeGroupIndex(const IncludeLine& include, const FormatterConfig& config, std::string_view sourcePath) {
    if (IsMainInclude(include, config, sourcePath)) {
        return 0;
    }
    for (size_t index = 0; index < config.includeGroups.size(); ++index) {
        if (std::regex_match(include.spelling, config.includeGroups[index].regex)) {
            return static_cast<int>(index + 1);
        }
    }
    return static_cast<int>(config.includeGroups.size() + 1);
}

std::vector<std::string> FormatIncludeRun(
    std::vector<IncludeLine> includes,
    const FormatterConfig& config,
    std::string_view sourcePath
) {
    for (IncludeLine& include : includes) {
        include.group = IncludeGroupIndex(include, config, sourcePath);
    }
    std::sort(includes.begin(), includes.end(), [](const IncludeLine& left, const IncludeLine& right) {
        if (left.group != right.group) {
            return left.group < right.group;
        }
        return tools::lint::ToLowerAscii(left.spelling) < tools::lint::ToLowerAscii(right.spelling);
    });
    std::vector<std::string> lines;
    int lastGroup = -1;
    for (const IncludeLine& include : includes) {
        if (lastGroup != -1 && include.group != lastGroup) {
            lines.push_back({});
        }
        lines.push_back(include.line);
        lastGroup = include.group;
    }
    return lines;
}

std::string SortIncludes(std::string_view text, const FormatterConfig& config, std::string_view sourcePath) {
    std::vector<std::string> lines = tools::lint::SplitLines(text);
    std::vector<std::string> output;
    std::vector<IncludeLine> includeRun;
    auto flushRun = [&]() {
        if (includeRun.empty()) {
            return;
        }
        std::vector<std::string> sorted = FormatIncludeRun(std::move(includeRun), config, sourcePath);
        output.insert(output.end(), sorted.begin(), sorted.end());
        includeRun.clear();
    };
    for (const std::string& line : lines) {
        if (std::optional<IncludeLine> include = ParseIncludeLine(line)) {
            includeRun.push_back(*include);
            continue;
        }
        if (tools::lint::Trim(line).empty() && !includeRun.empty()) {
            continue;
        }
        flushRun();
        if (!tools::lint::Trim(line).empty() && !output.empty() && tools::lint::StartsWith(output.back(), "#include")) {
            output.push_back({});
        }
        output.push_back(TrimRight(line));
    }
    flushRun();
    std::string result;
    for (const std::string& line : output) {
        result += line;
        result.push_back('\n');
    }
    return result;
}

ParseResult ParseCpp(std::string_view text) {
    TSParser* parser = ts_parser_new();
    if (parser == nullptr) {
        return {};
    }
    if (!ts_parser_set_language(parser, tree_sitter_cpp())) {
        ts_parser_delete(parser);
        return {};
    }
    TSTree* tree = ts_parser_parse_string(parser, nullptr, text.data(), static_cast<uint32_t>(text.size()));
    if (tree == nullptr) {
        ts_parser_delete(parser);
        return {};
    }
    const TSNode root = ts_tree_root_node(tree);
    ParseResult result;
    result.ok = true;
    result.hasErrors = ts_node_has_error(root);
    if (result.hasErrors) {
        const TSNode errorNode = FindFirstErrorNode(root);
        const TSPoint point = ts_node_start_point(errorNode);
        result.errorNodeType = ts_node_type(errorNode);
        result.errorLine = static_cast<int>(point.row) + 1;
        result.errorColumn = static_cast<int>(point.column) + 1;
        result.errorSnippet = SingleLineSnippet(text, ts_node_start_byte(errorNode), ts_node_end_byte(errorNode));
    }
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return result;
}

FileFormatResult FormatOneText(std::string_view text, const FormatterConfig& config, std::string_view sourcePath) {
    const std::string normalized = NormalizeLineEndings(text);
    const std::string includeSorted = SortIncludes(normalized, config, sourcePath);
    const ParseResult parse = ParseCpp(includeSorted);
    if (!parse.ok) {
        return {.ok = false, .error = "tree-sitter parser setup failed"};
    }
    PrettyFormatter formatter(config);
    FileFormatResult result;
    result.parseHadErrors = parse.hasErrors;
    result.parseErrorNodeType = parse.errorNodeType;
    result.parseErrorLine = parse.errorLine;
    result.parseErrorColumn = parse.errorColumn;
    result.parseErrorSnippet = parse.errorSnippet;
    result.formatted = formatter.Format(AddRequiredControlBraces(Tokenize(includeSorted)));
    result.changed = normalized != result.formatted;
    return result;
}

std::string ReadStdinText() {
    std::string text;
    char buffer[4096];
    while (true) {
        const size_t bytesRead = std::fread(buffer, 1, sizeof(buffer), stdin);
        if (bytesRead > 0) {
            text.append(buffer, bytesRead);
        }
        if (bytesRead < sizeof(buffer)) {
            break;
        }
    }
    return text;
}

FILE* SummaryStream(const FormatOptions& options) {
    return options.mode == FormatMode::Stdout ? stderr : stdout;
}

void PrintParseRecovery(const FileFormatResult& result, const std::string& path, const std::string& root) {
    const std::string relative = tools::lint::NormalizeSeparators(tools::lint::RelativePath(path, root));
    std::fprintf(
        stderr,
        "%s:%d:%d: tree-sitter parse recovery at %s: %s\n",
        relative.c_str(),
        result.parseErrorLine,
        result.parseErrorColumn,
        result.parseErrorNodeType.c_str(),
        result.parseErrorSnippet.c_str()
    );
}

void PrintFormatSummary(
    FILE* output,
    const char* verb,
    int processedCount,
    int changedCount,
    int ignoredCount,
    int parseErrorCount,
    std::chrono::steady_clock::time_point start
) {
    std::fprintf(
        output,
        "%s %d file%s in %s.",
        verb,
        processedCount,
        processedCount == 1 ? "" : "s",
        FormatElapsed(std::chrono::steady_clock::now() - start).c_str()
    );
    if (changedCount > 0) {
        std::fprintf(output, " %d file%s require formatting.", changedCount, changedCount == 1 ? "" : "s");
    }
    if (ignoredCount > 0) {
        std::fprintf(output, " Skipped %d ignored file%s.", ignoredCount, ignoredCount == 1 ? "" : "s");
    }
    if (parseErrorCount > 0) {
        std::fprintf(
            output,
            " %d file%s parsed with tree-sitter errors.",
            parseErrorCount,
            parseErrorCount == 1 ? "" : "s"
        );
    }
    std::fprintf(output, "\n");
}

}  // namespace

int RunFormat(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    std::string optionsError;
    std::optional<FormatOptions> parsed = tools::format::ParseFormatArgs(argc, argv, optionsError);
    if (!parsed) {
        if (!optionsError.empty()) {
            std::fprintf(stderr, "%s\n", optionsError.c_str());
        }
        tools::format::PrintFormatUsage(stderr);
        return 2;
    }
    const FormatOptions& options = *parsed;
    if (options.help) {
        tools::format::PrintFormatUsage(stdout);
        return 0;
    }

    tools::format::FormatStyleCache styleCache(options.explicitStylePath);
    const std::string currentDirectory = tools::lint::CurrentDirectoryAbsolute();
    FILE* summary = SummaryStream(options);

    if (options.files.empty() && !options.fileListProvided) {
        std::string error;
        const FormatterConfig* config = styleCache.ConfigForPath(currentDirectory, error);
        if (config == nullptr) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 2;
        }
        FileFormatResult result = FormatOneText(ReadStdinText(), *config, "<stdin>");
        if (!result.ok) {
            std::fprintf(stderr, "<stdin>: %s\n", result.error.c_str());
            return 1;
        }
        if (options.verbose && result.parseHadErrors) {
            PrintParseRecovery(result, "<stdin>", currentDirectory);
        }
        if (options.mode == FormatMode::DryRun && result.changed) {
            std::fprintf(
                summary,
                "Formatting is required for stdin. Checked stdin in %s.\n",
                FormatElapsed(std::chrono::steady_clock::now() - start).c_str()
            );
            return 1;
        }
        if (options.mode == FormatMode::Stdout) {
            std::fwrite(result.formatted.data(), 1, result.formatted.size(), stdout);
        }
        std::fprintf(
            summary,
            "%s stdin in %s.\n",
            options.mode == FormatMode::DryRun ? "Checked" : "Formatted",
            FormatElapsed(std::chrono::steady_clock::now() - start).c_str()
        );
        return 0;
    }

    bool failed = false;
    int parseErrorCount = 0;
    int changedCount = 0;
    int ignoredCount = 0;
    int processedCount = 0;
    const bool showProgress = _isatty(_fileno(summary)) != 0;
    size_t previousProgressLength = 0;

    for (int index = 0; index < static_cast<int>(options.files.size()); ++index) {
        const std::string file = tools::lint::AbsolutePath(options.files[static_cast<size_t>(index)]);
        std::string error;
        if (styleCache.IsIgnored(file, error)) {
            ++ignoredCount;
            continue;
        }
        if (!error.empty()) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 2;
        }

        const FormatterConfig* config = styleCache.ConfigForPath(file, error);
        if (config == nullptr) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 2;
        }
        if (showProgress) {
            const std::string relative =
                tools::lint::NormalizeSeparators(tools::lint::RelativePath(file, currentDirectory));
            std::string progress =
                "[" + std::to_string(index + 1) + "/" + std::to_string(options.files.size()) + "] format " + relative;
            if (progress.size() > 119) {
                progress = progress.substr(0, 116) + "...";
            }
            const std::string padding(
                previousProgressLength > progress.size() ? previousProgressLength - progress.size() : 0,
                ' '
            );
            std::fprintf(summary, "\r%s%s", progress.c_str(), padding.c_str());
            std::fflush(summary);
            previousProgressLength = progress.size();
        }

        std::optional<std::string> text = tools::lint::ReadFileText(file);
        if (!text) {
            std::fprintf(stderr, "Failed to read %s\n", file.c_str());
            failed = true;
            continue;
        }
        ++processedCount;
        FileFormatResult result = FormatOneText(*text, *config, file);
        if (!result.ok) {
            std::fprintf(stderr, "%s: %s\n", file.c_str(), result.error.c_str());
            failed = true;
            continue;
        }
        if (result.parseHadErrors) {
            ++parseErrorCount;
            if (options.verbose) {
                PrintParseRecovery(result, file, currentDirectory);
            }
        }
        if (options.mode == FormatMode::Stdout) {
            std::fwrite(result.formatted.data(), 1, result.formatted.size(), stdout);
        } else if (result.changed) {
            ++changedCount;
            if (options.mode == FormatMode::InPlace) {
                if (!tools::lint::WriteFileText(file, ToFileLineEndings(result.formatted))) {
                    std::fprintf(stderr, "Failed to write %s\n", file.c_str());
                    failed = true;
                }
            } else {
                failed = true;
            }
        }
    }
    if (showProgress) {
        std::fprintf(summary, "\r%s\r", std::string(previousProgressLength, ' ').c_str());
        std::fflush(summary);
    }
    if (failed) {
        if (options.mode == FormatMode::InPlace) {
            std::fprintf(summary, "Formatting failed");
        } else {
            std::fprintf(summary, "Formatting is required for %d file%s", changedCount, changedCount == 1 ? "" : "s");
        }
        if (parseErrorCount > 0) {
            std::fprintf(
                summary,
                " (%d file%s parsed with tree-sitter errors)",
                parseErrorCount,
                parseErrorCount == 1 ? "" : "s"
            );
        }
        if (ignoredCount > 0) {
            std::fprintf(summary, ". Skipped %d ignored file%s", ignoredCount, ignoredCount == 1 ? "" : "s");
        }
        std::fprintf(
            summary,
            ". Checked %d file%s in %s.\n",
            processedCount,
            processedCount == 1 ? "" : "s",
            FormatElapsed(std::chrono::steady_clock::now() - start).c_str()
        );
        return 1;
    }
    const char* verb = options.mode == FormatMode::DryRun ? "Checked" : "Formatted";
    PrintFormatSummary(
        summary,
        verb,
        processedCount,
        options.mode == FormatMode::DryRun ? changedCount : 0,
        ignoredCount,
        parseErrorCount,
        start
    );
    return 0;
}
