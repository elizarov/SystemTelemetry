#include "tools/format_model_builder.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tools/impl/lint_common.h"

namespace tools::format {

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

SourceLayoutNode BuildSourceLayoutRoot(TSNode root, const std::vector<Token>& tokens) {
    SourceLayoutNode treeRoot;
    treeRoot.kind = SourceLayoutKind::Root;
    treeRoot.begin = 0;
    treeRoot.end = tokens.size();
    const SourceTokenLookup lookup = BuildSourceTokenLookup(tokens);
    size_t order = 0;
    AppendSourceLayoutNodes(root, lookup, treeRoot, 0, order);
    return treeRoot;
}

void AnnotateTokenIndexesAndGroups(std::vector<Token>& tokens);

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

}  // namespace tools::format
