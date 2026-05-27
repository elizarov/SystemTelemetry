#include "tools/format.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <io.h>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <tree_sitter_cpp.h>
#include <utility>
#include <vector>

#include "tools/format_args.h"
#include "tools/format_config.h"
#include "tools/format_lexer.h"
#include "tools/impl/lint_common.h"

namespace {

using tools::format::FormatMode;
using tools::format::FormatOptions;
using tools::format::FormatterConfig;
using tools::format::DropTrailingCommas;
using tools::format::IsCommentOrNewline;
using tools::format::IsDigit;
using tools::format::IsHexDigit;
using tools::format::IsIdentifierBody;
using tools::format::IsIdentifierStart;
using tools::format::IsOctalDigit;
using tools::format::IsSpaceButNotNewline;
using tools::format::kNoTokenIndex;
using tools::format::Token;
using tools::format::TokenizeCharacterStream;
using tools::format::TokenKind;
using tools::format::TokenSpan;
using tools::format::TokenSubspan;

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

enum class SourceLayoutKind {
    Root,
    TemplateDeclaration,
    Assignment,
    DeclarationValue,
    Lambda,
    ConstructorInitializer,
    OperatorChain,
    StringLiteralSequence,
    Group,
};

struct SourceLayoutNode {
    SourceLayoutKind kind = SourceLayoutKind::Root;
    size_t begin = kNoTokenIndex;
    size_t end = kNoTokenIndex;
    size_t index = kNoTokenIndex;
    size_t groupOpen = kNoTokenIndex;
    size_t groupClose = kNoTokenIndex;
    bool stopChildren = false;
    int depth = 0;
    size_t order = 0;
    std::vector<SourceLayoutNode> children;
};

struct SourceLayoutTree {
    bool available = false;
    SourceLayoutNode root;
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

size_t NextCodeIndex(TokenSpan tokens, size_t index, size_t end) {
    while (index < end && IsCommentOrNewline(tokens[index])) {
        ++index;
    }
    return index;
}

std::optional<size_t> AnnotatedMatchingIndex(TokenSpan tokens, size_t index) {
    if (tokens.empty() || index >= tokens.size() || tokens[index].matchingIndex == kNoTokenIndex) {
        return std::nullopt;
    }
    const size_t base = tokens.front().modelIndex;
    if (base == kNoTokenIndex || tokens[index].matchingIndex < base) {
        return std::nullopt;
    }
    const size_t relative = tokens[index].matchingIndex - base;
    if (relative >= tokens.size() || tokens[relative].modelIndex != tokens[index].matchingIndex) {
        return std::nullopt;
    }
    return relative;
}

std::optional<size_t> FindControlBraceMatchingClose(TokenSpan tokens, size_t openIndex, size_t end) {
    if (openIndex >= end || !IsControlBraceGroupOpen(tokens[openIndex].text)) {
        return std::nullopt;
    }
    if (std::optional<size_t> annotated = AnnotatedMatchingIndex(tokens, openIndex); annotated && *annotated < end) {
        return annotated;
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

void AppendTokenRange(TokenSpan tokens, size_t begin, size_t end, std::vector<Token>& output) {
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

std::optional<size_t> FindControlHeaderEnd(TokenSpan tokens, size_t controlIndex, size_t end) {
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

void RewriteControlBracesRange(TokenSpan tokens, size_t begin, size_t end, std::vector<Token>& output);

size_t RewriteControlBracesStatement(TokenSpan tokens, size_t begin, size_t end, std::vector<Token>& output);

size_t RewriteIfControlStatement(TokenSpan tokens, size_t ifIndex, size_t end, std::vector<Token>& output);

size_t FindControlBracesStatementEnd(TokenSpan tokens, size_t begin, size_t end);

size_t RewriteBraceBlock(TokenSpan tokens, size_t openIndex, size_t end, std::vector<Token>& output) {
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

size_t FindSimpleControlBracesStatementEnd(TokenSpan tokens, size_t begin, size_t end) {
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

size_t FindIfControlBracesStatementEnd(TokenSpan tokens, size_t begin, size_t end) {
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

size_t FindElseControlBracesStatementEnd(TokenSpan tokens, size_t begin, size_t end) {
    return FindControlBracesStatementEnd(tokens, begin + 1, end);
}

bool ContainsOnlyNewlines(TokenSpan tokens, size_t begin, size_t end) {
    for (size_t index = begin; index < end; ++index) {
        if (tokens[index].kind != TokenKind::Newline) {
            return false;
        }
    }
    return true;
}

size_t FindHeaderBodyControlBracesStatementEnd(TokenSpan tokens, size_t begin, size_t end) {
    const std::optional<size_t> headerEnd = FindControlHeaderEnd(tokens, begin, end);
    if (!headerEnd) {
        return FindSimpleControlBracesStatementEnd(tokens, begin, end);
    }
    return FindControlBracesStatementEnd(tokens, *headerEnd, end);
}

size_t FindDoControlBracesStatementEnd(TokenSpan tokens, size_t begin, size_t end) {
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

size_t FindControlBracesStatementEnd(TokenSpan tokens, size_t begin, size_t end) {
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

void AppendRewrittenControlBody(TokenSpan tokens, size_t bodyBegin, size_t bodyEnd, std::vector<Token>& output) {
    output.push_back({TokenKind::Symbol, "{"});
    RewriteControlBracesStatement(tokens, bodyBegin, bodyEnd, output);
    output.push_back({TokenKind::Symbol, "}"});
}

size_t RewriteHeaderBodyControlStatement(
    TokenSpan tokens,
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

size_t RewriteElseControlStatement(TokenSpan tokens, size_t elseIndex, size_t end, std::vector<Token>& output) {
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

size_t RewriteIfControlStatement(TokenSpan tokens, size_t ifIndex, size_t end, std::vector<Token>& output) {
    size_t next = RewriteHeaderBodyControlStatement(tokens, ifIndex, end, output);
    const size_t elseIndex = NextCodeIndex(tokens, next, end);
    if (elseIndex < end && tokens[elseIndex].text == "else") {
        AppendTokenRange(tokens, next, elseIndex, output);
        next = RewriteElseControlStatement(tokens, elseIndex, end, output);
    }
    return next;
}

size_t RewriteDoControlStatement(TokenSpan tokens, size_t doIndex, size_t end, std::vector<Token>& output) {
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

size_t RewriteControlBracesStatement(TokenSpan tokens, size_t begin, size_t end, std::vector<Token>& output) {
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

void RewriteControlBracesRange(TokenSpan tokens, size_t begin, size_t end, std::vector<Token>& output) {
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

std::vector<Token> AddRequiredControlBraces(TokenSpan tokens) {
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
        bool executableBodyContext = false,
        const SourceLayoutTree* sourceLayout = nullptr
    ) : config_(config), sourceLayout_(sourceLayout), indentLevel_(initialIndentLevel) {
        if (executableBodyContext) {
            blockStack_.push_back({BlockKind::FunctionDefinition, true, DeclarationKind::None, false, -1});
        }
    }

    std::string Format(TokenSpan tokens) {
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
        TokenSpan condition;
        TokenSpan declaration;
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

    void HandleOriginalNewline(TokenSpan tokens, size_t& index) {
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

    bool ShouldForwardStandaloneLineCommentToPending(TokenSpan tokens, size_t newlineIndex, size_t newlineCount) const {
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

    bool ShouldPreserveOriginalBlankSeparator(TokenSpan tokens, size_t newlineIndex, size_t newlineCount) const {
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

    void EmitCodeToken(TokenSpan tokens, size_t& index) {
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

    void EmitStandaloneBlockOpenBrace(TokenSpan tokens, size_t& index) {
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

    bool NextTokenIsSameLineComment(TokenSpan tokens, size_t index) const {
        return index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LineComment;
    }

    void EmitOpenBrace(TokenSpan tokens, size_t& index) {
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

    void EmitCloseBrace(TokenSpan tokens, size_t& index) {
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

    void EmitLineComment(TokenSpan tokens, size_t& index) {
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

    bool IsTrailingCommentAfterEmittedClose(TokenSpan tokens, size_t index) const {
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
        std::vector<Token> replacementTokens = TokenizeCharacterStream(replacement);
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
        const std::string normalizedReplacement = FormatInline(TokenizeCharacterStream(replacement));
        if (FitsRawLine(defineLine + " " + normalizedReplacement)) {
            EmitLine(defineLine + " " + normalizedReplacement);
            pendingPreprocessorBlank_ = true;
            return;
        }
        EmitLine(defineLine + " \\");
        std::vector<std::string> replacementLines =
            FormatRange(TokenizeCharacterStream(normalizedReplacement), 1, {}, {});
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

    std::optional<std::vector<std::string>> FormatStructuredMacroReplacement(TokenSpan replacementTokens) const {
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

    bool IsStructuredMacroReplacement(TokenSpan replacementTokens) const {
        return ContainsTopLevelToken(replacementTokens, ";") && (
            ContainsWord(replacementTokens, "class") ||
            ContainsWord(replacementTokens, "enum") ||
            ContainsWord(replacementTokens, "struct") ||
            ContainsWord(replacementTokens, "template")
        );
    }

    std::vector<std::vector<Token>> SplitStatementLikeMacroReplacement(
        std::string_view defineLine,
        TokenSpan replacementTokens
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
        TokenSpan tokens,
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

    void EmitEnumEnumerators(TokenSpan tokens) {
        TokenSplitParts elements = SplitTopLevelParts(tokens, ',');
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(tokens, ',')) {
            EmitFormatted(tokens, ",");
            return;
        }
        bool emittedElement = false;
        for (size_t index = 0; index < elements.size(); ++index) {
            std::vector<std::string> elementLines =
                FormatDelimitedElement(elements.At(index), indentLevel_, ",", false, true, false, emittedElement);
            if (elementLines.empty()) {
                continue;
            }
            for (std::string& line : elementLines) {
                EmitLine(std::move(line));
            }
            emittedElement = true;
        }
    }

    void EmitFormatted(TokenSpan tokens, std::string_view suffix) {
        EmitFormattedAtIndent(tokens, indentLevel_, suffix);
    }

    void EmitFormattedAtIndent(TokenSpan tokens, int indentLevel, std::string_view suffix) {
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

    enum class LineTrimKind {
        None,
        Blank,
        OpenBrace,
        CloseBraceComma,
        Other,
    };

    enum class LayoutChoiceKind {
        None,
        Compact,
        TemplateDeclaration,
        Assignment,
        DeclarationValue,
        Lambda,
        ConstructorInitializer,
        OperatorChain,
        StringLiteralSequence,
        Group,
    };

    struct LayoutResult {
        bool fits = false;
        int maxOverflow = 0;
        size_t lineCount = 0;
        size_t lastLineWidth = 0;
        int deepestRenderedIndent = 0;
        int deepestBreakDepth = -1;
        size_t order = 0;
        bool compact = false;
        LineTrimKind firstTrim = LineTrimKind::None;
        LineTrimKind lastTrim = LineTrimKind::None;
        LayoutChoiceKind choice = LayoutChoiceKind::None;
        LayoutNode node{};
        int variant = 0;
    };

    struct TernaryExpressionParts {
        TokenSpan condition;
        TokenSpan trueBranch;
        TokenSpan falseBranch;
    };

    struct TokenSplitParts {
        std::vector<TokenSpan> spans;
        std::vector<std::vector<Token>> copies;
        bool usesCopies = false;

        size_t size() const {
            return usesCopies ? copies.size() : spans.size();
        }

        TokenSpan At(size_t index) const {
            return usesCopies ? TokenSpan(copies[index]) : spans[index];
        }
    };

    struct ShiftChainParts {
        TokenSpan receiver;
        std::vector<TokenSpan> segments;
        bool sawShiftOperator = false;
    };

    std::vector<std::string> FormatRange(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains = false,
        bool indentLogicalSplitChains = false
    ) const {
        std::vector<std::string> lines;
        RenderRange(
            lines,
            tokens,
            indentLevel,
            std::move(prefix),
            std::move(suffix),
            indentSplitChains,
            indentLogicalSplitChains
        );
        return lines;
    }

    void RenderRange(
        std::vector<std::string>& lines,
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains = false,
        bool indentLogicalSplitChains = false
    ) const {
        if (tokens.empty()) {
            if (prefix.empty() && suffix.empty()) {
                return;
            }
            lines.push_back(Indent(indentLevel) + prefix + suffix);
            return;
        }
        const LayoutContext
            context{indentLevel, std::move(prefix), std::move(suffix), indentSplitChains, indentLogicalSplitChains};
        (void)FormatLayoutTreeResult(tokens, context);
        RenderLayoutTree(lines, tokens, context);
    }

    LayoutResult FormatRangeResult(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains = false,
        bool indentLogicalSplitChains = false
    ) const {
        if (tokens.empty()) {
            if (!prefix.empty() || !suffix.empty()) {
                return SingleLineLayout(
                    indentLevel,
                    prefix.size() + suffix.size(),
                    true,
                    0,
                    0,
                    LayoutChoiceKind::Compact,
                    {},
                    0,
                    ClassifyTrimmedLine(prefix, suffix)
                );
            }
            return EmptyLayout(true, 0, 0, LayoutChoiceKind::Compact, {}, 0);
        }
        const LayoutContext
            context{indentLevel, std::move(prefix), std::move(suffix), indentSplitChains, indentLogicalSplitChains};
        return FormatLayoutTreeResult(tokens, context);
    }

    std::vector<std::string> FormatLayoutTree(TokenSpan tokens, const LayoutContext& context) const {
        (void)FormatLayoutTreeResult(tokens, context);
        std::vector<std::string> lines;
        RenderLayoutTree(lines, tokens, context);
        return lines;
    }

    LayoutResult FormatLayoutTreeResult(TokenSpan tokens, const LayoutContext& context) const {
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
            const size_t inlineWidth = context.prefix.size() +
                FormatInlineLength(tokens, InlineBudget(context.indentLevel, context.prefix, context.suffix)) +
                context.suffix.size();
            best = SingleLineLayout(
                context.indentLevel,
                inlineWidth,
                true,
                static_cast<int>(tokens.size()) + 1,
                0,
                LayoutChoiceKind::Compact,
                {},
                0,
                LineTrimKind::Other
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
            candidate->node = node;
            if (candidate->choice == LayoutChoiceKind::None) {
                candidate->choice = LayoutChoiceForNode(node.kind);
            }
            candidate->order = node.order;
            if (!hasBest || IsBetterLayout(*candidate, best)) {
                best = std::move(*candidate);
                hasBest = true;
            }
        }
        if (!hasBest) {
            best = SingleLineLayout(
                context.indentLevel,
                context.prefix.size() + FormatInlineLength(tokens) + context.suffix.size(),
                true,
                0,
                0,
                LayoutChoiceKind::Compact,
                {},
                0,
                LineTrimKind::Other
            );
        }
        layoutCache_[cacheKey] = best;
        return best;
    }

    void RenderLayoutTree(std::vector<std::string>& lines, TokenSpan tokens, const LayoutContext& context) const {
        const LayoutResult result = FormatLayoutTreeResult(tokens, context);
        switch (result.choice) {
            case LayoutChoiceKind::Compact:
            case LayoutChoiceKind::None: {
                std::string inlineText = context.prefix +
                    FormatInline(tokens, InlineBudget(context.indentLevel, context.prefix, context.suffix));
                AppendSuffix(inlineText, context.suffix);
                lines.push_back(Indent(context.indentLevel) + inlineText);
                return;
            }
            case LayoutChoiceKind::TemplateDeclaration:
                AppendRenderedLines(
                    lines,
                    IsRequiresClausePrefix(tokens) ?
                        FormatRequiresClause(tokens, context.indentLevel, context.prefix, context.suffix) :
                        FormatTemplateDeclaration(tokens, context.indentLevel, context.prefix, context.suffix)
                );
                return;
            case LayoutChoiceKind::Assignment:
                RenderAssignment(
                    lines,
                    tokens,
                    result.node.index,
                    context.indentLevel,
                    context.prefix,
                    context.suffix,
                    context.indentSplitChains,
                    context.indentLogicalSplitChains
                );
                return;
            case LayoutChoiceKind::DeclarationValue:
                RenderDeclarationValueBreak(
                    lines,
                    tokens,
                    result.node.index,
                    context.indentLevel,
                    context.prefix,
                    context.suffix
                );
                return;
            case LayoutChoiceKind::Lambda:
                AppendRenderedLines(
                    lines,
                    FormatSplitLambda(tokens, result.node.index, context.indentLevel, context.prefix, context.suffix)
                );
                return;
            case LayoutChoiceKind::ConstructorInitializer:
                AppendRenderedLines(
                    lines,
                    FormatConstructorInitializerList(
                        tokens,
                        result.node.index,
                        context.indentLevel,
                        context.prefix,
                        context.suffix
                    )
                );
                return;
            case LayoutChoiceKind::OperatorChain:
                RenderOperatorChain(
                    lines,
                    tokens,
                    context.indentLevel,
                    context.prefix,
                    context.suffix,
                    context.indentSplitChains,
                    context.indentLogicalSplitChains
                );
                return;
            case LayoutChoiceKind::StringLiteralSequence:
                AppendRenderedLines(
                    lines,
                    FormatStringLiteralSequence(tokens, context.indentLevel, context.prefix, context.suffix)
                );
                return;
            case LayoutChoiceKind::Group:
                if (result.variant == 1) {
                    RenderStackedNestedGroup(
                        lines,
                        tokens,
                        result.node.group,
                        context.indentLevel,
                        context.prefix,
                        context.suffix
                    );
                } else {
                    RenderSplitGroup(
                        lines,
                        tokens,
                        result.node.group,
                        context.indentLevel,
                        context.prefix,
                        context.suffix
                    );
                }
                return;
        }
    }

    static void AppendRenderedLines(std::vector<std::string>& lines, std::vector<std::string> rendered) {
        lines.insert(lines.end(), std::make_move_iterator(rendered.begin()), std::make_move_iterator(rendered.end()));
    }

    LayoutTree BuildLayoutTree(TokenSpan tokens) const {
        if (CanUseSourceLayout(tokens)) {
            return BuildTreeSitterLayoutTree(tokens);
        }
        return BuildFallbackLayoutTree(tokens);
    }

    LayoutTree BuildFallbackLayoutTree(TokenSpan tokens) const {
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

    bool CanUseSourceLayout(TokenSpan tokens) const {
        if (sourceLayout_ == nullptr || !sourceLayout_->available || tokens.empty()) {
            return false;
        }
        bool hasSourceToken = false;
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (tokens[index].modelIndex == kNoTokenIndex) {
                return false;
            }
            if (index > 0 && tokens[index].modelIndex != tokens[index - 1].modelIndex + 1) {
                return false;
            }
            hasSourceToken = hasSourceToken || tokens[index].sourceBegin != kNoTokenIndex;
        }
        return hasSourceToken;
    }

    bool HasOnlyTrailingStatementSuffix(TokenSpan tokens, size_t spanBegin, size_t sourceEnd) const {
        if (sourceEnd < spanBegin) {
            return false;
        }
        size_t index = sourceEnd - spanBegin;
        if (index > tokens.size()) {
            return false;
        }
        bool sawTerminator = false;
        for (; index < tokens.size(); ++index) {
            if (IsCommentOrNewline(tokens[index])) {
                continue;
            }
            if (!sawTerminator && (tokens[index].text == ";" || tokens[index].text == ",")) {
                sawTerminator = true;
                continue;
            }
            return false;
        }
        return sawTerminator;
    }

    LayoutTree BuildTreeSitterLayoutTree(TokenSpan tokens) const {
        LayoutTree tree;
        if (sourceLayout_ == nullptr || !sourceLayout_->available || tokens.empty()) {
            return tree;
        }
        const size_t spanBegin = tokens.front().modelIndex;
        const size_t spanEnd = tokens.back().modelIndex + 1;
        size_t order = 0;
        CollectTreeSitterLayoutNodes(sourceLayout_->root, tokens, spanBegin, spanEnd, tree, order);
        return tree;
    }

    void CollectTreeSitterLayoutNodes(
        const SourceLayoutNode& source,
        TokenSpan tokens,
        size_t spanBegin,
        size_t spanEnd,
        LayoutTree& tree,
        size_t& order
    ) const {
        for (const SourceLayoutNode& child : source.children) {
            if (child.end <= spanBegin || child.begin >= spanEnd) {
                continue;
            }
            bool stopAtChild = false;
            const bool contained = child.begin >= spanBegin && child.end <= spanEnd;
            const bool partialTemplate = child.kind == SourceLayoutKind::TemplateDeclaration &&
                child.begin >= spanBegin &&
                child.begin < spanEnd;
            const bool partialLambda = child.kind == SourceLayoutKind::Lambda &&
                child.index != kNoTokenIndex &&
                child.index >= spanBegin &&
                child.index < spanEnd;
            if (contained || partialTemplate || partialLambda) {
                stopAtChild = AddTreeSitterLayoutNode(child, tokens, spanBegin, spanEnd, tree, order);
            }
            if (!stopAtChild) {
                CollectTreeSitterLayoutNodes(child, tokens, spanBegin, spanEnd, tree, order);
            }
        }
    }

    bool AddTreeSitterLayoutNode(
        const SourceLayoutNode& source,
        TokenSpan tokens,
        size_t spanBegin,
        size_t spanEnd,
        LayoutTree& tree,
        size_t& order
    ) const {
        const auto addNode = [&](LayoutNode node) {
            for (const LayoutNode& existing : tree.breakNodes) {
                if (
                    existing.kind == node.kind &&
                    existing.index == node.index &&
                    existing.group.open == node.group.open &&
                    existing.group.close == node.group.close
                ) {
                    return false;
                }
            }
            node.depth = source.depth;
            node.order = order++;
            tree.breakNodes.push_back(node);
            return true;
        };

        switch (source.kind) {
            case SourceLayoutKind::Root:
                return false;
            case SourceLayoutKind::TemplateDeclaration:
                return addNode({LayoutNodeKind::TemplateDeclaration, 0, {}, source.depth, 0});
            case SourceLayoutKind::Assignment:
                if (source.index == kNoTokenIndex || source.index < spanBegin) {
                    return false;
                }
                (void)addNode({LayoutNodeKind::Assignment, source.index - spanBegin, {}, source.depth, 0});
                return false;
            case SourceLayoutKind::DeclarationValue:
                if (source.index == kNoTokenIndex || source.index <= spanBegin) {
                    return false;
                }
                (void)addNode({LayoutNodeKind::DeclarationValue, source.index - spanBegin, {}, source.depth, 0});
                return false;
            case SourceLayoutKind::Lambda:
                if (source.index == kNoTokenIndex || source.index < spanBegin) {
                    return false;
                }
                return addNode({LayoutNodeKind::Lambda, source.index - spanBegin, {}, source.depth, 0});
            case SourceLayoutKind::ConstructorInitializer:
                if (source.index == kNoTokenIndex || source.index < spanBegin) {
                    return false;
                }
                (void)addNode({LayoutNodeKind::ConstructorInitializer, source.index - spanBegin, {}, source.depth, 0});
                return false;
            case SourceLayoutKind::OperatorChain: {
                const bool ownsSegment = source.begin == spanBegin &&
                    (source.end == spanEnd || HasOnlyTrailingStatementSuffix(tokens, spanBegin, source.end));
                const bool ownsReturnSegment = source.end <= spanEnd &&
                    HasOnlyTrailingStatementSuffix(tokens, spanBegin, source.end) &&
                    !tokens.empty() &&
                    tokens.front().text == "return";
                if (!ownsSegment && !ownsReturnSegment) {
                    return false;
                }
                if (!CanSplitOperatorChain(tokens)) {
                    return false;
                }
                return addNode({LayoutNodeKind::OperatorChain, 0, {}, source.depth, 0}) && source.stopChildren;
            }
            case SourceLayoutKind::StringLiteralSequence:
                if (source.begin != spanBegin || (
                    source.end != spanEnd && !HasOnlyTrailingStatementSuffix(tokens, spanBegin, source.end)
                )) {
                    return false;
                }
                return addNode({LayoutNodeKind::StringLiteralSequence, 0, {}, source.depth, 0});
            case SourceLayoutKind::Group: {
                if (
                    source.groupOpen < spanBegin ||
                    source.groupClose < spanBegin ||
                    source.groupOpen == kNoTokenIndex ||
                    source.groupClose == kNoTokenIndex
                ) {
                    return false;
                }
                const size_t open = source.groupOpen - spanBegin;
                const size_t close = source.groupClose - spanBegin;
                if (
                    open >= tokens.size() ||
                    close >= tokens.size() ||
                    IsEmptyGroupPair(tokens, open, close) ||
                    IsNonWrappablePrefixGroup(tokens, open, close) ||
                    IsFunctionPointerDeclaratorGroupOpen(tokens, open)
                ) {
                    return false;
                }
                (void)addNode({LayoutNodeKind::Group, 0, GroupPair{open, close}, source.depth, 0});
                return false;
            }
        }
        return false;
    }

    void CollectLayoutGroupNodes(
        TokenSpan tokens,
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

    bool GroupOwnsNestedBreaks(TokenSpan tokens, size_t open, size_t close) const {
        if (open >= tokens.size() || close > tokens.size()) {
            return false;
        }
        if (IsLambdaBodyOpenToken(tokens, open)) {
            return true;
        }
        if (tokens[open].text == "{") {
            return true;
        }
        TokenSpan inner = TokenSubspan(tokens, open + 1, close);
        if (HasNestedWrappableGroupWithNonClosingTail(tokens, open + 1, close)) {
            return true;
        }
        return ContainsTopLevelSeparator(inner, ',') || ContainsTopLevelSeparator(inner, ';');
    }

    bool HasNestedWrappableGroupWithNonClosingTail(TokenSpan tokens, size_t begin, size_t end) const {
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

    bool HasNonClosingTokenBefore(TokenSpan tokens, size_t begin, size_t end) const {
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

    bool IsStackedClosingToken(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size()) {
            return false;
        }
        return tokens[index].text == ")" ||
            tokens[index].text == "]" ||
            tokens[index].text == "}" ||
            IsTemplateAngleCloseToken(tokens, index);
    }

    std::optional<GroupPair> FindLeadingControlHeaderGroup(TokenSpan tokens) const {
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

    std::optional<LayoutResult> FormatLayoutNodeResult(
        TokenSpan tokens,
        const LayoutContext& context,
        const LayoutNode& node
    ) const {
        switch (node.kind) {
            case LayoutNodeKind::TemplateDeclaration:
                if (!IsRequiresClausePrefix(tokens) && !IsTemplateDeclarationPrefix(tokens)) {
                    return std::nullopt;
                }
                return IsRequiresClausePrefix(tokens) ?
                    FormatRequiresClauseResult(tokens, context.indentLevel, context.prefix, context.suffix) :
                    FormatTemplateDeclarationResult(tokens, context.indentLevel, context.prefix, context.suffix);
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
                return FormatSplitLambdaResult(tokens, node.index, context.indentLevel, context.prefix, context.suffix);
            case LayoutNodeKind::ConstructorInitializer:
                return FormatConstructorInitializerListResult(
                    tokens,
                    node.index,
                    context.indentLevel,
                    context.prefix,
                    context.suffix
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
                return FormatStringLiteralSequenceResult(tokens, context.indentLevel, context.prefix, context.suffix);
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
        LayoutResult result = EmptyLayout(compact, breakDepth, order, LayoutChoiceKind::None, {}, 0);
        for (const std::string& line : lines) {
            AppendMeasuredLine(result, line.size(), LeadingIndentLevel(line), ClassifyTrimmedLine(line));
        }
        return result;
    }

    LayoutResult EmptyLayout(
        bool compact,
        int breakDepth,
        size_t order,
        LayoutChoiceKind choice,
        LayoutNode node,
        int variant
    ) const {
        LayoutResult result;
        result.fits = true;
        result.order = order;
        result.compact = compact;
        result.deepestBreakDepth = compact ? -1 : breakDepth;
        result.choice = choice;
        result.node = node;
        result.variant = variant;
        return result;
    }

    LayoutResult SingleLineLayout(
        int indentLevel,
        size_t contentWidth,
        bool compact,
        int breakDepth,
        size_t order,
        LayoutChoiceKind choice,
        LayoutNode node,
        int variant,
        LineTrimKind trimKind = LineTrimKind::Other
    ) const {
        LayoutResult result = EmptyLayout(compact, breakDepth, order, choice, node, variant);
        AppendMeasuredLine(result, IndentWidth(indentLevel) + contentWidth, indentLevel, trimKind);
        return result;
    }

    void AppendMeasuredLine(LayoutResult& result, size_t width, int indentLevel, LineTrimKind trimKind) const {
        const int overflow = std::max(0, static_cast<int>(width) - config_.columnLimit);
        result.maxOverflow = std::max(result.maxOverflow, overflow);
        result.fits = result.maxOverflow == 0;
        result.deepestRenderedIndent = std::max(result.deepestRenderedIndent, indentLevel);
        if (result.lineCount == 0) {
            result.firstTrim = trimKind;
        }
        result.lastTrim = trimKind;
        result.lastLineWidth = width;
        ++result.lineCount;
    }

    void AppendSuffixToLastLine(LayoutResult& result, std::string_view suffix) const {
        if (suffix.empty() || result.lineCount == 0) {
            return;
        }
        result.lastLineWidth += suffix.size();
        result.maxOverflow = std::max(result.maxOverflow, OverflowForWidth(result.lastLineWidth));
        result.fits = result.maxOverflow == 0;
        result.lastTrim = LineTrimKind::Other;
    }

    void AppendMeasuredLayout(
        LayoutResult& target,
        const LayoutResult& child,
        bool combineNestedInitializerBoundary = false,
        int breakDepthOffset = 0
    ) const {
        if (child.lineCount == 0) {
            MergeBreakDepth(target, child, breakDepthOffset);
            return;
        }
        if (target.lineCount == 0) {
            const int targetBreakDepth = target.deepestBreakDepth;
            const bool targetCompact = target.compact;
            const size_t targetOrder = target.order;
            const LayoutChoiceKind targetChoice = target.choice;
            const LayoutNode targetNode = target.node;
            const int targetVariant = target.variant;
            target = child;
            target.deepestBreakDepth = targetBreakDepth;
            target.compact = targetCompact;
            target.order = targetOrder;
            target.choice = targetChoice;
            target.node = targetNode;
            target.variant = targetVariant;
            MergeBreakDepth(target, child, breakDepthOffset);
            return;
        }
        if (
            combineNestedInitializerBoundary &&
            target.lastTrim == LineTrimKind::CloseBraceComma &&
            child.firstTrim == LineTrimKind::OpenBrace
        ) {
            const size_t combinedWidth = target.lastLineWidth + 2;
            target.maxOverflow =
                std::max(target.maxOverflow, std::max(child.maxOverflow, OverflowForWidth(combinedWidth)));
            target.fits = target.maxOverflow == 0;
            target.lineCount += child.lineCount - 1;
            if (child.lineCount == 1) {
                target.lastLineWidth = combinedWidth;
                target.lastTrim = LineTrimKind::Other;
            } else {
                target.lastLineWidth = child.lastLineWidth;
                target.lastTrim = child.lastTrim;
            }
            target.deepestRenderedIndent = std::max(target.deepestRenderedIndent, child.deepestRenderedIndent);
            MergeBreakDepth(target, child, breakDepthOffset);
            return;
        }
        target.maxOverflow = std::max(target.maxOverflow, child.maxOverflow);
        target.fits = target.maxOverflow == 0;
        target.lineCount += child.lineCount;
        target.lastLineWidth = child.lastLineWidth;
        target.lastTrim = child.lastTrim;
        target.deepestRenderedIndent = std::max(target.deepestRenderedIndent, child.deepestRenderedIndent);
        MergeBreakDepth(target, child, breakDepthOffset);
    }

    int OverflowForWidth(size_t width) const {
        return std::max(0, static_cast<int>(width) - config_.columnLimit);
    }

    size_t IndentWidth(int indentLevel) const {
        return static_cast<size_t>(std::max(0, indentLevel)) * static_cast<size_t>(config_.indentWidth);
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

    static LayoutChoiceKind LayoutChoiceForNode(LayoutNodeKind kind) {
        switch (kind) {
            case LayoutNodeKind::TemplateDeclaration:
                return LayoutChoiceKind::TemplateDeclaration;
            case LayoutNodeKind::Assignment:
                return LayoutChoiceKind::Assignment;
            case LayoutNodeKind::DeclarationValue:
                return LayoutChoiceKind::DeclarationValue;
            case LayoutNodeKind::Lambda:
                return LayoutChoiceKind::Lambda;
            case LayoutNodeKind::ConstructorInitializer:
                return LayoutChoiceKind::ConstructorInitializer;
            case LayoutNodeKind::OperatorChain:
                return LayoutChoiceKind::OperatorChain;
            case LayoutNodeKind::StringLiteralSequence:
                return LayoutChoiceKind::StringLiteralSequence;
            case LayoutNodeKind::Group:
                return LayoutChoiceKind::Group;
        }
        return LayoutChoiceKind::None;
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

    LineTrimKind ClassifyTrimmedLine(std::string_view line) const {
        size_t begin = 0;
        while (begin < line.size() && IsSpaceButNotNewline(line[begin])) {
            ++begin;
        }
        size_t end = line.size();
        while (end > begin && IsSpaceButNotNewline(line[end - 1])) {
            --end;
        }
        const std::string_view trimmed = line.substr(begin, end - begin);
        if (trimmed.empty()) {
            return LineTrimKind::Blank;
        }
        if (trimmed == "{") {
            return LineTrimKind::OpenBrace;
        }
        if (trimmed == "},") {
            return LineTrimKind::CloseBraceComma;
        }
        return LineTrimKind::Other;
    }

    LineTrimKind ClassifyTrimmedLine(std::string_view line, std::string_view suffix) const {
        if (line.empty() && suffix.empty()) {
            return LineTrimKind::Blank;
        }
        if (line == "{" && suffix.empty()) {
            return LineTrimKind::OpenBrace;
        }
        if (line == "}" && suffix == ",") {
            return LineTrimKind::CloseBraceComma;
        }
        return LineTrimKind::Other;
    }

    std::string LayoutCacheKey(TokenSpan tokens, const LayoutContext& context) const {
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

    bool IsTemplateDeclarationPrefix(TokenSpan tokens) const {
        if (tokens.empty() || tokens.front().text != "template") {
            return false;
        }
        const size_t open = NextSignificantIndex(tokens, 1);
        return open < tokens.size() && tokens[open].text == "<" && IsTemplateAngleOpen(tokens, open);
    }

    std::vector<std::string> FormatTemplateDeclaration(
        TokenSpan tokens,
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
        TokenSpan templatePrefix = TokenSubspan(tokens, 0, *close + 1);
        TokenSpan declaration = TokenSubspan(tokens, *close + 1, tokens.size());
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

    LayoutResult FormatTemplateDeclarationResult(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        const size_t open = NextSignificantIndex(tokens, 1);
        const std::optional<size_t> close = FindTemplateAngleClose(tokens, open);
        if (!close) {
            return SingleLineLayout(
                indentLevel,
                prefix.size() + FormatInlineLength(tokens) + suffix.size(),
                true,
                0,
                0,
                LayoutChoiceKind::TemplateDeclaration,
                {},
                0,
                LineTrimKind::Other
            );
        }
        TokenSpan templatePrefix = TokenSubspan(tokens, 0, *close + 1);
        TokenSpan declaration = TokenSubspan(tokens, *close + 1, tokens.size());
        const size_t templatePrefixWidth = prefix.size() + FormatInlineLength(templatePrefix);
        LayoutResult result = EmptyLayout(false, 0, 0, LayoutChoiceKind::TemplateDeclaration, {}, 0);
        if (FitsWidth(indentLevel, templatePrefixWidth)) {
            AppendMeasuredLine(
                result,
                IndentWidth(indentLevel) + templatePrefixWidth,
                indentLevel,
                LineTrimKind::Other
            );
        } else {
            LayoutResult prefixResult =
                FormatSplitGroupResult(templatePrefix, GroupPair{open, *close}, indentLevel, std::move(prefix), {});
            AppendMeasuredLayout(result, prefixResult, false, 1);
        }
        if (declaration.empty()) {
            AppendSuffixToLastLine(result, suffix);
            return result;
        }
        if (std::optional<RequiresClauseParts> requiresClause = SplitRequiresClause(declaration)) {
            const size_t inlineRequiresWidth = FormatInlineRequiresClauseLength(requiresClause->condition);
            if (result.lineCount == 1 && FitsWidth(indentLevel, templatePrefixWidth + 1 + inlineRequiresWidth)) {
                result = EmptyLayout(false, 0, 0, LayoutChoiceKind::TemplateDeclaration, {}, 0);
                AppendMeasuredLine(
                    result,
                    IndentWidth(indentLevel) + templatePrefixWidth + 1 + inlineRequiresWidth,
                    indentLevel,
                    LineTrimKind::Other
                );
                LayoutResult declarationResult =
                    FormatRangeResult(requiresClause->declaration, indentLevel, {}, std::move(suffix));
                AppendMeasuredLayout(result, declarationResult, false, 1);
            } else {
                LayoutResult declarationResult =
                    FormatRequiresClauseResult(declaration, indentLevel + 1, {}, std::move(suffix), indentLevel);
                AppendMeasuredLayout(result, declarationResult, false, 1);
            }
            return result;
        }
        LayoutResult declarationResult = FormatRangeResult(declaration, indentLevel, {}, std::move(suffix));
        AppendMeasuredLayout(result, declarationResult, false, 1);
        return result;
    }

    bool IsRequiresClausePrefix(TokenSpan tokens) const {
        return SplitRequiresClause(tokens).has_value();
    }

    std::optional<RequiresClauseParts> SplitRequiresClause(TokenSpan tokens) const {
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
            TokenSubspan(tokens, open + 1, *close),
            TokenSubspan(tokens, *close + 1, tokens.size())
        };
    }

    std::string FormatInlineRequiresClause(TokenSpan condition) const {
        return "requires(" + FormatInline(condition) + ")";
    }

    size_t FormatInlineRequiresClauseLength(TokenSpan condition) const {
        return std::string_view("requires(").size() + FormatInlineLength(condition) + 1;
    }

    std::vector<std::string> FormatRequiresClause(
        TokenSpan tokens,
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

    LayoutResult FormatRequiresClauseResult(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        std::optional<int> declarationIndentLevel = std::nullopt
    ) const {
        const std::optional<RequiresClauseParts> parts = SplitRequiresClause(tokens);
        if (!parts) {
            return SingleLineLayout(
                indentLevel,
                prefix.size() + FormatInlineLength(tokens) + suffix.size(),
                true,
                0,
                0,
                LayoutChoiceKind::TemplateDeclaration,
                {},
                0,
                LineTrimKind::Other
            );
        }
        LayoutResult result = EmptyLayout(false, 0, 0, LayoutChoiceKind::TemplateDeclaration, {}, 0);
        const size_t requiresLineWidth = prefix.size() + FormatInlineRequiresClauseLength(parts->condition);
        if (FitsWidth(indentLevel, requiresLineWidth)) {
            AppendMeasuredLine(result, IndentWidth(indentLevel) + requiresLineWidth, indentLevel, LineTrimKind::Other);
        } else {
            AppendMeasuredLine(
                result,
                IndentWidth(indentLevel) + prefix.size() + std::string_view("requires(").size(),
                indentLevel,
                LineTrimKind::Other
            );
            LayoutResult conditionResult = FormatRangeResult(parts->condition, indentLevel + 1, {}, {}, true);
            AppendMeasuredLayout(result, conditionResult, false, 1);
            AppendMeasuredLine(result, IndentWidth(indentLevel) + 1, indentLevel, LineTrimKind::Other);
        }
        if (parts->declaration.empty()) {
            AppendSuffixToLastLine(result, suffix);
            return result;
        }
        const int trailingDeclarationIndentLevel = declarationIndentLevel.value_or(indentLevel);
        LayoutResult declarationResult =
            FormatRangeResult(parts->declaration, trailingDeclarationIndentLevel, {}, std::move(suffix));
        AppendMeasuredLayout(result, declarationResult, false, 1);
        return result;
    }

    std::vector<std::string> FormatAssignment(
        TokenSpan tokens,
        size_t assignment,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains,
        bool indentLogicalSplitChains
    ) const {
        std::vector<std::string> lines;
        RenderAssignment(
            lines,
            tokens,
            assignment,
            indentLevel,
            std::move(prefix),
            std::move(suffix),
            indentSplitChains,
            indentLogicalSplitChains
        );
        return lines;
    }

    LayoutResult FormatAssignmentResult(
        TokenSpan tokens,
        size_t assignment,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains,
        bool indentLogicalSplitChains
    ) const {
        if (StartsWithInitializerList(tokens, assignment + 1)) {
            const size_t open = NextSignificantIndex(tokens, assignment + 1);
            const std::optional<size_t> close = FindMatchingClose(tokens, open);
            if (!close) {
                return SingleLineLayout(
                    indentLevel,
                    prefix.size() + FormatInlineLength(tokens) + suffix.size(),
                    true,
                    0,
                    0,
                    LayoutChoiceKind::Assignment,
                    {},
                    0,
                    LineTrimKind::Other
                );
            }
            LayoutResult result = FormatSplitGroupResult(
                tokens,
                GroupPair{open, *close},
                indentLevel,
                std::move(prefix),
                std::move(suffix)
            );
            result.choice = LayoutChoiceKind::Assignment;
            result.variant = 0;
            return result;
        }
        TokenSpan lhs = TokenSubspan(tokens, 0, assignment + 1);
        TokenSpan rhs = TokenSubspan(tokens, assignment + 1, tokens.size());
        LayoutResult rhsResult =
            FormatRangeResult(rhs, indentLevel + 1, {}, suffix, indentSplitChains, indentLogicalSplitChains);
        LayoutResult best = EmptyLayout(false, 0, 0, LayoutChoiceKind::Assignment, {}, 1);
        AppendMeasuredLine(
            best,
            IndentWidth(indentLevel) + prefix.size() + FormatInlineLength(lhs),
            indentLevel,
            LineTrimKind::Other
        );
        AppendMeasuredLayout(best, rhsResult, false, 1);
        std::string attachedPrefix = prefix + FormatInline(lhs) + " ";
        LayoutResult attachedRhs = FormatRangeResult(
            rhs,
            indentLevel,
            std::move(attachedPrefix),
            suffix,
            indentSplitChains,
            indentLogicalSplitChains
        );
        LayoutResult attached = attachedRhs;
        attached.choice = LayoutChoiceKind::Assignment;
        attached.variant = 2;
        attached.order = 1;
        attached.compact = attachedRhs.deepestBreakDepth < 0;
        attached.deepestBreakDepth = attached.compact ? -1 : attachedRhs.deepestBreakDepth + 1;
        if (IsBetterLayout(attached, best)) {
            best = std::move(attached);
        }

        TokenSpan lhsValue = TokenSubspan(tokens, 0, assignment);
        if (!lhsValue.empty()) {
            LayoutResult lhsResult = FormatRangeResult(lhsValue, indentLevel, prefix, " =", true);
            if (lhsResult.lineCount > 0) {
                LayoutResult splitRhs =
                    FormatRangeResult(rhs, indentLevel + 1, {}, suffix, indentSplitChains, indentLogicalSplitChains);
                LayoutResult splitLhs = EmptyLayout(false, 0, 2, LayoutChoiceKind::Assignment, {}, 3);
                AppendMeasuredLayout(splitLhs, lhsResult, false, 1);
                AppendMeasuredLayout(splitLhs, splitRhs, false, 1);
                if (IsBetterLayout(splitLhs, best)) {
                    best = std::move(splitLhs);
                }
            }
        }
        return best;
    }

    std::optional<LayoutResult> FormatDeclarationValueBreak(
        TokenSpan tokens,
        size_t declarator,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        if (declarator == 0 || declarator >= tokens.size()) {
            return std::nullopt;
        }
        TokenSpan typeTokens = TokenSubspan(tokens, 0, declarator);
        TokenSpan valueTokens = TokenSubspan(tokens, declarator, tokens.size());
        const size_t typeWidth = prefix.size() + FormatInlineLength(typeTokens);
        if (typeWidth == 0) {
            return std::nullopt;
        }

        LayoutResult valueResult = FormatRangeResult(valueTokens, indentLevel + 1, {}, std::move(suffix), true);
        LayoutResult result = EmptyLayout(false, 0, 0, LayoutChoiceKind::DeclarationValue, {}, 0);
        AppendMeasuredLine(result, IndentWidth(indentLevel) + typeWidth, indentLevel, LineTrimKind::Other);
        AppendMeasuredLayout(result, valueResult, false, 1);
        return result;
    }

    void RenderAssignment(
        std::vector<std::string>& lines,
        TokenSpan tokens,
        size_t assignment,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains,
        bool indentLogicalSplitChains
    ) const {
        const LayoutResult result = FormatAssignmentResult(
            tokens,
            assignment,
            indentLevel,
            prefix,
            suffix,
            indentSplitChains,
            indentLogicalSplitChains
        );
        if (StartsWithInitializerList(tokens, assignment + 1) || result.variant == 0) {
            AppendRenderedLines(
                lines,
                FormatInitializerAssignment(tokens, assignment, indentLevel, std::move(prefix), std::move(suffix))
            );
            return;
        }
        TokenSpan lhs = TokenSubspan(tokens, 0, assignment + 1);
        TokenSpan rhs = TokenSubspan(tokens, assignment + 1, tokens.size());
        if (result.variant == 2) {
            std::string attachedPrefix = std::move(prefix) + FormatInline(lhs) + " ";
            RenderRange(
                lines,
                rhs,
                indentLevel,
                std::move(attachedPrefix),
                std::move(suffix),
                indentSplitChains,
                indentLogicalSplitChains
            );
            return;
        }
        if (result.variant == 3) {
            TokenSpan lhsValue = TokenSubspan(tokens, 0, assignment);
            RenderRange(lines, lhsValue, indentLevel, std::move(prefix), " =", true);
            RenderRange(
                lines,
                rhs,
                indentLevel + 1,
                {},
                std::move(suffix),
                indentSplitChains,
                indentLogicalSplitChains
            );
            return;
        }
        lines.push_back(Indent(indentLevel) + std::move(prefix) + FormatInline(lhs));
        RenderRange(lines, rhs, indentLevel + 1, {}, std::move(suffix), indentSplitChains, indentLogicalSplitChains);
    }

    void RenderDeclarationValueBreak(
        std::vector<std::string>& lines,
        TokenSpan tokens,
        size_t declarator,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        if (declarator == 0 || declarator >= tokens.size()) {
            std::string inlineText = std::move(prefix) + FormatInline(tokens);
            AppendSuffix(inlineText, suffix);
            lines.push_back(Indent(indentLevel) + inlineText);
            return;
        }
        TokenSpan typeTokens = TokenSubspan(tokens, 0, declarator);
        TokenSpan valueTokens = TokenSubspan(tokens, declarator, tokens.size());
        lines.push_back(Indent(indentLevel) + std::move(prefix) + FormatInline(typeTokens));
        RenderRange(lines, valueTokens, indentLevel + 1, {}, std::move(suffix), true);
    }

    std::string FormatGroupOpeningLine(TokenSpan tokens, GroupPair group) const {
        if (group.open < tokens.size() && tokens[group.open].text == "<" && IsTemplateAngleOpen(tokens, group.open)) {
            TokenSpan beforeOpen = TokenSubspan(tokens, 0, group.open);
            std::string beforeOpenText = FormatInline(beforeOpen);
            if (!beforeOpen.empty() && beforeOpen.back().text == "template") {
                beforeOpenText += " ";
            }
            return beforeOpenText + "<";
        }
        TokenSpan firstLineTokens = TokenSubspan(tokens, 0, group.open + 1);
        return FormatInline(firstLineTokens);
    }

    size_t FormatGroupOpeningLineLength(TokenSpan tokens, GroupPair group) const {
        if (group.open < tokens.size() && tokens[group.open].text == "<" && IsTemplateAngleOpen(tokens, group.open)) {
            TokenSpan beforeOpen = TokenSubspan(tokens, 0, group.open);
            size_t length = FormatInlineLength(beforeOpen);
            if (!beforeOpen.empty() && beforeOpen.back().text == "template") {
                ++length;
            }
            return length + 1;
        }
        TokenSpan firstLineTokens = TokenSubspan(tokens, 0, group.open + 1);
        return FormatInlineLength(firstLineTokens);
    }

    LineTrimKind GroupOpeningTrimKind(TokenSpan tokens, GroupPair group, std::string_view prefix) const {
        if (prefix.empty() && group.open == 0 && group.open < tokens.size() && tokens[group.open].text == "{") {
            return LineTrimKind::OpenBrace;
        }
        return LineTrimKind::Other;
    }

    std::vector<std::string> FormatInitializerAssignment(
        TokenSpan tokens,
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
        TokenSpan tokens,
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
        TokenSpan header = TokenSubspan(tokens, 0, bodyOpen);
        TokenSpan body = TokenSubspan(tokens, bodyOpen + 1, *bodyClose);
        TokenSpan after = TokenSubspan(tokens, *bodyClose + 1, tokens.size());
        std::vector<std::string> lines = FormatLambdaHeaderWithLeadingTokens(header, indentLevel, std::move(prefix));
        PrettyFormatter bodyFormatter(config_, indentLevel + 1, true, sourceLayout_);
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

    LayoutResult FormatSplitLambdaResult(
        TokenSpan tokens,
        size_t bodyOpen,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        return ScoreLayoutCandidate(
            FormatSplitLambda(tokens, bodyOpen, indentLevel, std::move(prefix), std::move(suffix)),
            false,
            0,
            0
        );
    }

    std::vector<std::string> FormatLambdaHeaderWithLeadingTokens(
        TokenSpan header,
        int indentLevel,
        std::string prefix
    ) const {
        const std::optional<size_t> captureOpen = FindLambdaCaptureOpenInHeader(header);
        if (!captureOpen || *captureOpen == 0) {
            return FormatLambdaHeader(header, indentLevel, std::move(prefix));
        }
        TokenSpan leading = TokenSubspan(header, 0, *captureOpen);
        TokenSpan lambdaHeader = TokenSubspan(header, *captureOpen, header.size());
        prefix += FormatInline(leading);
        if (!prefix.empty() && prefix.back() != ' ' && prefix.back() != '(' && prefix.back() != '[') {
            prefix.push_back(' ');
        }
        return FormatLambdaHeader(lambdaHeader, indentLevel, std::move(prefix));
    }

    std::optional<size_t> FindLambdaCaptureOpenInHeader(TokenSpan header) const {
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

    std::vector<std::string> FormatLambdaHeader(TokenSpan header, int indentLevel, std::string prefix) const {
        LayoutResult best;
        std::vector<std::string> bestLines;
        bool hasBest = false;
        size_t order = 0;
        const auto consider = [&](std::vector<std::string> lines, int breakDepth) {
            LayoutResult candidate = ScoreLayoutCandidate(lines, false, breakDepth, order++);
            if (!hasBest || IsBetterLayout(candidate, best)) {
                best = std::move(candidate);
                bestLines = std::move(lines);
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
        return bestLines;
    }

    std::optional<std::vector<std::string>> FormatDetachedLambdaParameterLine(
        TokenSpan header,
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
        TokenSpan captures = TokenSubspan(header, 0, *captureClose + 1);
        const std::string captureLine = std::string(prefix) + FormatInline(captures);
        const std::string restLine = FormatLambdaHeaderAfterCaptures(header, captures);
        return std::vector<std::string>{
            Indent(indentLevel) + captureLine,
            Indent(indentLevel + 1) + restLine,
            Indent(indentLevel) + "{"
        };
    }

    std::string FormatLambdaHeaderAfterCaptures(TokenSpan header, TokenSpan captures) const {
        const std::string fullHeader = FormatInline(header);
        const std::string captureText = FormatInline(captures);
        if (fullHeader.size() >= captureText.size() && fullHeader.compare(0, captureText.size(), captureText) == 0) {
            return fullHeader.substr(captureText.size());
        }
        TokenSpan rest = TokenSubspan(header, captures.size(), header.size());
        return FormatInline(rest);
    }

    std::optional<std::vector<std::string>> FormatLambdaParameterGroup(
        TokenSpan header,
        int indentLevel,
        std::string_view prefix
    ) const {
        const std::optional<GroupPair> group = FindLambdaParameterGroup(header);
        if (!group) {
            return std::nullopt;
        }
        TokenSpan inner = TokenSubspan(header, group->open + 1, group->close);
        if (!ContainsTopLevelSeparator(inner, ',')) {
            return std::nullopt;
        }
        TokenSpan firstLineTokens = TokenSubspan(header, 0, group->open + 1);
        const std::string firstLine = std::string(prefix) + FormatInline(firstLineTokens);
        std::vector<std::string> lines;
        lines.push_back(Indent(indentLevel) + firstLine);
        AppendLambdaParameterLines(lines, inner, *group, header, indentLevel);
        AppendLambdaBodyOpen(lines);
        return lines;
    }

    std::optional<std::vector<std::string>> FormatLambdaCaptureGroup(
        TokenSpan header,
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
        TokenSpan captureInner = TokenSubspan(header, 1, *captureClose);
        if (!ContainsTopLevelSeparator(captureInner, ',')) {
            return std::nullopt;
        }
        std::vector<std::string> lines;
        lines.push_back(Indent(indentLevel) + std::string(prefix) + "[");
        TokenSplitParts captures = SplitTopLevelParts(captureInner, ',');
        for (size_t index = 0; index < captures.size(); ++index) {
            std::string elementSuffix;
            if (index + 1 < captures.size()) {
                elementSuffix = ",";
            }
            std::vector<std::string> captureLines =
                FormatRange(captures.At(index), indentLevel + 1, {}, std::move(elementSuffix), true);
            lines.insert(lines.end(), captureLines.begin(), captureLines.end());
        }
        const std::optional<GroupPair> parameterGroup = FindLambdaParameterGroup(header);
        if (parameterGroup) {
            TokenSpan rest = TokenSubspan(header, *captureClose, header.size());
            const std::string restLine = FormatInline(rest);
            if (Fits(indentLevel, restLine)) {
                lines.push_back(Indent(indentLevel) + restLine + " {");
                return lines;
            }
            TokenSpan inner = TokenSubspan(header, parameterGroup->open + 1, parameterGroup->close);
            if (ContainsTopLevelSeparator(inner, ',')) {
                TokenSpan firstLineTokens = TokenSubspan(header, *captureClose, parameterGroup->open + 1);
                lines.push_back(Indent(indentLevel) + FormatInline(firstLineTokens));
                AppendLambdaParameterLines(lines, inner, *parameterGroup, header, indentLevel);
                AppendLambdaBodyOpen(lines);
                return lines;
            }
        }
        TokenSpan rest = TokenSubspan(header, *captureClose, header.size());
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
        TokenSpan inner,
        GroupPair group,
        TokenSpan header,
        int indentLevel
    ) const {
        TokenSplitParts elements = SplitTopLevelParts(inner, ',');
        for (size_t index = 0; index < elements.size(); ++index) {
            if (elements.At(index).empty()) {
                continue;
            }
            std::string elementSuffix;
            if (index + 1 < elements.size()) {
                elementSuffix = ",";
            }
            std::vector<std::string> elementLines =
                FormatRange(elements.At(index), indentLevel + 1, {}, elementSuffix, true);
            AppendSplitElementLines(lines, elementLines, false);
        }
        TokenSpan afterTokens = TokenSubspan(header, group.close + 1, header.size());
        std::string closeLine = ")";
        std::string afterText = FormatInline(afterTokens);
        if (!afterText.empty()) {
            closeLine += " ";
            closeLine += afterText;
        }
        lines.push_back(Indent(indentLevel) + closeLine);
    }

    std::optional<GroupPair> FindLambdaParameterGroup(TokenSpan header) const {
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
        TokenSpan tokens,
        size_t initializerColon,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        TokenSpan header = TokenSubspan(tokens, 0, initializerColon);
        TokenSpan initializers = TokenSubspan(tokens, initializerColon + 1, tokens.size());
        std::vector<std::string> lines = FormatConstructorInitializerHeader(header, indentLevel, std::move(prefix));
        TokenSplitParts elements = SplitTopLevelParts(initializers, ',');
        const bool separateBodyOpen = suffix == " {";
        for (size_t index = 0; index < elements.size(); ++index) {
            if (elements.At(index).empty()) {
                continue;
            }
            std::string elementSuffix;
            if (index + 1 < elements.size()) {
                elementSuffix = ",";
            } else if (!separateBodyOpen) {
                elementSuffix = suffix;
            }
            std::vector<std::string> elementLines =
                FormatRange(elements.At(index), indentLevel + 1, {}, elementSuffix, true);
            lines.insert(lines.end(), elementLines.begin(), elementLines.end());
        }
        if (separateBodyOpen) {
            lines.push_back(Indent(indentLevel) + "{");
        }
        return lines;
    }

    LayoutResult FormatConstructorInitializerListResult(
        TokenSpan tokens,
        size_t initializerColon,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        TokenSpan header = TokenSubspan(tokens, 0, initializerColon);
        TokenSpan initializers = TokenSubspan(tokens, initializerColon + 1, tokens.size());
        LayoutResult result = FormatConstructorInitializerHeaderResult(header, indentLevel, std::move(prefix));
        TokenSplitParts elements = SplitTopLevelParts(initializers, ',');
        const bool separateBodyOpen = suffix == " {";
        for (size_t index = 0; index < elements.size(); ++index) {
            if (elements.At(index).empty()) {
                continue;
            }
            std::string elementSuffix;
            if (index + 1 < elements.size()) {
                elementSuffix = ",";
            } else if (!separateBodyOpen) {
                elementSuffix = suffix;
            }
            LayoutResult elementResult =
                FormatRangeResult(elements.At(index), indentLevel + 1, {}, elementSuffix, true);
            AppendMeasuredLayout(result, elementResult, false, 1);
        }
        if (separateBodyOpen) {
            AppendMeasuredLine(result, IndentWidth(indentLevel) + 1, indentLevel, LineTrimKind::OpenBrace);
        }
        result.choice = LayoutChoiceKind::ConstructorInitializer;
        return result;
    }

    std::vector<std::string> FormatConstructorInitializerHeader(
        TokenSpan header,
        int indentLevel,
        std::string prefix
    ) const {
        if (std::optional<GroupPair> group = FindFirstWrappableGroupPair(header)) {
            TokenSpan inner = TokenSubspan(header, group->open + 1, group->close);
            if (header[group->open].text == "(" && ContainsTopLevelSeparator(inner, ',')) {
                return FormatSplitGroup(header, *group, indentLevel, std::move(prefix), " :");
            }
        }
        return FormatRange(header, indentLevel, std::move(prefix), " :");
    }

    LayoutResult FormatConstructorInitializerHeaderResult(TokenSpan header, int indentLevel, std::string prefix) const {
        if (std::optional<GroupPair> group = FindFirstWrappableGroupPair(header)) {
            TokenSpan inner = TokenSubspan(header, group->open + 1, group->close);
            if (header[group->open].text == "(" && ContainsTopLevelSeparator(inner, ',')) {
                return FormatSplitGroupResult(header, *group, indentLevel, std::move(prefix), " :");
            }
        }
        return FormatRangeResult(header, indentLevel, std::move(prefix), " :");
    }

    std::vector<std::string> FormatSplitGroup(
        TokenSpan tokens,
        GroupPair group,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        const LayoutResult result = FormatSplitGroupResult(tokens, group, indentLevel, prefix, suffix);
        std::vector<std::string> lines;
        if (result.variant == 1) {
            RenderStackedNestedGroup(lines, tokens, group, indentLevel, prefix, suffix);
        } else {
            RenderSplitGroup(lines, tokens, group, indentLevel, std::move(prefix), std::move(suffix));
        }
        return lines;
    }

    LayoutResult FormatSplitGroupResult(
        TokenSpan tokens,
        GroupPair group,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        std::optional<LayoutResult> stacked =
            FormatStackedNestedGroupResult(tokens, group, indentLevel, prefix, suffix);
        TokenSpan firstLineTokens = TokenSubspan(tokens, 0, group.open + 1);
        TokenSpan inner = TokenSubspan(tokens, group.open + 1, group.close);
        TokenSpan suffixTokens = TokenSubspan(tokens, group.close, tokens.size());
        LayoutResult result = EmptyLayout(false, 0, 0, LayoutChoiceKind::Group, {}, 0);
        AppendMeasuredLine(
            result,
            IndentWidth(indentLevel) + prefix.size() + FormatGroupOpeningLineLength(tokens, group),
            indentLevel,
            GroupOpeningTrimKind(tokens, group, prefix)
        );
        const bool startsWithControlFor = StartsWithControlFor(firstLineTokens);
        const bool splitForHeader =
            startsWithControlFor || (StartsWithControlHeader(firstLineTokens) && ContainsTopLevelSeparator(inner, ';'));
        const bool indentElementChains = startsWithControlFor || !StartsWithControlHeader(firstLineTokens);
        const char separator = splitForHeader ? ';' : ',';
        TokenSplitParts elements = SplitTopLevelParts(inner, separator);
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(inner, separator)) {
            const bool indentSingleExpressionChains =
                !IsPlainParenthesizedExpressionGroup(tokens, group, firstLineTokens);
            LayoutResult childResult = FormatRangeResult(inner, indentLevel + 1, {}, {}, indentSingleExpressionChains);
            AppendMeasuredLayout(result, childResult, false, 1);
        } else {
            bool emittedElement = false;
            for (size_t index = 0; index < elements.size(); ++index) {
                std::string elementSuffix;
                if (index + 1 < elements.size()) {
                    elementSuffix = std::string(1, separator);
                }
                const bool isInitializerElement = tokens[group.open].text == "{" && separator == ',';
                LayoutResult elementResult = FormatDelimitedElementResult(
                    elements.At(index),
                    indentLevel + 1,
                    elementSuffix,
                    isInitializerElement,
                    indentElementChains,
                    startsWithControlFor,
                    emittedElement
                );
                if (elementResult.lineCount == 0) {
                    continue;
                }
                AppendMeasuredLayout(result, elementResult, isInitializerElement, 1);
                emittedElement = true;
            }
        }
        AppendMeasuredLine(
            result,
            IndentWidth(indentLevel) + FormatGroupClosingLineLength(tokens, group, suffixTokens) + suffix.size(),
            indentLevel,
            GroupClosingTrimKind(suffixTokens, suffix)
        );
        if (stacked) {
            stacked->order = 1;
            if (IsBetterLayout(*stacked, result)) {
                return *stacked;
            }
        }
        return result;
    }

    std::optional<std::vector<std::string>> FormatStackedNestedGroup(
        TokenSpan tokens,
        GroupPair group,
        int indentLevel,
        std::string_view prefix,
        std::string_view suffix
    ) const {
        std::optional<LayoutResult> result = FormatStackedNestedGroupResult(tokens, group, indentLevel, prefix, suffix);
        if (!result) {
            return std::nullopt;
        }
        std::vector<std::string> lines;
        RenderStackedNestedGroup(lines, tokens, group, indentLevel, prefix, suffix);
        return lines;
    }

    std::optional<LayoutResult> FormatStackedNestedGroupResult(
        TokenSpan tokens,
        GroupPair group,
        int indentLevel,
        std::string_view prefix,
        std::string_view suffix
    ) const {
        const std::optional<GroupPair> nestedGroup = FindStackableNestedGroup(tokens, group);
        if (!nestedGroup) {
            return std::nullopt;
        }
        TokenSpan firstLineTokens = TokenSubspan(tokens, 0, nestedGroup->open + 1);
        const size_t firstLineWidth = prefix.size() + FormatInlineLength(firstLineTokens);
        if (!FitsWidth(indentLevel, firstLineWidth)) {
            return std::nullopt;
        }

        TokenSpan nestedInner = TokenSubspan(tokens, nestedGroup->open + 1, nestedGroup->close);
        const int nestedBreakDepth = NestedGroupBreakDepth(tokens, group, *nestedGroup);
        LayoutResult result = EmptyLayout(false, nestedBreakDepth, 0, LayoutChoiceKind::Group, {}, 1);
        AppendMeasuredLine(result, IndentWidth(indentLevel) + firstLineWidth, indentLevel, LineTrimKind::Other);
        TokenSplitParts elements = SplitTopLevelParts(nestedInner, ',');
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(nestedInner, ',')) {
            LayoutResult childResult = FormatRangeResult(nestedInner, indentLevel + 1, {}, {}, true);
            AppendMeasuredLayout(result, childResult, false, nestedBreakDepth + 1);
        } else {
            bool emittedElement = false;
            for (size_t index = 0; index < elements.size(); ++index) {
                std::string elementSuffix;
                if (index + 1 < elements.size()) {
                    elementSuffix = ",";
                }
                LayoutResult elementResult = FormatDelimitedElementResult(
                    elements.At(index),
                    indentLevel + 1,
                    elementSuffix,
                    true,
                    true,
                    false,
                    emittedElement
                );
                if (elementResult.lineCount == 0) {
                    continue;
                }
                AppendMeasuredLayout(
                    result,
                    elementResult,
                    tokens[nestedGroup->open].text == "{",
                    nestedBreakDepth + 1
                );
                emittedElement = true;
            }
        }

        TokenSpan closingTokens = TokenSubspan(tokens, nestedGroup->close, tokens.size());
        AppendMeasuredLine(
            result,
            IndentWidth(indentLevel) + FormatInlineLength(closingTokens) + suffix.size(),
            indentLevel,
            LineTrimKind::Other
        );
        return result;
    }

    void RenderSplitGroup(
        std::vector<std::string>& lines,
        TokenSpan tokens,
        GroupPair group,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        TokenSpan firstLineTokens = TokenSubspan(tokens, 0, group.open + 1);
        TokenSpan inner = TokenSubspan(tokens, group.open + 1, group.close);
        TokenSpan suffixTokens = TokenSubspan(tokens, group.close, tokens.size());
        lines.push_back(Indent(indentLevel) + std::move(prefix) + FormatGroupOpeningLine(tokens, group));
        const bool startsWithControlFor = StartsWithControlFor(firstLineTokens);
        const bool splitForHeader =
            startsWithControlFor || (StartsWithControlHeader(firstLineTokens) && ContainsTopLevelSeparator(inner, ';'));
        const bool indentElementChains = startsWithControlFor || !StartsWithControlHeader(firstLineTokens);
        const char separator = splitForHeader ? ';' : ',';
        TokenSplitParts elements = SplitTopLevelParts(inner, separator);
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(inner, separator)) {
            const bool indentSingleExpressionChains =
                !IsPlainParenthesizedExpressionGroup(tokens, group, firstLineTokens);
            RenderRange(lines, inner, indentLevel + 1, {}, {}, indentSingleExpressionChains);
        } else {
            bool emittedElement = false;
            for (size_t index = 0; index < elements.size(); ++index) {
                std::string elementSuffix;
                if (index + 1 < elements.size()) {
                    elementSuffix = std::string(1, separator);
                }
                const bool isInitializerElement = tokens[group.open].text == "{" && separator == ',';
                std::vector<std::string> elementLines = FormatDelimitedElement(
                    elements.At(index),
                    indentLevel + 1,
                    elementSuffix,
                    isInitializerElement,
                    indentElementChains,
                    startsWithControlFor,
                    emittedElement
                );
                AppendSplitElementLines(lines, elementLines, isInitializerElement);
                emittedElement = emittedElement || !elementLines.empty();
            }
        }
        std::string closeLine = FormatGroupClosingLine(tokens, group, suffixTokens);
        AppendSuffix(closeLine, suffix);
        lines.push_back(Indent(indentLevel) + closeLine);
    }

    void RenderStackedNestedGroup(
        std::vector<std::string>& lines,
        TokenSpan tokens,
        GroupPair group,
        int indentLevel,
        std::string_view prefix,
        std::string_view suffix
    ) const {
        const std::optional<GroupPair> nestedGroup = FindStackableNestedGroup(tokens, group);
        if (!nestedGroup) {
            return;
        }
        TokenSpan firstLineTokens = TokenSubspan(tokens, 0, nestedGroup->open + 1);
        lines.push_back(Indent(indentLevel) + std::string(prefix) + FormatInline(firstLineTokens));

        TokenSpan nestedInner = TokenSubspan(tokens, nestedGroup->open + 1, nestedGroup->close);
        TokenSplitParts elements = SplitTopLevelParts(nestedInner, ',');
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(nestedInner, ',')) {
            RenderRange(lines, nestedInner, indentLevel + 1, {}, {}, true);
        } else {
            bool emittedElement = false;
            for (size_t index = 0; index < elements.size(); ++index) {
                std::string elementSuffix;
                if (index + 1 < elements.size()) {
                    elementSuffix = ",";
                }
                std::vector<std::string> elementLines = FormatDelimitedElement(
                    elements.At(index),
                    indentLevel + 1,
                    elementSuffix,
                    true,
                    true,
                    false,
                    emittedElement
                );
                AppendSplitElementLines(lines, elementLines, tokens[nestedGroup->open].text == "{");
                emittedElement = emittedElement || !elementLines.empty();
            }
        }

        TokenSpan closingTokens = TokenSubspan(tokens, nestedGroup->close, tokens.size());
        std::string closeLine = FormatInline(closingTokens);
        AppendSuffix(closeLine, suffix);
        lines.push_back(Indent(indentLevel) + closeLine);
    }

    int NestedGroupBreakDepth(TokenSpan tokens, GroupPair outer, GroupPair nested) const {
        int depth = 0;
        for (size_t index = outer.open + 1; index < nested.open && index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
        }
        return depth;
    }

    std::optional<GroupPair> FindStackableNestedGroup(TokenSpan tokens, GroupPair group) const {
        if (group.open >= tokens.size() || group.close > tokens.size()) {
            return std::nullopt;
        }
        TokenSpan inner = TokenSubspan(tokens, group.open + 1, group.close);
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

    std::string FormatGroupClosingLine(TokenSpan tokens, GroupPair group, TokenSpan suffixTokens) const {
        if (!IsTemplateAngleGroup(tokens, group) || suffixTokens.empty()) {
            return FormatInline(suffixTokens);
        }
        std::string closeLine = suffixTokens.front().text;
        TokenSpan afterClose = TokenSubspan(suffixTokens, 1, suffixTokens.size());
        std::string afterText = FormatInline(afterClose);
        if (!afterText.empty()) {
            if (NeedsSpaceAfterSplitTemplateClose(afterText)) {
                closeLine.push_back(' ');
            }
            closeLine += afterText;
        }
        return closeLine;
    }

    size_t FormatGroupClosingLineLength(TokenSpan tokens, GroupPair group, TokenSpan suffixTokens) const {
        if (!IsTemplateAngleGroup(tokens, group) || suffixTokens.empty()) {
            return FormatInlineLength(suffixTokens);
        }
        size_t length = suffixTokens.front().text.size();
        TokenSpan afterClose = TokenSubspan(suffixTokens, 1, suffixTokens.size());
        const size_t afterLength = FormatInlineLength(afterClose);
        if (afterLength > 0) {
            std::string afterText = FormatInline(afterClose);
            if (NeedsSpaceAfterSplitTemplateClose(afterText)) {
                ++length;
            }
            length += afterLength;
        }
        return length;
    }

    LineTrimKind GroupClosingTrimKind(TokenSpan suffixTokens, std::string_view suffix) const {
        if (suffixTokens.size() == 1 && suffixTokens.front().text == "}" && suffix == ",") {
            return LineTrimKind::CloseBraceComma;
        }
        return LineTrimKind::Other;
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

    bool IsPlainParenthesizedExpressionGroup(TokenSpan tokens, GroupPair group, TokenSpan firstLineTokens) const {
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

    std::optional<GroupPair> FindTopLevelInitializerListDeclarationGroup(TokenSpan tokens) const {
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
                TokenSpan inner = TokenSubspan(tokens, index + 1, *close);
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

    std::optional<size_t> InitializerListDeclarationDeclarator(TokenSpan tokens, GroupPair initializerList) const {
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

    bool IsLikelyDeclarationTypeBeforeName(TokenSpan tokens, size_t nameIndex) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, nameIndex);
        if (!previous) {
            return false;
        }
        if (IsPointerOrReferenceDeclaratorToken(tokens[*previous].text)) {
            return IsPointerOrReferenceDeclarator(tokens, *previous) && IsLikelyTypeBeforePointer(tokens, *previous);
        }
        return IsLikelyTypeNameToken(tokens, *previous);
    }

    bool IsStringLiteralSequence(TokenSpan tokens) const {
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
        TokenSpan tokens,
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

    LayoutResult FormatStringLiteralSequenceResult(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
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
        LayoutResult result = EmptyLayout(false, 0, 0, LayoutChoiceKind::StringLiteralSequence, {}, 0);
        bool first = true;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (token.kind != TokenKind::StringLiteral) {
                continue;
            }
            size_t lineWidth = token.text.size();
            if (first) {
                lineWidth += prefix.size();
            }
            if (lastStringLiteral && index == *lastStringLiteral) {
                lineWidth += suffix.size();
            }
            const int lineIndent = first ? indentLevel : indentLevel + 1;
            AppendMeasuredLine(result, IndentWidth(lineIndent) + lineWidth, lineIndent, LineTrimKind::Other);
            first = false;
        }
        return result;
    }

    std::vector<std::string> FormatOperatorChain(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains,
        bool indentLogicalSplitChains
    ) const {
        (
            void
        ) FormatOperatorChainResult(tokens, indentLevel, prefix, suffix, indentSplitChains, indentLogicalSplitChains);
        std::vector<std::string> lines;
        RenderOperatorChain(
            lines,
            tokens,
            indentLevel,
            std::move(prefix),
            std::move(suffix),
            indentSplitChains,
            indentLogicalSplitChains
        );
        return lines;
    }

    std::vector<TokenSpan> SplitOperatorChainParts(TokenSpan tokens, ChainKind chainKind) const {
        std::vector<TokenSpan> parts;
        size_t partBegin = 0;
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth == 0 && IsChainBreakOperator(tokens, index, chainKind)) {
                size_t partEnd = index + 1;
                if (index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LineComment) {
                    partEnd = index + 2;
                    ++index;
                }
                parts.push_back(TokenSubspan(tokens, partBegin, partEnd));
                partBegin = partEnd;
            }
        }
        if (partBegin < tokens.size()) {
            parts.push_back(TokenSubspan(tokens, partBegin, tokens.size()));
        }
        return parts;
    }

    std::vector<TokenSpan> SplitTernaryChainParts(TokenSpan tokens) const {
        std::vector<TokenSpan> parts;
        size_t partBegin = 0;
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth == 0 && tokens[index].text == ":") {
                size_t partEnd = index + 1;
                if (index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LineComment) {
                    partEnd = index + 2;
                    ++index;
                }
                parts.push_back(TokenSubspan(tokens, partBegin, partEnd));
                partBegin = partEnd;
            }
        }
        if (partBegin < tokens.size()) {
            parts.push_back(TokenSubspan(tokens, partBegin, tokens.size()));
        }
        return parts;
    }

    ShiftChainParts SplitShiftOperatorChain(TokenSpan tokens) const {
        ShiftChainParts result;
        size_t segmentBegin = 0;
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth == 0 && IsChainBreakOperator(tokens, index, ChainKind::Shift)) {
                if (!result.sawShiftOperator) {
                    result.receiver = TokenSubspan(tokens, segmentBegin, index);
                    result.sawShiftOperator = true;
                } else {
                    result.segments.push_back(TokenSubspan(tokens, segmentBegin, index));
                }
                segmentBegin = index;
            }
        }
        if (result.sawShiftOperator && segmentBegin < tokens.size()) {
            result.segments.push_back(TokenSubspan(tokens, segmentBegin, tokens.size()));
        }
        return result;
    }

    static TokenSpan MergeTokenSpans(TokenSpan first, TokenSpan second) {
        if (first.empty()) {
            return second;
        }
        if (second.empty()) {
            return first;
        }
        return TokenSpan(first.data(), static_cast<size_t>((second.data() + second.size()) - first.data()));
    }

    static TokenSpan CombinedTokenSpan(const std::vector<TokenSpan>& spans) {
        if (spans.empty()) {
            return {};
        }
        return MergeTokenSpans(spans.front(), spans.back());
    }

    LayoutResult FormatOperatorChainResult(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains,
        bool indentLogicalSplitChains
    ) const {
        const ChainKind chainKind = SelectChainKind(tokens);
        if (chainKind == ChainKind::Ternary) {
            LayoutResult result =
                FormatTernaryChainResult(tokens, indentLevel, std::move(prefix), std::move(suffix), indentSplitChains);
            result.choice = LayoutChoiceKind::OperatorChain;
            return result;
        }
        if (chainKind == ChainKind::Shift) {
            LayoutResult result =
                FormatShiftOperatorChainResult(tokens, indentLevel, std::move(prefix), std::move(suffix));
            result.choice = LayoutChoiceKind::OperatorChain;
            return result;
        }
        const std::vector<TokenSpan> parts = SplitOperatorChainParts(tokens, chainKind);
        LayoutResult result = EmptyLayout(false, 0, 0, LayoutChoiceKind::OperatorChain, {}, 0);
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
            AppendMeasuredLayout(result, partResult, false, 1);
        }
        return result;
    }

    void RenderOperatorChain(
        std::vector<std::string>& lines,
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains,
        bool indentLogicalSplitChains
    ) const {
        const ChainKind chainKind = SelectChainKind(tokens);
        if (chainKind == ChainKind::Ternary) {
            AppendRenderedLines(
                lines,
                FormatTernaryChain(tokens, indentLevel, std::move(prefix), std::move(suffix), indentSplitChains)
            );
            return;
        }
        if (chainKind == ChainKind::Shift) {
            AppendRenderedLines(
                lines,
                FormatShiftOperatorChain(tokens, indentLevel, std::move(prefix), std::move(suffix))
            );
            return;
        }
        const std::vector<TokenSpan> parts = SplitOperatorChainParts(tokens, chainKind);
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
            AppendRenderedLines(
                lines,
                FormatChainPart(parts[index], partIndent, std::move(partPrefix), std::move(partSuffix))
            );
        }
    }

    std::vector<std::string> FormatShiftOperatorChain(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        ShiftChainParts chain = SplitShiftOperatorChain(tokens);
        if (!chain.sawShiftOperator) {
            return FormatChainPart(tokens, indentLevel, std::move(prefix), std::move(suffix));
        }
        std::vector<std::string> lines = FormatRange(chain.receiver, indentLevel, std::move(prefix), {}, true);
        if (std::optional<std::string> compactTail = CompactShiftTail(chain.segments, indentLevel + 1, suffix)) {
            lines.push_back(Indent(indentLevel + 1) + *compactTail);
            return lines;
        }
        std::vector<TokenSpan> segments = GroupShiftSegmentsByBreakRules(chain.segments);
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
        const std::vector<TokenSpan>& segments,
        int indentLevel,
        std::string_view suffix
    ) const {
        TokenSpan combined = CombinedTokenSpan(segments);
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

    LayoutResult FormatShiftOperatorChainResult(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        ShiftChainParts chain = SplitShiftOperatorChain(tokens);
        if (!chain.sawShiftOperator) {
            return FormatChainPartResult(tokens, indentLevel, std::move(prefix), std::move(suffix));
        }
        LayoutResult result = EmptyLayout(false, 0, 0, LayoutChoiceKind::OperatorChain, {}, 0);
        LayoutResult receiverResult = FormatRangeResult(chain.receiver, indentLevel, std::move(prefix), {}, true);
        AppendMeasuredLayout(result, receiverResult, false, 1);
        if (std::optional<size_t> compactTailWidth = CompactShiftTailWidth(chain.segments, indentLevel + 1, suffix)) {
            AppendMeasuredLine(
                result,
                IndentWidth(indentLevel + 1) + *compactTailWidth,
                indentLevel + 1,
                LineTrimKind::Other
            );
            result.variant = 1;
            return result;
        }
        std::vector<TokenSpan> segments = GroupShiftSegmentsByBreakRules(chain.segments);
        for (size_t index = 0; index < segments.size();) {
            std::string segmentSuffix;
            if (index + 1 == segments.size()) {
                segmentSuffix = suffix;
            }
            LayoutResult segmentResult =
                FormatChainPartResult(segments[index], indentLevel + 1, {}, std::move(segmentSuffix));
            AppendMeasuredLayout(result, segmentResult, false, 1);
            ++index;
        }
        result.variant = 2;
        return result;
    }

    std::optional<size_t> CompactShiftTailWidth(
        const std::vector<TokenSpan>& segments,
        int indentLevel,
        std::string_view suffix
    ) const {
        TokenSpan combined = CombinedTokenSpan(segments);
        if (combined.empty() || HasOriginalBlankSeparator(combined) || ShouldForceSplit(combined)) {
            return std::nullopt;
        }
        const size_t width = FormatInlineLength(combined) + suffix.size();
        if (!FitsWidth(indentLevel, width)) {
            return std::nullopt;
        }
        return width;
    }

    std::vector<TokenSpan> GroupShiftSegmentsByBreakRules(const std::vector<TokenSpan>& segments) const {
        std::vector<TokenSpan> grouped;
        TokenSpan pendingConfiguration;
        for (TokenSpan segment : segments) {
            if (IsStreamShiftConfigurationSegment(segment)) {
                pendingConfiguration = MergeTokenSpans(pendingConfiguration, segment);
                continue;
            }
            if (!pendingConfiguration.empty()) {
                grouped.push_back(MergeTokenSpans(pendingConfiguration, segment));
                pendingConfiguration = {};
                continue;
            }
            grouped.push_back(segment);
        }
        if (!pendingConfiguration.empty()) {
            grouped.push_back(std::move(pendingConfiguration));
        }
        return grouped;
    }

    bool IsStreamShiftConfigurationSegment(TokenSpan segment) const {
        std::optional<std::string> methodName = StreamShiftConfigurationMethodName(segment);
        return methodName && IsConfiguredStreamShiftConfigurationMethod(*methodName);
    }

    std::optional<std::string> StreamShiftConfigurationMethodName(TokenSpan segment) const {
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
        TokenSpan tokens,
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

        const std::vector<TokenSpan> parts = SplitTernaryChainParts(tokens);
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

    LayoutResult FormatTernaryChainResult(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains
    ) const {
        if (std::optional<TernaryExpressionParts> single = SplitSingleTopLevelTernary(tokens)) {
            return FormatSingleTernaryExpressionResult(
                *single,
                indentLevel,
                std::move(prefix),
                std::move(suffix),
                indentSplitChains
            );
        }

        const std::vector<TokenSpan> parts = SplitTernaryChainParts(tokens);
        LayoutResult result = EmptyLayout(false, 0, 0, LayoutChoiceKind::OperatorChain, {}, 0);
        const bool indentContinuation =
            indentSplitChains || !prefix.empty() || (!tokens.empty() && tokens.front().text == "return");
        for (size_t index = 0; index < parts.size(); ++index) {
            std::string partPrefix = index == 0 ? prefix : std::string{};
            std::string partSuffix;
            if (index + 1 == parts.size()) {
                partSuffix = suffix;
            }
            const int partIndent = indentContinuation && index > 0 ? indentLevel + 1 : indentLevel;
            LayoutResult partResult =
                FormatTernaryChainPartResult(parts[index], partIndent, std::move(partPrefix), std::move(partSuffix));
            AppendMeasuredLayout(result, partResult, false, 1);
        }
        return result;
    }

    std::vector<std::string> FormatSingleTernaryExpression(
        const TernaryExpressionParts& parts,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains
    ) const {
        LayoutResult best;
        std::vector<std::string> bestLines;
        bool hasBest = false;
        size_t order = 0;
        const auto consider = [&](std::vector<std::string> lines, int breakDepth) {
            LayoutResult candidate = ScoreLayoutCandidate(lines, false, breakDepth, order++);
            if (!hasBest || IsBetterLayout(candidate, best)) {
                best = std::move(candidate);
                bestLines = std::move(lines);
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

        return bestLines;
    }

    LayoutResult FormatSingleTernaryExpressionResult(
        const TernaryExpressionParts& parts,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains
    ) const {
        LayoutResult best;
        bool hasBest = false;
        size_t order = 0;
        const auto consider = [&](LayoutResult candidate, int breakDepth) {
            candidate.compact = false;
            candidate.deepestBreakDepth = std::max(candidate.deepestBreakDepth, breakDepth);
            candidate.order = order++;
            candidate.choice = LayoutChoiceKind::OperatorChain;
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
        consider(FormatRangeResult(parts.trueBranch, indentLevel, conditionPrefix, trueSuffix, true), 1);

        std::string conditionSuffix = " ? " + trueText + " : " + falseText;
        consider(FormatRangeResult(parts.condition, indentLevel, prefix, conditionSuffix, true), 1);

        consider(
            FormatRangeResult(parts.falseBranch, indentLevel, conditionPrefix + trueText + " : ", suffix, true),
            1
        );

        LayoutResult questionBreak = EmptyLayout(false, 0, order, LayoutChoiceKind::OperatorChain, {}, 0);
        LayoutResult questionCondition =
            FormatRangeResult(parts.condition, indentLevel, prefix, " ?", indentSplitChains);
        LayoutResult questionTrue = FormatRangeResult(parts.trueBranch, indentLevel + 1, {}, trueSuffix, true);
        AppendMeasuredLayout(questionBreak, questionCondition, false, 1);
        AppendMeasuredLayout(questionBreak, questionTrue, false, 1);
        consider(std::move(questionBreak), 0);

        LayoutResult colonBreak = EmptyLayout(false, 0, order, LayoutChoiceKind::OperatorChain, {}, 0);
        LayoutResult colonTrue = FormatRangeResult(parts.trueBranch, indentLevel, conditionPrefix, " :", true);
        LayoutResult colonFalse = FormatRangeResult(parts.falseBranch, indentLevel + 1, {}, suffix, true);
        AppendMeasuredLayout(colonBreak, colonTrue, false, 1);
        AppendMeasuredLayout(colonBreak, colonFalse, false, 1);
        consider(std::move(colonBreak), 0);

        LayoutResult bothBreaks = EmptyLayout(false, 0, order, LayoutChoiceKind::OperatorChain, {}, 0);
        LayoutResult bothCondition = FormatRangeResult(parts.condition, indentLevel, prefix, " ?", indentSplitChains);
        LayoutResult bothTrue = FormatRangeResult(parts.trueBranch, indentLevel + 1, {}, " :", true);
        LayoutResult bothFalse = FormatRangeResult(parts.falseBranch, indentLevel + 1, {}, suffix, true);
        AppendMeasuredLayout(bothBreaks, bothCondition, false, 1);
        AppendMeasuredLayout(bothBreaks, bothTrue, false, 1);
        AppendMeasuredLayout(bothBreaks, bothFalse, false, 1);
        consider(std::move(bothBreaks), 0);

        return best;
    }

    std::vector<std::string> FormatTernaryChainPart(
        TokenSpan tokens,
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

    LayoutResult FormatTernaryChainPartResult(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        const size_t inlineWidth = prefix.size() + FormatInlineLength(tokens) + suffix.size();
        if (FitsWidth(indentLevel, inlineWidth)) {
            return SingleLineLayout(
                indentLevel,
                inlineWidth,
                true,
                0,
                0,
                LayoutChoiceKind::Compact,
                {},
                0,
                LineTrimKind::Other
            );
        }
        if (
            std::optional<LayoutResult> splitPart =
                FormatTernaryChainConditionPartResult(tokens, indentLevel, prefix, suffix)
        ) {
            return *splitPart;
        }
        return FormatChainPartResult(tokens, indentLevel, std::move(prefix), std::move(suffix));
    }

    std::optional<std::vector<std::string>> FormatTernaryChainConditionPart(
        TokenSpan tokens,
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
        TokenSpan condition = TokenSubspan(tokens, 0, *question);
        TokenSpan value = TokenSubspan(tokens, *question + 1, valueEnd);
        std::string valuePrefix = std::string(prefix) + FormatInline(condition) + " ? ";
        return FormatRange(value, indentLevel, std::move(valuePrefix), std::move(valueSuffix), true);
    }

    std::optional<LayoutResult> FormatTernaryChainConditionPartResult(
        TokenSpan tokens,
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
        TokenSpan condition = TokenSubspan(tokens, 0, *question);
        TokenSpan value = TokenSubspan(tokens, *question + 1, valueEnd);
        std::string valuePrefix = std::string(prefix) + FormatInline(condition) + " ? ";
        return FormatRangeResult(value, indentLevel, std::move(valuePrefix), std::move(valueSuffix), true);
    }

    std::optional<TernaryExpressionParts> SplitSingleTopLevelTernary(TokenSpan tokens) const {
        const std::optional<size_t> question = FindTopLevelToken(tokens, "?");
        if (!question) {
            return std::nullopt;
        }
        const std::optional<size_t> colon = FindMatchingTernaryColon(tokens, *question);
        if (!colon || *question == 0 || *question + 1 >= *colon || *colon + 1 >= tokens.size()) {
            return std::nullopt;
        }
        TokenSpan falseBranch = TokenSubspan(tokens, *colon + 1, tokens.size());
        if (FindTopLevelToken(falseBranch, "?")) {
            return std::nullopt;
        }
        return TernaryExpressionParts{
            TokenSubspan(tokens, 0, *question),
            TokenSubspan(tokens, *question + 1, *colon),
            falseBranch
        };
    }

    std::optional<size_t> FindMatchingTernaryColon(TokenSpan tokens, size_t question) const {
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
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        (void)FormatChainPartResult(tokens, indentLevel, prefix, suffix);
        std::vector<std::string> lines;
        RenderChainPart(lines, tokens, indentLevel, std::move(prefix), std::move(suffix));
        return lines;
    }

    LayoutResult FormatChainPartResult(
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        const size_t inlineWidth = prefix.size() + FormatInlineLength(tokens) + suffix.size();
        if (FitsWidth(indentLevel, inlineWidth)) {
            return SingleLineLayout(
                indentLevel,
                inlineWidth,
                true,
                0,
                0,
                LayoutChoiceKind::Compact,
                {},
                0,
                LineTrimKind::Other
            );
        }
        const ChainKind chainKind = SelectChainKind(tokens);
        const std::optional<size_t> trailingOperator = PreviousNonNewlineIndex(tokens, tokens.size());
        if (
            chainKind != ChainKind::None &&
            trailingOperator &&
            *trailingOperator + 1 == tokens.size() &&
            IsChainBreakOperator(tokens, *trailingOperator, chainKind)
        ) {
            TokenSpan value = TokenSubspan(tokens, 0, *trailingOperator);
            if (!value.empty()) {
                std::string operatorSuffix = " " + tokens[*trailingOperator].text;
                AppendSuffix(operatorSuffix, suffix);
                LayoutResult result =
                    FormatRangeResult(value, indentLevel, std::move(prefix), std::move(operatorSuffix), true);
                result.variant = 1;
                return result;
            }
        }
        LayoutResult result = FormatRangeResult(tokens, indentLevel, std::move(prefix), std::move(suffix), true);
        result.variant = 2;
        return result;
    }

    void RenderChainPart(
        std::vector<std::string>& lines,
        TokenSpan tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        std::string inlineText = prefix + FormatInline(tokens);
        AppendSuffix(inlineText, suffix);
        if (Fits(indentLevel, inlineText)) {
            lines.push_back(Indent(indentLevel) + inlineText);
            return;
        }
        const ChainKind chainKind = SelectChainKind(tokens);
        const std::optional<size_t> trailingOperator = PreviousNonNewlineIndex(tokens, tokens.size());
        if (
            chainKind != ChainKind::None &&
            trailingOperator &&
            *trailingOperator + 1 == tokens.size() &&
            IsChainBreakOperator(tokens, *trailingOperator, chainKind)
        ) {
            TokenSpan value = TokenSubspan(tokens, 0, *trailingOperator);
            if (!value.empty()) {
                std::string operatorSuffix = " " + tokens[*trailingOperator].text;
                AppendSuffix(operatorSuffix, suffix);
                RenderRange(lines, value, indentLevel, std::move(prefix), std::move(operatorSuffix), true);
                return;
            }
        }
        RenderRange(lines, tokens, indentLevel, std::move(prefix), std::move(suffix), true);
    }

    std::string JoinOutput() const {
        std::string result;
        for (const std::string& line : outputLines_) {
            result += line;
            result.push_back('\n');
        }
        return result;
    }

    size_t FormatInlineLength(TokenSpan tokens, std::optional<size_t> maxLength = std::nullopt) const {
        size_t length = 0;
        std::optional<Token> previous;
        std::optional<std::string> trailingStringLiteral;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (token.kind == TokenKind::Newline) {
                continue;
            }
            if (token.kind == TokenKind::LineComment) {
                if (length > 0) {
                    length += 2;
                }
                length += TrimmedLength(token.text);
                previous = token;
                trailingStringLiteral.reset();
                continue;
            }
            if (
                token.kind == TokenKind::StringLiteral &&
                previous &&
                previous->kind == TokenKind::StringLiteral &&
                trailingStringLiteral &&
                !HasUserDefinedLiteralSuffix(tokens, index)
            ) {
                if (std::optional<std::string> currentTail = ConcatenatedStringLiteralTail(
                    *trailingStringLiteral,
                    token.text
                )) {
                    const size_t candidateLength = length - 1 + currentTail->size();
                    if (!maxLength || candidateLength <= *maxLength) {
                        length = candidateLength;
                        trailingStringLiteral->pop_back();
                        trailingStringLiteral->append(*currentTail);
                        previous = Token{TokenKind::StringLiteral, *trailingStringLiteral};
                        continue;
                    }
                }
            }
            if (previous && NeedsSpaceBefore(tokens, index, *previous)) {
                ++length;
            }
            length += token.text.size();
            previous = token;
            if (token.kind == TokenKind::StringLiteral) {
                trailingStringLiteral = token.text;
            } else {
                trailingStringLiteral.reset();
            }
        }
        return length;
    }

    static size_t TrimmedLength(std::string_view value) {
        size_t begin = 0;
        while (begin < value.size() && IsSpaceButNotNewline(value[begin])) {
            ++begin;
        }
        size_t end = value.size();
        while (end > begin && IsSpaceButNotNewline(value[end - 1])) {
            --end;
        }
        return end - begin;
    }

    std::string FormatInline(TokenSpan tokens, std::optional<size_t> maxLength = std::nullopt) const {
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
        TokenSpan tokens,
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

    bool HasUserDefinedLiteralSuffix(TokenSpan tokens, size_t stringLiteralIndex) const {
        const size_t next = NextSignificantIndex(tokens, stringLiteralIndex + 1);
        return next < tokens.size() && tokens[next].kind == TokenKind::Word;
    }

    bool NeedsSpaceBefore(TokenSpan tokens, size_t index, const Token& previous) const {
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

    bool NeedsSpaceBeforeLeadingGlobalQualifier(TokenSpan tokens, size_t prevIndex) const {
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

    bool IsOperatorFunctionNameToken(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].kind != TokenKind::Symbol) {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        return previous && tokens[*previous].text == "operator";
    }

    bool IsDestructorNameToken(TokenSpan tokens, size_t index) const {
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

    std::optional<size_t> PreviousNonNewlineIndex(TokenSpan tokens, size_t index) const {
        while (index > 0) {
            --index;
            if (tokens[index].kind != TokenKind::Newline) {
                return index;
            }
        }
        return std::nullopt;
    }

    bool IsUnaryPrefixOperator(TokenSpan tokens, size_t index) const {
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

    bool IsCStyleCastCloseBeforeExpression(TokenSpan tokens, size_t close) const {
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

    bool IsCStyleCastContextBeforeOpen(TokenSpan tokens, size_t beforeOpen) const {
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

    bool IsLikelyCStyleCastType(TokenSpan tokens, size_t open, size_t close) const {
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

    bool IsLikelyTypeBeforePointer(TokenSpan tokens, size_t index) const {
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

    bool IsLikelyTypeNameToken(TokenSpan tokens, size_t index) const {
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

    bool IsDecltypeCloseBeforePointer(TokenSpan tokens, size_t closeIndex) const {
        const std::optional<size_t> open = FindMatchingOpen(tokens, closeIndex);
        if (!open || *open == 0) {
            return false;
        }
        const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
        return beforeOpen && tokens[*beforeOpen].text == "decltype";
    }

    bool IsLikelyTemplateTypeClose(TokenSpan tokens, size_t closeIndex) const {
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

    bool IsLikelyDeclaratorContextBeforePointer(TokenSpan tokens, size_t index) const {
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

    bool IsTypeQualifierStartInDeclaratorContext(TokenSpan tokens, size_t qualifier) const {
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

    bool HasWordBefore(TokenSpan tokens, size_t before, std::string_view word) const {
        for (size_t index = 0; index < before; ++index) {
            if (tokens[index].kind == TokenKind::Word && tokens[index].text == word) {
                return true;
            }
        }
        return false;
    }

    std::optional<size_t> TypeNameStartBeforePointer(TokenSpan tokens, size_t index) const {
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

    std::optional<size_t> UnwrapTemplateTypeNameStart(TokenSpan tokens, size_t index) const {
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

    bool FitsWidth(int indentLevel, size_t textWidth) const {
        return static_cast<int>(IndentWidth(indentLevel) + textWidth) <= config_.columnLimit;
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
        TokenSpan tokens,
        int indentLevel,
        std::string_view suffix,
        bool isInitializerElement,
        bool indentSplitChains,
        bool indentLogicalSplitChains,
        bool preserveLeadingBlankSeparator
    ) const {
        (void)FormatDelimitedElementResult(
            tokens,
            indentLevel,
            suffix,
            isInitializerElement,
            indentSplitChains,
            indentLogicalSplitChains,
            preserveLeadingBlankSeparator
        );
        std::vector<std::string> lines;
        RenderDelimitedElement(
            lines,
            tokens,
            indentLevel,
            suffix,
            isInitializerElement,
            indentSplitChains,
            indentLogicalSplitChains,
            preserveLeadingBlankSeparator
        );
        return lines;
    }

    LayoutResult FormatDelimitedElementResult(
        TokenSpan tokens,
        int indentLevel,
        std::string_view suffix,
        bool isInitializerElement,
        bool indentSplitChains,
        bool indentLogicalSplitChains,
        bool preserveLeadingBlankSeparator
    ) const {
        LayoutResult result = EmptyLayout(true, 0, 0, LayoutChoiceKind::None, {}, 0);
        size_t begin = 0;
        bool preserveBlankSeparator = preserveLeadingBlankSeparator;
        while (begin < tokens.size()) {
            size_t newlineCount = 0;
            while (begin < tokens.size() && tokens[begin].kind == TokenKind::Newline) {
                ++newlineCount;
                ++begin;
            }
            if (begin >= tokens.size()) {
                return result;
            }
            const bool hasBlankSeparator = newlineCount > 1;
            if (tokens[begin].kind == TokenKind::LineComment) {
                if (hasBlankSeparator && preserveBlankSeparator) {
                    AppendMeasuredLine(result, 0, 0, LineTrimKind::Blank);
                }
                AppendMeasuredLine(
                    result,
                    IndentWidth(indentLevel) + TrimmedLength(tokens[begin].text),
                    indentLevel,
                    LineTrimKind::Other
                );
                ++begin;
                preserveBlankSeparator = true;
                continue;
            }
            if (hasBlankSeparator && preserveBlankSeparator) {
                AppendMeasuredLine(result, 0, 0, LineTrimKind::Blank);
            }
            break;
        }
        TokenSpan remaining = TokenSubspan(tokens, begin, tokens.size());
        if (remaining.empty() || ContainsOnlyNewlines(remaining, 0, remaining.size())) {
            return result;
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
        result.compact = remainingResult.deepestBreakDepth < 0 && result.compact;
        AppendMeasuredLayout(result, remainingResult);
        return result;
    }

    void RenderDelimitedElement(
        std::vector<std::string>& lines,
        TokenSpan tokens,
        int indentLevel,
        std::string_view suffix,
        bool isInitializerElement,
        bool indentSplitChains,
        bool indentLogicalSplitChains,
        bool preserveLeadingBlankSeparator
    ) const {
        size_t begin = 0;
        bool preserveBlankSeparator = preserveLeadingBlankSeparator;
        while (begin < tokens.size()) {
            size_t newlineCount = 0;
            while (begin < tokens.size() && tokens[begin].kind == TokenKind::Newline) {
                ++newlineCount;
                ++begin;
            }
            if (begin >= tokens.size()) {
                return;
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
        TokenSpan remaining = TokenSubspan(tokens, begin, tokens.size());
        if (remaining.empty() || ContainsOnlyNewlines(remaining, 0, remaining.size())) {
            return;
        }
        if (isInitializerElement) {
            RenderRange(lines, remaining, indentLevel, {}, std::string(suffix), true);
        } else {
            RenderRange(
                lines,
                remaining,
                indentLevel,
                {},
                std::string(suffix),
                indentSplitChains,
                indentLogicalSplitChains
            );
        }
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

    bool ShouldForceSplit(TokenSpan tokens) const {
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

    bool HasLineTerminatedStringLiteralSequence(TokenSpan tokens) const {
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

    bool HasExpandableEscapeStringLiteralSequence(TokenSpan tokens) const {
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

    bool HasMultiStatementLambdaBody(TokenSpan tokens) const {
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

    size_t CountTopLevelLambdaBodyStatements(TokenSpan tokens, size_t begin, size_t end) const {
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

    bool IsTopLevelLambdaStatementBlockOpen(TokenSpan tokens, size_t open, size_t bodyBegin) const {
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

    bool HasOriginalBlankSeparator(TokenSpan tokens) const {
        for (size_t index = 1; index < tokens.size(); ++index) {
            if (tokens[index - 1].kind == TokenKind::Newline && tokens[index].kind == TokenKind::Newline) {
                return true;
            }
        }
        return false;
    }

    bool HasLineComment(TokenSpan tokens) const {
        return std::any_of(tokens.begin(), tokens.end(), [](const Token& token) {
            return token.kind == TokenKind::LineComment;
        });
    }

    bool LineCommentBeforeTopLevelStatementTerminator(TokenSpan tokens) const {
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

    std::optional<size_t> FindTopLevelAssignment(TokenSpan tokens) const {
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

    std::optional<GroupPair> FindFirstWrappableGroupPair(TokenSpan tokens) const {
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
        TokenSpan tokens,
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
                        TokenSpan inner = TokenSubspan(tokens, index + 1, *close);
                        if (ContainsTopLevelSeparator(inner, separator) && FindTopLevelLambdaBodyOpen(inner)) {
                            return GroupPair{index, *close};
                        }
                    }
                }
            }
        }
        return std::nullopt;
    }

    bool IsWrappableGroupOpen(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size()) {
            return false;
        }
        return IsGroupOpen(tokens[index].text) || IsWrappableTemplateAngleOpen(tokens, index);
    }

    bool IsWrappableTemplateAngleOpen(TokenSpan tokens, size_t index) const {
        if (!IsTemplateAngleOpen(tokens, index)) {
            return false;
        }
        const std::optional<size_t> close = FindTemplateAngleClose(tokens, index);
        if (!close || tokens[*close].text != ">") {
            return false;
        }
        TokenSpan inner = TokenSubspan(tokens, index + 1, *close);
        return ContainsTopLevelSeparator(inner, ',');
    }

    bool IsTemplateAngleGroup(TokenSpan tokens, GroupPair group) const {
        return group.open < tokens.size() &&
            group.close < tokens.size() &&
            tokens[group.open].text == "<" &&
            IsTemplateAngleOpen(tokens, group.open);
    }

    std::optional<size_t> FindWrappableGroupClose(TokenSpan tokens, size_t open) const {
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

    bool IsNonWrappablePrefixGroup(TokenSpan tokens, size_t open, size_t close) const {
        return IsParenthesizedCalleeGroup(tokens, open, close) || IsDeclspecGroup(tokens, open);
    }

    bool IsParenthesizedCalleeGroup(TokenSpan tokens, size_t open, size_t close) const {
        if (open >= tokens.size() || tokens[open].text != "(") {
            return false;
        }
        if (IsFunctionPointerDeclaratorGroupOpen(tokens, open)) {
            return false;
        }
        const size_t next = NextSignificantIndex(tokens, close + 1);
        return next < tokens.size() && tokens[next].text == "(";
    }

    bool IsDeclspecGroup(TokenSpan tokens, size_t open) const {
        if (open >= tokens.size() || tokens[open].text != "(") {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, open);
        return previous && tokens[*previous].text == "__declspec";
    }

    bool IsEmptyGroupPair(TokenSpan tokens, size_t open, size_t close) const {
        for (size_t index = open + 1; index < close; ++index) {
            if (tokens[index].kind != TokenKind::Newline) {
                return false;
            }
        }
        return true;
    }

    std::optional<size_t> FindMatchingClose(TokenSpan tokens, size_t openIndex) const {
        if (std::optional<size_t> annotated = AnnotatedMatchingIndex(tokens, openIndex)) {
            return annotated;
        }
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

    std::optional<size_t> FindMatchingOpen(TokenSpan tokens, size_t closeIndex) const {
        if (std::optional<size_t> annotated = AnnotatedMatchingIndex(tokens, closeIndex)) {
            return annotated;
        }
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

    std::vector<std::vector<Token>> SplitTopLevel(TokenSpan tokens, char separator) const {
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

    std::optional<std::vector<TokenSpan>> SplitTopLevelSpans(TokenSpan tokens, char separator) const {
        std::vector<TokenSpan> result;
        size_t partBegin = 0;
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (depth == 0 && token.text.size() == 1 && token.text[0] == separator) {
                if (index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LineComment) {
                    return std::nullopt;
                }
                result.push_back(TokenSubspan(tokens, partBegin, index));
                partBegin = index + 1;
                continue;
            }
            UpdateDepth(tokens, index, depth);
        }
        result.push_back(TokenSubspan(tokens, partBegin, tokens.size()));
        return result;
    }

    TokenSplitParts SplitTopLevelParts(TokenSpan tokens, char separator) const {
        TokenSplitParts result;
        if (std::optional<std::vector<TokenSpan>> spans = SplitTopLevelSpans(tokens, separator)) {
            result.spans = std::move(*spans);
            return result;
        }
        result.copies = SplitTopLevel(tokens, separator);
        result.usesCopies = true;
        return result;
    }

    bool ContainsTopLevelSeparator(TokenSpan tokens, char separator) const {
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

    bool HasTopLevelStatementTerminator(TokenSpan tokens) const {
        int depth = 0;
        for (const Token& token : tokens) {
            if (depth == 0 && token.text == ";") {
                return true;
            }
            UpdateDepth(token, depth);
        }
        return false;
    }

    bool CanSplitOperatorChain(TokenSpan tokens) const {
        return SelectChainKind(tokens) != ChainKind::None;
    }

    bool IsTrueOperatorChain(TokenSpan tokens, ChainKind chainKind) const {
        if (chainKind == ChainKind::None) {
            return false;
        }
        if (chainKind == ChainKind::Ternary) {
            return !SplitSingleTopLevelTernary(tokens).has_value();
        }
        return CountTopLevelChainBreakOperators(tokens, chainKind) > 1;
    }

    size_t CountTopLevelChainBreakOperators(TokenSpan tokens, ChainKind chainKind) const {
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

    ChainKind SelectChainKind(TokenSpan tokens) const {
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

    bool IsChainBreakOperator(TokenSpan tokens, size_t index, ChainKind chainKind) const {
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

    bool StartsWithInitializerList(TokenSpan tokens, size_t index) const {
        const size_t first = NextSignificantIndex(tokens, index);
        return first < tokens.size() && tokens[first].text == "{";
    }

    std::optional<size_t> FindConstructorInitializerColon(TokenSpan tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (depth == 0 && tokens[index].text == ":" && IsConstructorInitializerColon(tokens, index)) {
                return index;
            }
            UpdateDepth(tokens[index], depth);
        }
        return std::nullopt;
    }

    bool IsConstructorInitializerColon(TokenSpan tokens, size_t index) const {
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

    std::optional<size_t> FindConstructorParameterListCloseBeforeColon(TokenSpan tokens, size_t colon) const {
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

    bool HasTopLevelTokenBefore(TokenSpan tokens, size_t before, std::string_view text) const {
        int depth = 0;
        for (size_t index = 0; index < before; ++index) {
            if (depth == 0 && tokens[index].text == text) {
                return true;
            }
            UpdateDepth(tokens[index], depth);
        }
        return false;
    }

    std::optional<size_t> FindLambdaBodyOpen(TokenSpan tokens) const {
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

    std::optional<size_t> FindTopLevelLambdaBodyOpen(TokenSpan tokens) const {
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

    bool IsPointerOrReferenceDeclarator(TokenSpan tokens, size_t index) const {
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

    bool IsFunctionPointerDeclaratorGroupOpen(TokenSpan tokens, size_t index) const {
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

    bool IsFunctionPointerDeclaratorContextBeforeGroup(TokenSpan tokens, size_t index) const {
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

    bool FunctionPointerDeclaratorGroupStartsWithCallingConvention(TokenSpan tokens, size_t index) const {
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

    BlockKind ClassifyBlock(TokenSpan tokens) const {
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

    bool IsTypeDeclarationTrailingDeclarator(BlockKind blockKind, TokenSpan tokens, size_t next) const {
        if (blockKind != BlockKind::TypeDeclaration && blockKind != BlockKind::EnumDeclaration) {
            return false;
        }
        if (next >= tokens.size()) {
            return false;
        }
        const Token& token = tokens[next];
        return token.kind == TokenKind::Word || IsPointerOrReferenceDeclaratorToken(token.text);
    }

    DeclarationKind ClassifySemicolonDeclaration(TokenSpan tokens) const {
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

    bool IsDefaultedDeletedOrPureVirtualMethodDeclaration(TokenSpan tokens) const {
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

    std::optional<size_t> FindTypeDeclarationKeyword(TokenSpan tokens) const {
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

    bool IsFunctionDefinitionBlock(TokenSpan tokens) const {
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

    bool ContainsTopLevelAssignment(TokenSpan tokens) const {
        return FindTopLevelAssignment(tokens).has_value();
    }

    bool ContainsTopLevelToken(TokenSpan tokens, std::string_view tokenText) const {
        return FindTopLevelToken(tokens, tokenText).has_value();
    }

    bool ContainsTopLevelFunctionParameterList(TokenSpan tokens) const {
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

    bool IsFunctionParameterListOpen(TokenSpan tokens, size_t open) const {
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

    bool IsOperatorCallNameClose(TokenSpan tokens, size_t close) const {
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

    std::optional<size_t> FindTopLevelToken(TokenSpan tokens, std::string_view tokenText) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth == 0 && tokens[index].text == tokenText) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> FindTopLevelTokenAfter(TokenSpan tokens, std::string_view tokenText, size_t begin) const {
        int depth = 0;
        for (size_t index = begin; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth == 0 && tokens[index].text == tokenText) {
                return index;
            }
        }
        return std::nullopt;
    }

    bool ContainsTokenAfter(TokenSpan tokens, size_t begin, std::string_view tokenText) const {
        for (size_t index = begin; index < tokens.size(); ++index) {
            if (tokens[index].text == tokenText) {
                return true;
            }
        }
        return false;
    }

    bool ContainsWord(TokenSpan tokens, std::string_view text) const {
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

    bool IsLabelColonToken(TokenSpan tokens, size_t index) const {
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

    bool IsLambdaReturnArrowToken(TokenSpan tokens, size_t index) const {
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

    bool IsLambdaBodyOpenToken(TokenSpan tokens, size_t index) const {
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

    bool IsLambdaIntroducerClose(TokenSpan tokens, size_t index) const {
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

    bool IsTemplateAngleToken(TokenSpan tokens, size_t index) const {
        return IsTemplateAngleOpen(tokens, index) || IsTemplateAngleClose(tokens, index);
    }

    bool IsTemplateAngleCloseToken(TokenSpan tokens, size_t index) const {
        return index < tokens.size() &&
            (tokens[index].text == ">" || tokens[index].text == ">>") &&
            IsTemplateAngleClose(tokens, index);
    }

    bool IsTemplateAngleOpen(TokenSpan tokens, size_t index) const {
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

    bool IsTemplateAngleClose(TokenSpan tokens, size_t index) const {
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

    std::optional<size_t> FindTemplateAngleOpen(TokenSpan tokens, size_t close) const {
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

    std::optional<size_t> FindTemplateAngleClose(TokenSpan tokens, size_t open) const {
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

    bool IsTemplateArgumentReferenceToken(TokenSpan tokens, size_t index) const {
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

    bool StartsWithControlFor(TokenSpan tokens) const {
        return FirstControlHeaderToken(tokens) && FirstControlHeaderToken(tokens)->text == "for";
    }

    bool StartsWithControlHeader(TokenSpan tokens) const {
        return FirstControlHeaderToken(tokens).has_value();
    }

    std::optional<Token> FirstControlHeaderToken(TokenSpan tokens) const {
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

    const Token* NextNonNewline(TokenSpan tokens, size_t index) const {
        while (index < tokens.size()) {
            if (tokens[index].kind != TokenKind::Newline) {
                return &tokens[index];
            }
            ++index;
        }
        return nullptr;
    }

    Token NextSignificant(TokenSpan tokens, size_t index) const {
        const size_t found = NextSignificantIndex(tokens, index);
        if (found < tokens.size()) {
            return tokens[found];
        }
        return {};
    }

    size_t NextSignificantIndex(TokenSpan tokens, size_t index) const {
        while (index < tokens.size() && tokens[index].kind == TokenKind::Newline) {
            ++index;
        }
        return index;
    }

    void UpdateDepth(TokenSpan tokens, size_t index, int& depth) const {
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
    const SourceLayoutTree* sourceLayout_ = nullptr;
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

void SortIncludeRun(std::vector<IncludeLine>& includes, const FormatterConfig& config, std::string_view sourcePath) {
    for (IncludeLine& include : includes) {
        include.group = IncludeGroupIndex(include, config, sourcePath);
    }
    std::sort(includes.begin(), includes.end(), [](const IncludeLine& left, const IncludeLine& right) {
        if (left.group != right.group) {
            return left.group < right.group;
        }
        return tools::lint::ToLowerAscii(left.spelling) < tools::lint::ToLowerAscii(right.spelling);
    });
}

std::optional<IncludeLine> ParseIncludeToken(const Token& token) {
    if (token.kind != TokenKind::Preprocessor) {
        return std::nullopt;
    }
    std::vector<std::string> lines = tools::lint::SplitLines(token.text);
    if (lines.empty()) {
        lines.push_back(token.text);
    }
    if (lines.size() != 1) {
        return std::nullopt;
    }
    return ParseIncludeLine(lines.front());
}

void AppendSortedIncludeRun(
    std::vector<Token>& output,
    std::vector<IncludeLine>& includeRun,
    const FormatterConfig& config,
    std::string_view sourcePath,
    bool hasFollowingToken
) {
    if (includeRun.empty()) {
        return;
    }
    SortIncludeRun(includeRun, config, sourcePath);
    int lastGroup = -1;
    for (const IncludeLine& include : includeRun) {
        if (lastGroup != -1 && include.group != lastGroup) {
            output.push_back({TokenKind::Newline, "\n"});
        }
        output.push_back({TokenKind::Preprocessor, include.line});
        lastGroup = include.group;
    }
    if (hasFollowingToken) {
        output.push_back({TokenKind::Newline, "\n"});
    }
    includeRun.clear();
}

std::vector<Token> SortIncludeTokens(
    std::vector<Token> tokens,
    const FormatterConfig& config,
    std::string_view sourcePath
) {
    std::vector<Token> output;
    output.reserve(tokens.size());
    std::vector<IncludeLine> includeRun;
    for (size_t index = 0; index < tokens.size(); ++index) {
        if (std::optional<IncludeLine> include = ParseIncludeToken(tokens[index])) {
            includeRun.push_back(*include);
            continue;
        }
        if (tokens[index].kind == TokenKind::Newline && !includeRun.empty()) {
            continue;
        }
        AppendSortedIncludeRun(output, includeRun, config, sourcePath, true);
        output.push_back(std::move(tokens[index]));
    }
    AppendSortedIncludeRun(output, includeRun, config, sourcePath, false);
    return output;
}

struct FormatModel {
    ParseResult parse;
    std::vector<Token> tokens;
    SourceLayoutTree layout;
};

bool IsNewlineByte(char ch) {
    return ch == '\r' || ch == '\n';
}

size_t AdvanceNewline(std::string_view text, size_t index) {
    if (index < text.size() && text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
        return index + 2;
    }
    return std::min(index + 1, text.size());
}

bool IsPreprocessorLineStart(std::string_view text, size_t index) {
    if (index > text.size() || index != 0 && !IsNewlineByte(text[index - 1])) {
        return false;
    }
    while (index < text.size() && IsSpaceButNotNewline(text[index])) {
        ++index;
    }
    return index < text.size() && text[index] == '#';
}

size_t PreprocessorLineEnd(std::string_view text, size_t index) {
    while (index < text.size()) {
        const size_t lineStart = index;
        while (index < text.size() && !IsNewlineByte(text[index])) {
            ++index;
        }
        size_t lineEnd = index;
        while (lineEnd > lineStart && IsSpaceButNotNewline(text[lineEnd - 1])) {
            --lineEnd;
        }
        const bool continued = lineEnd > lineStart && text[lineEnd - 1] == '\\';
        if (!continued) {
            return index;
        }
        if (index < text.size()) {
            index = AdvanceNewline(text, index);
        }
    }
    return index;
}

size_t SkipFollowingNewline(std::string_view text, size_t index) {
    if (index < text.size() && IsNewlineByte(text[index])) {
        return AdvanceNewline(text, index);
    }
    return index;
}

std::string SourceText(std::string_view text, uint32_t begin, uint32_t end) {
    if (begin > text.size()) {
        begin = static_cast<uint32_t>(text.size());
    }
    if (end < begin) {
        end = begin;
    }
    if (end > text.size()) {
        end = static_cast<uint32_t>(text.size());
    }
    return std::string(text.substr(begin, end - begin));
}

void AppendPreprocessorDirective(std::string_view text, size_t& index, std::vector<Token>& tokens) {
    const size_t start = index;
    const size_t directiveEnd = PreprocessorLineEnd(text, index);
    tokens.push_back(
        {TokenKind::Preprocessor, std::string(text.substr(start, directiveEnd - start)), start, directiveEnd}
    );
    index = SkipFollowingNewline(text, directiveEnd);
}

size_t AppendSourceTrivia(std::string_view text, size_t begin, size_t end, std::vector<Token>& tokens) {
    size_t index = begin;
    while (index < end) {
        if (IsPreprocessorLineStart(text, index)) {
            AppendPreprocessorDirective(text, index, tokens);
            continue;
        }
        if (IsNewlineByte(text[index])) {
            const size_t newlineEnd = AdvanceNewline(text, index);
            tokens.push_back({TokenKind::Newline, "\n", index, newlineEnd});
            index = newlineEnd;
            continue;
        }
        ++index;
    }
    return index;
}

bool IsSimplePreprocessorNode(std::string_view type) {
    return type == "preproc_include" ||
        type == "preproc_def" ||
        type == "preproc_function_def" ||
        type == "preproc_call" ||
        type == "preproc_using";
}

bool IsAtomicTreeNode(std::string_view type, uint32_t childCount) {
    return childCount == 0 ||
        type == "comment" ||
        type == "char_literal" ||
        type == "string_literal" ||
        type == "raw_string_literal" ||
        type == "number_literal";
}

TokenKind ClassifyTreeToken(std::string_view type, std::string_view text) {
    if (type == "comment") {
        return tools::lint::StartsWith(text, "//") ? TokenKind::LineComment : TokenKind::BlockComment;
    }
    if (type == "char_literal") {
        return TokenKind::CharLiteral;
    }
    if (type == "string_literal" || type == "raw_string_literal") {
        return TokenKind::StringLiteral;
    }
    if (type == "number_literal" || (!text.empty() && IsDigit(text.front()))) {
        return TokenKind::Number;
    }
    if (!text.empty() && IsIdentifierStart(text.front())) {
        return TokenKind::Word;
    }
    return TokenKind::Symbol;
}

void TrimLineCommentTerminator(std::string& text) {
    while (!text.empty() && IsNewlineByte(text.back())) {
        text.pop_back();
    }
}

void AppendTreeTokens(TSNode node, std::string_view text, std::vector<Token>& tokens);

void AppendTreeChildTokens(TSNode node, std::string_view text, std::vector<Token>& tokens) {
    const uint32_t childCount = ts_node_child_count(node);
    size_t cursor = ts_node_start_byte(node);
    const size_t nodeEnd = ts_node_end_byte(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        const size_t childStart = ts_node_start_byte(child);
        const size_t childEnd = ts_node_end_byte(child);
        if (childEnd <= cursor) {
            continue;
        }
        if (childStart > cursor) {
            cursor = AppendSourceTrivia(text, cursor, childStart, tokens);
        }
        if (childStart < cursor) {
            continue;
        }
        AppendTreeTokens(child, text, tokens);
        cursor = std::max(cursor, childEnd);
    }
    if (cursor < nodeEnd) {
        (void)AppendSourceTrivia(text, cursor, nodeEnd, tokens);
    }
}

void AppendTreeTokens(TSNode node, std::string_view text, std::vector<Token>& tokens) {
    const std::string_view type = ts_node_type(node);
    const uint32_t childCount = ts_node_child_count(node);
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (start >= end) {
        return;
    }
    if (IsPreprocessorLineStart(text, start)) {
        size_t index = start;
        AppendPreprocessorDirective(text, index, tokens);
        return;
    }
    if (IsSimplePreprocessorNode(type)) {
        tokens.push_back({TokenKind::Preprocessor, SourceText(text, start, end), start, end});
        return;
    }
    if (IsAtomicTreeNode(type, childCount)) {
        std::string tokenText = SourceText(text, start, end);
        if (type == "comment" && tools::lint::StartsWith(tokenText, "//")) {
            TrimLineCommentTerminator(tokenText);
        }
        tokens.push_back({ClassifyTreeToken(type, tokenText), tokenText, start, end});
        return;
    }
    AppendTreeChildTokens(node, text, tokens);
}

ParseResult ParseTreeResult(TSNode root, std::string_view text) {
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
    return result;
}

struct SourceTokenLookup {
    std::map<size_t, size_t> byBegin;
    const std::vector<Token>* tokens = nullptr;
};

SourceTokenLookup BuildSourceTokenLookup(const std::vector<Token>& tokens) {
    SourceTokenLookup lookup;
    lookup.tokens = &tokens;
    for (size_t index = 0; index < tokens.size(); ++index) {
        if (tokens[index].sourceBegin != kNoTokenIndex) {
            lookup.byBegin.emplace(tokens[index].sourceBegin, index);
        }
    }
    return lookup;
}

std::optional < std::pair<
    size_t,
    size_t
> > TokenSpanForByteRange(const SourceTokenLookup& lookup, size_t begin, size_t end) {
    if (lookup.tokens == nullptr || begin >= end) {
        return std::nullopt;
    }
    auto current = lookup.byBegin.lower_bound(begin);
    if (current == lookup.byBegin.end() || current->first >= end) {
        return std::nullopt;
    }
    size_t first = current->second;
    size_t last = current->second;
    for (; current != lookup.byBegin.end() && current->first < end; ++current) {
        first = std::min(first, current->second);
        last = std::max(last, current->second);
    }
    if (first > last) {
        return std::nullopt;
    }
    return std::pair<size_t, size_t>{first, last + 1};
}

std::optional<std::pair<size_t, size_t>> TokenSpanForTreeNode(const SourceTokenLookup& lookup, TSNode node) {
    return TokenSpanForByteRange(lookup, ts_node_start_byte(node), ts_node_end_byte(node));
}

std::optional<size_t> FindFirstSourceTokenText(
    const SourceTokenLookup& lookup,
    size_t begin,
    size_t end,
    std::string_view text
) {
    if (lookup.tokens == nullptr) {
        return std::nullopt;
    }
    for (
        auto current = lookup.byBegin.lower_bound(begin);
        current != lookup.byBegin.end() && current->first < end;
        ++current
    ) {
        if ((*lookup.tokens)[current->second].text == text) {
            return current->second;
        }
    }
    return std::nullopt;
}

std::optional<size_t> FindLastSourceTokenText(
    const SourceTokenLookup& lookup,
    size_t begin,
    size_t end,
    std::string_view text
) {
    if (lookup.tokens == nullptr) {
        return std::nullopt;
    }
    std::optional<size_t> result;
    for (
        auto current = lookup.byBegin.lower_bound(begin);
        current != lookup.byBegin.end() && current->first < end;
        ++current
    ) {
        if ((*lookup.tokens)[current->second].text == text) {
            result = current->second;
        }
    }
    return result;
}

std::optional<size_t> FindFirstSourceTokenAnyText(
    const SourceTokenLookup& lookup,
    size_t begin,
    size_t end,
    std::initializer_list<std::string_view> texts
) {
    if (lookup.tokens == nullptr) {
        return std::nullopt;
    }
    for (
        auto current = lookup.byBegin.lower_bound(begin);
        current != lookup.byBegin.end() && current->first < end;
        ++current
    ) {
        const std::string& tokenText = (*lookup.tokens)[current->second].text;
        for (std::string_view text : texts) {
            if (tokenText == text) {
                return current->second;
            }
        }
    }
    return std::nullopt;
}

std::optional<size_t> FindDirectChildSourceTokenAnyText(
    TSNode node,
    const SourceTokenLookup& lookup,
    std::initializer_list<std::string_view> texts
) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        const std::optional<std::pair<size_t, size_t>> span = TokenSpanForTreeNode(lookup, child);
        if (!span || span->second != span->first + 1 || lookup.tokens == nullptr) {
            continue;
        }
        const std::string& tokenText = (*lookup.tokens)[span->first].text;
        for (std::string_view text : texts) {
            if (tokenText == text) {
                return span->first;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> FindDirectChildSourceTokenText(
    TSNode node,
    const SourceTokenLookup& lookup,
    std::initializer_list<std::string_view> texts
) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        const std::optional<std::pair<size_t, size_t>> span = TokenSpanForTreeNode(lookup, child);
        if (!span || span->second != span->first + 1 || lookup.tokens == nullptr) {
            continue;
        }
        const std::string& tokenText = (*lookup.tokens)[span->first].text;
        for (std::string_view text : texts) {
            if (tokenText == text) {
                return tokenText;
            }
        }
    }
    return std::nullopt;
}

std::optional<size_t> FindLastSourceTokenAnyText(
    const SourceTokenLookup& lookup,
    size_t begin,
    size_t end,
    std::initializer_list<std::string_view> texts
) {
    if (lookup.tokens == nullptr) {
        return std::nullopt;
    }
    std::optional<size_t> result;
    for (
        auto current = lookup.byBegin.lower_bound(begin);
        current != lookup.byBegin.end() && current->first < end;
        ++current
    ) {
        const std::string& tokenText = (*lookup.tokens)[current->second].text;
        for (std::string_view text : texts) {
            if (tokenText == text) {
                result = current->second;
            }
        }
    }
    return result;
}

bool TreeNodeHasDirectChildType(TSNode node, std::string_view type) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        if (std::string_view(ts_node_type(ts_node_child(node, index))) == type) {
            return true;
        }
    }
    return false;
}

std::optional<TSNode> FindDirectChildType(TSNode node, std::string_view type) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        TSNode child = ts_node_child(node, index);
        if (std::string_view(ts_node_type(child)) == type) {
            return child;
        }
    }
    return std::nullopt;
}

bool IsTreeGroupNodeType(std::string_view type) {
    return type == "argument_list" ||
        type == "parameter_list" ||
        type == "requires_parameter_list" ||
        type == "condition_clause" ||
        type == "parenthesized_expression" ||
        type == "initializer_list" ||
        type == "subscript_argument_list" ||
        type == "lambda_capture_specifier" ||
        type == "structured_binding_declarator" ||
        type == "template_argument_list" ||
        type == "template_parameter_list";
}

bool IsTreeControlHeaderNodeType(std::string_view type) {
    return type == "if_statement" ||
        type == "for_statement" ||
        type == "for_range_loop" ||
        type == "while_statement" ||
        type == "switch_statement";
}

std::string_view TreeGroupOpenDelimiter(std::string_view type) {
    if (
        type == "subscript_argument_list" ||
        type == "lambda_capture_specifier" ||
        type == "structured_binding_declarator"
    ) {
        return "[";
    }
    if (type == "initializer_list") {
        return "{";
    }
    if (type == "template_argument_list" || type == "template_parameter_list") {
        return "<";
    }
    return "(";
}

bool IsTreeGroupCloseDelimiter(std::string_view type, std::string_view text) {
    if (
        type == "subscript_argument_list" ||
        type == "lambda_capture_specifier" ||
        type == "structured_binding_declarator"
    ) {
        return text == "]";
    }
    if (type == "initializer_list") {
        return text == "}";
    }
    if (type == "template_argument_list" || type == "template_parameter_list") {
        return text == ">" || text == ">>";
    }
    return text == ")";
}

bool IsAssignmentTreeNodeType(std::string_view type) {
    return type == "assignment_expression" || type == "init_declarator" || type == "condition_declaration";
}

std::optional<std::string> FindBinaryOperatorText(TSNode node, const SourceTokenLookup& lookup) {
    if (std::string_view(ts_node_type(node)) != "binary_expression") {
        return std::nullopt;
    }
    return FindDirectChildSourceTokenText(
        node,
        lookup,
        {"&&", "||", "|", "^", "==", "!=", "<", ">", "<=", ">=", "<<", ">>", "+", "*"}
    );
}

bool HasDirectBinaryChildWithOperator(TSNode node, const SourceTokenLookup& lookup, std::string_view text) {
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        const std::optional<std::string> childOperator = FindBinaryOperatorText(child, lookup);
        if (childOperator && *childOperator == text) {
            return true;
        }
    }
    return false;
}

std::optional<SourceLayoutNode> MakeSourceLayoutNode(TSNode node, const SourceTokenLookup& lookup, size_t order) {
    const std::string_view type = ts_node_type(node);
    const size_t nodeBegin = ts_node_start_byte(node);
    const size_t nodeEnd = ts_node_end_byte(node);
    const std::optional<std::pair<size_t, size_t>> span = TokenSpanForByteRange(lookup, nodeBegin, nodeEnd);
    if (!span) {
        return std::nullopt;
    }

    SourceLayoutNode source;
    source.begin = span->first;
    source.end = span->second;
    source.order = order;

    if (type == "template_declaration" || type == "requires_clause") {
        source.kind = SourceLayoutKind::TemplateDeclaration;
        return source;
    }
    if (type == "lambda_expression") {
        std::optional<size_t> bodyOpen;
        if (const std::optional<TSNode> body = FindDirectChildType(node, "compound_statement")) {
            bodyOpen = FindFirstSourceTokenText(lookup, ts_node_start_byte(*body), ts_node_end_byte(*body), "{");
        }
        if (!bodyOpen) {
            return std::nullopt;
        }
        source.kind = SourceLayoutKind::Lambda;
        source.index = *bodyOpen;
        return source;
    }
    if (type == "field_initializer_list") {
        const std::optional<size_t> colon = FindFirstSourceTokenText(lookup, nodeBegin, nodeEnd, ":");
        if (!colon) {
            return std::nullopt;
        }
        source.kind = SourceLayoutKind::ConstructorInitializer;
        source.index = *colon;
        return source;
    }
    if (IsAssignmentTreeNodeType(type)) {
        const std::optional<size_t> assignment = FindDirectChildSourceTokenAnyText(
            node,
            lookup,
            {"=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|=", "and_eq", "or_eq", "xor_eq"}
        );
        if (assignment) {
            source.kind = SourceLayoutKind::Assignment;
            source.index = *assignment;
            return source;
        }
        if (type == "init_declarator" && (
            TreeNodeHasDirectChildType(node, "initializer_list") || TreeNodeHasDirectChildType(node, "argument_list")
        )) {
            source.kind = SourceLayoutKind::DeclarationValue;
            source.index = span->first;
            return source;
        }
    }
    if (type == "conditional_expression") {
        source.kind = SourceLayoutKind::OperatorChain;
        source.stopChildren = TreeNodeHasDirectChildType(node, "conditional_expression");
        return source;
    }
    if (type == "binary_expression") {
        const std::optional<std::string> breakOperator = FindBinaryOperatorText(node, lookup);
        if (!breakOperator) {
            return std::nullopt;
        }
        source.kind = SourceLayoutKind::OperatorChain;
        source.stopChildren = HasDirectBinaryChildWithOperator(node, lookup, *breakOperator);
        return source;
    }
    if (type == "concatenated_string") {
        source.kind = SourceLayoutKind::StringLiteralSequence;
        return source;
    }
    if (IsTreeControlHeaderNodeType(type)) {
        const std::optional<size_t> open = FindFirstSourceTokenText(lookup, nodeBegin, nodeEnd, "(");
        if (!open || lookup.tokens == nullptr) {
            return std::nullopt;
        }
        const size_t close = (*lookup.tokens)[*open].matchingIndex;
        if (close == kNoTokenIndex || close <= *open || close >= lookup.tokens->size()) {
            return std::nullopt;
        }
        source.kind = SourceLayoutKind::Group;
        source.begin = *open;
        source.end = close + 1;
        source.groupOpen = *open;
        source.groupClose = close;
        return source;
    }
    if (IsTreeGroupNodeType(type)) {
        const std::optional<size_t> open =
            FindFirstSourceTokenText(lookup, nodeBegin, nodeEnd, TreeGroupOpenDelimiter(type));
        std::optional<size_t> close;
        if (lookup.tokens != nullptr) {
            for (
                auto current = lookup.byBegin.lower_bound(nodeBegin);
                current != lookup.byBegin.end() && current->first < nodeEnd;
                ++current
            ) {
                if (IsTreeGroupCloseDelimiter(type, (*lookup.tokens)[current->second].text)) {
                    close = current->second;
                }
            }
        }
        if (!open || !close || *open >= *close) {
            return std::nullopt;
        }
        if (
            type == "template_parameter_list" &&
            lookup.tokens != nullptr &&
            *open > 0 &&
            (*lookup.tokens)[*open - 1].text == "template"
        ) {
            source.kind = SourceLayoutKind::TemplateDeclaration;
            source.begin = *open - 1;
            source.end = span->second;
            return source;
        }
        source.kind = SourceLayoutKind::Group;
        source.begin = *open;
        source.end = *close + 1;
        source.groupOpen = *open;
        source.groupClose = *close;
        return source;
    }
    return std::nullopt;
}

void AppendSourceLayoutNodes(
    TSNode node,
    const SourceTokenLookup& lookup,
    SourceLayoutNode& parent,
    int depth,
    size_t& order
) {
    SourceLayoutNode* childParent = &parent;
    std::optional<SourceLayoutNode> source = MakeSourceLayoutNode(node, lookup, order);
    if (source) {
        source->depth = depth;
        source->order = order++;
        parent.children.push_back(std::move(*source));
        childParent = &parent.children.back();
        ++depth;
    }

    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        if (ts_node_start_byte(child) == ts_node_end_byte(child)) {
            continue;
        }
        AppendSourceLayoutNodes(child, lookup, *childParent, depth, order);
    }
}

SourceLayoutTree BuildSourceLayoutTree(TSNode root, const std::vector<Token>& tokens) {
    SourceLayoutTree tree;
    tree.available = true;
    tree.root.kind = SourceLayoutKind::Root;
    tree.root.begin = 0;
    tree.root.end = tokens.size();
    const SourceTokenLookup lookup = BuildSourceTokenLookup(tokens);
    size_t order = 0;
    AppendSourceLayoutNodes(root, lookup, tree.root, 0, order);
    return tree;
}

void AnnotateTokenIndexesAndGroups(std::vector<Token>& tokens);

FormatModel BuildFormatModel(std::string_view text, const FormatterConfig& config, std::string_view sourcePath) {
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
    FormatModel model;
    model.parse = ParseTreeResult(root, text);
    AppendTreeChildTokens(root, text, model.tokens);
    (void)AppendSourceTrivia(text, ts_node_end_byte(root), text.size(), model.tokens);
    model.tokens = SortIncludeTokens(std::move(model.tokens), config, sourcePath);
    model.tokens = DropTrailingCommas(std::move(model.tokens));
    AnnotateTokenIndexesAndGroups(model.tokens);
    model.tokens = AddRequiredControlBraces(model.tokens);
    AnnotateTokenIndexesAndGroups(model.tokens);
    model.layout = BuildSourceLayoutTree(root, model.tokens);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return model;
}

bool TextMatchesFormattedOutput(std::string_view source, std::string_view formatted) {
    size_t sourceIndex = 0;
    size_t formattedIndex = 0;
    while (sourceIndex < source.size() || formattedIndex < formatted.size()) {
        if (sourceIndex >= source.size() || formattedIndex >= formatted.size()) {
            return false;
        }
        char sourceChar = source[sourceIndex];
        char formattedChar = formatted[formattedIndex];
        if (IsNewlineByte(sourceChar)) {
            sourceChar = '\n';
            sourceIndex = AdvanceNewline(source, sourceIndex);
        } else {
            ++sourceIndex;
        }
        if (IsNewlineByte(formattedChar)) {
            formattedChar = '\n';
            formattedIndex = AdvanceNewline(formatted, formattedIndex);
        } else {
            ++formattedIndex;
        }
        if (sourceChar != formattedChar) {
            return false;
        }
    }
    return true;
}

void AnnotateTokenIndexesAndGroups(std::vector<Token>& tokens) {
    std::vector<size_t> stack;
    for (size_t index = 0; index < tokens.size(); ++index) {
        tokens[index].modelIndex = index;
        tokens[index].matchingIndex = kNoTokenIndex;
        const std::string& text = tokens[index].text;
        if (text == "(" || text == "[" || text == "{") {
            stack.push_back(index);
            continue;
        }
        if (text != ")" && text != "]" && text != "}") {
            continue;
        }
        const std::string open = text == ")" ? "(" : text == "]" ? "[" : "{";
        while (!stack.empty() && tokens[stack.back()].text != open) {
            stack.pop_back();
        }
        if (stack.empty()) {
            continue;
        }
        const size_t openIndex = stack.back();
        stack.pop_back();
        tokens[openIndex].matchingIndex = index;
        tokens[index].matchingIndex = openIndex;
    }
}

FileFormatResult FormatOneText(std::string_view text, const FormatterConfig& config, std::string_view sourcePath) {
    FormatModel model = BuildFormatModel(text, config, sourcePath);
    if (!model.parse.ok) {
        return {.ok = false, .error = "tree-sitter parser setup failed"};
    }
    PrettyFormatter formatter(config, 0, false, &model.layout);
    FileFormatResult result;
    result.parseHadErrors = model.parse.hasErrors;
    result.parseErrorNodeType = model.parse.errorNodeType;
    result.parseErrorLine = model.parse.errorLine;
    result.parseErrorColumn = model.parse.errorColumn;
    result.parseErrorSnippet = model.parse.errorSnippet;
    result.formatted = formatter.Format(model.tokens);
    result.changed = !TextMatchesFormattedOutput(text, result.formatted);
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
