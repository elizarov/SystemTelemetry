#include "tools/lint_check.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <io.h>
#include <regex>
#include <set>
#include <stdexcept>

#include "tools/impl/lint_checkers.h"
#include "tools/impl/lint_common.h"
#include "tools/impl/lint_json.h"

namespace tools::lint {

namespace {

constexpr int kToolStaleExitCode = 3;

const std::vector<std::string>& ToolRefreshExcludedPrefixes() {
    static const std::vector<std::string> prefixes = {"src/vendor/", "src/tools/vendor/"};
    return prefixes;
}

struct LintArgs {
    std::string configPath;
    bool check = false;
    bool noProgress = false;
    bool verbose = false;
    std::optional<std::string> reportJson;
};

bool IsNewerThan(std::string_view source, std::uint64_t targetTime) {
    const std::optional<std::uint64_t> sourceTime = LastWriteTime(source);
    return sourceTime.has_value() && *sourceTime > targetTime;
}

bool IsCMakeBuildGraphStale(const std::string& repoRoot) {
    const std::optional<std::uint64_t> cmakeListsTime = LastWriteTime(JoinPath(repoRoot, "CMakeLists.txt"));
    if (!cmakeListsTime.has_value()) {
        return false;
    }

    // A CMake edit can refresh build.ninja without relinking CaseDashTools when the tool target is unchanged.
    const std::optional<std::uint64_t> buildGraphTime = LastWriteTime(JoinPath(repoRoot, "build/cmake/build.ninja"));
    return !buildGraphTime.has_value() || *cmakeListsTime > *buildGraphTime;
}

bool IsToolStale() {
    const std::string exePath = ExecutablePath();
    const std::optional<std::uint64_t> exeTime = LastWriteTime(exePath);
    if (!exeTime.has_value()) {
        return false;
    }
    const std::string repoRoot = ParentPath(ParentPath(exePath));
    if (IsCMakeBuildGraphStale(repoRoot)) {
        return true;
    }
    for (const std::string& path : RecursiveFiles(JoinPath(repoRoot, "src/tools"))) {
        const std::string relative = RelativePath(path, repoRoot);
        // The freshness check runs before lint config parsing, so keep vendored roots out here as well.
        if (IsExcluded(relative, ToolRefreshExcludedPrefixes())) {
            continue;
        }
        const std::string suffix = Extension(path);
        if ((suffix == ".cpp" || suffix == ".h") && IsNewerThan(path, *exeTime)) {
            return true;
        }
    }
    return false;
}

std::string ResolveProjectPath(const std::string& projectRoot, const std::string& path) {
    const bool isAbsolute =
        (path.size() >= 2 && path[1] == ':') || (!path.empty() && (path[0] == '/' || path[0] == '\\'));
    if (isAbsolute) {
        return AbsolutePath(path);
    }
    return AbsolutePath(JoinPath(projectRoot, path));
}

LintArgs ParseArgs(int argc, char** argv, const std::string& projectRoot) {
    LintArgs args;
    args.configPath = JoinPath(projectRoot, "tools/lint_config.json");
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--config requires a value");
            }
            args.configPath = ResolveProjectPath(projectRoot, argv[++i]);
        } else if (arg == "--check") {
            args.check = true;
        } else if (arg == "--no-progress") {
            args.noProgress = true;
        } else if (arg == "--report-json") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--report-json requires a value");
            }
            args.reportJson = argv[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            args.verbose = true;
        } else {
            throw std::runtime_error("unknown argument " + arg);
        }
    }
    args.configPath = ResolveProjectPath(projectRoot, args.configPath);
    return args;
}

std::map<std::string, std::set<std::string>> ParseSuffixGroups(const JsonValue& config) {
    std::map<std::string, std::set<std::string>> groups;
    for (const auto& [name, values] : config.At("suffix_groups").AsObject()) {
        std::set<std::string> suffixes;
        for (const JsonValue& value : values.AsArray()) {
            suffixes.insert(value.AsString());
        }
        groups[name] = std::move(suffixes);
    }
    return groups;
}

void RequireSingleSuffixGroup(
    const std::map<std::string, std::set<std::string>>& suffixGroups,
    std::string_view configPath,
    std::string_view groupName
) {
    const std::set<std::string> values = RequireSuffixGroup(suffixGroups, configPath, groupName);
    if (values.size() != 1) {
        throw std::runtime_error(std::string(configPath) + " must reference a suffix group with exactly one suffix");
    }
}

void ValidateConfig(const JsonValue& config, const std::map<std::string, std::set<std::string>>& suffixGroups) {
    RequireSuffixGroup(suffixGroups, "scan.suffix_group", config.At("scan").At("suffix_group").AsString());
    RequireSingleSuffixGroup(
        suffixGroups,
        "architecture.header_suffix_group",
        config.At("architecture").At("header_suffix_group").AsString()
    );
    RequireSingleSuffixGroup(
        suffixGroups,
        "architecture.implementation_suffix_group",
        config.At("architecture").At("implementation_suffix_group").AsString()
    );
    RequireSuffixGroup(
        suffixGroups,
        "include_style.suffix_group",
        config.At("include_style").At("suffix_group").AsString()
    );
    RequireSuffixGroup(
        suffixGroups,
        "source_dependencies.suffix_group",
        config.At("source_dependencies").At("suffix_group").AsString()
    );
    RequireSuffixGroup(
        suffixGroups,
        "source_dependencies.header_suffix_group",
        config.At("source_dependencies").At("header_suffix_group").AsString()
    );
    RequireSuffixGroup(
        suffixGroups,
        "source_policy.suffix_group",
        config.At("source_policy").At("suffix_group").AsString()
    );
    if (ConfigStrings(config.At("source_dependencies"), "roots").size() != 1) {
        throw std::runtime_error("source_dependencies.roots must contain exactly one root");
    }
}

ScanSettings ParseScanSettings(
    const JsonValue& config,
    const std::map<std::string, std::set<std::string>>& suffixGroups
) {
    const JsonValue& scan = config.At("scan");
    ScanSettings settings;
    settings.roots = ConfigStrings(scan, "roots");
    settings.suffixes = RequireSuffixGroup(suffixGroups, "scan.suffix_group", scan.At("suffix_group").AsString());
    settings.strippedSuffixes = RequireSuffixGroup(suffixGroups, "suffix_groups.source", "source");
    settings.excludedPrefixes = ConfigStrings(scan, "excluded_prefixes");
    settings.includePattern = scan.At("include_pattern").AsString();
    return settings;
}

bool IsLintInput(const FileEntry& entry, const std::string& projectRoot, const ScanSettings& settings) {
    const std::string relative = RelativePath(entry.path, projectRoot);
    if (settings.suffixes.find(Extension(entry.path)) == settings.suffixes.end()) {
        return false;
    }
    if (!settings.roots.empty() && !HasRoot(relative, settings.roots)) {
        return false;
    }
    return !IsExcluded(relative, settings.excludedPrefixes);
}

std::set<std::string> ProjectPathsFromGit(const std::string& projectRoot, const std::vector<std::string>& lines) {
    std::set<std::string> paths;
    for (const std::string& line : lines) {
        paths.insert(NormalizePathKey(JoinPath(projectRoot, line)));
    }
    return paths;
}

std::vector<FileEntry> DiscoverLintInputs(const std::string& projectRoot, const ScanSettings& settings) {
    const std::optional<std::vector<std::string>> trackedLines = RunGitLsFiles(settings.roots);
    std::vector<std::string> untrackedArgs = {"--others", "--exclude-standard"};
    untrackedArgs.insert(untrackedArgs.end(), settings.roots.begin(), settings.roots.end());
    const std::optional<std::vector<std::string>> untrackedLines = RunGitLsFiles(untrackedArgs);

    std::vector<FileEntry> entries;
    if (!trackedLines.has_value() || !untrackedLines.has_value()) {
        for (const std::string& root : settings.roots) {
            for (const std::string& path : RecursiveFiles(JoinPath(projectRoot, root))) {
                FileEntry entry{path, true};
                if (IsLintInput(entry, projectRoot, settings)) {
                    entries.push_back(std::move(entry));
                }
            }
        }
    } else {
        const std::set<std::string> trackedPaths = ProjectPathsFromGit(projectRoot, *trackedLines);
        std::set<std::string> allPaths = trackedPaths;
        const std::set<std::string> untrackedPaths = ProjectPathsFromGit(projectRoot, *untrackedLines);
        allPaths.insert(untrackedPaths.begin(), untrackedPaths.end());
        for (const std::string& key : allPaths) {
            FileEntry entry{AbsolutePath(key), trackedPaths.find(key) != trackedPaths.end()};
            if (FileExists(entry.path) && IsLintInput(entry, projectRoot, settings)) {
                entries.push_back(std::move(entry));
            }
        }
    }

    std::sort(entries.begin(), entries.end(), [&](const FileEntry& left, const FileEntry& right) {
        return RelativePath(left.path, projectRoot) < RelativePath(right.path, projectRoot);
    });
    return entries;
}

FileRecord ScanFile(const FileEntry& entry, const std::string& projectRoot, const ScanSettings& settings) {
    static const std::regex includePattern(R"include(^\s*#include\s+("([^"]+)"|<([^>]+)>))include");

    const std::optional<std::string> text = ReadFileText(entry.path);
    if (!text.has_value()) {
        throw std::runtime_error("could not read " + entry.path);
    }

    FileRecord record;
    record.path = AbsolutePath(entry.path);
    record.relative = RelativePath(record.path, projectRoot);
    record.tracked = entry.tracked;
    record.text = *text;
    record.lines = SplitLines(record.text);

    for (int index = 0; index < static_cast<int>(record.lines.size()); ++index) {
        const std::string& line = record.lines[static_cast<size_t>(index)];
        std::smatch match;
        if (!std::regex_search(line, match, includePattern)) {
            continue;
        }
        const bool quoted = match.length(2) > 0;
        record.includes.push_back({index + 1, quoted ? match.str(2) : match.str(3), quoted});
    }

    if (settings.strippedSuffixes.find(Extension(entry.path)) != settings.strippedSuffixes.end()) {
        record.strippedText = StripCommentsAndStrings(record.text);
        record.strippedLines = SplitLines(record.strippedText);
    }
    return record;
}

int ConsoleColumns() {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        return info.srWindow.Right - info.srWindow.Left + 1;
    }
    return 120;
}

std::string TruncateProgressLine(const std::string& prefix, const std::string& relative) {
    const int columns = ConsoleColumns();
    if (columns <= 1) {
        return prefix + relative;
    }
    const size_t maxLength = static_cast<size_t>(columns - 1);
    const std::string fullLine = prefix + relative;
    if (fullLine.size() <= maxLength) {
        return fullLine;
    }
    const size_t pathBudget = maxLength > prefix.size() ? maxLength - prefix.size() : 0;
    if (pathBudget <= 3) {
        return fullLine.substr(0, maxLength);
    }
    return prefix + "..." + relative.substr(relative.size() - (pathBudget - 3));
}

std::vector<FileRecord> ScanLintInputs(
    const std::vector<FileEntry>& entries,
    const std::string& projectRoot,
    const ScanSettings& settings,
    std::vector<std::unique_ptr<Checker>>& checkers,
    bool showProgress
) {
    std::vector<FileRecord> records;
    records.reserve(entries.size());
    const bool useProgress = showProgress && _isatty(_fileno(stdout)) != 0;
    size_t previousProgressLength = 0;
    for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
        if (useProgress) {
            const std::string relative = RelativePath(entries[static_cast<size_t>(index)].path, projectRoot);
            const std::string progress = TruncateProgressLine(
                "[" + std::to_string(index + 1) + "/" + std::to_string(entries.size()) + "] lint-check ",
                relative
            );
            const std::string padding(
                previousProgressLength > progress.size() ? previousProgressLength - progress.size() : 0,
                ' '
            );
            std::printf("\r%s%s", progress.c_str(), padding.c_str());
            std::fflush(stdout);
            previousProgressLength = progress.size();
        }
        FileRecord record = ScanFile(entries[static_cast<size_t>(index)], projectRoot, settings);
        for (std::unique_ptr<Checker>& checker : checkers) {
            try {
                checker->ProcessFile(record);
            } catch (const std::exception& error) {
                throw std::runtime_error("while processing " + record.relative + ": " + error.what());
            }
        }
        records.push_back(std::move(record));
    }
    if (useProgress) {
        std::printf("\r%s\r", std::string(previousProgressLength, ' ').c_str());
        std::fflush(stdout);
    }
    return records;
}

std::string CheckName(const CheckResult& result) {
    std::string title = result.title;
    if (EndsWith(title, ":")) {
        title.pop_back();
    }
    if (EndsWith(title, " check")) {
        title.resize(title.size() - 6);
    }
    title = ToLowerAscii(Trim(title));
    std::replace(title.begin(), title.end(), ' ', '_');
    return title;
}

std::vector<Diagnostic> DiagnosticsForResult(const CheckResult& result) {
    std::vector<Diagnostic> diagnostics;
    const std::string check = CheckName(result);
    for (const std::string& error : result.errors) {
        diagnostics.push_back({check, "error", {}, {}, error});
    }
    for (const Finding& finding : result.findings) {
        diagnostics.push_back({check, "finding", finding.location, finding.kind, finding.message});
    }
    return diagnostics;
}

std::vector<Diagnostic> CollectDiagnostics(const std::vector<CheckResult>& results) {
    std::vector<Diagnostic> diagnostics;
    for (const CheckResult& result : results) {
        const std::vector<Diagnostic> resultDiagnostics = DiagnosticsForResult(result);
        diagnostics.insert(diagnostics.end(), resultDiagnostics.begin(), resultDiagnostics.end());
    }
    return diagnostics;
}

bool WriteReportJson(const std::string& path, bool failed, const std::vector<Diagnostic>& diagnostics) {
    std::string json;
    json += "{\n";
    json += "  \"schema_version\": 1,\n";
    json += std::string("  \"failed\": ") + (failed ? "true" : "false") + ",\n";
    json += "  \"diagnostics\": [\n";
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        const Diagnostic& diagnostic = diagnostics[i];
        json += "    {\n";
        json += "      \"check\": \"" + JsonEscape(diagnostic.check) + "\",\n";
        json += "      \"type\": \"" + JsonEscape(diagnostic.type) + "\",\n";
        if (!diagnostic.location.empty()) {
            json += "      \"location\": \"" + JsonEscape(diagnostic.location) + "\",\n";
            json += "      \"kind\": \"" + JsonEscape(diagnostic.kind) + "\",\n";
        }
        json += "      \"message\": \"" + JsonEscape(diagnostic.message) + "\"\n";
        json += "    }";
        json += i + 1 == diagnostics.size() ? "\n" : ",\n";
    }
    json += "  ]\n";
    json += "}\n";
    return WriteFileText(path, json);
}

void PrintScannedLocSummary(const std::vector<FileRecord>& records) {
    int loc = 0;
    for (const FileRecord& record : records) {
        loc += record.LineCount();
    }
    std::printf("Scanned %s LOC across %zu lint input file(s).\n", FormatCount(loc).c_str(), records.size());
}

void PrintFailureResult(const CheckResult& result) {
    std::printf("%s\n", result.title.c_str());
    for (const Diagnostic& diagnostic : DiagnosticsForResult(result)) {
        if (diagnostic.type == "error") {
            std::printf("%s\n", diagnostic.message.c_str());
        } else {
            std::printf(
                "%s: %s: %s\n",
                diagnostic.location.c_str(),
                diagnostic.kind.c_str(),
                diagnostic.message.c_str()
            );
        }
    }
    if (!result.summary.empty()) {
        std::printf("%s\n", result.summary.c_str());
    }
}

std::string FormatElapsed(double seconds) {
    if (seconds < 1.0) {
        return std::to_string(static_cast<int>(std::llround(seconds * 1000.0))) + "ms";
    }
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.3fs", seconds);
    return buffer;
}

double ElapsedSeconds(std::chrono::steady_clock::time_point started) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}

}  // namespace

}  // namespace tools::lint

int RunLintCheck(int argc, char** argv) {
    using namespace tools;
    using namespace tools::lint;

    const auto started = std::chrono::steady_clock::now();
    if (IsToolStale()) {
        std::fprintf(stderr, "CaseDashTools build inputs changed; rebuilding.\n");
        return kToolStaleExitCode;
    }

    const std::string projectRoot = CurrentDirectoryAbsolute();
    LintArgs args;
    JsonValue config;
    std::map<std::string, std::set<std::string>> suffixGroups;
    ScanSettings settings;
    try {
        args = ParseArgs(argc, argv, projectRoot);
        const std::optional<std::string> configText = ReadFileText(args.configPath);
        if (!configText.has_value()) {
            throw std::runtime_error("could not read " + args.configPath);
        }
        config = ParseJson(*configText);
        suffixGroups = ParseSuffixGroups(config);
        ValidateConfig(config, suffixGroups);
        settings = ParseScanSettings(config, suffixGroups);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "lint config error: %s\n", error.what());
        std::printf("Lint failed in %s.\n", FormatElapsed(ElapsedSeconds(started)).c_str());
        return 2;
    }

    CheckerContext context;
    context.projectRoot = projectRoot;
    context.suffixGroups = suffixGroups;
    context.excludedPrefixes = settings.excludedPrefixes;
    context.checkDependencies = args.check;

    std::vector<std::unique_ptr<Checker>> checkers;
    try {
        checkers = CreateCheckers(config, context);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "lint config error: %s\n", error.what());
        std::printf("Lint failed in %s.\n", FormatElapsed(ElapsedSeconds(started)).c_str());
        return 2;
    }

    std::vector<FileRecord> records;
    std::vector<CheckResult> results;
    try {
        records = ScanLintInputs(
            DiscoverLintInputs(projectRoot, settings),
            projectRoot,
            settings,
            checkers,
            !args.noProgress
        );
        for (std::unique_ptr<Checker>& checker : checkers) {
            results.push_back(checker->Finish(args.verbose));
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "lint error: %s\n", error.what());
        std::printf("Lint failed in %s.\n", FormatElapsed(ElapsedSeconds(started)).c_str());
        return 2;
    }

    const bool failed =
        std::any_of(results.begin(), results.end(), [](const CheckResult& result) { return result.Failed(); });
    const std::vector<Diagnostic> diagnostics = CollectDiagnostics(results);

    if (args.reportJson.has_value()) {
        const std::string reportPath = ResolveProjectPath(projectRoot, *args.reportJson);
        if (!WriteReportJson(reportPath, failed, diagnostics)) {
            std::fprintf(stderr, "lint report error: could not write %s\n", reportPath.c_str());
            std::printf("Lint failed in %s.\n", FormatElapsed(ElapsedSeconds(started)).c_str());
            return 2;
        }
    }

    bool printedReport = false;
    for (const CheckResult& result : results) {
        if (!result.Failed()) {
            continue;
        }
        if (printedReport) {
            std::printf("\n");
        }
        PrintFailureResult(result);
        printedReport = true;
    }

    if (failed) {
        if (printedReport) {
            std::printf("\n");
        }
        std::printf("Lint failed in %s.\n", FormatElapsed(ElapsedSeconds(started)).c_str());
        return 1;
    }

    PrintScannedLocSummary(records);
    if (args.verbose) {
        std::vector<std::string> verboseLines;
        for (const CheckResult& result : results) {
            verboseLines.insert(verboseLines.end(), result.verboseLines.begin(), result.verboseLines.end());
        }
        if (!verboseLines.empty()) {
            std::printf("\n");
            for (const std::string& line : verboseLines) {
                std::printf("%s\n", line.c_str());
            }
        }
    }
    std::printf("Lint succeeded in %s.\n", FormatElapsed(ElapsedSeconds(started)).c_str());
    return 0;
}
