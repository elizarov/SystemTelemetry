#include "tools/impl/lint_common.h"

#include <cstdio>
#include <stdexcept>

#include "tools/impl/tools_common.h"
#include "util/strings.h"

namespace tools::lint {

namespace {

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

}  // namespace

int FileRecord::LineCount() const {
    return static_cast<int>(lines.size());
}

bool CheckResult::Failed() const {
    return !findings.empty() || !errors.empty();
}

std::vector<std::string> ConfigStrings(const JsonValue& config, std::string_view key) {
    const JsonValue* value = config.Find(key);
    if (value == nullptr || value->IsNull()) {
        return {};
    }
    std::vector<std::string> strings;
    for (const JsonValue& item : value->AsArray()) {
        strings.push_back(item.AsString());
    }
    return strings;
}

std::set<std::string> RequireSuffixGroup(
    const std::map<std::string, std::set<std::string>>& suffixGroups,
    std::string_view configPath,
    std::string_view groupName
) {
    const auto found = suffixGroups.find(std::string(groupName));
    if (found == suffixGroups.end()) {
        throw std::runtime_error(
            std::string(configPath) + " references unknown suffix group " + std::string(groupName)
        );
    }
    return found->second;
}

std::optional<std::vector<std::string>> RunGitLsFiles(const std::vector<std::string>& args) {
    std::string command = "git ls-files";
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
    for (std::string line : SplitLines(output)) {
        line = Trim(line);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

}  // namespace tools::lint
