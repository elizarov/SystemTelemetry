#include "tools/format.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <io.h>
#include <optional>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <tree_sitter_cpp.h>
#include <vector>

#include "tools/format_args.h"
#include "tools/format_config.h"
#include "tools/format_lexer.h"
#include "tools/format_model.h"
#include "tools/format_model_builder.h"
#include "tools/format_pretty_printer.h"
#include "tools/impl/lint_common.h"

namespace {

using tools::format::AdvanceNewline;
using tools::format::AppendSourceTrivia;
using tools::format::AppendTreeChildTokens;
using tools::format::BuildSourceLayoutRoot;
using tools::format::DropTrailingCommas;
using tools::format::FormatMode;
using tools::format::FormatModel;
using tools::format::FormatModelText;
using tools::format::FormatOptions;
using tools::format::FormatterConfig;
using tools::format::IsCommentOrNewline;
using tools::format::kNoTokenIndex;
using tools::format::Token;
using tools::format::TokenKind;
using tools::format::TokenSpan;
using tools::format::IsNewlineByte;
using tools::format::ParseTreeResult;
using tools::format::SortIncludeTokens;

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
    model.layout = BuildSourceLayoutRoot(root, model.tokens);
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

FileFormatResult FormatOneText(std::string_view text, const FormatterConfig& config, std::string_view sourcePath) {
    FormatModel model = BuildFormatModel(text, config, sourcePath);
    if (!model.parse.ok) {
        return {.ok = false, .error = "tree-sitter parser setup failed"};
    }
    FileFormatResult result;
    result.parseHadErrors = model.parse.hasErrors;
    result.parseErrorNodeType = model.parse.errorNodeType;
    result.parseErrorLine = model.parse.errorLine;
    result.parseErrorColumn = model.parse.errorColumn;
    result.parseErrorSnippet = model.parse.errorSnippet;
    result.formatted = FormatModelText(config, model);
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
