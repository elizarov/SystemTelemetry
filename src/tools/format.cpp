#include "tools/format.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <io.h>
#include <optional>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <tree_sitter_cpp.h>

#include "tools/format_args.h"
#include "tools/format_config.h"
#include "tools/format_lexer.h"
#include "tools/format_model.h"
#include "tools/format_model_builder.h"
#include "tools/format_pretty_printer.h"
#include "tools/impl/lint_common.h"

namespace {

using tools::format::AdvanceNewline;
using tools::format::AddRequiredControlBraces;
using tools::format::AppendSourceTrivia;
using tools::format::AppendTreeChildTokens;
using tools::format::BuildSourceLayoutRoot;
using tools::format::DropTrailingCommas;
using tools::format::FormatMode;
using tools::format::FormatModel;
using tools::format::FormatModelText;
using tools::format::FormatOptions;
using tools::format::FormatterConfig;
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
