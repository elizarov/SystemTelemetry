#include "tools/format.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <tree_sitter_cpp.h>
#include <vector>

#include "tools/impl/lint_common.h"

namespace {

constexpr int kDefaultColumnLimit = 120;
constexpr int kDefaultIndentWidth = 4;
constexpr int kDefaultTabWidth = 4;
constexpr int kDefaultContinuationIndentWidth = 4;

enum class Mode {
    Check,
    Fix,
};

enum class Scope {
    All,
    Changed,
};

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

struct Options {
    Mode mode = Mode::Check;
    Scope scope = Scope::All;
    std::string root;
    std::string targetFile;
    bool stdoutMode = false;
    bool verbose = false;
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

struct IncludeGroup {
    std::string name;
    std::regex regex;
};

struct FormatterConfig {
    int columnLimit = kDefaultColumnLimit;
    int indentWidth = kDefaultIndentWidth;
    int tabWidth = kDefaultTabWidth;
    int continuationIndentWidth = kDefaultContinuationIndentWidth;
    std::string mainIncludeRegex = "(Test)?$";
    bool mainIncludeQuote = true;
    std::vector<IncludeGroup> includeGroups;
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

void PrintUsage() {
    std::fprintf(stderr, "Usage:\n");
    std::fprintf(stderr, "  CaseDashTools.exe format\n");
    std::fprintf(stderr, "  CaseDashTools.exe format fix\n");
    std::fprintf(stderr, "  CaseDashTools.exe format changed\n");
    std::fprintf(stderr, "  CaseDashTools.exe format fix changed\n");
    std::fprintf(stderr, "  CaseDashTools.exe format [--root path] --file path [--stdout] [-v|--verbose]\n");
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

bool IsSpaceButNotNewline(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v';
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

        static constexpr std::string_view kThreeCharOps[] = {"<<=", ">>=", "<=>", "..."};
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
        static constexpr std::string_view kTwoCharOps[] = {"::",
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
            ".*",
            "->*"};
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

bool IsWordLike(const Token& token) {
    return token.kind == TokenKind::Word || token.kind == TokenKind::Number || token.kind == TokenKind::StringLiteral ||
        token.kind == TokenKind::CharLiteral;
}

bool IsControlKeyword(std::string_view text) {
    return text == "if" || text == "for" || text == "while" || text == "switch" || text == "catch";
}

bool IsBinaryOperator(std::string_view text) {
    static constexpr std::string_view kOperators[] = {"=",
        "+",
        "-",
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
        ":"};
    return std::find(std::begin(kOperators), std::end(kOperators), text) != std::end(kOperators);
}

bool IsPrefixOperator(std::string_view text) {
    return text == "!" || text == "~" || text == "++" || text == "--";
}

bool IsNoSpaceBefore(std::string_view text) {
    return text == ")" || text == "]" || text == ";" || text == "," || text == "." || text == "->" || text == "::" ||
        text == "++" || text == "--";
}

bool IsNoSpaceAfter(std::string_view text) {
    return text == "(" || text == "[" || text == "." || text == "->" || text == "::" || text == "~" || text == "!";
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
    explicit PrettyFormatter(const FormatterConfig& config) :
        config_(config) {}

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
    void EmitLine(std::string text) {
        outputLines_.push_back(TrimRight(std::move(text)));
        allowOriginalBlank_ = true;
    }

    void EmitBlankLine() {
        FlushPending();
        if (allowOriginalBlank_ && !outputLines_.empty() && !outputLines_.back().empty()) {
            outputLines_.push_back({});
            allowOriginalBlank_ = false;
        }
    }

    void HandleOriginalNewline(const std::vector<Token>& tokens, size_t& index) {
        size_t newlineCount = 1;
        while (index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::Newline) {
            ++index;
            ++newlineCount;
        }
        if (newlineCount > 1 && pendingTokens_.empty() && pendingPrefix_.empty()) {
            EmitBlankLine();
        } else if (newlineCount == 1 && pendingTokens_.empty() && pendingPrefix_.empty() && !outputLines_.empty() &&
            tools::lint::StartsWith(outputLines_.back(), "#")) {
            EmitBlankLine();
        }
    }

    void EmitCodeToken(const std::vector<Token>& tokens, size_t& index) {
        const Token& token = tokens[index];
        if (token.text == "}") {
            if (groupDepth_ > 0) {
                --groupDepth_;
                pendingTokens_.push_back(token);
            } else {
                EmitCloseBrace(tokens, index);
            }
            return;
        }
        if (token.text == "{") {
            if (groupDepth_ == 0 && IsCodeBlockOpen()) {
                EmitOpenBrace(tokens, index);
            } else {
                ++groupDepth_;
                pendingTokens_.push_back(token);
            }
            return;
        }
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
            if (groupDepth_ == 0 && NextSignificant(tokens, index + 1).kind != TokenKind::LineComment) {
                FlushPending();
            }
            return;
        }
        if (token.text == ":" && groupDepth_ == 0 && IsLabelColon()) {
            pendingTokens_.push_back(token);
            FlushPending();
            return;
        }
        pendingTokens_.push_back(token);
    }

    void EmitOpenBrace(const std::vector<Token>& tokens, size_t& index) {
        const size_t closeIndex = NextSignificantIndex(tokens, index + 1);
        if (closeIndex < tokens.size() && tokens[closeIndex].text == "}") {
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
            return;
        }
        EmitFormatted(pendingTokens_, " {");
        ClearPending();
        ++indentLevel_;
    }

    void EmitCloseBrace(const std::vector<Token>& tokens, size_t& index) {
        FlushPending();
        indentLevel_ = std::max(0, indentLevel_ - 1);
        const size_t next = NextSignificantIndex(tokens, index + 1);
        if (next < tokens.size() && tokens[next].text == ";") {
            EmitLine(Indent() + "};");
            index = next;
            return;
        }
        if (next < tokens.size() && tokens[next].kind == TokenKind::Word &&
            (tokens[next].text == "else" || tokens[next].text == "catch" || tokens[next].text == "while")) {
            pendingPrefix_ = "} ";
            return;
        }
        EmitLine(Indent() + "}");
    }

    void EmitLineComment(const std::vector<Token>& tokens, size_t& index) {
        pendingTokens_.push_back({TokenKind::LineComment, TrimRight(TrimLeft(tokens[index].text))});
        if (groupDepth_ == 0 && HasTopLevelStatementTerminator(pendingTokens_)) {
            FlushPending();
        }
    }

    void EmitBlockComment(std::string_view text) {
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

    void EmitPreprocessor(std::string_view text) {
        FlushPending();
        std::vector<std::string> lines = tools::lint::SplitLines(text);
        for (std::string& line : lines) {
            EmitLine(TrimRight(TrimLeft(line)));
        }
    }

    void FlushPending() {
        if (pendingTokens_.empty()) {
            return;
        }
        EmitFormatted(pendingTokens_, {});
        ClearPending();
    }

    void EmitFormatted(const std::vector<Token>& tokens, std::string_view suffix) {
        for (std::string& line : FormatRange(tokens, indentLevel_, pendingPrefix_, std::string(suffix))) {
            EmitLine(std::move(line));
        }
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

    std::vector<std::string> FormatRange(
        const std::vector<Token>& tokens, int indentLevel, std::string prefix, std::string suffix) const {
        if (tokens.empty()) {
            if (!prefix.empty() || !suffix.empty()) {
                return {Indent(indentLevel) + prefix + suffix};
            }
            return {};
        }

        std::string inlineText = prefix + FormatInline(tokens);
        AppendSuffix(inlineText, suffix);
        if (!ShouldForceSplit(tokens) && Fits(indentLevel, inlineText)) {
            return {Indent(indentLevel) + inlineText};
        }

        if (std::optional<size_t> assignment = FindTopLevelAssignment(tokens)) {
            std::vector<std::string> lines;
            std::vector<Token> lhs(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(*assignment + 1));
            std::vector<Token> rhs(tokens.begin() + static_cast<std::ptrdiff_t>(*assignment + 1), tokens.end());
            lines.push_back(Indent(indentLevel) + prefix + FormatInline(lhs));
            std::vector<std::string> rhsLines = FormatRange(rhs, indentLevel + 1, {}, std::move(suffix));
            lines.insert(lines.end(), rhsLines.begin(), rhsLines.end());
            return lines;
        }

        if (std::optional<GroupPair> group = FindFirstGroupPair(tokens)) {
            return FormatSplitGroup(tokens, *group, indentLevel, std::move(prefix), std::move(suffix));
        }

        if (CanSplitOperatorChain(tokens)) {
            return FormatOperatorChain(tokens, indentLevel, std::move(prefix), std::move(suffix));
        }

        return {Indent(indentLevel) + inlineText};
    }

    std::vector<std::string> FormatSplitGroup(
        const std::vector<Token>& tokens, GroupPair group, int indentLevel, std::string prefix, std::string suffix) const {
        std::vector<Token> firstLineTokens(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(group.open + 1));
        std::vector<Token> inner(tokens.begin() + static_cast<std::ptrdiff_t>(group.open + 1),
            tokens.begin() + static_cast<std::ptrdiff_t>(group.close));
        std::vector<Token> suffixTokens(tokens.begin() + static_cast<std::ptrdiff_t>(group.close), tokens.end());

        std::vector<std::string> lines;
        lines.push_back(Indent(indentLevel) + prefix + FormatInline(firstLineTokens));

        const bool splitForHeader = StartsWithControlFor(firstLineTokens);
        const char separator = splitForHeader ? ';' : ',';
        std::vector<std::vector<Token>> elements = SplitTopLevel(inner, separator);
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(inner, separator)) {
            std::vector<std::string> childLines = FormatRange(inner, indentLevel + 1, {}, {});
            lines.insert(lines.end(), childLines.begin(), childLines.end());
        } else {
            for (size_t index = 0; index < elements.size(); ++index) {
                if (elements[index].empty()) {
                    continue;
                }
                std::string elementSuffix;
                if (index + 1 < elements.size()) {
                    elementSuffix = std::string(1, separator);
                }
                std::vector<std::string> elementLines = FormatRange(elements[index], indentLevel + 1, {}, elementSuffix);
                lines.insert(lines.end(), elementLines.begin(), elementLines.end());
            }
        }

        std::string closeLine = FormatInline(suffixTokens);
        AppendSuffix(closeLine, suffix);
        lines.push_back(Indent(indentLevel) + closeLine);
        return lines;
    }

    std::vector<std::string> FormatOperatorChain(
        const std::vector<Token>& tokens, int indentLevel, std::string prefix, std::string suffix) const {
        std::vector<std::vector<Token>> parts;
        std::vector<Token> current;
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            UpdateDepth(token, depth);
            current.push_back(token);
            if (depth == 0 && IsChainBreakOperator(token.text)) {
                if (index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LineComment) {
                    current.push_back(tokens[index + 1]);
                    ++index;
                }
                if (token.text != "?") {
                    parts.push_back(current);
                    current.clear();
                }
            }
        }
        if (!current.empty()) {
            parts.push_back(current);
        }

        std::vector<std::string> lines;
        for (size_t index = 0; index < parts.size(); ++index) {
            std::string text = (index == 0 ? prefix : std::string{}) + FormatInline(parts[index]);
            if (index + 1 == parts.size()) {
                AppendSuffix(text, suffix);
            }
            lines.push_back(Indent(indentLevel) + text);
        }
        return lines;
    }

    std::string JoinOutput() const {
        std::string result;
        for (const std::string& line : outputLines_) {
            result += line;
            result.push_back('\n');
        }
        return result;
    }

    std::string FormatInline(const std::vector<Token>& tokens) const {
        std::string result;
        std::optional<Token> previous;
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
                continue;
            }
            if (previous && NeedsSpaceBefore(tokens, index, *previous)) {
                result.push_back(' ');
            }
            result += token.text;
            previous = token;
        }
        return result;
    }

    bool NeedsSpaceBefore(const std::vector<Token>& tokens, size_t index, const Token& previous) const {
        const Token& token = tokens[index];
        const std::string_view current = token.text;
        const std::string_view prev = previous.text;
        if (current == "(" && previous.kind == TokenKind::Word) {
            return IsControlKeyword(prev);
        }
        if (current == "<" && prev == "template") {
            return true;
        }
        if (current == "{" || IsNoSpaceBefore(current) || IsNoSpaceAfter(prev)) {
            return false;
        }
        if (current == "*" || current == "&") {
            const Token* next = NextNonNewline(tokens, index + 1);
            if ((previous.kind == TokenKind::Word || prev == ">" || prev == ")" || prev == "]") && next != nullptr &&
                next->kind == TokenKind::Word) {
                return false;
            }
        }
        if ((prev == "*" || prev == "&") && token.kind == TokenKind::Word) {
            return true;
        }
        if (IsBinaryOperatorLike(current) || IsBinaryOperatorLike(prev)) {
            return true;
        }
        if ((prev == "," || prev == ";") && current != ")" && current != "]") {
            return true;
        }
        if ((prev == ")" || prev == "]") && (token.kind == TokenKind::Word || token.kind == TokenKind::StringLiteral)) {
            return true;
        }
        return IsWordLike(previous) && IsWordLike(token);
    }

    bool Fits(int indentLevel, std::string_view text) const {
        return static_cast<int>(indentLevel * config_.indentWidth + text.size()) <= config_.columnLimit;
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

    bool ShouldForceSplit(const std::vector<Token>& tokens) const {
        if (!HasLineComment(tokens)) {
            return false;
        }
        return FindFirstGroupPair(tokens).has_value() || CanSplitOperatorChain(tokens);
    }

    bool HasLineComment(const std::vector<Token>& tokens) const {
        return std::any_of(tokens.begin(), tokens.end(), [](const Token& token) {
            return token.kind == TokenKind::LineComment;
        });
    }

    std::optional<size_t> FindTopLevelAssignment(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (depth == 0 && IsAssignmentOperator(token.text)) {
                return index;
            }
            UpdateDepth(token, depth);
        }
        return std::nullopt;
    }

    std::optional<GroupPair> FindFirstGroupPair(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const std::string& text = tokens[index].text;
            if (IsGroupOpen(text) && depth == 0) {
                if (std::optional<size_t> close = FindMatchingClose(tokens, index)) {
                    return GroupPair{index, *close};
                }
            }
            UpdateDepth(tokens[index], depth);
        }
        return std::nullopt;
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
            UpdateDepth(token, depth);
        }
        result.push_back(current);
        return result;
    }

    bool ContainsTopLevelSeparator(const std::vector<Token>& tokens, char separator) const {
        int depth = 0;
        for (const Token& token : tokens) {
            if (depth == 0 && token.text.size() == 1 && token.text[0] == separator) {
                return true;
            }
            UpdateDepth(token, depth);
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
        int depth = 0;
        int breakCount = 0;
        for (const Token& token : tokens) {
            UpdateDepth(token, depth);
            if (depth == 0 && IsChainBreakOperator(token.text)) {
                ++breakCount;
            }
        }
        return breakCount > 0;
    }

    bool IsCodeBlockOpen() const {
        if (pendingTokens_.empty()) {
            return false;
        }
        const std::string first = pendingTokens_.front().text;
        if (first == "namespace" || first == "class" || first == "struct" || first == "enum" || first == "if" ||
            first == "for" || first == "while" || first == "switch" || first == "catch" || first == "try" ||
            first == "do" || first == "else") {
            return true;
        }
        if (!pendingPrefix_.empty()) {
            return true;
        }
        if (ContainsTopLevelAssignment(pendingTokens_)) {
            return false;
        }
        return EndsWithMatchingParen(pendingTokens_);
    }

    bool ContainsTopLevelAssignment(const std::vector<Token>& tokens) const {
        return FindTopLevelAssignment(tokens).has_value();
    }

    bool EndsWithMatchingParen(const std::vector<Token>& tokens) const {
        return !tokens.empty() && tokens.back().text == ")";
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

    bool StartsWithControlFor(const std::vector<Token>& tokens) const {
        return !tokens.empty() && tokens.front().text == "for";
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
            "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};
        return std::find(std::begin(kOperators), std::end(kOperators), text) != std::end(kOperators);
    }

    static bool IsBinaryOperatorLike(std::string_view text) {
        static constexpr std::string_view kOperators[] = {"=",
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
            "|",
            "^",
            "?",
            ":"};
        return std::find(std::begin(kOperators), std::end(kOperators), text) != std::end(kOperators);
    }

    static bool IsChainBreakOperator(std::string_view text) {
        if (IsAssignmentOperator(text)) {
            return false;
        }
        return IsBinaryOperatorLike(text);
    }

    std::vector<std::string> outputLines_;
    std::vector<Token> pendingTokens_;
    std::string pendingPrefix_;
    const FormatterConfig& config_;
    int indentLevel_ = 0;
    int groupDepth_ = 0;
    bool allowOriginalBlank_ = false;
};

std::optional<FormatterConfig> LoadFormatterConfig(const std::string& root, std::string& error) {
    const std::string configPath = tools::lint::JoinPath(root, "tools/format_config.json");
    std::optional<std::string> text = tools::lint::ReadFileText(configPath);
    if (!text) {
        error = "missing native formatter config: " + configPath;
        return std::nullopt;
    }
    try {
        const tools::lint::JsonValue configJson = tools::lint::ParseJson(*text);
        FormatterConfig config;
        const tools::lint::JsonValue& formatting = configJson.At("formatting");
        config.columnLimit = formatting.At("line_width").AsInt();
        config.indentWidth = formatting.At("indent_width").AsInt();
        config.tabWidth = formatting.At("tab_width").AsInt();
        config.continuationIndentWidth = formatting.At("continuation_indent_width").AsInt();

        const tools::lint::JsonValue& includeSorting = configJson.At("include_sorting");
        const tools::lint::JsonValue& mainInclude = includeSorting.At("main_include");
        config.mainIncludeRegex = mainInclude.At("include_is_main_regex").AsString();
        config.mainIncludeQuote = mainInclude.At("quote").AsBool();
        for (const tools::lint::JsonValue& group : includeSorting.At("groups").AsArray()) {
            config.includeGroups.push_back({group.At("name").AsString(), std::regex(group.At("regex").AsString())});
        }
        return config;
    } catch (const std::exception& exception) {
        error = "invalid native formatter config " + configPath + ": " + exception.what();
        return std::nullopt;
    }
}

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
    std::vector<IncludeLine> includes, const FormatterConfig& config, std::string_view sourcePath) {
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
    result.formatted = formatter.Format(Tokenize(includeSorted));
    result.changed = normalized != result.formatted;
    return result;
}

bool IsEligibleCppPath(std::string_view root, std::string_view path) {
    const std::string relative = tools::lint::NormalizeSeparators(tools::lint::RelativePath(path, root));
    if (!(tools::lint::StartsWith(relative, "src/") || tools::lint::StartsWith(relative, "tests/"))) {
        return false;
    }
    if (tools::lint::StartsWith(relative, "src/vendor/")) {
        return false;
    }
    const std::string extension = tools::lint::ToLowerAscii(tools::lint::Extension(path));
    return extension == ".cpp" || extension == ".h";
}

std::string QuoteCommandArgument(std::string_view value) {
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
}

std::optional<std::vector<std::string>> RunGit(std::string_view root, const std::vector<std::string>& args) {
    std::string command = "git -C ";
    command += QuoteCommandArgument(root);
    for (const std::string& arg : args) {
        command.push_back(' ');
        command += QuoteCommandArgument(arg);
    }
    command += " 2>nul";

    FILE* pipe = _popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }
    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    const int status = _pclose(pipe);
    if (status != 0) {
        return std::nullopt;
    }

    std::vector<std::string> lines;
    for (std::string line : tools::lint::SplitLines(output)) {
        line = tools::lint::Trim(line);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

std::vector<std::string> UniqueSorted(std::vector<std::string> paths) {
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

std::vector<std::string> EligibleFromRelative(const std::string& root, const std::vector<std::string>& relativePaths) {
    std::vector<std::string> files;
    for (const std::string& relative : relativePaths) {
        const std::string fullPath = tools::lint::AbsolutePath(tools::lint::JoinPath(root, relative));
        if (IsEligibleCppPath(root, fullPath) && tools::lint::FileExists(fullPath)) {
            files.push_back(fullPath);
        }
    }
    return UniqueSorted(std::move(files));
}

std::vector<std::string> GetAllFiles(const std::string& root) {
    const std::optional<std::vector<std::string>> files = RunGit(root,
        {"-c",
            "core.quotepath=off",
            "-c",
            "core.safecrlf=false",
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "--",
            "*.cpp",
            "*.h"});
    if (!files) {
        return {};
    }
    return EligibleFromRelative(root, *files);
}

bool GitHeadExists(const std::string& root) {
    return RunGit(root, {"rev-parse", "--verify", "HEAD"}).has_value();
}

std::vector<std::string> GetChangedFiles(const std::string& root) {
    std::vector<std::string> paths;
    if (GitHeadExists(root)) {
        if (std::optional<std::vector<std::string>> changed = RunGit(root,
                {"-c",
                    "core.safecrlf=false",
                    "diff",
                    "--name-only",
                    "--diff-filter=ACMR",
                    "HEAD",
                    "--",
                    "*.cpp",
                    "*.h"})) {
            paths.insert(paths.end(), changed->begin(), changed->end());
        }
    } else if (
        std::optional<std::vector<std::string>> staged = RunGit(root,
            {"-c",
                "core.safecrlf=false",
                "diff",
                "--name-only",
                "--diff-filter=ACMR",
                "--cached",
                "--",
                "*.cpp",
                "*.h"})) {
        paths.insert(paths.end(), staged->begin(), staged->end());
    }
    if (std::optional<std::vector<std::string>> untracked = RunGit(
            root, {"-c", "core.safecrlf=false", "ls-files", "--others", "--exclude-standard", "--", "*.cpp", "*.h"})) {
        paths.insert(paths.end(), untracked->begin(), untracked->end());
    }
    return EligibleFromRelative(root, UniqueSorted(std::move(paths)));
}

std::optional<std::string> ResolveTargetFile(const std::string& root, std::string_view targetFile) {
    if (targetFile.empty()) {
        return std::nullopt;
    }
    const std::string fullPath = tools::lint::AbsolutePath(tools::lint::JoinPath(root, targetFile));
    const std::string normalizedRoot = tools::lint::NormalizeSeparators(tools::lint::AbsolutePath(root));
    const std::string normalizedFile = tools::lint::NormalizeSeparators(fullPath);
    if (!tools::lint::StartsWith(
            tools::lint::ToLowerAscii(normalizedFile), tools::lint::ToLowerAscii(normalizedRoot) + "/")) {
        return std::nullopt;
    }
    if (!IsEligibleCppPath(root, fullPath)) {
        return std::nullopt;
    }
    return fullPath;
}

std::optional<Options> ParseOptions(int argc, char** argv) {
    Options options;
    options.root = tools::lint::CurrentDirectoryAbsolute();
    for (int index = 0; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "fix") {
            options.mode = Mode::Fix;
        } else if (arg == "changed") {
            options.scope = Scope::Changed;
        } else if (arg == "--root") {
            if (index + 1 >= argc) {
                return std::nullopt;
            }
            options.root = tools::lint::AbsolutePath(argv[++index]);
        } else if (arg == "--file") {
            if (index + 1 >= argc) {
                return std::nullopt;
            }
            options.targetFile = argv[++index];
        } else if (arg == "--stdout") {
            options.stdoutMode = true;
        } else if (arg == "-v" || arg == "--verbose") {
            options.verbose = true;
        } else {
            return std::nullopt;
        }
    }
    if (options.stdoutMode && options.targetFile.empty()) {
        return std::nullopt;
    }
    if (options.stdoutMode && options.mode == Mode::Fix) {
        return std::nullopt;
    }
    if (!options.targetFile.empty() && options.scope == Scope::Changed) {
        return std::nullopt;
    }
    return options;
}

}  // namespace

int RunFormat(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    std::optional<Options> parsed = ParseOptions(argc, argv);
    if (!parsed) {
        PrintUsage();
        return 2;
    }
    const Options& options = *parsed;

    std::string configError;
    std::optional<FormatterConfig> config = LoadFormatterConfig(options.root, configError);
    if (!config) {
        std::fprintf(stderr, "%s\n", configError.c_str());
        return 2;
    }

    std::vector<std::string> files;
    if (!options.targetFile.empty()) {
        std::optional<std::string> target = ResolveTargetFile(options.root, options.targetFile);
        if (!target) {
            std::fprintf(stderr, "--file target is not an eligible formatter input: %s\n", options.targetFile.c_str());
            return 2;
        }
        files.push_back(*target);
    } else if (options.scope == Scope::Changed) {
        files = GetChangedFiles(options.root);
    } else {
        files = GetAllFiles(options.root);
    }

    if (files.empty()) {
        if (options.scope == Scope::All) {
            std::fprintf(stderr, "No non-vendored C++ source files were found.\n");
            return 1;
        }
        std::printf("No eligible changed C++ source files were found.\n");
        return 0;
    }

    bool failed = false;
    int parseErrorCount = 0;
    int changedCount = 0;
    for (const std::string& file : files) {
        std::optional<std::string> text = tools::lint::ReadFileText(file);
        if (!text) {
            std::fprintf(stderr, "Failed to read %s\n", file.c_str());
            failed = true;
            continue;
        }
        FileFormatResult result = FormatOneText(*text, *config, file);
        if (!result.ok) {
            std::fprintf(stderr, "%s: %s\n", file.c_str(), result.error.c_str());
            failed = true;
            continue;
        }
        if (result.parseHadErrors) {
            ++parseErrorCount;
            if (options.verbose) {
                const std::string relative =
                    tools::lint::NormalizeSeparators(tools::lint::RelativePath(file, options.root));
                std::fprintf(stderr,
                    "%s:%d:%d: tree-sitter parse recovery at %s: %s\n",
                    relative.c_str(),
                    result.parseErrorLine,
                    result.parseErrorColumn,
                    result.parseErrorNodeType.c_str(),
                    result.parseErrorSnippet.c_str());
            }
        }
        if (options.stdoutMode) {
            std::fwrite(result.formatted.data(), 1, result.formatted.size(), stdout);
            return 0;
        }
        if (result.changed) {
            ++changedCount;
            if (options.mode == Mode::Fix) {
                if (!tools::lint::WriteFileText(file, ToFileLineEndings(result.formatted))) {
                    std::fprintf(stderr, "Failed to write %s\n", file.c_str());
                    failed = true;
                }
            } else {
                failed = true;
            }
        }
    }

    if (failed) {
        if (options.mode == Mode::Fix) {
            std::printf("Native formatting failed");
        } else {
            std::printf("Native formatting is required for %d file%s", changedCount, changedCount == 1 ? "" : "s");
        }
        if (parseErrorCount > 0) {
            std::printf(
                " (%d file%s parsed with tree-sitter errors)", parseErrorCount, parseErrorCount == 1 ? "" : "s");
        }
        std::printf(". Checked %d file%s in %s.\n",
            static_cast<int>(files.size()),
            files.size() == 1 ? "" : "s",
            FormatElapsed(std::chrono::steady_clock::now() - start).c_str());
        return 1;
    }

    const char* mode = options.mode == Mode::Fix ? "Formatted" : "Checked";
    const char* scope = options.scope == Scope::Changed ? "changed" : "all";
    std::printf("%s %d %s file%s with native tree-sitter formatter in %s.",
        mode,
        static_cast<int>(files.size()),
        scope,
        files.size() == 1 ? "" : "s",
        FormatElapsed(std::chrono::steady_clock::now() - start).c_str());
    if (parseErrorCount > 0) {
        std::printf(" %d file%s parsed with tree-sitter errors.", parseErrorCount, parseErrorCount == 1 ? "" : "s");
    }
    std::printf("\n");
    return 0;
}
