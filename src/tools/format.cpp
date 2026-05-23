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
    Fix
};

enum class Scope {
    All,
    Changed,
    Path
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
    Newline
};

struct Options {
    Mode mode = Mode::Check;
    Scope scope = Scope::All;
    std::string root;
    std::string targetFile;
    std::string targetPath;
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
    std::vector<std::string> statementLikeMacroParameters;
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
    std::fprintf(stderr, "  CaseDashTools.exe format [fix] [--root path] --path file-or-directory [-v|--verbose]\n");
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
    ) :
        config_(config),
        indentLevel_(initialIndentLevel)
    {
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
        FunctionDefinition
    };

    enum class DeclarationKind {
        None,
        Field,
        MacroDefinition,
        Method,
        NamespaceDeclaration,
        TypeDeclaration
    };

    struct BlockState {
        BlockKind kind = BlockKind::Other;
        bool indentsBody = true;
        DeclarationKind previousDeclarationKind = DeclarationKind::None;
        bool previousDeclarationWasMultilineField = false;
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
            previousDeclarationWasMultilineField_,
            caseBodyIndentLevel_
        });
        previousDeclarationKind_ = DeclarationKind::None;
        previousDeclarationWasMultilineField_ = false;
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
            previousDeclarationWasMultilineField_,
            caseBodyIndentLevel_
        });
        if (blockKind == BlockKind::SwitchStatement) {
            caseBodyIndentLevel_ = -1;
        }
        previousDeclarationKind_ = DeclarationKind::None;
        previousDeclarationWasMultilineField_ = false;
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
        previousDeclarationWasMultilineField_ = closedBlock.previousDeclarationWasMultilineField;
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
        if (
            next < tokens.size() &&
            tokens[next].kind == TokenKind::Word &&
            (
                tokens[next].text == "else" ||
                    tokens[next].text == "catch" ||
                    (tokens[next].text == "while" && closedBlock.kind == BlockKind::DoStatement)
            )
        ) {
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
            for (size_t index = 0; index < statementLines.size(); ++index) {
                std::string line = statementLines[index];
                if (index + 1 < statementLines.size()) {
                    line += " \\";
                }
                EmitLine(std::move(line));
            }
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
        for (size_t index = 0; index < replacementLines.size(); ++index) {
            std::string line = replacementLines[index];
            if (index + 1 < replacementLines.size()) {
                line += " \\";
            }
            EmitLine(std::move(line));
        }
        pendingPreprocessorBlank_ = true;
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
        const bool multilineField = declarationKind == DeclarationKind::Field && lines.size() > 1;
        EmitBlankBeforeDeclarationKind(declarationKind, separateSameKind, multilineField);
        for (std::string& line : lines) {
            EmitLine(std::move(line));
        }
        ClearPending();
        NoteDeclarationKind(declarationKind, multilineField);
    }

    bool IsInsideEnumDeclaration() const {
        return !blockStack_.empty() && blockStack_.back().kind == BlockKind::EnumDeclaration;
    }

    void EmitEnumEnumerators(const std::vector<Token>& tokens) {
        std::vector<std::vector<Token>> elements = SplitTopLevel(tokens, ',');
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(tokens, ',')) {
            EmitFormatted(tokens, {});
            return;
        }
        for (size_t index = 0; index < elements.size(); ++index) {
            if (elements[index].empty()) {
                continue;
            }
            std::string elementSuffix;
            if (index + 1 < elements.size()) {
                elementSuffix = ",";
            }
            for (std::string& line : FormatRange(elements[index], indentLevel_, {}, elementSuffix)) {
                EmitLine(std::move(line));
            }
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
            previousDeclarationWasMultilineField_ = false;
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
            previousDeclarationWasMultilineField_,
            caseBodyIndentLevel_
        });
        previousDeclarationKind_ = DeclarationKind::None;
        previousDeclarationWasMultilineField_ = false;
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
        bool currentDeclarationIsMultilineField = false
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
            !previousDeclarationWasMultilineField_ &&
            !currentDeclarationIsMultilineField
        ) {
            return;
        }
        EmitBlankBeforeSiblingGroupIfNeeded();
    }

    void NoteDeclarationKind(DeclarationKind declarationKind, bool multilineField = false) {
        if (declarationKind == DeclarationKind::None || !IsDeclarationContext()) {
            return;
        }
        previousDeclarationKind_ = declarationKind;
        previousDeclarationWasMultilineField_ = declarationKind == DeclarationKind::Field && multilineField;
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
        Multiplicative
    };

    std::vector<std::string> FormatRange(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains = false
    ) const {
        if (tokens.empty()) {
            if (!prefix.empty() || !suffix.empty()) {
                return {Indent(indentLevel) + prefix + suffix};
            }
            return {};
        }
        if (IsTemplateDeclarationPrefix(tokens)) {
            return FormatTemplateDeclaration(tokens, indentLevel, std::move(prefix), std::move(suffix));
        }
        std::string inlineText = prefix + FormatInline(tokens);
        AppendSuffix(inlineText, suffix);
        if (!HasOriginalBlankSeparator(tokens) && !ShouldForceSplit(tokens) && Fits(indentLevel, inlineText)) {
            return {Indent(indentLevel) + inlineText};
        }
        if (
            std::optional<size_t> assignment = FindTopLevelAssignment(tokens);
            assignment && !IsDefaultedDeletedOrPureVirtualMethodDeclaration(tokens)
        ) {
            if (StartsWithInitializerList(tokens, *assignment + 1)) {
                return FormatInitializerAssignment(
                    tokens,
                    *assignment,
                    indentLevel,
                    std::move(prefix),
                    std::move(suffix)
                );
            }
            std::vector<std::string> lines;
            std::vector<Token> lhs(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(*assignment + 1));
            std::vector<Token> rhs(tokens.begin() + static_cast<std::ptrdiff_t>(*assignment + 1), tokens.end());
            std::string attachedPrefix = prefix + FormatInline(lhs) + " ";
            const bool compactCallFitsOnContinuation = CanKeepCallRhsCompactOnContinuation(rhs, indentLevel, suffix);
            if (
                !compactCallFitsOnContinuation &&
                SelectChainKind(rhs) != ChainKind::Ternary &&
                (
                    CanAttachAssignmentToWrappedCall(rhs, indentLevel, attachedPrefix) ||
                        CanAttachAssignmentToWrappedLeadingGroup(rhs, indentLevel, attachedPrefix) ||
                        CanAttachAssignmentToWrappedLambda(rhs, indentLevel, attachedPrefix)
                )
            ) {
                return FormatRange(rhs, indentLevel, std::move(attachedPrefix), std::move(suffix), indentSplitChains);
            }
            lines.push_back(Indent(indentLevel) + prefix + FormatInline(lhs));
            std::vector<std::string> rhsLines =
                FormatRange(rhs, indentLevel + 1, {}, std::move(suffix), indentSplitChains);
            lines.insert(lines.end(), rhsLines.begin(), rhsLines.end());
            return lines;
        }
        if (std::optional<size_t> lambdaBody = FindLambdaBodyOpen(tokens)) {
            return FormatSplitLambda(tokens, *lambdaBody, indentLevel, std::move(prefix), std::move(suffix));
        }
        if (std::optional<size_t> initializerColon = FindConstructorInitializerColon(tokens)) {
            return FormatConstructorInitializerList(
                tokens,
                *initializerColon,
                indentLevel,
                std::move(prefix),
                std::move(suffix)
            );
        }
        if (CanSplitOperatorChain(tokens)) {
            return FormatOperatorChain(tokens, indentLevel, std::move(prefix), std::move(suffix), indentSplitChains);
        }
        if (std::optional<size_t> memberAccess = FindTopLevelMemberAccess(tokens)) {
            return FormatMemberAccessChain(tokens, *memberAccess, indentLevel, std::move(prefix), std::move(suffix));
        }
        if (std::optional<GroupPair> group = FindFirstWrappableGroupPair(tokens)) {
            return FormatSplitGroup(tokens, *group, indentLevel, std::move(prefix), std::move(suffix));
        }
        return {Indent(indentLevel) + inlineText};
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
        lines.push_back(Indent(indentLevel) + prefix + FormatInline(templatePrefix));
        if (declaration.empty()) {
            if (!suffix.empty()) {
                AppendSuffix(lines.back(), suffix);
            }
            return lines;
        }
        std::vector<std::string> declarationLines = FormatRange(declaration, indentLevel, {}, std::move(suffix));
        lines.insert(lines.end(), declarationLines.begin(), declarationLines.end());
        return lines;
    }

    bool CanKeepCallRhsCompactOnContinuation(
        const std::vector<Token>& rhs,
        int assignmentIndentLevel,
        std::string_view suffix
    ) const {
        if (!IsTopLevelCallExpression(rhs) || ShouldForceSplit(rhs)) {
            return false;
        }
        std::string inlineText = FormatInline(rhs);
        AppendSuffix(inlineText, suffix);
        return Fits(assignmentIndentLevel + 1, inlineText);
    }

    bool CanAttachAssignmentToWrappedCall(
        const std::vector<Token>& rhs,
        int indentLevel,
        std::string_view attachedPrefix
    ) const {
        const std::optional<GroupPair> group = FindFirstGroupPair(rhs);
        if (!group || group->open == 0 || rhs[group->open].text != "(") {
            return false;
        }
        std::vector<Token> firstLineTokens(rhs.begin(), rhs.begin() + static_cast<std::ptrdiff_t>(group->open + 1));
        return Fits(indentLevel, std::string(attachedPrefix) + FormatInline(firstLineTokens));
    }

    bool IsTopLevelCallExpression(const std::vector<Token>& rhs) const {
        const std::optional<GroupPair> group = FindFirstGroupPair(rhs);
        return group &&
            group->open > 0 &&
            rhs[group->open].text == "(" &&
            !IsFunctionPointerDeclaratorGroupOpen(rhs, group->open);
    }

    bool CanAttachAssignmentToWrappedLeadingGroup(
        const std::vector<Token>& rhs,
        int indentLevel,
        std::string_view attachedPrefix
    ) const {
        const std::optional<GroupPair> group = FindFirstGroupPair(rhs);
        if (!group || group->open != 0 || rhs[group->open].text != "(") {
            return false;
        }
        std::vector<Token> firstLineTokens(rhs.begin(), rhs.begin() + static_cast<std::ptrdiff_t>(group->open + 1));
        return Fits(indentLevel, std::string(attachedPrefix) + FormatInline(firstLineTokens));
    }

    bool CanAttachAssignmentToWrappedLambda(
        const std::vector<Token>& rhs,
        int indentLevel,
        std::string_view attachedPrefix
    ) const {
        const std::optional<size_t> bodyOpen = FindLambdaBodyOpen(rhs);
        if (!bodyOpen) {
            return false;
        }
        std::vector<Token> header(rhs.begin(), rhs.begin() + static_cast<std::ptrdiff_t>(*bodyOpen));
        if (Fits(indentLevel, std::string(attachedPrefix) + FormatInline(header) + " {")) {
            return true;
        }
        return !header.empty() && header.front().text == "[" && Fits(indentLevel, std::string(attachedPrefix) + "[");
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
        std::vector<Token> lhs(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(assignment + 1));
        std::vector<Token> inner(
            tokens.begin() + static_cast<std::ptrdiff_t>(open + 1),
            tokens.begin() + static_cast<std::ptrdiff_t>(*close)
        );
        std::vector<Token> after(tokens.begin() + static_cast<std::ptrdiff_t>(*close + 1), tokens.end());
        if (std::optional<std::vector<std::string>> combined = TryFormatCombinedNestedInitializerAssignment(
            lhs,
            inner,
            after,
            indentLevel,
            prefix,
            suffix
        )) {
            return *combined;
        }
        std::vector<std::string> lines;
        lines.push_back(Indent(indentLevel) + prefix + FormatInline(lhs) + " {");
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
                FormatInitializerElement(elements[index], indentLevel + 1, elementSuffix);
            AppendSplitElementLines(lines, elementLines, true);
        }
        std::string closeLine = "}" + FormatInline(after);
        AppendSuffix(closeLine, suffix);
        lines.push_back(Indent(indentLevel) + closeLine);
        return lines;
    }

    std::optional<std::vector<std::string>> TryFormatCombinedNestedInitializerAssignment(
        const std::vector<Token>& lhs,
        const std::vector<Token>& inner,
        const std::vector<Token>& after,
        int indentLevel,
        std::string_view prefix,
        std::string_view suffix
    ) const {
        const size_t nestedOpen = NextSignificantIndex(inner, 0);
        if (nestedOpen >= inner.size() || inner[nestedOpen].text != "{") {
            return std::nullopt;
        }
        const std::optional<size_t> nestedClose = FindMatchingClose(inner, nestedOpen);
        if (!nestedClose || NextSignificantIndex(inner, *nestedClose + 1) < inner.size()) {
            return std::nullopt;
        }
        std::string firstLine = std::string(prefix) + FormatInline(lhs) + " {{";
        if (!Fits(indentLevel, firstLine)) {
            return std::nullopt;
        }
        std::vector<Token> nestedInner(
            inner.begin() + static_cast<std::ptrdiff_t>(nestedOpen + 1),
            inner.begin() + static_cast<std::ptrdiff_t>(*nestedClose)
        );
        std::vector<std::string> lines;
        lines.push_back(Indent(indentLevel) + firstLine);
        std::vector<std::vector<Token>> elements = SplitTopLevel(nestedInner, ',');
        for (size_t index = 0; index < elements.size(); ++index) {
            if (elements[index].empty()) {
                continue;
            }
            std::string elementSuffix;
            if (index + 1 < elements.size()) {
                elementSuffix = ",";
            }
            std::vector<std::string> elementLines =
                FormatInitializerElement(elements[index], indentLevel + 1, elementSuffix);
            AppendSplitElementLines(lines, elementLines, true);
        }
        std::string closeLine = "}}" + FormatInline(after);
        AppendSuffix(closeLine, suffix);
        lines.push_back(Indent(indentLevel) + closeLine);
        return lines;
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
        std::vector<std::string> lines = FormatLambdaHeader(header, indentLevel, std::move(prefix));
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

    std::vector<std::string> FormatLambdaHeader(
        const std::vector<Token>& header,
        int indentLevel,
        std::string prefix
    ) const {
        const std::string inlineHeader = prefix + FormatInline(header) + " {";
        if (Fits(indentLevel, inlineHeader)) {
            return {Indent(indentLevel) + inlineHeader};
        }
        if (!header.empty() && header.front().text == "[") {
            if (std::optional<size_t> captureClose = FindMatchingClose(header, 0)) {
                std::vector<Token> captureInner(
                    header.begin() + 1,
                    header.begin() + static_cast<std::ptrdiff_t>(*captureClose)
                );
                if (ContainsTopLevelSeparator(captureInner, ',')) {
                    std::vector<std::string> lines;
                    lines.push_back(Indent(indentLevel) + prefix + "[");
                    std::vector<std::vector<Token>> captures = SplitTopLevel(captureInner, ',');
                    for (size_t index = 0; index < captures.size(); ++index) {
                        std::string line = FormatInline(captures[index]);
                        if (index + 1 < captures.size()) {
                            line += ",";
                        }
                        lines.push_back(Indent(indentLevel + 1) + line);
                    }
                    std::vector<Token> rest(header.begin() + static_cast<std::ptrdiff_t>(*captureClose), header.end());
                    lines.push_back(Indent(indentLevel) + FormatInline(rest) + " {");
                    return lines;
                }
            }
        }
        return {Indent(indentLevel) + inlineHeader};
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
        if (std::optional<std::vector<std::string>> combined = TryFormatCombinedControlNestedCall(
            tokens,
            group,
            indentLevel,
            prefix,
            suffix
        )) {
            return *combined;
        }
        if (std::optional<std::vector<std::string>> combined = TryFormatCombinedSingleNestedGroup(
            tokens,
            group,
            indentLevel,
            prefix,
            suffix
        )) {
            return *combined;
        }
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
        lines.push_back(Indent(indentLevel) + prefix + FormatInline(firstLineTokens));
        const bool splitForHeader = StartsWithControlFor(firstLineTokens) ||
            (StartsWithControlHeader(firstLineTokens) && ContainsTopLevelSeparator(inner, ';'));
        const bool indentElementChains = !StartsWithControlHeader(firstLineTokens);
        const char separator = splitForHeader ? ';' : ',';
        std::vector<std::vector<Token>> elements = SplitTopLevel(inner, separator);
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(inner, separator)) {
            std::vector<std::string> childLines = FormatRange(inner, indentLevel + 1, {}, {}, indentElementChains);
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
                std::vector<std::string> elementLines;
                if (tokens[group.open].text == "{" && separator == ',') {
                    elementLines = FormatInitializerElement(elements[index], indentLevel + 1, elementSuffix);
                } else {
                    elementLines =
                        FormatRange(elements[index], indentLevel + 1, {}, elementSuffix, indentElementChains);
                }
                AppendSplitElementLines(lines, elementLines, tokens[group.open].text == "{" && separator == ',');
            }
        }
        std::string closeLine = FormatInline(suffixTokens);
        AppendSuffix(closeLine, suffix);
        lines.push_back(Indent(indentLevel) + closeLine);
        return lines;
    }

    std::optional<std::vector<std::string>> TryFormatCombinedSingleNestedGroup(
        const std::vector<Token>& tokens,
        GroupPair group,
        int indentLevel,
        std::string_view prefix,
        std::string_view suffix
    ) const {
        std::vector<Token> outerPrefix(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(group.open + 1));
        std::vector<Token> inner(
            tokens.begin() + static_cast<std::ptrdiff_t>(group.open + 1),
            tokens.begin() + static_cast<std::ptrdiff_t>(group.close)
        );
        const std::optional<GroupPair> nested = FindFirstWrappableGroupPair(inner);
        if (!nested || IsEmptyGroupPair(inner, nested->open, nested->close)) {
            return std::nullopt;
        }
        std::vector<Token> beforeNested(inner.begin(), inner.begin() + static_cast<std::ptrdiff_t>(nested->open));
        if (ContainsTopLevelSeparator(beforeNested, ',')) {
            return std::nullopt;
        }
        if (NextSignificantIndex(inner, nested->close + 1) < inner.size()) {
            return std::nullopt;
        }
        std::vector<Token> nestedPrefix(inner.begin(), inner.begin() + static_cast<std::ptrdiff_t>(nested->open + 1));
        std::string firstLine = std::string(prefix) + FormatInline(outerPrefix) + FormatInline(nestedPrefix);
        if (!Fits(indentLevel, firstLine)) {
            return std::nullopt;
        }
        std::vector<Token> nestedInner(
            inner.begin() + static_cast<std::ptrdiff_t>(nested->open + 1),
            inner.begin() + static_cast<std::ptrdiff_t>(nested->close)
        );
        std::vector<Token> suffixTokens(tokens.begin() + static_cast<std::ptrdiff_t>(group.close), tokens.end());
        std::vector<std::string> lines;
        lines.push_back(Indent(indentLevel) + firstLine);
        std::vector<std::vector<Token>> elements = SplitTopLevel(nestedInner, ',');
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(nestedInner, ',')) {
            std::vector<std::string> childLines = FormatRange(nestedInner, indentLevel + 1, {}, {}, true);
            lines.insert(lines.end(), childLines.begin(), childLines.end());
        } else {
            for (size_t index = 0; index < elements.size(); ++index) {
                if (elements[index].empty()) {
                    continue;
                }
                std::string elementSuffix;
                if (index + 1 < elements.size()) {
                    elementSuffix = ",";
                }
                std::vector<std::string> elementLines;
                if (inner[nested->open].text == "{") {
                    elementLines = FormatInitializerElement(elements[index], indentLevel + 1, elementSuffix);
                } else {
                    elementLines = FormatRange(elements[index], indentLevel + 1, {}, elementSuffix, true);
                }
                AppendSplitElementLines(lines, elementLines, inner[nested->open].text == "{");
            }
        }
        std::vector<Token> closeTokens;
        closeTokens.push_back(inner[nested->close]);
        closeTokens.insert(closeTokens.end(), suffixTokens.begin(), suffixTokens.end());
        std::string closeLine = FormatInline(closeTokens);
        AppendSuffix(closeLine, suffix);
        lines.push_back(Indent(indentLevel) + closeLine);
        return lines;
    }

    std::vector<std::string> FormatMemberAccessChain(
        const std::vector<Token>& tokens,
        size_t memberAccess,
        int indentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        std::vector<Token> receiver(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(memberAccess));
        std::vector<Token> member(tokens.begin() + static_cast<std::ptrdiff_t>(memberAccess), tokens.end());
        std::vector<std::string> lines;
        std::string receiverLine = prefix + FormatInline(receiver);
        if (Fits(indentLevel, receiverLine)) {
            lines.push_back(Indent(indentLevel) + receiverLine);
        } else {
            std::vector<std::string> receiverLines = FormatRange(receiver, indentLevel, std::move(prefix), {});
            lines.insert(lines.end(), receiverLines.begin(), receiverLines.end());
        }
        std::vector<std::string> memberLines = FormatRange(member, indentLevel + 1, {}, std::move(suffix));
        lines.insert(lines.end(), memberLines.begin(), memberLines.end());
        return lines;
    }

    std::optional<std::vector<std::string>> TryFormatCombinedControlNestedCall(
        const std::vector<Token>& tokens,
        GroupPair group,
        int indentLevel,
        std::string_view prefix,
        std::string_view suffix
    ) const {
        if (tokens[group.open].text != "(") {
            return std::nullopt;
        }
        std::vector<Token> controlPrefix(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(group.open + 1));
        if (!StartsWithControlHeader(controlPrefix) || StartsWithControlFor(controlPrefix)) {
            return std::nullopt;
        }
        std::vector<Token> condition(
            tokens.begin() + static_cast<std::ptrdiff_t>(group.open + 1),
            tokens.begin() + static_cast<std::ptrdiff_t>(group.close)
        );
        const std::optional<GroupPair> nested = FindFirstGroupPair(condition);
        if (!nested || nested->open == 0 || condition[nested->open].text != "(") {
            return std::nullopt;
        }
        if (NextSignificantIndex(condition, nested->close + 1) < condition.size()) {
            return std::nullopt;
        }
        std::vector<Token> nestedPrefix(
            condition.begin(),
            condition.begin() + static_cast<std::ptrdiff_t>(nested->open + 1)
        );
        std::string firstLine = std::string(prefix) + FormatInline(controlPrefix) + FormatInline(nestedPrefix);
        if (!Fits(indentLevel, firstLine)) {
            return std::nullopt;
        }
        std::vector<Token> nestedInner(
            condition.begin() + static_cast<std::ptrdiff_t>(nested->open + 1),
            condition.begin() + static_cast<std::ptrdiff_t>(nested->close)
        );
        std::vector<Token> controlSuffix(tokens.begin() + static_cast<std::ptrdiff_t>(group.close), tokens.end());
        std::vector<std::string> lines;
        lines.push_back(Indent(indentLevel) + firstLine);
        std::vector<std::vector<Token>> elements = SplitTopLevel(nestedInner, ',');
        if (elements.size() <= 1 && !ContainsTopLevelSeparator(nestedInner, ',')) {
            std::vector<std::string> childLines = FormatRange(nestedInner, indentLevel + 1, {}, {}, true);
            lines.insert(lines.end(), childLines.begin(), childLines.end());
        } else {
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
                lines.insert(lines.end(), elementLines.begin(), elementLines.end());
            }
        }
        std::string closeLine = ")" + FormatInline(controlSuffix);
        AppendSuffix(closeLine, suffix);
        lines.push_back(Indent(indentLevel) + closeLine);
        return lines;
    }

    std::vector<std::string> FormatOperatorChain(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains
    ) const {
        const ChainKind chainKind = SelectChainKind(tokens);
        if (chainKind == ChainKind::Ternary) {
            return FormatTernaryChain(tokens, indentLevel, std::move(prefix), std::move(suffix), indentSplitChains);
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
                FormatChainPart(parts[index], partIndent, std::move(partPrefix), std::move(partSuffix));
            if (index + 2 == parts.size() && partLines.size() > 1 && TryAppendFinalChainPartToLastLine(
                partLines,
                parts[index + 1],
                suffix
            )) {
                lines.insert(lines.end(), partLines.begin(), partLines.end());
                break;
            }
            lines.insert(lines.end(), partLines.begin(), partLines.end());
        }
        return lines;
    }

    bool TryAppendFinalChainPartToLastLine(
        std::vector<std::string>& lines,
        const std::vector<Token>& finalPart,
        std::string_view suffix
    ) const {
        if (lines.empty() || finalPart.empty() || HasOriginalBlankSeparator(finalPart) || ShouldForceSplit(finalPart)) {
            return false;
        }
        const std::string inlineText = FormatInline(finalPart);
        if (inlineText.empty()) {
            return false;
        }
        std::string candidate = TrimRight(lines.back()) + " " + inlineText;
        AppendSuffix(candidate, suffix);
        if (static_cast<int>(candidate.size()) > config_.columnLimit) {
            return false;
        }
        lines.back() = std::move(candidate);
        return true;
    }

    std::vector<std::string> FormatTernaryPart(
        const std::vector<Token>& tokens,
        int indentLevel,
        int valueIndentLevel,
        std::string prefix,
        std::string suffix
    ) const {
        std::string inlineText = prefix + FormatInline(tokens);
        AppendSuffix(inlineText, suffix);
        if (Fits(indentLevel, inlineText)) {
            return {Indent(indentLevel) + inlineText};
        }
        const std::optional<size_t> question = FindTopLevelToken(tokens, "?");
        if (!question) {
            return FormatChainPart(tokens, indentLevel, std::move(prefix), std::move(suffix));
        }
        std::vector<Token> condition(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(*question + 1));
        std::string conditionLine = prefix + FormatInline(condition);
        if (!Fits(indentLevel, conditionLine)) {
            return FormatChainPart(tokens, indentLevel, std::move(prefix), std::move(suffix));
        }
        std::vector<Token> value(tokens.begin() + static_cast<std::ptrdiff_t>(*question + 1), tokens.end());
        std::vector<std::string> lines;
        lines.push_back(Indent(indentLevel) + conditionLine);
        std::vector<std::string> valueLines = FormatRange(value, valueIndentLevel, {}, std::move(suffix), true);
        lines.insert(lines.end(), valueLines.begin(), valueLines.end());
        return lines;
    }

    std::vector<std::string> FormatTernaryChain(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string prefix,
        std::string suffix,
        bool indentSplitChains
    ) const {
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
            std::vector<std::string> partLines = FormatTernaryPart(
                parts[index],
                partIndent,
                indentLevel + 1,
                std::move(partPrefix),
                std::move(partSuffix)
            );
            lines.insert(lines.end(), partLines.begin(), partLines.end());
        }
        return lines;
    }

    std::vector<std::string> FormatChainPart(
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
        if (std::optional<GroupPair> group = FindFirstWrappableGroupPair(tokens)) {
            return FormatSplitGroup(tokens, *group, indentLevel, std::move(prefix), std::move(suffix));
        }
        return {Indent(indentLevel) + inlineText};
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
        if (
            IsTemplateAngleCloseToken(tokens, prevIndex) &&
            (current == "(" || current == "*" || current == "&" || current == "&&" || IsNoSpaceBefore(current))
        ) {
            return false;
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
        if (prev == "{" && IsLambdaBodyOpenToken(tokens, prevIndex)) {
            return true;
        }
        if ((prev == "," || prev == ";") && current != ")" && current != "]" && current != ";") {
            return true;
        }
        if ((current == "++" || current == "--") && prev == ")") {
            return true;
        }
        if (current == "{" || IsNoSpaceBefore(current) || IsNoSpaceAfter(prev)) {
            return false;
        }
        if (current == "*" || current == "&" || current == "&&") {
            if (IsPointerOrReferenceDeclarator(tokens, index)) {
                return false;
            }
        }
        if (IsUnaryPrefixOperator(tokens, index) && IsUnaryPrefixOperator(tokens, prevIndex)) {
            return false;
        }
        if ((prev == "*" || prev == "&") && token.kind == TokenKind::Word) {
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
        return before != ")" && before != "]" && before != "}";
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
            (
                (tokens[last].text == "*" || tokens[last].text == "&" || tokens[last].text == "&&") &&
                    IsLikelyTypeBeforePointer(tokens, last)
            );
    }

    bool IsLikelyTypeBeforePointer(const std::vector<Token>& tokens, size_t index) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        if (!previous) {
            return false;
        }
        return IsLikelyTypeNameToken(tokens, *previous);
    }

    bool IsLikelyTypeNameToken(const std::vector<Token>& tokens, size_t index) const {
        const Token& token = tokens[index];
        if (IsTemplateAngleCloseToken(tokens, index)) {
            return true;
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
        if (!token.text.empty() && token.text.front() >= 'A' && token.text.front() <= 'Z') {
            return true;
        }
        const std::optional<size_t> beforeType = PreviousNonNewlineIndex(tokens, index);
        return beforeType && tokens[*beforeType].text == "::";
    }

    bool IsDecltypeCloseBeforePointer(const std::vector<Token>& tokens, size_t closeIndex) const {
        const std::optional<size_t> open = FindMatchingOpen(tokens, closeIndex);
        if (!open || *open == 0) {
            return false;
        }
        const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
        return beforeOpen && tokens[*beforeOpen].text == "decltype";
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
        return text == "(" ||
            text == "[" ||
            text == "{" ||
            text == "," ||
            text == "<" ||
            text == "*" ||
            text == "&" ||
            text == "&&" ||
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

    std::vector<std::string> FormatInitializerElement(
        const std::vector<Token>& tokens,
        int indentLevel,
        std::string_view suffix
    ) const {
        return FormatRange(tokens, indentLevel, {}, std::string(suffix), true);
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
        if (!HasLineComment(tokens)) {
            return false;
        }
        if (!LineCommentBeforeTopLevelStatementTerminator(tokens)) {
            return false;
        }
        return FindFirstWrappableGroupPair(tokens).has_value() || CanSplitOperatorChain(tokens);
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
        if (previousText == "else" || previousText == "try" || previousText == "do") {
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

    std::optional<size_t> FindTopLevelMemberAccess(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (
                index > 0 && depth == 0 && IsMemberAccessOperator(token.text) && IsMemberAccessSplitPoint(tokens, index)
            ) {
                return index;
            }
            UpdateDepth(token, depth);
        }
        return std::nullopt;
    }

    bool IsMemberAccessSplitPoint(const std::vector<Token>& tokens, size_t index) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        const size_t next = NextSignificantIndex(tokens, index + 1);
        return previous &&
            next < tokens.size() &&
            tokens[next].kind == TokenKind::Word &&
            HasTopLevelGroupBefore(tokens, index);
    }

    bool HasTopLevelGroupBefore(const std::vector<Token>& tokens, size_t before) const {
        int depth = 0;
        for (size_t index = 0; index < before; ++index) {
            if (depth == 0 && IsGroupOpen(tokens[index].text)) {
                return true;
            }
            UpdateDepth(tokens[index], depth);
        }
        return false;
    }

    std::optional<GroupPair> FindFirstGroupPair(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const std::string& text = tokens[index].text;
            if (IsGroupOpen(text) && depth == 0) {
                if (std::optional<size_t> close = FindMatchingClose(tokens, index)) {
                    if (IsNonWrappablePrefixGroup(tokens, index, *close)) {
                        UpdateDepth(tokens[index], depth);
                        continue;
                    }
                    return GroupPair{index, *close};
                }
            }
            UpdateDepth(tokens[index], depth);
        }
        return std::nullopt;
    }

    std::optional<GroupPair> FindFirstWrappableGroupPair(const std::vector<Token>& tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const std::string& text = tokens[index].text;
            if (IsGroupOpen(text) && depth == 0) {
                if (std::optional<size_t> close = FindMatchingClose(tokens, index)) {
                    if (
                        !IsEmptyGroupPair(tokens, index, *close) &&
                        !IsNonWrappablePrefixGroup(tokens, index, *close) &&
                        !IsFunctionPointerDeclaratorGroupOpen(tokens, index)
                    ) {
                        return GroupPair{index, *close};
                    }
                }
            }
            UpdateDepth(tokens[index], depth);
        }
        return std::nullopt;
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
        return SelectChainKind(tokens) != ChainKind::None;
    }

    ChainKind SelectChainKind(const std::vector<Token>& tokens) const {
        int depth = 0;
        bool hasTernary = false;
        bool hasLogical = false;
        bool hasBitwise = false;
        bool hasEquality = false;
        bool hasRelational = false;
        bool hasShift = false;
        bool hasAdditive = false;
        bool hasMultiplicative = false;
        for (size_t index = 0; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth != 0 || IsTemplateAngleToken(tokens, index)) {
                continue;
            }
            const std::string& text = tokens[index].text;
            if (text == "?") {
                hasTernary = true;
            } else if (text == "&&" && IsPointerOrReferenceDeclarator(tokens, index)) {
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
            } else if (text == "+" || text == "-") {
                hasAdditive = true;
            } else if ((text == "*" || text == "&") && IsPointerOrReferenceDeclarator(tokens, index)) {
                continue;
            } else if (text == "*" || text == "/" || text == "%") {
                hasMultiplicative = true;
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
        if (hasAdditive) {
            return ChainKind::Additive;
        }
        if (hasMultiplicative) {
            return ChainKind::Multiplicative;
        }
        return ChainKind::None;
    }

    bool IsChainBreakOperator(const std::vector<Token>& tokens, size_t index, ChainKind chainKind) const {
        if (chainKind == ChainKind::None || IsTemplateAngleToken(tokens, index)) {
            return false;
        }
        if ((tokens[index].text == "*" || tokens[index].text == "&") && IsPointerOrReferenceDeclarator(tokens, index)) {
            return false;
        }
        if (tokens[index].text == "&&" && IsPointerOrReferenceDeclarator(tokens, index)) {
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
                return text == "+" || text == "-";
            case ChainKind::Multiplicative:
                return text == "*" || text == "/" || text == "%";
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
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        if (!previous || tokens[*previous].text != ")") {
            return false;
        }
        const std::optional<size_t> open = FindMatchingOpen(tokens, *previous);
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
            if (IsLambdaBodyOpenToken(tokens, index)) {
                return index;
            }
        }
        return std::nullopt;
    }

    bool IsPointerOrReferenceDeclarator(const std::vector<Token>& tokens, size_t index) const {
        if (index >= tokens.size() || (
            tokens[index].text != "*" && tokens[index].text != "&" && tokens[index].text != "&&"
        )) {
            return false;
        }
        const size_t nextIndex = NextSignificantIndex(tokens, index + 1);
        if (nextIndex >= tokens.size()) {
            return false;
        }
        const Token* next = &tokens[nextIndex];
        const bool beforeDeclaratorName = next->kind == TokenKind::Word;
        const bool beforeTemplateClose = IsTemplateAngleCloseToken(tokens, nextIndex);
        const bool beforeStructuredBinding = tokens[index].text != "*" && next->text == "[";
        const bool beforeUnnamedDeclaratorEnd = next->text == ")" || next->text == "," || next->text == "=";
        const bool beforeFunctionPointerDeclarator =
            tokens[index].text == "*" && IsFunctionPointerDeclaratorGroupOpen(tokens, nextIndex);
        const bool beforeDeclarator =
            beforeDeclaratorName ||
            beforeTemplateClose ||
            beforeStructuredBinding ||
            beforeUnnamedDeclaratorEnd ||
            beforeFunctionPointerDeclarator;
        return beforeDeclarator &&
            IsLikelyTypeBeforePointer(tokens, index) &&
            IsLikelyDeclaratorContextBeforePointer(tokens, index);
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
            if (!sawSignificant) {
                if (tokens[inner].text != "*" && tokens[inner].text != "&" && tokens[inner].text != "&&") {
                    return false;
                }
                sawSignificant = true;
            }
            if (tokens[inner].text == "*" || tokens[inner].text == "&" || tokens[inner].text == "&&") {
                sawPointer = true;
            }
        }
        return sawPointer;
    }

    bool IsFunctionPointerDeclaratorContextBeforeGroup(const std::vector<Token>& tokens, size_t index) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        if (!previous) {
            return false;
        }
        if (
            (tokens[*previous].text == "*" || tokens[*previous].text == "&" || tokens[*previous].text == "&&") &&
            IsPointerOrReferenceDeclarator(tokens, *previous)
        ) {
            return true;
        }
        return IsLikelyTypeBeforePointer(tokens, index) && IsLikelyDeclaratorContextBeforePointer(tokens, index);
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
            first == "do" ||
            first == "else"
        ) {
            return true;
        }
        if (
            ContainsWord(pendingTokens_, "class") ||
            ContainsWord(pendingTokens_, "struct") ||
            ContainsWord(pendingTokens_, "enum")
        ) {
            return true;
        }
        if (IsBracedConstructorExpressionOpen()) {
            return false;
        }
        if (ContainsTopLevelAssignment(pendingTokens_)) {
            return false;
        }
        return EndsWithMatchingParen(pendingTokens_) || ContainsTopLevelToken(pendingTokens_, ")");
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
        if (ContainsWord(tokens, "enum")) {
            return BlockKind::EnumDeclaration;
        }
        if (ContainsWord(tokens, "class") || ContainsWord(tokens, "struct") || ContainsWord(tokens, "enum")) {
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
        return token.kind == TokenKind::Word || token.text == "*" || token.text == "&" || token.text == "&&";
    }

    DeclarationKind ClassifySemicolonDeclaration(const std::vector<Token>& tokens) const {
        if (!IsDeclarationContext() || tokens.empty()) {
            return DeclarationKind::None;
        }
        const std::string& first = tokens.front().text;
        if (first == "using" || first == "typedef" || first == "static_assert") {
            return DeclarationKind::Field;
        }
        if (ContainsWord(tokens, "class") || ContainsWord(tokens, "struct") || ContainsWord(tokens, "enum")) {
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
            first == "do" ||
            first == "else"
        ) {
            return false;
        }
        return EndsWithMatchingParen(tokens) || ContainsTopLevelToken(tokens, ")");
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

    bool EndsWithMatchingParen(const std::vector<Token>& tokens) const {
        return !tokens.empty() && tokens.back().text == ")";
    }

    bool ContainsTopLevelToken(const std::vector<Token>& tokens, std::string_view tokenText) const {
        return FindTopLevelToken(tokens, tokenText).has_value();
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
            } else if (depth == 0 && IsTemplateScanBoundary(text)) {
                return std::nullopt;
            }
        }
        return std::nullopt;
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

    static bool IsMemberAccessOperator(std::string_view text) {
        return text == "." || text == "->" || text == ".*" || text == "->*";
    }

    std::vector<std::string> outputLines_;
    std::vector<Token> pendingTokens_;
    std::string pendingPrefix_;
    std::vector<BlockState> blockStack_;
    const FormatterConfig& config_;
    int indentLevel_ = 0;
    int groupDepth_ = 0;
    int caseBodyIndentLevel_ = -1;
    DeclarationKind previousDeclarationKind_ = DeclarationKind::None;
    bool previousDeclarationWasMultilineField_ = false;
    bool pendingLogicalBlank_ = false;
    bool pendingPreprocessorBlank_ = false;
    bool pendingPragmaOnceBlank_ = false;
    bool pendingUndefBlank_ = false;
    bool allowOriginalBlank_ = false;
    bool justEmittedCaseLabel_ = false;
};

std::optional<FormatterConfig> LoadFormatterConfig(const std::string& root, std::string& error) {
    std::string configPath = tools::lint::JoinPath(root, "tools/format_config.json");
    std::optional<std::string> text = tools::lint::ReadFileText(configPath);
    if (!text) {
        const std::string currentRoot = tools::lint::CurrentDirectoryAbsolute();
        const std::string fallbackPath = tools::lint::JoinPath(currentRoot, "tools/format_config.json");
        if (tools::lint::NormalizePathKey(fallbackPath) != tools::lint::NormalizePathKey(configPath)) {
            text = tools::lint::ReadFileText(fallbackPath);
            if (text) {
                configPath = fallbackPath;
            }
        }
    }
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
        if (const tools::lint::JsonValue* macroCategories = configJson.Find("macro_categories")) {
            if (const tools::lint::JsonValue* parameters = macroCategories->Find("statement_like_parameters")) {
                for (const tools::lint::JsonValue& parameter : parameters->AsArray()) {
                    config.statementLikeMacroParameters.push_back(parameter.AsString());
                }
            }
        }
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
    result.formatted = formatter.Format(Tokenize(includeSorted));
    result.changed = normalized != result.formatted;
    return result;
}

bool IsEligibleCppPath(std::string_view root, std::string_view path) {
    const std::string relative = tools::lint::NormalizeSeparators(tools::lint::RelativePath(path, root));
    if (!(tools::lint::StartsWith(relative, "src/") || tools::lint::StartsWith(relative, "tests/"))) {
        return false;
    }
    if (tools::lint::StartsWith(relative, "src/vendor/") || tools::lint::StartsWith(relative, "src/tools/vendor/")) {
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

bool IsInsideRootOrRoot(const std::string& root, const std::string& path) {
    const std::string normalizedRoot = tools::lint::NormalizeSeparators(tools::lint::AbsolutePath(root));
    const std::string normalizedPath = tools::lint::NormalizeSeparators(tools::lint::AbsolutePath(path));
    const std::string lowerRoot = tools::lint::ToLowerAscii(normalizedRoot);
    const std::string lowerPath = tools::lint::ToLowerAscii(normalizedPath);
    return lowerPath == lowerRoot || tools::lint::StartsWith(lowerPath, lowerRoot + "/");
}

std::vector<std::string> GetAllFiles(const std::string& root) {
    const std::optional<std::vector<std::string>> files = RunGit(
        root,
        {
            "-c",
            "core.quotepath=off",
            "-c",
            "core.safecrlf=false",
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "--",
            "*.cpp",
            "*.h"
        }
    );
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
        if (std::optional<std::vector<std::string>> changed = RunGit(
            root,
            {"-c", "core.safecrlf=false", "diff", "--name-only", "--diff-filter=ACMR", "HEAD", "--", "*.cpp", "*.h"}
        )) {
            paths.insert(paths.end(), changed->begin(), changed->end());
        }
    } else if (std::optional<std::vector<std::string>> staged = RunGit(
        root,
        {"-c", "core.safecrlf=false", "diff", "--name-only", "--diff-filter=ACMR", "--cached", "--", "*.cpp", "*.h"}
    )) {
        paths.insert(paths.end(), staged->begin(), staged->end());
    }
    if (std::optional<std::vector<std::string>> untracked = RunGit(
        root,
        {"-c", "core.safecrlf=false", "ls-files", "--others", "--exclude-standard", "--", "*.cpp", "*.h"}
    )) {
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
        tools::lint::ToLowerAscii(normalizedFile),
        tools::lint::ToLowerAscii(normalizedRoot) + "/"
    )) {
        return std::nullopt;
    }
    if (!IsEligibleCppPath(root, fullPath)) {
        return std::nullopt;
    }
    return fullPath;
}

std::optional<std::vector<std::string>> ResolveTargetPathFiles(const std::string& root, std::string_view targetPath) {
    if (targetPath.empty()) {
        return std::nullopt;
    }
    const std::string fullPath = tools::lint::AbsolutePath(tools::lint::JoinPath(root, targetPath));
    if (!IsInsideRootOrRoot(root, fullPath)) {
        return std::nullopt;
    }
    if (tools::lint::FileExists(fullPath)) {
        if (!IsEligibleCppPath(root, fullPath)) {
            return std::nullopt;
        }
        return std::vector<std::string>{fullPath};
    }
    if (!tools::lint::DirectoryExists(fullPath)) {
        return std::nullopt;
    }
    std::vector<std::string> files;
    for (const std::string& path : tools::lint::RecursiveFiles(fullPath)) {
        if (IsEligibleCppPath(root, path)) {
            files.push_back(path);
        }
    }
    return UniqueSorted(std::move(files));
}

std::optional<Options> ParseOptions(int argc, char * *argv) {
    Options options;
    options.root = tools::lint::CurrentDirectoryAbsolute();
    for (int index = 0; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "fix") {
            options.mode = Mode::Fix;
        } else if (arg == "changed") {
            if (!options.targetPath.empty()) {
                return std::nullopt;
            }
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
        } else if (arg == "--path") {
            if (index + 1 >= argc) {
                return std::nullopt;
            }
            if (options.scope == Scope::Changed) {
                return std::nullopt;
            }
            options.targetPath = argv[++index];
            options.scope = Scope::Path;
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
    if (!options.targetFile.empty() && !options.targetPath.empty()) {
        return std::nullopt;
    }
    if (!options.targetFile.empty() && options.scope == Scope::Changed) {
        return std::nullopt;
    }
    if (!options.targetPath.empty() && options.stdoutMode) {
        return std::nullopt;
    }
    if (!options.targetPath.empty() && options.scope == Scope::Changed) {
        return std::nullopt;
    }
    return options;
}

}  // namespace

int RunFormat(int argc, char * *argv) {
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
    } else if (!options.targetPath.empty()) {
        std::optional<std::vector<std::string>> targetFiles = ResolveTargetPathFiles(options.root, options.targetPath);
        if (!targetFiles) {
            std::fprintf(stderr, "--path target is not an eligible formatter input: %s\n", options.targetPath.c_str());
            return 2;
        }
        files = *targetFiles;
    } else if (options.scope == Scope::Changed) {
        files = GetChangedFiles(options.root);
    } else {
        files = GetAllFiles(options.root);
    }
    if (files.empty()) {
        if (options.scope == Scope::Changed) {
            std::printf("No eligible changed C++ source files were found.\n");
            return 0;
        }
        if (options.scope == Scope::Path) {
            std::fprintf(
                stderr,
                "No eligible C++ source files were found under --path target: %s\n",
                options.targetPath.c_str()
            );
            return 1;
        }
        if (options.scope == Scope::All) {
            std::fprintf(stderr, "No non-vendored C++ source files were found.\n");
            return 1;
        }
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
                " (%d file%s parsed with tree-sitter errors)",
                parseErrorCount,
                parseErrorCount == 1 ? "" : "s"
            );
        }
        std::printf(
            ". Checked %d file%s in %s.\n",
            static_cast<int>(files.size()),
            files.size() == 1 ? "" : "s",
            FormatElapsed(std::chrono::steady_clock::now() - start).c_str()
        );
        return 1;
    }
    const char* mode = options.mode == Mode::Fix ? "Formatted" : "Checked";
    const char* scope = "all";
    if (options.scope == Scope::Changed) {
        scope = "changed";
    } else if (options.scope == Scope::Path) {
        scope = "path";
    }
    std::printf(
        "%s %d %s file%s with native tree-sitter formatter in %s.",
        mode,
        static_cast<int>(files.size()),
        scope,
        files.size() == 1 ? "" : "s",
        FormatElapsed(std::chrono::steady_clock::now() - start).c_str()
    );
    if (parseErrorCount > 0) {
        std::printf(" %d file%s parsed with tree-sitter errors.", parseErrorCount, parseErrorCount == 1 ? "" : "s");
    }
    std::printf("\n");
    return 0;
}
