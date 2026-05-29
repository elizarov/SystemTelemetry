#include "tools/format.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tools/impl/format_args.h"
#include "tools/impl/format_config.h"
#include "tools/impl/format_model.h"
#include "tools/impl/format_model_parse.h"
#include "tools/impl/format_pretty_printer.h"
#include "tools/impl/tools_common.h"
#include "tools/impl/tools_parallel.h"
#include "tools/impl/tools_progress.h"
#include "util/file_path.h"

namespace {

struct PendingFileFormat {
    std::string file;
    SourceFormatResult result;
};

struct ResolvedFileFormat {
    std::string file;
    const FormatterConfig* config = nullptr;
};

struct CompletedFileFormat {
    PendingFileFormat pending;
    bool hasPending = false;
    bool readFailed = false;
};

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

size_t AdvanceNewline(std::string_view text, size_t index) {
    if (index < text.size() && text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
        return index + 2;
    }
    return std::min(index + 1, text.size());
}

bool IsNewlineByte(char ch) {
    return ch == '\r' || ch == '\n';
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

}  // namespace

SourceFormatResult FormatSourceText(std::string_view text, const FormatterConfig& config, std::string_view sourcePath) {
    FormatModel model = ParseFormatModel(text);
    SourceFormatResult result;
    if (!model.parse.ok) {
        result.ok = false;
        result.error = model.parse.error.empty() ? "tree-sitter parser setup failed" : model.parse.error;
        return result;
    }
    result.formatted = FormatModelText(config, model, sourcePath);
    result.changed = model.sourceText != nullptr && !TextMatchesFormattedOutput(*model.sourceText, result.formatted);
    return result;
}

namespace {

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

std::string CompletedFileText(int completedCount, size_t totalCount) {
    std::string text = std::to_string(completedCount);
    if (completedCount != static_cast<int>(totalCount)) {
        text += "/" + std::to_string(totalCount);
    }
    text += totalCount == 1 ? " file" : " files";
    return text;
}

void PrintFormatSummary(
    FILE* output,
    const char* verb,
    int processedCount,
    size_t totalCount,
    int changedCount,
    int ignoredCount,
    int parseErrorCount,
    std::chrono::steady_clock::time_point start
) {
    const std::string completedFiles = CompletedFileText(processedCount, totalCount);
    std::fprintf(
        output,
        "%s %s in %s.",
        verb,
        completedFiles.c_str(),
        FormatToolElapsed(std::chrono::steady_clock::now() - start).c_str()
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
    std::optional<FormatOptions> parsed = ParseFormatArgs(argc, argv, optionsError);
    if (!parsed) {
        if (!optionsError.empty()) {
            std::fprintf(stderr, "%s\n", optionsError.c_str());
        }
        PrintFormatUsage(stderr);
        return 2;
    }
    const FormatOptions& options = *parsed;
    if (options.help) {
        PrintFormatUsage(stdout);
        return 0;
    }

    FormatStyleCache styleCache(options.explicitStylePath);
    const std::string currentDirectory = AbsolutePath(CurrentDirectoryPath().string());
    FILE* summary = SummaryStream(options);

    if (options.files.empty() && !options.fileListProvided) {
        std::string error;
        const FormatterConfig* config = styleCache.ConfigForPath(currentDirectory, error);
        if (config == nullptr) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 2;
        }
        SourceFormatResult result = FormatSourceText(ReadStdinText(), *config, "<stdin>");
        if (!result.ok) {
            std::fprintf(stderr, "<stdin>: %s\n", result.error.c_str());
            return 1;
        }
        if (options.mode == FormatMode::DryRun && result.changed) {
            std::fprintf(
                summary,
                "Formatting is required for stdin. Checked stdin in %s.\n",
                FormatToolElapsed(std::chrono::steady_clock::now() - start).c_str()
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
            FormatToolElapsed(std::chrono::steady_clock::now() - start).c_str()
        );
        return 0;
    }

    bool failed = false;
    int parseErrorCount = 0;
    int changedCount = 0;
    int ignoredCount = 0;
    int processedCount = 0;
    std::vector<PendingFileFormat> pendingResults;
    std::vector<ResolvedFileFormat> work;
    work.reserve(options.files.size());

    for (int index = 0; index < static_cast<int>(options.files.size()); ++index) {
        const std::string file = AbsolutePath(options.files[static_cast<size_t>(index)]);
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
        work.push_back({file, config});
    }

    std::vector<CompletedFileFormat> completed(work.size());
    ToolFileProgress progress(summary, "format", work.size(), start, true);
    RunToolParallelFor(work.size(), options.concurrency, &progress, [&](size_t index) {
        const ResolvedFileFormat& item = work[index];
        CompletedFileFormat result;
        result.pending.file = item.file;
        std::optional<std::string> text = ReadFileBinary(item.file);
        if (!text) {
            result.readFailed = true;
            result.pending.result.ok = false;
        } else {
            result.hasPending = true;
            result.pending.result = FormatSourceText(*text, *item.config, item.file);
        }
        completed[index] = std::move(result);
    });

    for (CompletedFileFormat& completedFormat : completed) {
        const std::string& file = completedFormat.pending.file;
        if (completedFormat.readFailed) {
            std::fprintf(stderr, "Failed to read %s\n", file.c_str());
            failed = true;
            continue;
        }
        if (!completedFormat.hasPending) {
            continue;
        }
        ++processedCount;
        SourceFormatResult& result = completedFormat.pending.result;
        if (!result.ok) {
            std::fprintf(stderr, "%s: %s\n", file.c_str(), result.error.c_str());
            ++parseErrorCount;
            failed = true;
            continue;
        }
        if (result.changed) {
            ++changedCount;
            if (options.mode == FormatMode::DryRun) {
                failed = true;
            }
        }
        pendingResults.push_back(std::move(completedFormat.pending));
    }
    if (!failed) {
        for (const PendingFileFormat& pending : pendingResults) {
            if (options.mode == FormatMode::Stdout) {
                std::fwrite(pending.result.formatted.data(), 1, pending.result.formatted.size(), stdout);
            } else if (options.mode == FormatMode::InPlace && pending.result.changed) {
                if (!WriteFileBinary(pending.file, ToFileLineEndings(pending.result.formatted))) {
                    std::fprintf(stderr, "Failed to write %s\n", pending.file.c_str());
                    failed = true;
                }
            }
        }
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
        const std::string completedFiles = CompletedFileText(processedCount, work.size());
        std::fprintf(
            summary,
            ". Checked %s in %s.\n",
            completedFiles.c_str(),
            FormatToolElapsed(std::chrono::steady_clock::now() - start).c_str()
        );
        return 1;
    }
    const char* verb = options.mode == FormatMode::DryRun ? "Checked" : "Formatted";
    PrintFormatSummary(
        summary,
        verb,
        processedCount,
        work.size(),
        options.mode == FormatMode::DryRun ? changedCount : 0,
        ignoredCount,
        parseErrorCount,
        start
    );
    return 0;
}
