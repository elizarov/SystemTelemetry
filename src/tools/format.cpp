#include "tools/format.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <tree_sitter_cpp.h>
#include <vector>

#include "tools/impl/lint_common.h"

namespace {

constexpr int kColumnLimit = 120;
constexpr int kIndentWidth = 4;
constexpr int kContinuationIndentWidth = 4;

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

class SimpleFormatter {
public:
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
                    EmitLineComment(token.text);
                    break;
                case TokenKind::BlockComment:
                    EmitBlockComment(token.text);
                    break;
                default:
                    EmitCodeToken(tokens, index);
                    break;
            }
        }
        FlushLine();
        while (!outputLines_.empty() && outputLines_.back().empty()) {
            outputLines_.pop_back();
        }
        return JoinOutput();
    }

private:
    void EnsureLine(int extraIndent = 0) {
        if (!currentLine_.empty()) {
            return;
        }
        currentIndent_ = std::max(0, indentLevel_ + extraIndent) * kIndentWidth;
        currentLine_ = Spaces(currentIndent_);
    }

    void FlushLine() {
        const std::string trimmed = TrimRight(currentLine_);
        if (!trimmed.empty()) {
            outputLines_.push_back(trimmed);
        }
        currentLine_.clear();
        previousToken_.reset();
        allowOriginalBlank_ = true;
    }

    void EmitBlankLine() {
        FlushLine();
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
        if (newlineCount > 1 && currentLine_.empty()) {
            EmitBlankLine();
        }
    }

    void AppendRaw(std::string_view text) {
        EnsureLine();
        currentLine_ += text;
    }

    void AppendCodeToken(const Token& token) {
        EnsureLine();
        if (NeedsSpaceBefore(token)) {
            currentLine_.push_back(' ');
        }
        const int projected = static_cast<int>(currentLine_.size() + token.text.size());
        if (projected > kColumnLimit && static_cast<int>(currentLine_.size()) > currentIndent_) {
            WrapLine();
        }
        currentLine_ += token.text;
        previousToken_ = token;
    }

    void WrapLine() {
        FlushLine();
        currentIndent_ = std::max(0, indentLevel_) * kIndentWidth + kContinuationIndentWidth;
        currentLine_ = Spaces(currentIndent_);
    }

    bool NeedsSpaceBefore(const Token& token) const {
        if (!previousToken_) {
            return false;
        }
        const std::string_view current = token.text;
        const std::string_view previous = previousToken_->text;
        if (current == "(" && previousToken_->kind == TokenKind::Word) {
            return IsControlKeyword(previous);
        }
        if (IsNoSpaceBefore(current) || IsNoSpaceAfter(previous) || IsPrefixOperator(current) ||
            IsPrefixOperator(previous)) {
            return false;
        }
        if (IsBinaryOperator(current) || IsBinaryOperator(previous)) {
            return true;
        }
        if ((previous == "," || previous == ";" || previous == "*" || previous == "&") && current != ")" &&
            current != "]") {
            return true;
        }
        if ((previous == ")" || previous == "]") && token.kind == TokenKind::Word) {
            return true;
        }
        return IsWordLike(*previousToken_) && IsWordLike(token);
    }

    void EmitCodeToken(const std::vector<Token>& tokens, size_t& index) {
        const Token& token = tokens[index];
        if (token.text == "}") {
            FlushLine();
            indentLevel_ = std::max(0, indentLevel_ - 1);
            EnsureLine();
            currentLine_ += "}";
            previousToken_ = token;
            if (index + 1 < tokens.size() && tokens[index + 1].text == ";") {
                currentLine_ += ";";
                ++index;
                FlushLine();
            } else if (index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::Word &&
                (tokens[index + 1].text == "else" || tokens[index + 1].text == "while")) {
                currentLine_ += " ";
            } else {
                FlushLine();
            }
            return;
        }
        if (token.text == "{") {
            if (!currentLine_.empty() && currentLine_.size() > static_cast<size_t>(currentIndent_) &&
                currentLine_.back() != ' ') {
                currentLine_.push_back(' ');
            }
            AppendRaw("{");
            FlushLine();
            ++indentLevel_;
            return;
        }
        if (token.text == "(" || token.text == "[") {
            ++groupDepth_;
            AppendCodeToken(token);
            return;
        }
        if (token.text == ")" || token.text == "]") {
            groupDepth_ = std::max(0, groupDepth_ - 1);
            AppendCodeToken(token);
            return;
        }
        if (token.text == ";") {
            AppendCodeToken(token);
            if (groupDepth_ == 0 && !(index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::LineComment)) {
                FlushLine();
            }
            return;
        }
        if (token.text == ",") {
            AppendCodeToken(token);
            if (groupDepth_ > 0 && static_cast<int>(currentLine_.size()) > kColumnLimit - 20) {
                WrapLine();
            }
            return;
        }
        if (token.text == ":" && IsLabelColon()) {
            currentLine_ = TrimRight(currentLine_);
            currentLine_ += ":";
            previousToken_ = token;
            FlushLine();
            return;
        }
        AppendCodeToken(token);
    }

    void EmitLineComment(std::string_view text) {
        if (currentLine_.empty()) {
            EnsureLine();
            currentLine_ += TrimLeft(text);
        } else {
            currentLine_ = TrimRight(currentLine_);
            currentLine_ += "  ";
            currentLine_ += TrimLeft(text);
        }
        FlushLine();
    }

    void EmitBlockComment(std::string_view text) {
        if (text.find('\n') == std::string_view::npos) {
            AppendCodeToken({TokenKind::BlockComment, std::string(text)});
            return;
        }
        FlushLine();
        std::vector<std::string> lines = tools::lint::SplitLines(text);
        for (std::string& line : lines) {
            outputLines_.push_back(Spaces(indentLevel_ * kIndentWidth) + TrimRight(TrimLeft(line)));
        }
        previousToken_.reset();
    }

    void EmitPreprocessor(std::string_view text) {
        FlushLine();
        std::vector<std::string> lines = tools::lint::SplitLines(text);
        for (std::string& line : lines) {
            outputLines_.push_back(TrimRight(TrimLeft(line)));
        }
        previousToken_.reset();
    }

    std::string JoinOutput() const {
        std::string result;
        for (const std::string& line : outputLines_) {
            result += line;
            result.push_back('\n');
        }
        return result;
    }

    bool IsLabelColon() const {
        if (!previousToken_) {
            return false;
        }
        if (previousToken_->text == "public" || previousToken_->text == "private" ||
            previousToken_->text == "protected" || previousToken_->text == "default") {
            return true;
        }
        const std::string trimmed = tools::lint::Trim(currentLine_);
        return tools::lint::StartsWith(trimmed, "case ");
    }

    std::vector<std::string> outputLines_;
    std::string currentLine_;
    std::optional<Token> previousToken_;
    int indentLevel_ = 0;
    int groupDepth_ = 0;
    int currentIndent_ = 0;
    bool allowOriginalBlank_ = false;
};

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

FileFormatResult FormatOneText(std::string_view text) {
    const std::string normalized = NormalizeLineEndings(text);
    const ParseResult parse = ParseCpp(normalized);
    if (!parse.ok) {
        return {.ok = false, .error = "tree-sitter parser setup failed"};
    }
    SimpleFormatter formatter;
    FileFormatResult result;
    result.parseHadErrors = parse.hasErrors;
    result.parseErrorNodeType = parse.errorNodeType;
    result.parseErrorLine = parse.errorLine;
    result.parseErrorColumn = parse.errorColumn;
    result.parseErrorSnippet = parse.errorSnippet;
    result.formatted = formatter.Format(Tokenize(normalized));
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
        FileFormatResult result = FormatOneText(*text);
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
