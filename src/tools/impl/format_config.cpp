#include "tools/impl/format_config.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "tools/impl/tools_common.h"

namespace {

struct ConfigLine {
    int indent = 0;
    std::string text;
};

int CountIndent(std::string_view line) {
    int indent = 0;
    while (indent < static_cast<int>(line.size()) && line[static_cast<size_t>(indent)] == ' ') {
        ++indent;
    }
    return indent;
}

std::string StripYamlComment(std::string_view text) {
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    std::string result;
    for (size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\'' && !inDoubleQuote) {
            inSingleQuote = !inSingleQuote;
        } else if (ch == '"' && !inSingleQuote) {
            inDoubleQuote = !inDoubleQuote;
        } else if (ch == '#' && !inSingleQuote && !inDoubleQuote) {
            break;
        }
        result.push_back(ch);
    }
    return Trim(result);
}

std::vector<ConfigLine> ReadConfigLines(std::string_view text) {
    std::vector<ConfigLine> lines;
    for (const std::string& line : SplitLines(text)) {
        std::string stripped = StripYamlComment(line);
        if (stripped.empty() || stripped == "---" || stripped == "...") {
            continue;
        }
        lines.push_back({CountIndent(line), std::move(stripped)});
    }
    return lines;
}

std::pair<std::string, std::string> SplitKeyValue(std::string_view text) {
    const size_t colon = text.find(':');
    if (colon == std::string_view::npos) {
        return {};
    }
    return {Trim(text.substr(0, colon)), Trim(text.substr(colon + 1))};
}

std::string UnquoteScalar(std::string value) {
    value = Trim(value);
    if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
        std::string result;
        for (size_t index = 1; index + 1 < value.size(); ++index) {
            if (value[index] == '\'' && index + 2 < value.size() && value[index + 1] == '\'') {
                result.push_back('\'');
                ++index;
            } else {
                result.push_back(value[index]);
            }
        }
        return result;
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

int ParseInt(std::string_view text, std::string_view key) {
    try {
        size_t used = 0;
        const int value = std::stoi(std::string(text), &used);
        if (used != text.size()) {
            throw std::invalid_argument("trailing input");
        }
        return value;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string(key) + " must be an integer");
    }
}

std::vector<std::string>
    ParseIndentedStringList(const std::vector<ConfigLine>& lines, size_t& index, int parentIndent)
{
    std::vector<std::string> values;
    for (++index; index < lines.size(); ++index) {
        const ConfigLine& line = lines[index];
        if (line.indent <= parentIndent) {
            --index;
            break;
        }
        if (line.text.size() >= 2 && line.text[0] == '-' && line.text[1] == ' ') {
            values.push_back(UnquoteScalar(line.text.substr(2)));
        }
    }
    return values;
}

void ParseIncludeCategories(const std::vector<ConfigLine>& lines, size_t& index, FormatterConfig& config) {
    std::vector<IncludeGroup> groups;
    const int parentIndent = lines[index].indent;
    for (++index; index < lines.size(); ++index) {
        const ConfigLine& line = lines[index];
        if (line.indent <= parentIndent) {
            --index;
            break;
        }
        if (!StartsWith(line.text, "- ")) {
            continue;
        }
        IncludeGroup group;
        group.priority = static_cast<int>(groups.size()) + 1;
        std::string first = Trim(std::string_view(line.text).substr(2));
        if (!first.empty()) {
            const auto [key, value] = SplitKeyValue(first);
            if (key == "Regex") {
                group.name = UnquoteScalar(value);
                group.regex = std::regex(group.name);
            } else if (key == "Priority") {
                group.priority = ParseInt(value, "IncludeCategories.Priority");
            }
        }
        for (++index; index < lines.size(); ++index) {
            const ConfigLine& child = lines[index];
            if (child.indent <= line.indent) {
                --index;
                break;
            }
            const auto [key, value] = SplitKeyValue(child.text);
            if (key == "Regex") {
                group.name = UnquoteScalar(value);
                group.regex = std::regex(group.name);
            } else if (key == "Priority") {
                group.priority = ParseInt(value, "IncludeCategories.Priority");
            }
        }
        if (group.name.empty()) {
            throw std::runtime_error("IncludeCategories entries require Regex");
        }
        groups.push_back(std::move(group));
    }
    std::sort(groups.begin(), groups.end(), [](const IncludeGroup& left, const IncludeGroup& right) {
        return left.priority < right.priority;
    });
    config.includeGroups = std::move(groups);
}

void ParseMacroCategories(const std::vector<ConfigLine>& lines, size_t& index, FormatterConfig& config) {
    const int parentIndent = lines[index].indent;
    for (++index; index < lines.size(); ++index) {
        const ConfigLine& line = lines[index];
        if (line.indent <= parentIndent) {
            --index;
            break;
        }
        const auto [key, value] = SplitKeyValue(line.text);
        if (key == "StatementLikeParameters" && value.empty()) {
            config.statementLikeMacroParameters = ParseIndentedStringList(lines, index, line.indent);
        }
    }
}

void ParseStreamShift(const std::vector<ConfigLine>& lines, size_t& index, FormatterConfig& config) {
    const int parentIndent = lines[index].indent;
    for (++index; index < lines.size(); ++index) {
        const ConfigLine& line = lines[index];
        if (line.indent <= parentIndent) {
            --index;
            break;
        }
        const auto [key, value] = SplitKeyValue(line.text);
        if (key == "ConfigurationMethods" && value.empty()) {
            config.streamShiftConfigurationMethods = ParseIndentedStringList(lines, index, line.indent);
        }
    }
}

FormatterConfig ParseFormatterConfig(std::string_view text) {
    FormatterConfig config;
    const std::vector<ConfigLine> lines = ReadConfigLines(text);
    for (size_t index = 0; index < lines.size(); ++index) {
        const ConfigLine& line = lines[index];
        if (line.indent != 0) {
            continue;
        }
        const auto [key, value] = SplitKeyValue(line.text);
        if (key == "ColumnLimit") {
            config.columnLimit = ParseInt(value, key);
        } else if (key == "IndentWidth") {
            config.indentWidth = ParseInt(value, key);
        } else if (key == "TabWidth") {
            config.tabWidth = ParseInt(value, key);
        } else if (key == "MainIncludeChar") {
            config.mainIncludeQuote = UnquoteScalar(value) == "Quote";
        } else if (key == "IncludeIsMainRegex") {
            config.mainIncludeRegex = UnquoteScalar(value);
        } else if (key == "IncludeCategories" && value.empty()) {
            ParseIncludeCategories(lines, index, config);
        } else if (key == "MacroCategories" && value.empty()) {
            ParseMacroCategories(lines, index, config);
        } else if (key == "StreamShift" && value.empty()) {
            ParseStreamShift(lines, index, config);
        }
    }
    return config;
}

std::string StartDirectory(std::string_view path) {
    const std::string absolute = AbsolutePath(path);
    if (DirectoryExists(absolute)) {
        return absolute;
    }
    return FilePath(absolute).ParentPath().string();
}

std::optional<std::string> FindUpwards(std::string_view startDirectory, std::string_view fileName) {
    std::string directory = AbsolutePath(startDirectory);
    while (!directory.empty()) {
        const std::string candidate = (FilePath(directory) / fileName).string();
        if (FileExists(candidate)) {
            return candidate;
        }
        const std::string parent = FilePath(directory).ParentPath().string();
        if (parent.empty() || NormalizePathKey(parent) == NormalizePathKey(directory)) {
            break;
        }
        directory = parent;
    }
    return std::nullopt;
}

std::optional<FormatterConfig> LoadConfigFile(std::string_view path, std::string& error) {
    const std::optional<std::string> text = ReadFileBinary(path);
    if (!text.has_value()) {
        error = "could not read formatter config: " + std::string(path);
        return std::nullopt;
    }
    try {
        return ParseFormatterConfig(*text);
    } catch (const std::exception& exception) {
        error = "invalid formatter config " + std::string(path) + ": " + exception.what();
        return std::nullopt;
    }
}

FormatterIgnoreFile ParseIgnoreFile(std::string_view path, std::string_view text) {
    FormatterIgnoreFile ignore;
    ignore.path = AbsolutePath(path);
    ignore.directory = FilePath(ignore.path).ParentPath().string();
    for (const std::string& line : SplitLines(text)) {
        std::string entry = StripYamlComment(line);
        entry = NormalizeSeparators(entry);
        while (StartsWith(entry, "./")) {
            entry.erase(0, 2);
        }
        while (!entry.empty() && entry.back() == '/') {
            entry.pop_back();
        }
        if (!entry.empty()) {
            ignore.entries.push_back(ToLower(entry));
        }
    }
    return ignore;
}

std::optional<FormatterIgnoreFile> LoadIgnoreFile(std::string_view path, std::string& error) {
    const std::optional<std::string> text = ReadFileBinary(path);
    if (!text.has_value()) {
        error = "could not read formatter ignore file: " + std::string(path);
        return std::nullopt;
    }
    return ParseIgnoreFile(path, *text);
}

}  // namespace

bool FormatterIgnoreFile::Ignores(std::string_view filePath) const {
    const std::string relative = ToLower(NormalizeSeparators(RelativePath(filePath, directory)));
    const std::vector<std::string> parts = Split(relative, '/');
    for (const std::string& entry : entries) {
        if (Contains(entry, "/")) {
            if (relative == entry || StartsWith(relative, entry + "/")) {
                return true;
            }
            continue;
        }
        for (const std::string& part : parts) {
            if (part == entry) {
                return true;
            }
        }
    }
    return false;
}

FormatStyleCache::FormatStyleCache(std::optional<std::string> explicitConfigPath) :
    explicitConfigPath_(std::move(explicitConfigPath)) {}

const FormatterConfig* FormatStyleCache::ConfigForPath(std::string_view path, std::string& error) {
    if (explicitConfigPath_.has_value()) {
        if (!explicitConfig_.has_value()) {
            explicitConfig_ = LoadConfigFile(*explicitConfigPath_, error);
        }
        return explicitConfig_.has_value() ? &*explicitConfig_ : nullptr;
    }

    const std::string start = StartDirectory(path);
    const std::string startKey = NormalizePathKey(start);
    auto cachedSearch = configSearchCache_.find(startKey);
    if (cachedSearch == configSearchCache_.end()) {
        cachedSearch = configSearchCache_.emplace(startKey, FindUpwards(start, ".cpp-format")).first;
    }
    if (!cachedSearch->second.has_value()) {
        error = "missing formatter config .cpp-format while searching upward from " + start;
        return nullptr;
    }

    const std::string configPath = AbsolutePath(*cachedSearch->second);
    const std::string configKey = NormalizePathKey(configPath);
    auto cachedConfig = configsByPath_.find(configKey);
    if (cachedConfig == configsByPath_.end()) {
        std::optional<FormatterConfig> loaded = LoadConfigFile(configPath, error);
        if (!loaded.has_value()) {
            return nullptr;
        }
        cachedConfig = configsByPath_.emplace(configKey, std::move(*loaded)).first;
    }
    return &cachedConfig->second;
}

bool FormatStyleCache::IsIgnored(std::string_view path, std::string& error) {
    const std::string start = StartDirectory(path);
    const std::string startKey = NormalizePathKey(start);
    auto cachedSearch = ignoreSearchCache_.find(startKey);
    if (cachedSearch == ignoreSearchCache_.end()) {
        cachedSearch = ignoreSearchCache_.emplace(startKey, FindUpwards(start, ".cpp-format-ignore")).first;
    }
    if (!cachedSearch->second.has_value()) {
        return false;
    }

    const std::string ignorePath = AbsolutePath(*cachedSearch->second);
    const std::string ignoreKey = NormalizePathKey(ignorePath);
    auto cachedIgnore = ignoresByPath_.find(ignoreKey);
    if (cachedIgnore == ignoresByPath_.end()) {
        std::optional<FormatterIgnoreFile> loaded = LoadIgnoreFile(ignorePath, error);
        if (!loaded.has_value()) {
            return false;
        }
        cachedIgnore = ignoresByPath_.emplace(ignoreKey, std::move(*loaded)).first;
    }
    return cachedIgnore->second.Ignores(path);
}
