#pragma once

#include <map>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

struct IncludeGroup {
    std::string name;
    std::regex regex;
    int priority = 0;
};

struct FormatterConfig {
    int columnLimit = 120;
    int indentWidth = 4;
    int tabWidth = 4;
    std::string mainIncludeRegex = "(Test)?$";
    bool mainIncludeQuote = true;
    std::vector<std::string> statementLikeMacroParameters;
    std::vector<std::string> streamShiftConfigurationMethods;
    std::vector<IncludeGroup> includeGroups;
};

struct FormatterIgnoreFile {
    std::string path;
    std::string directory;
    std::vector<std::string> entries;

    bool Ignores(std::string_view filePath) const;
};

class FormatStyleCache {
public:
    explicit FormatStyleCache(std::optional<std::string> explicitConfigPath);

    const FormatterConfig* ConfigForPath(std::string_view path, std::string& error);
    bool IsIgnored(std::string_view path, std::string& error);

private:
    std::optional<std::string> explicitConfigPath_;
    std::optional<FormatterConfig> explicitConfig_;
    std::map<std::string, FormatterConfig> configsByPath_;
    std::map<std::string, FormatterIgnoreFile> ignoresByPath_;
    std::map<std::string, std::optional<std::string>> configSearchCache_;
    std::map<std::string, std::optional<std::string>> ignoreSearchCache_;
};
