#include "tools/impl/lint_checkers.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "tools/impl/tools_common.h"
#include "util/file_path.h"
#include "util/strings.h"

namespace tools::lint {

namespace {

struct Statement {
    int line = 0;
    std::string text;
    char terminator = '\0';
};

struct Definition {
    std::string relpath;
    int line = 0;
    std::string name;
    std::string ownerName;
    bool qualified = false;
};

bool FindingLess(const Finding& left, const Finding& right) {
    return std::tie(left.location, left.kind, left.message) < std::tie(right.location, right.kind, right.message);
}

bool FindingLocationMessageLess(const Finding& left, const Finding& right) {
    return std::tie(left.location, left.message) < std::tie(right.location, right.message);
}

std::regex MakeRegex(std::string pattern) {
    pattern = ReplaceAll(std::move(pattern), "(?:", "(");
    return std::regex(pattern, std::regex_constants::ECMAScript | std::regex_constants::optimize);
}

std::vector<Statement> CollectTopLevelStatements(const std::string& strippedText) {
    std::vector<Statement> statements;
    int depth = 0;
    std::vector<std::string> buffer;
    int startLine = 0;
    const std::vector<std::string> lines = SplitLines(strippedText);
    for (int index = 0; index < static_cast<int>(lines.size()); ++index) {
        const int lineNumber = index + 1;
        const std::string& rawLine = lines[index];
        const std::string line = Trim(rawLine);
        if (depth == 0 && !line.empty() && !StartsWith(line, "#")) {
            if (buffer.empty()) {
                startLine = lineNumber;
            }
            buffer.push_back(line);
            if (Contains(line, ";") || Contains(line, "{")) {
                const std::string statement = CollapseWhitespace(buffer);
                const size_t brace = statement.find('{');
                const size_t semi = statement.find(';');
                if (brace != std::string::npos && (semi == std::string::npos || brace < semi)) {
                    statements.push_back({startLine, Trim(statement.substr(0, brace)), '{'});
                } else if (semi != std::string::npos) {
                    statements.push_back({startLine, Trim(statement.substr(0, semi)), ';'});
                }
                buffer.clear();
            }
        }

        const int opens = static_cast<int>(std::count(rawLine.begin(), rawLine.end(), '{'));
        const int closes = static_cast<int>(std::count(rawLine.begin(), rawLine.end(), '}'));
        depth += opens - closes;
        if (depth < 0) {
            depth = 0;
        }
    }
    return statements;
}

void UpdateNamespaceStack(const std::string& line, std::vector<std::string>& namespaceStack) {
    std::string trimmed = Trim(line);
    if (!trimmed.empty() && trimmed.back() == '{') {
        trimmed.pop_back();
        trimmed = Trim(trimmed);
        if (StartsWith(trimmed, "inline namespace ")) {
            namespaceStack.push_back(Trim(trimmed.substr(17)));
            return;
        }
        if (StartsWith(trimmed, "namespace ")) {
            namespaceStack.push_back(Trim(trimmed.substr(10)));
            return;
        }
    }
    if (trimmed == "}" && !namespaceStack.empty()) {
        namespaceStack.pop_back();
    }
}

std::string JoinNamespaceName(const std::vector<std::string>& namespaceStack, const std::string& name) {
    std::string qualified;
    for (const std::string& item : namespaceStack) {
        if (!qualified.empty()) {
            qualified += "::";
        }
        qualified += item;
    }
    if (!qualified.empty()) {
        qualified += "::";
    }
    qualified += name;
    return qualified;
}

std::optional<std::string> FunctionNameBeforeBody(std::string signaturePrefix) {
    signaturePrefix = Trim(signaturePrefix);
    const size_t closeParen = signaturePrefix.rfind(')');
    if (closeParen == std::string::npos) {
        return std::nullopt;
    }
    const std::string tail = Trim(signaturePrefix.substr(closeParen + 1));
    if (
        !tail.empty() &&
        tail != "const" &&
        tail != "noexcept" &&
        tail != "const noexcept" &&
        tail != "noexcept const" &&
        !StartsWith(tail, "->")
    ) {
        return std::nullopt;
    }
    const size_t openParen = signaturePrefix.rfind('(', closeParen);
    if (openParen == std::string::npos) {
        return std::nullopt;
    }
    const std::string parameters = signaturePrefix.substr(openParen + 1, closeParen - openParen - 1);
    if (Contains(parameters, "(") || Contains(parameters, ")")) {
        return std::nullopt;
    }
    size_t end = openParen;
    while (end > 0 && std::isspace(static_cast<unsigned char>(signaturePrefix[end - 1])) != 0) {
        --end;
    }
    size_t begin = end;
    while (begin > 0) {
        const char ch = signaturePrefix[begin - 1];
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_' && ch != ':' && ch != '~') {
            break;
        }
        --begin;
    }
    if (begin == end) {
        return std::nullopt;
    }
    if (begin > 0 && (signaturePrefix[begin - 1] == '.' || signaturePrefix[begin - 1] == ']')) {
        return std::nullopt;
    }
    if (begin > 1 && signaturePrefix[begin - 1] == '>' && signaturePrefix[begin - 2] == '-') {
        return std::nullopt;
    }
    std::string name = signaturePrefix.substr(begin, end - begin);
    if (Contains(name, "operator")) {
        return std::nullopt;
    }
    return name;
}

std::string FormatTemplate(std::string text, const std::map<std::string, std::string>& values) {
    for (const auto& [key, value] : values) {
        text = ReplaceAll(std::move(text), "{" + key + "}", value);
    }
    return text;
}

std::string TopLevelPackage(const std::string& moduleName) {
    const size_t slash = moduleName.find('/');
    return slash == std::string::npos ? moduleName : moduleName.substr(0, slash);
}

bool IsPackagePrivateModule(const std::string& moduleName) {
    return Split(moduleName, '/').size() > 2;
}

std::string ModuleDirectory(const std::string& moduleName) {
    const size_t slash = moduleName.find_last_of('/');
    return slash == std::string::npos ? "." : moduleName.substr(0, slash);
}

std::string FormatAllowedPackageDependencies(
    const std::string& package,
    const std::set<std::string>& dependencies,
    const std::set<std::string>& universal
) {
    std::set<std::string> allowed = dependencies;
    if (universal.find(package) == universal.end()) {
        allowed.insert(universal.begin(), universal.end());
    }
    std::vector<std::string> names;
    names.push_back(package);
    names.insert(names.end(), allowed.begin(), allowed.end());
    if (names.size() == 1) {
        return names[0] + " modules";
    }
    std::string text;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            text += i + 1 == names.size() ? ", or " : ", ";
        }
        text += names[i];
    }
    return text + " modules";
}

bool IsIdentifierChar(char ch) {
    return ch == '_' || std::isalnum(static_cast<unsigned char>(ch)) != 0;
}

int SkipQuotedLiteral(const std::string& text, int index, char quote) {
    ++index;
    while (index < static_cast<int>(text.size())) {
        const char ch = text[static_cast<size_t>(index)];
        if (ch == '\\' && index + 1 < static_cast<int>(text.size())) {
            index += 2;
            continue;
        }
        ++index;
        if (ch == quote) {
            break;
        }
    }
    return index;
}

int SkipRawStringLiteral(const std::string& text, int index) {
    const int delimiterStart = index + 2;
    const size_t paren = text.find('(', static_cast<size_t>(delimiterStart));
    if (paren == std::string::npos) {
        return index + 2;
    }
    const std::string delimiter = text.substr(static_cast<size_t>(delimiterStart), paren - delimiterStart);
    const std::string terminator = ")" + delimiter + '"';
    const size_t end = text.find(terminator, paren + 1);
    return end == std::string::npos ? static_cast<int>(text.size()) : static_cast<int>(end + terminator.size());
}

std::pair<int, int> LineBounds(const std::string& text, int index) {
    const size_t start = text.rfind('\n', static_cast<size_t>(index));
    const size_t end = text.find('\n', static_cast<size_t>(index));
    return {
        start == std::string::npos ? 0 : static_cast<int>(start + 1),
        end == std::string::npos ? static_cast<int>(text.size()) : static_cast<int>(end)
    };
}

bool IsAllowedWideConstPrefix(std::string prefix) {
    prefix = Trim(prefix);
    bool consumedStorage = true;
    while (consumedStorage) {
        consumedStorage = false;
        if (StartsWith(prefix, "static ")) {
            prefix = Trim(prefix.substr(6));
            consumedStorage = true;
        } else if (StartsWith(prefix, "inline ")) {
            prefix = Trim(prefix.substr(6));
            consumedStorage = true;
        }
    }
    if (StartsWith(prefix, "constexpr ")) {
        prefix = Trim(prefix.substr(10));
    } else if (StartsWith(prefix, "const ")) {
        prefix = Trim(prefix.substr(6));
    } else {
        return false;
    }
    const size_t wcharIndex = prefix.find("wchar_t");
    if (wcharIndex == std::string::npos) {
        return false;
    }
    if (wcharIndex != 0 && IsIdentifierChar(prefix[wcharIndex - 1])) {
        return false;
    }
    const size_t wcharEnd = wcharIndex + 7;
    if (wcharEnd < prefix.size() && IsIdentifierChar(prefix[wcharEnd])) {
        return false;
    }
    return EndsWith(Trim(prefix), "=");
}

bool IsAllowedConstWideStringLiteral(const std::string& text, int prefixIndex, int literalIndex, int endIndex) {
    if (literalIndex >= static_cast<int>(text.size()) || text[static_cast<size_t>(literalIndex)] != '"') {
        return false;
    }
    const auto [lineStart, lineEnd] = LineBounds(text, prefixIndex);
    const std::string line = text.substr(static_cast<size_t>(lineStart), static_cast<size_t>(lineEnd - lineStart));
    const int literalEndInLine = endIndex - lineStart;
    const size_t comment = line.find("//", static_cast<size_t>(literalEndInLine));
    if (comment == std::string::npos || Trim(line.substr(comment + 2)).empty()) {
        return false;
    }
    if (Trim(line.substr(static_cast<size_t>(literalEndInLine), comment - literalEndInLine)) != ";") {
        return false;
    }
    return IsAllowedWideConstPrefix(line.substr(0, static_cast<size_t>(prefixIndex - lineStart)));
}

std::vector<int> FindUndocumentedWideLiteralLines(const std::string& text) {
    std::vector<int> lines;
    int line = 1;
    int index = 0;
    bool inLineComment = false;
    bool inBlockComment = false;
    while (index < static_cast<int>(text.size())) {
        const char ch = text[static_cast<size_t>(index)];
        const char next = index + 1 < static_cast<int>(text.size()) ? text[static_cast<size_t>(index + 1)] : '\0';
        if (ch == '\n') {
            ++line;
        }
        if (inLineComment) {
            if (ch == '\n') {
                inLineComment = false;
            }
            ++index;
            continue;
        }
        if (inBlockComment) {
            if (ch == '*' && next == '/') {
                inBlockComment = false;
                index += 2;
            } else {
                ++index;
            }
            continue;
        }
        if (ch == '/' && next == '/') {
            inLineComment = true;
            index += 2;
            continue;
        }
        if (ch == '/' && next == '*') {
            inBlockComment = true;
            index += 2;
            continue;
        }
        if (ch == 'L' && (index == 0 || !IsIdentifierChar(text[static_cast<size_t>(index - 1)]))) {
            int literalIndex = index + 1;
            if (literalIndex < static_cast<int>(text.size()) && text[static_cast<size_t>(literalIndex)] == 'R') {
                ++literalIndex;
            }
            while (literalIndex < static_cast<int>(text.size()) && (
                text[static_cast<size_t>(literalIndex)] == ' ' ||
                text[static_cast<size_t>(literalIndex)] == '\t' ||
                text[static_cast<size_t>(literalIndex)] == '\r' ||
                text[static_cast<size_t>(literalIndex)] == '\n'
            )) {
                ++literalIndex;
            }
            if (literalIndex < static_cast<int>(text.size()) && (
                text[static_cast<size_t>(literalIndex)] == '"' || text[static_cast<size_t>(literalIndex)] == '\''
            )) {
                const bool raw = literalIndex > 0 && text.substr(static_cast<size_t>(literalIndex - 1), 2) == "R\"";
                const int end = raw ? SkipRawStringLiteral(text, literalIndex - 1) :
                    SkipQuotedLiteral(text, literalIndex, text[static_cast<size_t>(literalIndex)]);
                if (!IsAllowedConstWideStringLiteral(text, index, literalIndex, end)) {
                    lines.push_back(line);
                }
                line += static_cast<int>(
                    std::count(text.begin() + index, text.begin() + std::min(end, static_cast<int>(text.size())), '\n')
                );
                index = end;
                continue;
            }
        }
        if (ch == 'R' && next == '"') {
            const int end = SkipRawStringLiteral(text, index);
            line += static_cast<int>(
                std::count(text.begin() + index, text.begin() + std::min(end, static_cast<int>(text.size())), '\n')
            );
            index = end;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            const int end = SkipQuotedLiteral(text, index, ch);
            line += static_cast<int>(
                std::count(text.begin() + index, text.begin() + std::min(end, static_cast<int>(text.size())), '\n')
            );
            index = end;
            continue;
        }
        ++index;
    }
    return lines;
}

class ArchitectureChecker final : public Checker {
public:
    ArchitectureChecker(const JsonValue& config, CheckerContext context) :
        context_(std::move(context)),
        roots_(ConfigStrings(config, "roots")),
        headerSuffixes_(RequireSuffixGroup(
            context_.suffixGroups,
            "architecture.header_suffix_group",
            config.At("header_suffix_group").AsString()
        )),
        implementationSuffixes_(RequireSuffixGroup(
            context_.suffixGroups,
            "architecture.implementation_suffix_group",
            config.At("implementation_suffix_group").AsString()
        )),
        headerSuffix_(*headerSuffixes_.begin()),
        implementationSuffix_(*implementationSuffixes_.begin())
    {
        const std::vector<std::string> headerBodyAllowlist = ConfigStrings(config, "header_body_allowlist");
        headerBodyAllowlist_.insert(headerBodyAllowlist.begin(), headerBodyAllowlist.end());
        const std::vector<std::string> cppWithoutHeaderAllowlist =
            ConfigStrings(config, "cpp_without_header_allowlist");
        cppWithoutHeaderAllowlist_.insert(cppWithoutHeaderAllowlist.begin(), cppWithoutHeaderAllowlist.end());
        const std::vector<std::string> controlKeywords = ConfigStrings(config, "control_keywords");
        controlKeywords_.insert(controlKeywords.begin(), controlKeywords.end());
    }

    void ProcessFile(const FileRecord& record) override {
        if (!IsEligible(record)) {
            return;
        }
        if (headerSuffixes_.find(Extension(record.path)) != headerSuffixes_.end()) {
            ProcessHeader(record);
        } else if (implementationSuffixes_.find(Extension(record.path)) != implementationSuffixes_.end()) {
            ProcessImplementation(record);
        }
    }

    CheckResult Finish(bool verbose) override {
        (void)verbose;
        CollectDefinitionViolations();
        std::sort(violations_.begin(), violations_.end(), FindingLess);
        CheckResult result;
        result.title = "Architecture check:";
        result.findings = violations_;
        if (!result.findings.empty()) {
            result.summary =
                "Architecture check failed with " + std::to_string(result.findings.size()) + " violation(s).";
        }
        return result;
    }

private:
    bool IsEligible(const FileRecord& record) const {
        const std::string suffix = Extension(record.path);
        if (
            headerSuffixes_.find(suffix) == headerSuffixes_.end() &&
            implementationSuffixes_.find(suffix) == implementationSuffixes_.end()
        ) {
            return false;
        }
        if (!roots_.empty() && !HasRoot(record.relative, roots_)) {
            return false;
        }
        return !IsExcluded(record.relative, context_.excludedPrefixes);
    }

    void ProcessHeader(const FileRecord& record) {
        static const std::regex classDeclPattern(R"(\b(class|struct)\s+([A-Za-z_]\w*(::[A-Za-z_]\w*)*)\b)");
        static const std::regex
            freeDeclPattern(R"((^|[\s*&])([A-Za-z_]\w*)\s*\([^;]*\)\s*(const\b)?\s*(noexcept\b)?(\s*->\s*.+)?$)");

        std::vector<std::string> namespaceStack;
        for (const std::string& rawLine : record.strippedLines) {
            UpdateNamespaceStack(rawLine, namespaceStack);
            for (std::sregex_iterator it(rawLine.begin(), rawLine.end(), classDeclPattern), end; it != end; ++it) {
                const std::smatch& match = *it;
                const std::string suffix = rawLine.substr(static_cast<size_t>(match.position() + match.length()));
                if (!Contains(suffix, "{")) {
                    continue;
                }
                const std::string name = match.str(2);
                classHeaders_[name].insert(record.relative);
                if (!namespaceStack.empty()) {
                    classHeaders_[JoinNamespaceName(namespaceStack, name)].insert(record.relative);
                }
            }
        }

        for (const Statement& statement : CollectTopLevelStatements(record.strippedText)) {
            if (
                statement.terminator != ';' ||
                StartsWith(statement.text, "class ") ||
                StartsWith(statement.text, "struct ") ||
                StartsWith(statement.text, "enum ") ||
                StartsWith(statement.text, "using ") ||
                StartsWith(statement.text, "typedef ") ||
                Contains(statement.text, "operator")
            ) {
                continue;
            }
            std::smatch match;
            if (!std::regex_search(statement.text, match, freeDeclPattern)) {
                continue;
            }
            const std::string name = match.str(2);
            if (controlKeywords_.find(name) == controlKeywords_.end()) {
                freeHeaders_[name].insert(record.relative);
            }
        }

        if (headerBodyAllowlist_.find(record.relative) != headerBodyAllowlist_.end()) {
            return;
        }
        size_t brace = record.strippedText.find('{');
        while (brace != std::string::npos) {
            const size_t prefixStart = brace > 300 ? brace - 300 : 0;
            const std::string statementPrefix = Trim(record.strippedText.substr(prefixStart, brace - prefixStart));
            const std::optional<std::string> maybeName = FunctionNameBeforeBody(statementPrefix);
            if (!maybeName.has_value()) {
                brace = record.strippedText.find('{', brace + 1);
                continue;
            }
            const std::string name = *maybeName;
            if (controlKeywords_.find(name) != controlKeywords_.end()) {
                brace = record.strippedText.find('{', brace + 1);
                continue;
            }
            const size_t templatePrefixStart = brace > 80 ? brace - 80 : 0;
            const std::string prefix = record.strippedText.substr(templatePrefixStart, brace - templatePrefixStart);
            if (
                Contains(prefix, "template <") ||
                Contains(prefix, "template<") ||
                Contains(statementPrefix, "template <") ||
                Contains(statementPrefix, "template<")
            ) {
                brace = record.strippedText.find('{', brace + 1);
                continue;
            }
            const int line = static_cast<int>(
                std::count(record.strippedText.begin(), record.strippedText.begin() + brace, '\n')
            ) + 1;
            violations_.push_back({
                record.relative + ":" + std::to_string(line),
                "header-body",
                "Function body for " + name + " appears in a header; move non-template logic to a matching .cpp file."
            });
            brace = record.strippedText.find('{', brace + 1);
        }
    }

    void ProcessImplementation(const FileRecord& record) {
        static const std::regex qualifiedDefPattern(
            R"((^|[\s*&])([A-Za-z_]\w*(::[A-Za-z_~]\w*)+)\s*\([^;]*\)\s*(const\b)?\s*(noexcept\b)?(\s*->\s*.+)?$)"
        );
        static const std::regex
            freeDeclPattern(R"((^|[\s*&])([A-Za-z_]\w*)\s*\([^;]*\)\s*(const\b)?\s*(noexcept\b)?(\s*->\s*.+)?$)");

        if (cppWithoutHeaderAllowlist_.find(record.relative) == cppWithoutHeaderAllowlist_.end()) {
            const std::string expected = PairedHeaderForCpp(record.relative);
            if (!FileExists(FilePath(context_.projectRoot) / expected)) {
                violations_.push_back({
                    record.relative + ":1",
                    "missing-header",
                    record.relative +
                        " has no matching header " +
                        expected +
                        "; add one or allowlist the translation unit."
                });
            }
        }

        for (const Statement& statement : CollectTopLevelStatements(record.strippedText)) {
            if (statement.terminator != '{' || Contains(statement.text, "operator")) {
                continue;
            }
            std::smatch match;
            if (std::regex_search(statement.text, match, qualifiedDefPattern)) {
                const std::string fullName = match.str(2);
                const size_t ownerEnd = fullName.rfind("::");
                definitions_.push_back({record.relative, statement.line, fullName, fullName.substr(0, ownerEnd), true});
                continue;
            }
            if (!std::regex_search(statement.text, match, freeDeclPattern)) {
                continue;
            }
            const std::string name = match.str(2);
            if (controlKeywords_.find(name) == controlKeywords_.end()) {
                definitions_.push_back({record.relative, statement.line, name, name, false});
            }
        }
    }

    void CollectDefinitionViolations() {
        for (const Definition& definition : definitions_) {
            const auto owners =
                definition.qualified ? classHeaders_.find(definition.ownerName) : freeHeaders_.find(definition.name);
            if (
                owners == (definition.qualified ? classHeaders_.end() : freeHeaders_.end()) ||
                owners->second.size() != 1
            ) {
                continue;
            }
            const std::string ownerHeader = *owners->second.begin();
            const std::string expectedCpp = PairedCppForHeader(ownerHeader);
            if (definition.relpath == expectedCpp) {
                continue;
            }
            violations_.push_back({
                definition.relpath + ":" + std::to_string(definition.line),
                "impl-mismatch",
                definition.name +
                    " is declared from " +
                    ownerHeader +
                    " but implemented in " +
                    definition.relpath +
                    "; expected " +
                    expectedCpp +
                    "."
            });
        }
    }

    std::string PairedCppForHeader(const std::string& header) const {
        return header.substr(0, header.size() - headerSuffix_.size()) + implementationSuffix_;
    }

    std::string PairedHeaderForCpp(const std::string& cpp) const {
        return cpp.substr(0, cpp.size() - implementationSuffix_.size()) + headerSuffix_;
    }

    CheckerContext context_;
    std::vector<std::string> roots_;
    std::set<std::string> headerSuffixes_;
    std::set<std::string> implementationSuffixes_;
    std::string headerSuffix_;
    std::string implementationSuffix_;
    std::set<std::string> headerBodyAllowlist_;
    std::set<std::string> cppWithoutHeaderAllowlist_;
    std::set<std::string> controlKeywords_;
    std::map<std::string, std::set<std::string>> classHeaders_;
    std::map<std::string, std::set<std::string>> freeHeaders_;
    std::vector<Definition> definitions_;
    std::vector<Finding> violations_;
};

struct IncludeRoot {
    std::string name;
    std::string path;
};

class IncludeStyleChecker final : public Checker {
public:
    IncludeStyleChecker(const JsonValue& config, CheckerContext context) :
        context_(std::move(context)),
        roots_(ConfigStrings(config, "roots")),
        suffixes_(RequireSuffixGroup(
            context_.suffixGroups,
            "include_style.suffix_group",
            config.At("suffix_group").AsString()
        )),
        trackedOnly_(config.Find("tracked_only") != nullptr && config.At("tracked_only").AsBool()),
        nolintPattern_(MakeRegex(config.At("nolint_pattern").AsString())),
        nolintMessage_(config.At("nolint_message").AsString())
    {
        for (const std::string& root : ConfigStrings(config, "include_roots")) {
            includeRoots_.push_back({root, AbsolutePath((FilePath(context_.projectRoot) / root).string())});
        }
    }

    void ProcessFile(const FileRecord& record) override {
        if (!IsEligible(record)) {
            return;
        }
        for (int index = 0; index < static_cast<int>(record.lines.size()); ++index) {
            if (std::regex_search(record.lines[static_cast<size_t>(index)], nolintPattern_)) {
                violations_.push_back(
                    {record.relative + ":" + std::to_string(index + 1), "include-style", nolintMessage_}
                );
            }
        }

        for (const IncludeDirective& include : record.includes) {
            if (!include.quoted) {
                continue;
            }
            const auto resolved = ResolveProjectInclude(record.path, include.text);
            if (!resolved.has_value()) {
                continue;
            }
            const std::string expected = ExpectedIncludeFor(resolved->first, resolved->second);
            if (NormalizeInclude(include.text) == expected) {
                continue;
            }
            violations_.push_back({
                record.relative + ":" + std::to_string(include.line),
                "include-style",
                "Project header \"" +
                    include.text +
                    "\" resolves to " +
                    RelativePath(resolved->first, context_.projectRoot) +
                    "; use \"" +
                    expected +
                    "\" from the " +
                    resolved->second +
                    " include root instead of a relative or local shorthand path."
            });
        }
    }

    CheckResult Finish(bool verbose) override {
        (void)verbose;
        std::sort(violations_.begin(), violations_.end(), FindingLocationMessageLess);
        CheckResult result;
        result.title = "Include style check:";
        result.findings = violations_;
        if (!result.findings.empty()) {
            result.summary =
                "Include style check failed with " + std::to_string(result.findings.size()) + " violation(s).";
        }
        return result;
    }

private:
    bool IsEligible(const FileRecord& record) const {
        if (trackedOnly_ && !record.tracked) {
            return false;
        }
        if (suffixes_.find(Extension(record.path)) == suffixes_.end()) {
            return false;
        }
        if (!roots_.empty() && !HasRoot(record.relative, roots_)) {
            return false;
        }
        return !IsExcluded(record.relative, context_.excludedPrefixes);
    }

    std::optional<std::pair<std::string, std::string>>
        ResolveProjectInclude(const std::string& currentFile, const std::string& includeText) const
    {
        const std::string includePath = NormalizeInclude(includeText);
        std::vector<std::string> candidates;
        candidates.push_back((FilePath(currentFile).ParentPath() / includePath).string());
        for (const IncludeRoot& root : includeRoots_) {
            candidates.push_back((FilePath(root.path) / includePath).string());
        }
        for (const std::string& candidate : candidates) {
            if (!FileExists(candidate)) {
                continue;
            }
            const std::string resolved = AbsolutePath(candidate);
            for (const IncludeRoot& root : includeRoots_) {
                const std::string relativeToRoot = RelativePath(resolved, root.path);
                if (relativeToRoot == NormalizeSeparators(resolved)) {
                    continue;
                }
                const std::string projectRelative = RelativePath(resolved, context_.projectRoot);
                if (IsExcluded(projectRelative, context_.excludedPrefixes)) {
                    return std::nullopt;
                }
                return std::make_pair(resolved, root.name);
            }
        }
        return std::nullopt;
    }

    std::string ExpectedIncludeFor(const std::string& path, const std::string& rootName) const {
        for (const IncludeRoot& root : includeRoots_) {
            if (root.name == rootName) {
                return RelativePath(path, root.path);
            }
        }
        throw std::runtime_error("Unknown include root " + rootName);
    }

    CheckerContext context_;
    std::vector<std::string> roots_;
    std::set<std::string> suffixes_;
    bool trackedOnly_ = false;
    std::regex nolintPattern_;
    std::string nolintMessage_;
    std::vector<IncludeRoot> includeRoots_;
    std::vector<Finding> violations_;
};

struct Module {
    std::string name;
    std::string directory;
    int headerFiles = 0;
    int headerLoc = 0;
    int cppFiles = 0;
    int cppLoc = 0;

    int TotalLoc() const {
        return headerLoc + cppLoc;
    }
};

struct IncludeUse {
    std::string source;
    std::string target;
    std::string kind;
};

struct PendingInclude {
    std::string sourceModule;
    std::string sourceFile;
    IncludeDirective include;
    std::string kind;
};

struct LargeFile {
    std::string relative;
    int loc = 0;
};

struct ExternalModule {
    std::string name;
    std::string directory;
    std::set<std::string> includeNames;
    std::set<std::string> allowedPackages;
    std::string violationKind;
    std::string violationMessage;
};

struct PackageLocSummary {
    int headerFiles = 0;
    int headerLoc = 0;
    int cppFiles = 0;
    int cppLoc = 0;

    int TotalLoc() const {
        return headerLoc + cppLoc;
    }
};

class SourceDependencyChecker final : public Checker {
public:
    SourceDependencyChecker(const JsonValue& config, CheckerContext context) :
        context_(std::move(context)),
        roots_(ConfigStrings(config, "roots")),
        sourceRootName_(roots_.empty() ? "" : roots_[0]),
        sourceRoot_(AbsolutePath((FilePath(context_.projectRoot) / sourceRootName_).string())),
        suffixes_(RequireSuffixGroup(
            context_.suffixGroups,
            "source_dependencies.suffix_group",
            config.At("suffix_group").AsString()
        )),
        headerSuffixes_(RequireSuffixGroup(
            context_.suffixGroups,
            "source_dependencies.header_suffix_group",
            config.At("header_suffix_group").AsString()
        )),
        largeSourceFileLocThreshold_(
            config.Find("large_source_file_loc_threshold") != nullptr ?
                config.At("large_source_file_loc_threshold").AsInt() : 1000
        ),
        packageEncapsulationMessage_(config.At("package_encapsulation_message").AsString()),
        packageDependencyMessage_(config.At("package_dependency_message").AsString())
    {
        const std::vector<std::string> universalPackageDependencies =
            ConfigStrings(config, "universal_package_dependencies");
        universalPackageDependencies_.insert(universalPackageDependencies.begin(), universalPackageDependencies.end());
        for (const auto& [package, dependencies] : config.At("package_dependency_limits").AsObject()) {
            std::set<std::string> values;
            for (const JsonValue& dependency : dependencies.AsArray()) {
                values.insert(dependency.AsString());
            }
            packageDependencyLimits_[package] = std::move(values);
        }
        const JsonValue* externalModules = config.Find("external_modules");
        if (externalModules != nullptr) {
            for (const JsonValue& item : externalModules->AsArray()) {
                ExternalModule external;
                external.name = item.At("name").AsString();
                const JsonValue* directory = item.Find("directory");
                external.directory = directory != nullptr ? directory->AsString() : "external";
                for (const JsonValue& includeName : item.At("include_names").AsArray()) {
                    external.includeNames.insert(ToLower(includeName.AsString()));
                }
                for (const JsonValue& package : item.At("allowed_packages").AsArray()) {
                    external.allowedPackages.insert(package.AsString());
                }
                external.violationKind = item.At("violation_kind").AsString();
                external.violationMessage = item.At("violation_message").AsString();
                for (const std::string& includeName : external.includeNames) {
                    externalModuleByInclude_[includeName] = external.name;
                }
                externalModuleByName_[external.name] = external;
            }
        }
    }

    void ProcessFile(const FileRecord& record) override {
        if (!IsEligible(record)) {
            return;
        }
        const std::string moduleName = ModuleNameFor(record.path);
        if (moduleName.empty()) {
            return;
        }
        Module& module = modules_[moduleName];
        module.name = moduleName;
        module.directory = ModuleDirectory(moduleName);
        if (headerSuffixes_.find(Extension(record.path)) != headerSuffixes_.end()) {
            ++module.headerFiles;
            module.headerLoc += record.LineCount();
        } else {
            ++module.cppFiles;
            module.cppLoc += record.LineCount();
        }
        moduleByFile_[NormalizePathKey(record.path)] = moduleName;
        if (record.LineCount() > largeSourceFileLocThreshold_) {
            largeFiles_.push_back({record.relative, record.LineCount()});
        }

        const std::string kind =
            headerSuffixes_.find(Extension(record.path)) != headerSuffixes_.end() ? "public" : "private";
        for (const IncludeDirective& include : record.includes) {
            const std::string normalized = ToLower(NormalizeInclude(include.text));
            const auto external = externalModuleByInclude_.find(normalized);
            if (external != externalModuleByInclude_.end()) {
                uses_.push_back({moduleName, external->second, kind});
                continue;
            }
            pendingIncludes_.push_back({moduleName, record.path, include, kind});
        }
    }

    CheckResult Finish(bool verbose) override {
        ResolvePendingIncludes();
        edges_ = MergeEdges(uses_);
        for (const auto& [edge, kind] : edges_) {
            (void)kind;
            const auto external = externalModuleByName_.find(edge.second);
            if (external != externalModuleByName_.end() && modules_.find(edge.second) == modules_.end()) {
                modules_[edge.second] = {external->second.name, external->second.directory};
            }
        }

        std::vector<std::string> errors;
        const auto packageOrder = TopologicalPackageOrder();
        if (!packageOrder.second.empty()) {
            errors.push_back(packageOrder.second);
        }

        std::vector<Finding> findings;
        if (context_.checkDependencies) {
            const std::vector<Finding> graphFindings = CheckGraphRules();
            findings.insert(findings.end(), graphFindings.begin(), graphFindings.end());
        }
        std::sort(findings.begin(), findings.end(), FindingLess);

        CheckResult result;
        result.title = "Source dependency check:";
        result.errors = std::move(errors);
        result.findings = std::move(findings);
        if (!result.findings.empty()) {
            result.summary =
                "Source dependency check failed with " + std::to_string(result.findings.size()) + " violation(s).";
        }
        if (verbose) {
            result.verboseLines = VerboseLines(packageOrder.first);
        }
        return result;
    }

private:
    bool IsEligible(const FileRecord& record) const {
        if (suffixes_.find(Extension(record.path)) == suffixes_.end()) {
            return false;
        }
        if (!roots_.empty() && !HasRoot(record.relative, roots_)) {
            return false;
        }
        return !IsExcluded(record.relative, context_.excludedPrefixes);
    }

    std::string ModuleNameFor(const std::string& path) const {
        return RemoveExtension(RelativePath(path, sourceRoot_));
    }

    void ResolvePendingIncludes() {
        for (const PendingInclude& pending : pendingIncludes_) {
            const std::optional<std::string> target = ResolveInclude(pending.sourceFile, pending.include.text);
            if (!target.has_value() || *target == pending.sourceModule) {
                continue;
            }
            uses_.push_back({pending.sourceModule, *target, pending.kind});
        }
    }

    std::optional<std::string> ResolveInclude(const std::string& currentFile, const std::string& includeText) const {
        const std::string normalized = NormalizeInclude(includeText);
        const std::vector<std::string> candidates = {
            (FilePath(currentFile).ParentPath() / normalized).string(),
            (FilePath(sourceRoot_) / normalized).string(),
            (FilePath(context_.projectRoot) / normalized).string()
        };
        for (const std::string& candidate : candidates) {
            if (!FileExists(candidate)) {
                continue;
            }
            const auto found = moduleByFile_.find(NormalizePathKey(candidate));
            if (found != moduleByFile_.end()) {
                return found->second;
            }
        }
        return std::nullopt;
    }

    std::map<std::pair<std::string, std::string>, std::string> MergeEdges(const std::vector<IncludeUse>& uses) const {
        std::map<std::pair<std::string, std::string>, std::string> edges;
        for (const IncludeUse& use : uses) {
            const std::pair<std::string, std::string> key{use.source, use.target};
            const auto previous = edges.find(key);
            if (previous != edges.end() && (previous->second == "public" || previous->second == use.kind)) {
                continue;
            }
            edges[key] = use.kind == "public" ? "public" : "private";
        }
        return edges;
    }

    std::vector<Finding> CheckGraphRules() const {
        std::vector<Finding> findings = CheckPackageEncapsulation();
        const std::vector<Finding> dependencyFindings = CheckPackageDependencyLimits();
        const std::vector<Finding> externalFindings = CheckExternalModuleLimits();
        findings.insert(findings.end(), dependencyFindings.begin(), dependencyFindings.end());
        findings.insert(findings.end(), externalFindings.begin(), externalFindings.end());
        return findings;
    }

    std::vector<Finding> CheckPackageEncapsulation() const {
        std::vector<Finding> findings;
        for (const auto& [edge, kind] : edges_) {
            (void)kind;
            const std::string& source = edge.first;
            const std::string& target = edge.second;
            if (!IsPackagePrivateModule(target)) {
                continue;
            }
            const std::string sourcePackage = TopLevelPackage(source);
            const std::string targetPackage = TopLevelPackage(target);
            if (sourcePackage == targetPackage) {
                continue;
            }
            findings.push_back({
                source + " -> " + target,
                "package-encapsulation",
                FormatTemplate(packageEncapsulationMessage_, {
                    {"source", source},
                    {"target", target},
                    {"source_package", sourcePackage},
                    {"target_package", targetPackage},
                    {"target_package_root", sourceRootName_ + "/" + targetPackage}
                })
            });
        }
        return findings;
    }

    std::vector<Finding> CheckPackageDependencyLimits() const {
        std::vector<Finding> findings;
        for (const auto& [edge, kind] : edges_) {
            (void)kind;
            const std::string& source = edge.first;
            const std::string& target = edge.second;
            if (externalModuleByName_.find(target) != externalModuleByName_.end()) {
                continue;
            }
            const std::string sourcePackage = TopLevelPackage(source);
            const std::string targetPackage = TopLevelPackage(target);
            const auto allowed = packageDependencyLimits_.find(sourcePackage);
            if (allowed == packageDependencyLimits_.end()) {
                continue;
            }
            if (
                universalPackageDependencies_.find(targetPackage) != universalPackageDependencies_.end() &&
                universalPackageDependencies_.find(sourcePackage) == universalPackageDependencies_.end()
            ) {
                continue;
            }
            if (targetPackage == sourcePackage || allowed->second.find(targetPackage) != allowed->second.end()) {
                continue;
            }
            findings.push_back({
                source + " -> " + target,
                "package-dependency-" + sourcePackage,
                FormatTemplate(packageDependencyMessage_, {
                    {"source", source},
                    {"target", target},
                    {"source_package", sourcePackage},
                    {"target_package", targetPackage},
                    {
                        "allowed_dependencies",
                        FormatAllowedPackageDependencies(sourcePackage, allowed->second, universalPackageDependencies_)
                    }
                })
            });
        }
        return findings;
    }

    std::vector<Finding> CheckExternalModuleLimits() const {
        std::vector<Finding> findings;
        for (const auto& [edge, kind] : edges_) {
            (void)kind;
            const std::string& source = edge.first;
            const std::string& target = edge.second;
            const auto external = externalModuleByName_.find(target);
            if (external == externalModuleByName_.end()) {
                continue;
            }
            const std::string sourcePackage = TopLevelPackage(source);
            if (external->second.allowedPackages.find(sourcePackage) != external->second.allowedPackages.end()) {
                continue;
            }
            std::string allowedPackages;
            for (const std::string& package : external->second.allowedPackages) {
                if (!allowedPackages.empty()) {
                    allowedPackages += ", ";
                }
                allowedPackages += package;
            }
            findings.push_back(
                {source + " -> " + target, external->second.violationKind, FormatTemplate(
                    external->second.violationMessage,
                    {{"source", source}, {"target", target}, {"source_package", sourcePackage}, {
                        "allowed_packages",
                        allowedPackages
                    }}
                )}
            );
        }
        return findings;
    }

    std::map<std::string, std::set<std::string>> PackageDependencies() const {
        std::map<std::string, std::set<std::string>> dependencies;
        for (const auto& [name, module] : modules_) {
            (void)module;
            if (externalModuleByName_.find(name) == externalModuleByName_.end()) {
                dependencies[TopLevelPackage(name)];
            }
        }
        for (const auto& [edge, kind] : edges_) {
            (void)kind;
            if (
                externalModuleByName_.find(edge.first) != externalModuleByName_.end() ||
                externalModuleByName_.find(edge.second) != externalModuleByName_.end()
            ) {
                continue;
            }
            const std::string sourcePackage = TopLevelPackage(edge.first);
            const std::string targetPackage = TopLevelPackage(edge.second);
            dependencies[sourcePackage];
            dependencies[targetPackage];
            if (sourcePackage != targetPackage) {
                dependencies[sourcePackage].insert(targetPackage);
            }
        }
        return dependencies;
    }

    std::pair<std::vector<std::string>, std::string> TopologicalPackageOrder() const {
        const std::map<std::string, std::set<std::string>> graph = PackageDependencies();
        std::map<std::string, int> indegrees;
        for (const auto& [package, targets] : graph) {
            indegrees[package];
            for (const std::string& target : targets) {
                ++indegrees[target];
            }
        }
        std::vector<std::string> ready;
        for (const auto& [package, indegree] : indegrees) {
            if (indegree == 0) {
                ready.push_back(package);
            }
        }
        std::vector<std::string> ordered;
        while (!ready.empty()) {
            std::sort(ready.begin(), ready.end());
            const std::string package = ready.front();
            ready.erase(ready.begin());
            ordered.push_back(package);
            const auto targets = graph.find(package);
            if (targets == graph.end()) {
                continue;
            }
            for (const std::string& target : targets->second) {
                --indegrees[target];
                if (indegrees[target] == 0) {
                    ready.push_back(target);
                }
            }
        }
        if (ordered.size() == indegrees.size()) {
            return {ordered, {}};
        }
        std::string cyclic;
        for (const auto& [package, indegree] : indegrees) {
            (void)indegree;
            if (std::find(ordered.begin(), ordered.end(), package) != ordered.end()) {
                continue;
            }
            if (!cyclic.empty()) {
                cyclic += ", ";
            }
            cyclic += package;
        }
        return {ordered, "Package dependency graph is not a DAG; cycle detected among: " + cyclic + "."};
    }

    std::map<std::string, PackageLocSummary> SummarizePackageLoc() const {
        std::map<std::string, PackageLocSummary> summaries;
        for (const auto& [name, module] : modules_) {
            if (externalModuleByName_.find(name) != externalModuleByName_.end()) {
                continue;
            }
            PackageLocSummary& summary = summaries[TopLevelPackage(name)];
            summary.headerFiles += module.headerFiles;
            summary.headerLoc += module.headerLoc;
            summary.cppFiles += module.cppFiles;
            summary.cppLoc += module.cppLoc;
        }
        return summaries;
    }

    std::vector<std::string> VerboseLines(const std::vector<std::string>& packageOrder) const {
        std::vector<std::string> lines;
        if (!largeFiles_.empty()) {
            lines.push_back("Source files over " + FormatCount(largeSourceFileLocThreshold_) + " LOC:");
            std::vector<LargeFile> sorted = largeFiles_;
            std::sort(sorted.begin(), sorted.end(), [](const LargeFile& left, const LargeFile& right) {
                return std::tie(right.loc, left.relative) < std::tie(left.loc, right.relative);
            });
            for (const LargeFile& file : sorted) {
                lines.push_back("  " + file.relative + ": " + FormatCount(file.loc) + " LOC");
            }
        }
        lines.push_back("");
        lines.push_back("Package dependencies in topological order:");
        const std::map<std::string, PackageLocSummary> summaries = SummarizePackageLoc();
        const std::map<std::string, std::set<std::string>> graph = PackageDependencies();
        for (int index = 0; index < static_cast<int>(packageOrder.size()); ++index) {
            const std::string& package = packageOrder[static_cast<size_t>(index)];
            std::string dependencies = "(none)";
            const auto targets = graph.find(package);
            if (targets != graph.end() && !targets->second.empty()) {
                dependencies.clear();
                for (const std::string& target : targets->second) {
                    if (!dependencies.empty()) {
                        dependencies += ", ";
                    }
                    dependencies += target;
                }
            }
            const auto summary = summaries.find(package);
            const int totalLoc = summary == summaries.end() ? 0 : summary->second.TotalLoc();
            lines.push_back(
                "  " +
                    std::to_string(index + 1) +
                    ". " +
                    FormatCount(totalLoc) +
                    " LOC: " +
                    package +
                    " -> " +
                    dependencies
            );
        }
        return lines;
    }

    CheckerContext context_;
    std::vector<std::string> roots_;
    std::string sourceRootName_;
    std::string sourceRoot_;
    std::set<std::string> suffixes_;
    std::set<std::string> headerSuffixes_;
    int largeSourceFileLocThreshold_ = 1000;
    std::set<std::string> universalPackageDependencies_;
    std::map<std::string, std::set<std::string>> packageDependencyLimits_;
    std::string packageEncapsulationMessage_;
    std::string packageDependencyMessage_;
    std::map<std::string, ExternalModule> externalModuleByName_;
    std::map<std::string, std::string> externalModuleByInclude_;
    std::map<std::string, Module> modules_;
    std::map<std::string, std::string> moduleByFile_;
    std::vector<IncludeUse> uses_;
    std::vector<PendingInclude> pendingIncludes_;
    std::vector<LargeFile> largeFiles_;
    std::map<std::pair<std::string, std::string>, std::string> edges_;
};

struct LineRule {
    std::string kind;
    std::string patternText;
    std::regex pattern;
    std::string message;
    int captureGroup = -1;
    std::map<std::string, std::set<std::string>> allowedMatchesByFile;
    std::vector<std::string> excludedPrefixes;
};

bool LineCouldMatchRule(const LineRule& rule, const std::string& line) {
    if (Contains(rule.patternText, "std\\s*::\\s*function")) {
        return Contains(line, "std") && Contains(line, "function");
    }
    if (Contains(rule.patternText, "filesystem")) {
        return Contains(line, "filesystem");
    }
    if (Contains(rule.patternText, "std\\s*::\\s*hash")) {
        return Contains(line, "std") && Contains(line, "hash");
    }
    if (Contains(rule.patternText, "condition_variable")) {
        return Contains(line, "condition_variable") ||
            Contains(line, "jthread") ||
            Contains(line, "mutex") ||
            Contains(line, "shared_mutex") ||
            Contains(line, "thread") ||
            Contains(line, "timed_mutex");
    }
    if (Contains(rule.patternText, "#\\s*(?:if")) {
        return StartsWith(Trim(line), "#");
    }
    if (Contains(rule.patternText, "A-Za-z0-9_]*W")) {
        return Contains(line, "W") && Contains(line, "(");
    }
    if (Contains(rule.patternText, "AddComboStringUtf8")) {
        return Contains(line, "Utf8") || Contains(line, "kAppTitleUtf8");
    }
    return true;
}

class SourcePolicyChecker final : public Checker {
public:
    SourcePolicyChecker(const JsonValue& config, CheckerContext context) :
        context_(std::move(context)),
        roots_(ConfigStrings(config, "roots")),
        suffixes_(RequireSuffixGroup(
            context_.suffixGroups,
            "source_policy.suffix_group",
            config.At("suffix_group").AsString()
        )),
        guardrailsDoc_(config.Find("guardrails_doc") != nullptr ? config.At("guardrails_doc").AsString() : ""),
        wideLiteralMessage_(config.At("wide_literals").At("message").AsString())
    {
        const JsonValue* rules = config.Find("line_rules");
        if (rules != nullptr) {
            for (const JsonValue& rule : rules->AsArray()) {
                lineRules_.push_back(ParseRule(rule));
            }
        }
    }

    void ProcessFile(const FileRecord& record) override {
        if (!IsEligible(record)) {
            return;
        }
        for (int index = 0; index < static_cast<int>(record.strippedLines.size()); ++index) {
            const std::string& line = record.strippedLines[static_cast<size_t>(index)];
            for (const LineRule& rule : lineRules_) {
                if (IsExcluded(record.relative, rule.excludedPrefixes)) {
                    // Repository tools are not size-optimized, so src/tools/ may use STL threading primitives.
                    continue;
                }
                if (!LineCouldMatchRule(rule, line)) {
                    continue;
                }
                try {
                    for (std::sregex_iterator it(line.begin(), line.end(), rule.pattern), end; it != end; ++it) {
                        const std::smatch& match = *it;
                        if (rule.captureGroup >= 0) {
                            const std::string matched = match.str(static_cast<size_t>(rule.captureGroup));
                            const auto allowedFile = rule.allowedMatchesByFile.find(record.relative);
                            if (
                                allowedFile != rule.allowedMatchesByFile.end() &&
                                allowedFile->second.find(matched) != allowedFile->second.end()
                            ) {
                                continue;
                            }
                        }
                        violations_.push_back({
                            record.relative + ":" + std::to_string(index + 1),
                            rule.kind,
                            FormatRuleMessage(rule.message, match)
                        });
                        if (rule.captureGroup < 0) {
                            break;
                        }
                    }
                } catch (const std::regex_error& error) {
                    throw std::runtime_error(
                        "source-policy regex failed for pattern " + rule.patternText + ": " + error.what()
                    );
                }
            }
        }

        for (int line : FindUndocumentedWideLiteralLines(record.text)) {
            violations_.push_back({
                record.relative + ":" + std::to_string(line),
                "source-policy",
                FormatRuleMessage(wideLiteralMessage_, {})
            });
        }
    }

    CheckResult Finish(bool verbose) override {
        (void)verbose;
        std::sort(violations_.begin(), violations_.end(), FindingLocationMessageLess);
        CheckResult result;
        result.title = "Source policy check:";
        result.findings = violations_;
        if (!result.findings.empty()) {
            result.summary =
                "Source policy check failed with " + std::to_string(result.findings.size()) + " violation(s).";
        }
        return result;
    }

private:
    bool IsEligible(const FileRecord& record) const {
        if (suffixes_.find(Extension(record.path)) == suffixes_.end()) {
            return false;
        }
        if (!roots_.empty() && !HasRoot(record.relative, roots_)) {
            return false;
        }
        return !IsExcluded(record.relative, context_.excludedPrefixes);
    }

    LineRule ParseRule(const JsonValue& rule) const {
        LineRule parsed;
        const JsonValue* kind = rule.Find("kind");
        parsed.kind = kind != nullptr ? kind->AsString() : "source-policy";
        parsed.patternText = rule.At("pattern").AsString();
        parsed.pattern = MakeRegex(parsed.patternText);
        parsed.message = rule.At("message").AsString();
        const JsonValue* captureGroup = rule.Find("capture_group");
        parsed.captureGroup = captureGroup != nullptr && !captureGroup->IsNull() ? captureGroup->AsInt() : -1;
        const JsonValue* allowed = rule.Find("allowed_matches_by_file");
        if (allowed != nullptr) {
            for (const auto& [path, values] : allowed->AsObject()) {
                for (const JsonValue& item : values.AsArray()) {
                    parsed.allowedMatchesByFile[path].insert(item.AsString());
                }
            }
        }
        parsed.excludedPrefixes = ConfigStrings(rule, "excluded_prefixes");
        return parsed;
    }

    std::string FormatRuleMessage(std::string text, const std::smatch& match) const {
        text = ReplaceAll(std::move(text), "{guardrails_doc}", guardrailsDoc_);
        if (!match.empty()) {
            text = ReplaceAll(std::move(text), "{match}", match.str(0));
            for (size_t index = 1; index < match.size(); ++index) {
                text = ReplaceAll(std::move(text), "{match" + std::to_string(index) + "}", match.str(index));
            }
        }
        return text;
    }

    CheckerContext context_;
    std::vector<std::string> roots_;
    std::set<std::string> suffixes_;
    std::string guardrailsDoc_;
    std::vector<LineRule> lineRules_;
    std::string wideLiteralMessage_;
    std::vector<Finding> violations_;
};

}  // namespace

Checker::~Checker() = default;

std::vector<std::unique_ptr<Checker>> CreateCheckers(const JsonValue& config, const CheckerContext& context) {
    std::vector<std::unique_ptr<Checker>> checkers;
    checkers.push_back(std::make_unique<ArchitectureChecker>(config.At("architecture"), context));
    checkers.push_back(std::make_unique<SourceDependencyChecker>(config.At("source_dependencies"), context));
    checkers.push_back(std::make_unique<IncludeStyleChecker>(config.At("include_style"), context));
    checkers.push_back(std::make_unique<SourcePolicyChecker>(config.At("source_policy"), context));
    return checkers;
}

}  // namespace tools::lint
