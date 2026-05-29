#include "tools/format.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <io.h>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "tools/impl/format_args.h"
#include "tools/impl/format_config.h"
#include "tools/impl/format_model.h"
#include "tools/impl/format_model_parse.h"
#include "tools/impl/format_pretty_printer.h"
#include "tools/impl/tools_common.h"

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

size_t FormatWorkerCount(size_t workSize) {
    if (workSize <= 1) {
        return workSize;
    }
    const unsigned int hardware = std::thread::hardware_concurrency();
    const size_t workerCount = hardware == 0 ? 4 : static_cast<size_t>(hardware);
    return std::min(workSize, std::max<size_t>(1, workerCount));
}

void RunFormatWorker(
    const std::vector<ResolvedFileFormat>& work,
    std::vector<CompletedFileFormat>& completed,
    std::atomic<size_t>& nextIndex,
    std::atomic<size_t>& completedCount
) {
    while (true) {
        const size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
        if (index >= work.size()) {
            return;
        }

        const ResolvedFileFormat& item = work[index];
        CompletedFileFormat result;
        result.pending.file = item.file;
        std::optional<std::string> text = tools::ReadFileText(item.file);
        if (!text) {
            result.readFailed = true;
            result.pending.result.ok = false;
        } else {
            result.hasPending = true;
            result.pending.result = FormatSourceText(*text, *item.config, item.file);
        }
        completed[index] = std::move(result);
        completedCount.fetch_add(1, std::memory_order_relaxed);
    }
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
    const std::string currentDirectory = tools::CurrentDirectoryAbsolute();
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
    std::vector<PendingFileFormat> pendingResults;
    const bool showProgress = _isatty(_fileno(summary)) != 0;
    size_t previousProgressLength = 0;
    std::vector<ResolvedFileFormat> work;
    work.reserve(options.files.size());

    for (int index = 0; index < static_cast<int>(options.files.size()); ++index) {
        const std::string file = tools::AbsolutePath(options.files[static_cast<size_t>(index)]);
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

    std::atomic<size_t> nextIndex = 0;
    std::atomic<size_t> completedCount = 0;
    const size_t workerCount = FormatWorkerCount(work.size());
    std::vector<CompletedFileFormat> completed(work.size());
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
        workers.emplace_back(
            RunFormatWorker,
            std::cref(work),
            std::ref(completed),
            std::ref(nextIndex),
            std::ref(completedCount)
        );
    }
    while (showProgress && completedCount.load(std::memory_order_relaxed) < work.size()) {
        const std::string progress = "format " +
            std::to_string(completedCount.load(std::memory_order_relaxed)) +
            "/" +
            std::to_string(work.size()) +
            " files";
        const std::string padding(
            previousProgressLength > progress.size() ? previousProgressLength - progress.size() : 0,
            ' '
        );
        std::fprintf(summary, "\r%s%s", progress.c_str(), padding.c_str());
        std::fflush(summary);
        previousProgressLength = progress.size();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    if (showProgress && previousProgressLength > 0) {
        std::fprintf(summary, "\r%s\r", std::string(previousProgressLength, ' ').c_str());
        std::fflush(summary);
    }

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
                if (!tools::WriteFileText(pending.file, ToFileLineEndings(pending.result.formatted))) {
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
