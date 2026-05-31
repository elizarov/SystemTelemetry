#include "tools/impl/format_include_sort.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "tools/impl/format_config.h"
#include "tools/impl/format_model.h"
#include "tools/impl/tools_common.h"
#include "util/strings.h"

namespace {

constexpr int kMainIncludePriority = std::numeric_limits<int>::min();
constexpr int kUnmatchedIncludePriority = std::numeric_limits<int>::max();

struct IncludeText {
    std::string line;
    std::string target;
};

struct IncludeEntry {
    std::string line;
    std::string target;
    std::string sortKey;
    int priority = kUnmatchedIncludePriority;
    size_t originalIndex = 0;
};

struct IncludeSortContext {
    const FormatterConfig& config;
    std::optional<std::regex> mainIncludeRegex;
};

bool IsWhitespace(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

std::string_view TrimView(std::string_view value) {
    while (!value.empty() && IsWhitespace(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && IsWhitespace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

bool IsBlankSourceLine(std::string_view line) {
    return TrimView(line).empty();
}

bool IsCommentSourceLine(std::string_view line) {
    const std::string_view trimmed = TrimView(line);
    return StartsWith(trimmed, "//") || StartsWith(trimmed, "/*");
}

bool IsIncludeSourceLine(std::string_view line) {
    return StartsWith(TrimView(line), "#include");
}

bool IsIncludeGuardOpening(const std::vector<std::string>& lines) {
    if (lines.size() < 3) {
        return false;
    }
    return StartsWith(TrimView(lines[0]), "#ifndef") && StartsWith(TrimView(lines[1]), "#define");
}

void AppendSourceLines(std::string& output, const std::vector<std::string>& lines, size_t first, size_t last) {
    for (size_t index = first; index < last; ++index) {
        output.append(lines[index]);
        output.push_back('\n');
    }
}

size_t SkipBlankSourceLines(const std::vector<std::string>& lines, size_t index) {
    while (index < lines.size() && IsBlankSourceLine(lines[index])) {
        ++index;
    }
    return index;
}

bool IsIncludeSeparatorBlank(const std::vector<std::string>& lines, size_t index, size_t& nextLine) {
    if (index >= lines.size() || !IsBlankSourceLine(lines[index])) {
        return false;
    }
    nextLine = SkipBlankSourceLines(lines, index + 1);
    return nextLine < lines.size() && IsIncludeSourceLine(lines[nextLine]);
}

std::string_view ConsumeIncludeDirective(std::string_view text) {
    text = TrimView(text);
    if (text.empty() || text.front() != '#') {
        return {};
    }
    text.remove_prefix(1);
    text = TrimView(text);
    if (!StartsWith(text, "include")) {
        return {};
    }
    text.remove_prefix(std::string_view("include").size());
    return TrimView(text);
}

std::string_view HeaderNameTarget(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.front() == '<') {
        const size_t end = text.find('>');
        return end == std::string_view::npos ? text : text.substr(0, end + 1);
    }
    if (text.front() == '"') {
        const size_t end = text.find('"', 1);
        return end == std::string_view::npos ? text : text.substr(0, end + 1);
    }

    size_t end = 0;
    while (end < text.size() && !IsWhitespace(text[end])) {
        ++end;
    }
    return text.substr(0, end);
}

IncludeText ParseIncludeText(std::string_view text) {
    std::string_view rest = ConsumeIncludeDirective(text);
    std::string_view target = HeaderNameTarget(rest);
    if (target.empty()) {
        return {.line = Trim(text), .target = {}};
    }

    std::string line = "#include ";
    line.append(target);
    std::string_view suffix = TrimView(rest.substr(target.size()));
    if (!suffix.empty()) {
        line.push_back(' ');
        line.append(suffix);
    }
    return {.line = std::move(line), .target = std::string(target)};
}

bool HasHeaderDelimiter(std::string_view target, bool quote) {
    if (target.size() < 2) {
        return false;
    }
    return quote ? target.front() == '"' && target.back() == '"' : target.front() == '<' && target.back() == '>';
}

std::string StripHeaderDelimiter(std::string_view target) {
    if (target.size() >= 2 && (
        (target.front() == '"' && target.back() == '"') || (target.front() == '<' && target.back() == '>')
    )) {
        target.remove_prefix(1);
        target.remove_suffix(1);
    }
    return NormalizeSeparators(std::string(target));
}

std::string BaseNameNoExtension(std::string_view path) {
    std::string normalized = NormalizeSeparators(std::string(path));
    if (normalized.empty() || normalized.front() == '<') {
        return {};
    }
    const size_t slash = normalized.find_last_of('/');
    if (slash != std::string::npos) {
        normalized.erase(0, slash + 1);
    }
    return RemoveExtension(normalized);
}

std::string EscapeRegexLiteral(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
            case '\\':
            case '^':
            case '$':
            case '.':
            case '|':
            case '?':
            case '*':
            case '+':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
                escaped.push_back('\\');
                break;
            default:
                break;
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::optional<std::regex> BuildMainIncludeRegex(const FormatterConfig& config, std::string_view sourcePath) {
    const std::string sourceStem = BaseNameNoExtension(sourcePath);
    if (sourceStem.empty()) {
        return std::nullopt;
    }

    try {
        return std::regex("^" + EscapeRegexLiteral(sourceStem) + config.mainIncludeRegex);
    } catch (const std::regex_error&) {
        return std::nullopt;
    }
}

bool IsMainInclude(const IncludeSortContext& context, std::string_view target) {
    if (!context.mainIncludeRegex.has_value() || !HasHeaderDelimiter(target, context.config.mainIncludeQuote)) {
        return false;
    }

    const std::string targetStem = BaseNameNoExtension(StripHeaderDelimiter(target));
    if (targetStem.empty()) {
        return false;
    }

    return std::regex_match(targetStem, *context.mainIncludeRegex);
}

int IncludePriority(const IncludeSortContext& context, std::string_view target) {
    if (IsMainInclude(context, target)) {
        return kMainIncludePriority;
    }

    const std::string targetText(target);
    for (const IncludeGroup& group : context.config.includeGroups) {
        if (std::regex_match(targetText, group.regex)) {
            return group.priority;
        }
    }
    return kUnmatchedIncludePriority;
}

bool IsIncludeNode(const SyntaxNode& node) {
    return node.kind == SyntaxNodeKind::PreprocInclude;
}

std::string FormatIncludeTextsPreservingOrder(const std::vector<std::string>& includeTexts) {
    std::string result;
    for (const std::string& text : includeTexts) {
        if (TrimView(text).empty()) {
            result.push_back('\n');
            continue;
        }
        result += ParseIncludeText(text).line;
        result.push_back('\n');
    }
    return result;
}

std::string FormatIncludeEntriesText(
    const FormatterConfig& config,
    const std::vector<std::string>& includeTexts,
    std::string_view sourcePath
) {
    if (config.includeGroups.empty()) {
        return FormatIncludeTextsPreservingOrder(includeTexts);
    }

    const IncludeSortContext context{.config = config, .mainIncludeRegex = BuildMainIncludeRegex(config, sourcePath)};
    std::vector<IncludeEntry> includes;
    includes.reserve(includeTexts.size());
    for (const std::string& text : includeTexts) {
        if (TrimView(text).empty()) {
            continue;
        }
        IncludeText include = ParseIncludeText(text);
        includes.push_back({
            .line = std::move(include.line),
            .target = include.target,
            .sortKey = ToLower(include.target),
            .priority = IncludePriority(context, include.target),
            .originalIndex = includes.size()
        });
    }

    std::stable_sort(includes.begin(), includes.end(), [](const IncludeEntry& left, const IncludeEntry& right) {
        if (left.priority != right.priority) {
            return left.priority < right.priority;
        }
        if (left.sortKey != right.sortKey) {
            return left.sortKey < right.sortKey;
        }
        return left.originalIndex < right.originalIndex;
    });

    std::string result;
    int previousPriority = 0;
    bool first = true;
    for (const IncludeEntry& include : includes) {
        if (!first && previousPriority != include.priority) {
            result.push_back('\n');
        }
        result += include.line;
        result.push_back('\n');
        previousPriority = include.priority;
        first = false;
    }
    return result;
}

std::string FormatOpeningIncludeBlocks(
    const FormatterConfig& config,
    std::string_view text,
    std::string_view sourcePath
) {
    const std::vector<std::string> lines = SplitLines(text);
    if (!IsIncludeGuardOpening(lines)) {
        return std::string(text);
    }

    size_t runStart = 2;
    while (runStart < lines.size() && (
        IsBlankSourceLine(lines[runStart]) ||
        IsCommentSourceLine(lines[runStart])
    )) {
        ++runStart;
    }
    if (runStart >= lines.size() || !IsIncludeSourceLine(lines[runStart])) {
        return std::string(text);
    }

    std::vector<std::string> includeLines;
    size_t runEnd = runStart;
    for (; runEnd < lines.size(); ++runEnd) {
        if (IsIncludeSourceLine(lines[runEnd])) {
            includeLines.push_back(lines[runEnd]);
            continue;
        }
        if (IsBlankSourceLine(lines[runEnd])) {
            size_t nextLine = runEnd;
            if (IsIncludeSeparatorBlank(lines, runEnd, nextLine)) {
                includeLines.emplace_back();
                runEnd = nextLine - 1;
                continue;
            }
            runEnd = nextLine;
            break;
        }
        break;
    }

    std::string result;
    AppendSourceLines(result, lines, 0, runStart);
    result.append(FormatIncludeEntriesText(config, includeLines, sourcePath));
    if (runEnd < lines.size()) {
        result.push_back('\n');
    }
    AppendSourceLines(result, lines, runEnd, lines.size());
    return result;
}

}  // namespace

std::string
    FormatIncludeRunText(const FormatterConfig& config, const SyntaxNode& includeRun, std::string_view sourcePath)
{
    std::vector<std::string> includeTexts;
    includeTexts.reserve(includeRun.children.size());
    for (const SyntaxNode* child : includeRun.children) {
        if (child == nullptr) {
            continue;
        }
        if (IsIncludeNode(*child)) {
            includeTexts.push_back(std::string(child->text));
        } else if (child->kind == SyntaxNodeKind::BlankLine) {
            includeTexts.emplace_back();
        }
    }
    return FormatIncludeEntriesText(config, includeTexts, sourcePath);
}

std::string FormatIncludeLinesText(
    const FormatterConfig& config,
    const std::vector<std::string>& includeLines,
    std::string_view sourcePath
) {
    return FormatIncludeEntriesText(config, includeLines, sourcePath);
}

std::string FormatOpeningIncludeBlocksText(
    const FormatterConfig& config,
    std::string_view text,
    std::string_view sourcePath
) {
    return FormatOpeningIncludeBlocks(config, text, sourcePath);
}
