#include "tools/format_args.h"

#include <cstdio>

#include "tools/impl/lint_common.h"

namespace tools::format {

namespace {

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::optional<std::string> ParseStyleValue(std::string_view value, std::string& error) {
    if (value == "file") {
        return std::nullopt;
    }
    constexpr std::string_view filePrefix = "file:";
    if (StartsWith(value, filePrefix)) {
        const std::string path(value.substr(filePrefix.size()));
        if (path.empty()) {
            error = "--style=file:<path> requires a path";
            return std::string{};
        }
        return tools::lint::AbsolutePath(path);
    }
    if (value.empty()) {
        error = "--style requires a value";
        return std::string{};
    }
    return tools::lint::AbsolutePath(value);
}

}  // namespace

std::optional<FormatOptions> ParseFormatArgs(int argc, char** argv, std::string& error) {
    FormatOptions options;
    for (int index = 0; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "-h" || arg == "--help") {
            options.help = true;
        } else if (arg == "-i") {
            if (options.mode == FormatMode::DryRun) {
                error = "-i is incompatible with --dry-run";
                return std::nullopt;
            }
            options.mode = FormatMode::InPlace;
        } else if (arg == "-n" || arg == "--dry-run") {
            if (options.mode == FormatMode::InPlace) {
                error = "--dry-run is incompatible with -i";
                return std::nullopt;
            }
            options.mode = FormatMode::DryRun;
        } else if (arg == "-v" || arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--style") {
            if (index + 1 >= argc) {
                error = "--style requires a value";
                return std::nullopt;
            }
            std::optional<std::string> parsed = ParseStyleValue(argv[++index], error);
            if (!error.empty()) {
                return std::nullopt;
            }
            options.explicitStylePath = std::move(parsed);
        } else if (StartsWith(arg, "--style=")) {
            std::optional<std::string> parsed = ParseStyleValue(std::string_view(arg).substr(8), error);
            if (!error.empty()) {
                return std::nullopt;
            }
            options.explicitStylePath = std::move(parsed);
        } else if (StartsWith(arg, "-style=")) {
            std::optional<std::string> parsed = ParseStyleValue(std::string_view(arg).substr(7), error);
            if (!error.empty()) {
                return std::nullopt;
            }
            options.explicitStylePath = std::move(parsed);
        } else if (!arg.empty() && arg[0] == '-') {
            error = "unknown argument " + arg;
            return std::nullopt;
        } else {
            options.files.push_back(arg);
        }
    }
    if (options.mode == FormatMode::InPlace && options.files.empty()) {
        error = "-i requires at least one file";
        return std::nullopt;
    }
    return options;
}

void PrintFormatUsage(FILE* output) {
    std::fprintf(output, "Usage:\n");
    std::fprintf(
        output,
        "  CaseDashTools.exe format [--style=file|--style=<path>|--style=file:<path>] [-v|--verbose]\n"
    );
    std::fprintf(
        output,
        "  CaseDashTools.exe format [--style=file|--style=<path>|--style=file:<path>] [-i|-n|--dry-run] file...\n"
    );
}

}  // namespace tools::format
