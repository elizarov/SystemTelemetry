#include "tools/format_include_sorter.h"

#include <algorithm>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "tools/impl/lint_common.h"

namespace tools::format {

namespace {

struct IncludeLine {
    std::string line;
    std::string spelling;
    bool quoted = false;
    int group = 0;
};

std::optional<IncludeLine> ParseIncludeLine(std::string_view line) {
    const std::string trimmed = tools::lint::Trim(line);
    constexpr std::string_view prefix = "#include";
    if (!tools::lint::StartsWith(trimmed, prefix)) {
        return std::nullopt;
    }
    size_t index = prefix.size();
    while (index < trimmed.size() && IsSpaceButNotNewline(trimmed[index])) {
        ++index;
    }
    if (index >= trimmed.size()) {
        return std::nullopt;
    }
    const char open = trimmed[index];
    const char close = open == '"' ? '"' : open == '<' ? '>' : '\0';
    if (close == '\0') {
        return std::nullopt;
    }
    const size_t end = trimmed.find(close, index + 1);
    if (end == std::string::npos) {
        return std::nullopt;
    }
    if (!tools::lint::Trim(std::string_view(trimmed).substr(end + 1)).empty()) {
        return std::nullopt;
    }
    IncludeLine include;
    include.line = "#include " + trimmed.substr(index, end - index + 1);
    include.spelling = trimmed.substr(index, end - index + 1);
    include.quoted = open == '"';
    return include;
}

std::string Stem(std::string_view path) {
    const std::string normalized = tools::lint::NormalizeSeparators(std::string(path));
    const size_t slash = normalized.find_last_of('/');
    const size_t start = slash == std::string::npos ? 0 : slash + 1;
    const size_t dot = normalized.find_last_of('.');
    if (dot == std::string::npos || dot < start) {
        return normalized.substr(start);
    }
    return normalized.substr(start, dot - start);
}

bool IsMainInclude(const IncludeLine& include, const FormatterConfig& config, std::string_view sourcePath) {
    if (!include.quoted || !config.mainIncludeQuote) {
        return false;
    }
    std::string includeText = include.spelling.substr(1, include.spelling.size() - 2);
    if (tools::lint::Extension(includeText) != ".h") {
        return false;
    }
    const std::string includeStem = Stem(includeText);
    const std::string sourceStem = Stem(sourcePath);
    if (tools::lint::StartsWith(sourceStem, includeStem + ".")) {
        return true;
    }
    if (!tools::lint::StartsWith(includeStem, sourceStem)) {
        return false;
    }
    const std::string suffix = includeStem.substr(sourceStem.size());
    return std::regex_match(suffix, std::regex(config.mainIncludeRegex));
}

int IncludeGroupIndex(const IncludeLine& include, const FormatterConfig& config, std::string_view sourcePath) {
    if (IsMainInclude(include, config, sourcePath)) {
        return 0;
    }
    for (size_t index = 0; index < config.includeGroups.size(); ++index) {
        if (std::regex_match(include.spelling, config.includeGroups[index].regex)) {
            return static_cast<int>(index + 1);
        }
    }
    return static_cast<int>(config.includeGroups.size() + 1);
}

void SortIncludeRun(std::vector<IncludeLine>& includes, const FormatterConfig& config, std::string_view sourcePath) {
    for (IncludeLine& include : includes) {
        include.group = IncludeGroupIndex(include, config, sourcePath);
    }
    std::sort(includes.begin(), includes.end(), [](const IncludeLine& left, const IncludeLine& right) {
        if (left.group != right.group) {
            return left.group < right.group;
        }
        return tools::lint::ToLowerAscii(left.spelling) < tools::lint::ToLowerAscii(right.spelling);
    });
}

std::optional<IncludeLine> ParseIncludeToken(const Token& token) {
    if (token.kind != TokenKind::Preprocessor) {
        return std::nullopt;
    }
    std::vector<std::string> lines = tools::lint::SplitLines(token.text);
    if (lines.empty()) {
        lines.push_back(token.text);
    }
    if (lines.size() != 1) {
        return std::nullopt;
    }
    return ParseIncludeLine(lines.front());
}

}  // namespace

std::vector<std::optional<std::string>> SortedIncludeRunLines(
    TokenSpan tokens,
    const FormatterConfig& config,
    std::string_view sourcePath
) {
    std::vector<IncludeLine> includes;
    for (const Token& token : tokens) {
        if (std::optional<IncludeLine> include = ParseIncludeToken(token)) {
            includes.push_back(std::move(*include));
        }
    }
    SortIncludeRun(includes, config, sourcePath);
    std::vector<std::optional<std::string>> lines;
    int lastGroup = -1;
    for (const IncludeLine& include : includes) {
        if (lastGroup != -1 && include.group != lastGroup) {
            lines.push_back(std::nullopt);
        }
        lines.push_back(include.line);
        lastGroup = include.group;
    }
    return lines;
}

}  // namespace tools::format
