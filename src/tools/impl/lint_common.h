#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "tools/impl/lint_json.h"

namespace tools::lint {

struct IncludeDirective {
    int line = 0;
    std::string text;
    bool quoted = false;
};

struct FileRecord {
    std::string path;
    std::string relative;
    bool tracked = false;
    std::string text;
    std::vector<std::string> lines;
    std::vector<IncludeDirective> includes;
    std::string strippedText;
    std::vector<std::string> strippedLines;

    int LineCount() const;
};

struct FileEntry {
    std::string path;
    bool tracked = false;
};

struct Finding {
    std::string location;
    std::string kind;
    std::string message;
};

struct CheckResult {
    std::string title;
    std::vector<Finding> findings;
    std::vector<std::string> errors;
    std::string summary;
    std::vector<std::string> verboseLines;

    bool Failed() const;
};

struct Diagnostic {
    std::string check;
    std::string type;
    std::string location;
    std::string kind;
    std::string message;
};

struct ScanSettings {
    std::vector<std::string> roots;
    std::set<std::string> suffixes;
    std::set<std::string> strippedSuffixes;
    std::vector<std::string> excludedPrefixes;
    std::string includePattern;
};

struct CheckerContext {
    std::string projectRoot;
    std::map<std::string, std::set<std::string>> suffixGroups;
    std::vector<std::string> excludedPrefixes;
    bool checkDependencies = false;
};

std::vector<std::string> ConfigStrings(const JsonValue& config, std::string_view key);
std::set<std::string> RequireSuffixGroup(
    const std::map<std::string, std::set<std::string>>& suffixGroups,
    std::string_view configPath,
    std::string_view groupName
);

std::optional<std::vector<std::string>> RunGitLsFiles(const std::vector<std::string>& args);

}  // namespace tools::lint
