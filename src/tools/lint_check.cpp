#include "tools/lint_check.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <regex>
#include <set>
#include <stdexcept>

#include "tools/impl/lint_checkers.h"
#include "tools/impl/lint_common.h"
#include "tools/impl/lint_json.h"
#include "tools/impl/tools_common.h"
#include "tools/impl/tools_parallel.h"
#include "tools/impl/tools_progress.h"
#include "util/file_path.h"
#include "util/strings.h"

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
    size_t concurrency = 0;
    std::optional<std::string> reportJson;
};

struct CompletedLintScan {
    FileRecord record;
    std::string error;
};

bool IsNewerThan(std::string_view source, std::uint64_t targetTime) {
    const std::optional<std::uint64_t> sourceTime = LastWriteTime(source);
    return sourceTime.has_value() && *sourceTime > targetTime;
}

bool IsCMakeBuildGraphStale(const std::string& repoRoot) {
    const std::optional<std::uint64_t> cmakeListsTime = LastWriteTime((FilePath(repoRoot) / "CMakeLists.txt").string());
    if (!cmakeListsTime.has_value()) {
        return false;
    }

    // A CMake edit can refresh build.ninja without relinking CaseDashTools when the tool target is unchanged.
    const std::optional<std::uint64_t> buildGraphTime =
        LastWriteTime((FilePath(repoRoot) / "build/cmake/build.ninja").string());
    return !buildGraphTime.has_value() || *cmakeListsTime > *buildGraphTime;
}

bool IsToolStale() {
    const std::string exePath = ExecutablePath();
    const std::optional<std::uint64_t> exeTime = LastWriteTime(exePath);
    if (!exeTime.has_value()) {
        return false;
    }
    const std::string repoRoot = FilePath(FilePath(exePath).ParentPath().string()).ParentPath().string();
    if (IsCMakeBuildGraphStale(repoRoot)) {
        return true;
    }
    for (const std::string& path : RecursiveFiles((FilePath(repoRoot) / "src/tools").string())) {
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
    return AbsolutePath((FilePath(projectRoot) / path).string());
}

LintArgs ParseArgs(int argc, char** argv, const std::string& projectRoot) {
    LintArgs args;
    args.configPath = (FilePath(projectRoot) / "tools/lint_config.json").string();
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
        } else if (arg == "--concurrency") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--concurrency requires a value");
            }
            std::string error;
            if (!ParseToolConcurrency(argv[++i], args.concurrency, error)) {
                throw std::runtime_error(error);
            }
        } else if (StartsWith(arg, "--concurrency=")) {
            std::string error;
            if (!ParseToolConcurrency(std::string_view(arg).substr(14), args.concurrency, error)) {
                throw std::runtime_error(error);
            }
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

ScanSettings
    ParseScanSettings(const JsonValue& config, const std::map<std::string, std::set<std::string>>& suffixGroups)
{
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
        paths.insert(NormalizePathKey((FilePath(projectRoot) / line).string()));
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
            for (const std::string& path : RecursiveFiles((FilePath(projectRoot) / root).string())) {
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
            if (FileExists(FilePath(entry.path)) && IsLintInput(entry, projectRoot, settings)) {
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

    const std::optional<std::string> text = ReadFileBinary(entry.path);
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

std::vector<FileRecord> ScanLintInputs(
    const std::vector<FileEntry>& entries,
    const std::string& projectRoot,
    const ScanSettings& settings,
    size_t concurrency,
    bool showProgress,
    std::chrono::steady_clock::time_point started
) {
    std::vector<CompletedLintScan> completed(entries.size());
    ToolFileProgress progress(stdout, "lint-check", entries.size(), started, showProgress);
    RunToolParallelFor(entries.size(), concurrency, &progress, [&](size_t index) {
        try {
            completed[index].record = ScanFile(entries[index], projectRoot, settings);
        } catch (const std::exception& error) {
            completed[index].error = error.what();
        }
    });

    std::vector<FileRecord> records;
    records.reserve(entries.size());
    for (CompletedLintScan& scan : completed) {
        if (!scan.error.empty()) {
            throw std::runtime_error(scan.error);
        }
        records.push_back(std::move(scan.record));
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
    title = ToLower(Trim(title));
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
    return EnsureParentDirectory(path) && WriteFileBinary(path, json);
}

std::string ScannedFileCountText(size_t scannedFiles, size_t totalFiles) {
    if (scannedFiles == totalFiles) {
        return std::to_string(scannedFiles);
    }
    return std::to_string(scannedFiles) + "/" + std::to_string(totalFiles);
}

void PrintScannedLocSummary(const std::vector<FileRecord>& records, size_t totalFiles) {
    int loc = 0;
    for (const FileRecord& record : records) {
        loc += record.LineCount();
    }
    const std::string scannedFiles = ScannedFileCountText(records.size(), totalFiles);
    std::printf("Scanned %s LOC across %s lint input file(s).\n", FormatCount(loc).c_str(), scannedFiles.c_str());
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

}  // namespace

}  // namespace tools::lint

int RunLintCheck(int argc, char** argv) {
    using namespace tools::lint;

    const auto started = std::chrono::steady_clock::now();
    if (IsToolStale()) {
        std::fprintf(stderr, "CaseDashTools build inputs changed; rebuilding.\n");
        return kToolStaleExitCode;
    }

    const std::string projectRoot = AbsolutePath(CurrentDirectoryPath().string());
    LintArgs args;
    JsonValue config;
    std::map<std::string, std::set<std::string>> suffixGroups;
    ScanSettings settings;
    try {
        args = ParseArgs(argc, argv, projectRoot);
        const std::optional<std::string> configText = ReadFileBinary(args.configPath);
        if (!configText.has_value()) {
            throw std::runtime_error("could not read " + args.configPath);
        }
        config = ParseJson(*configText);
        suffixGroups = ParseSuffixGroups(config);
        ValidateConfig(config, suffixGroups);
        settings = ParseScanSettings(config, suffixGroups);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "lint config error: %s\n", error.what());
        std::printf("Lint failed in %s.\n", FormatToolElapsed(std::chrono::steady_clock::now() - started).c_str());
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
        std::printf("Lint failed in %s.\n", FormatToolElapsed(std::chrono::steady_clock::now() - started).c_str());
        return 2;
    }

    std::vector<FileEntry> entries;
    std::vector<FileRecord> records;
    std::vector<CheckResult> results;
    try {
        entries = DiscoverLintInputs(projectRoot, settings);
        records = ScanLintInputs(entries, projectRoot, settings, args.concurrency, !args.noProgress, started);
        for (const FileRecord& record : records) {
            for (std::unique_ptr<Checker>& checker : checkers) {
                try {
                    checker->ProcessFile(record);
                } catch (const std::exception& error) {
                    throw std::runtime_error("while processing " + record.relative + ": " + error.what());
                }
            }
        }
        for (std::unique_ptr<Checker>& checker : checkers) {
            results.push_back(checker->Finish(args.verbose));
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "lint error: %s\n", error.what());
        const std::string scannedFiles = ScannedFileCountText(records.size(), entries.size());
        std::printf(
            "Lint failed after scanning %s file(s) in %s.\n",
            scannedFiles.c_str(),
            FormatToolElapsed(std::chrono::steady_clock::now() - started).c_str()
        );
        return 2;
    }

    const bool failed =
        std::any_of(results.begin(), results.end(), [](const CheckResult& result) { return result.Failed(); });
    const std::vector<Diagnostic> diagnostics = CollectDiagnostics(results);

    if (args.reportJson.has_value()) {
        const std::string reportPath = ResolveProjectPath(projectRoot, *args.reportJson);
        if (!WriteReportJson(reportPath, failed, diagnostics)) {
            std::fprintf(stderr, "lint report error: could not write %s\n", reportPath.c_str());
            const std::string scannedFiles = ScannedFileCountText(records.size(), entries.size());
            std::printf(
                "Lint failed after scanning %s file(s) in %s.\n",
                scannedFiles.c_str(),
                FormatToolElapsed(std::chrono::steady_clock::now() - started).c_str()
            );
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
        const std::string scannedFiles = ScannedFileCountText(records.size(), entries.size());
        std::printf(
            "Lint failed after scanning %s file(s) in %s.\n",
            scannedFiles.c_str(),
            FormatToolElapsed(std::chrono::steady_clock::now() - started).c_str()
        );
        return 1;
    }

    PrintScannedLocSummary(records, entries.size());
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
    std::printf(
        "Lint succeeded after scanning %s file(s) in %s.\n",
        ScannedFileCountText(records.size(), entries.size()).c_str(),
        FormatToolElapsed(std::chrono::steady_clock::now() - started).c_str()
    );
    return 0;
}
