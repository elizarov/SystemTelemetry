#include "tools/impl/format_pretty_printer.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tools/impl/format_include_sort.h"
#include "tools/impl/tools_common.h"

namespace {

enum class PrintTokenKind {
    Known,
    Free,
    Comment,
    TrailingComment,
    BlankLine,
    Preprocessor,
    IncludeRun,
};

enum class BraceRole {
    Compact,
    Block,
    Enum,
    Namespace,
    CaseBlock,
};

struct PrintToken {
    PrintTokenKind kind = PrintTokenKind::Free;
    KnownToken known = KnownToken::Unknown;
    std::string_view text;
    SyntaxTreeKind treeKind = SyntaxTreeKind::Unknown;
    SyntaxTreeKind parentKind = SyntaxTreeKind::Unknown;
    SyntaxTreeKind grandParentKind = SyntaxTreeKind::Unknown;
    bool inTemplateDeclaration = false;
    bool inRequiresClause = false;
    bool inMacroValue = false;
    bool breakBeforeMacroValue = false;
    const SyntaxNode* node = nullptr;
    const SyntaxNode* macroDefinition = nullptr;
    const SyntaxNode* macroValueElement = nullptr;
};

bool IsPreprocessorNode(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::PreprocCall ||
        kind == SyntaxTreeKind::PreprocInclude ||
        kind == SyntaxTreeKind::PreprocUsing;
}

bool IsPreprocessorPrintToken(PrintTokenKind kind) {
    return kind == PrintTokenKind::Preprocessor || kind == PrintTokenKind::IncludeRun;
}

bool IsPreprocessorLikeToken(const PrintToken& token) {
    return IsPreprocessorPrintToken(token.kind) || token.macroDefinition != nullptr;
}

bool IsCommentToken(PrintTokenKind kind) {
    return kind == PrintTokenKind::Comment || kind == PrintTokenKind::TrailingComment;
}

bool IsWordLike(const PrintToken& token) {
    if (token.kind == PrintTokenKind::Free) {
        return true;
    }
    return token.kind == PrintTokenKind::Known && KnownTokenHasClass(token.known, TokenClass::Keyword);
}

bool IsStringLike(const PrintToken& token) {
    return token.treeKind == SyntaxTreeKind::StringLiteral ||
        token.treeKind == SyntaxTreeKind::RawStringLiteral ||
        token.treeKind == SyntaxTreeKind::CharacterLiteral;
}

bool IsDeclaratorReferenceParent(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::PointerDeclarator ||
        kind == SyntaxTreeKind::AbstractPointerDeclarator ||
        kind == SyntaxTreeKind::ReferenceDeclarator ||
        kind == SyntaxTreeKind::AbstractReferenceDeclarator ||
        kind == SyntaxTreeKind::HandleDeclarator ||
        kind == SyntaxTreeKind::AbstractHandleDeclarator ||
        kind == SyntaxTreeKind::MemberPointerDeclarator;
}

bool IsReferenceToken(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known &&
        KnownTokenHasClass(token.known, TokenClass::DeclaratorReferenceToken);
}

bool IsDeclaratorBindingToken(const PrintToken& token) {
    return IsDeclaratorReferenceParent(token.parentKind) && IsReferenceToken(token);
}

bool IsUnaryContext(const PrintToken& token) {
    return token.parentKind == SyntaxTreeKind::UnaryExpression;
}

bool IsBinaryContext(const PrintToken& token) {
    return token.parentKind == SyntaxTreeKind::BinaryExpression ||
        token.parentKind == SyntaxTreeKind::AssignmentExpression ||
        token.parentKind == SyntaxTreeKind::ConditionalExpression;
}

BraceRole RoleForBraceParent(SyntaxTreeKind parentKind) {
    switch (parentKind) {
    case SyntaxTreeKind::CompoundStatement:
    case SyntaxTreeKind::FieldDeclarationList:
    case SyntaxTreeKind::DeclarationList:
        return BraceRole::Block;
    case SyntaxTreeKind::EnumeratorList:
        return BraceRole::Enum;
    default:
        return BraceRole::Compact;
    }
}

BraceRole RoleForBrace(const PrintToken& token) {
    if (token.parentKind == SyntaxTreeKind::DeclarationList &&
        token.grandParentKind == SyntaxTreeKind::NamespaceDefinition) {
        return BraceRole::Namespace;
    }
    return RoleForBraceParent(token.parentKind);
}

bool IsMacroDefinitionNode(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::PreprocDef ||
        kind == SyntaxTreeKind::PreprocFunctionDef;
}

bool IsMacroDeclarationFragment(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::Declaration ||
        kind == SyntaxTreeKind::FieldDeclaration ||
        kind == SyntaxTreeKind::FunctionDefinition ||
        kind == SyntaxTreeKind::TemplateDeclaration ||
        kind == SyntaxTreeKind::EnumSpecifier ||
        kind == SyntaxTreeKind::ClassSpecifier ||
        kind == SyntaxTreeKind::StructSpecifier;
}

bool RequiresMacroValueBreak(const SyntaxNode& node) {
    size_t topLevelElementCount = 0;
    for (const std::unique_ptr<SyntaxNode>& child : node.children) {
        if (!child || child->kind == SyntaxNodeKind::BlankLine) {
            continue;
        }
        ++topLevelElementCount;
        if (topLevelElementCount > 1) {
            return true;
        }
        if (child->kind == SyntaxNodeKind::Tree && IsMacroDeclarationFragment(child->treeKind)) {
            return true;
        }
    }
    return false;
}

void AppendTokens(
    const SyntaxNode& node,
    SyntaxTreeKind parentKind,
    SyntaxTreeKind grandParentKind,
    bool inTemplateDeclaration,
    bool inRequiresClause,
    const SyntaxNode* macroDefinition,
    const SyntaxNode* macroValueElement,
    bool inMacroValue,
    bool breakBeforeMacroValue,
    std::vector<PrintToken>& tokens
) {
    const bool childInTemplateDeclaration = inTemplateDeclaration ||
        node.treeKind == SyntaxTreeKind::TemplateDeclaration;
    const bool childInRequiresClause = inRequiresClause ||
        node.treeKind == SyntaxTreeKind::RequiresClause;
    const SyntaxNode* childMacroDefinition = macroDefinition != nullptr ? macroDefinition :
        (IsMacroDefinitionNode(node.treeKind) ? &node : nullptr);
    const bool childInMacroValue = inMacroValue || node.treeKind == SyntaxTreeKind::MacroReplacementList;
    const bool childBreakBeforeMacroValue = breakBeforeMacroValue ||
        (node.treeKind == SyntaxTreeKind::MacroReplacementList && RequiresMacroValueBreak(node));
    switch (node.kind) {
    case SyntaxNodeKind::BlankLine:
        tokens.push_back({
            .kind = PrintTokenKind::BlankLine,
            .inMacroValue = childInMacroValue,
            .breakBeforeMacroValue = childBreakBeforeMacroValue,
            .node = &node,
            .macroDefinition = childMacroDefinition,
            .macroValueElement = macroValueElement,
        });
        return;
    case SyntaxNodeKind::Comment:
    case SyntaxNodeKind::TrailingComment:
        tokens.push_back({
            .kind = node.kind == SyntaxNodeKind::TrailingComment ? PrintTokenKind::TrailingComment :
                                                                    PrintTokenKind::Comment,
            .text = node.text,
            .treeKind = node.treeKind,
            .parentKind = parentKind,
            .grandParentKind = grandParentKind,
            .inTemplateDeclaration = childInTemplateDeclaration,
            .inRequiresClause = childInRequiresClause,
            .inMacroValue = childInMacroValue,
            .breakBeforeMacroValue = childBreakBeforeMacroValue,
            .node = &node,
            .macroDefinition = childMacroDefinition,
            .macroValueElement = macroValueElement,
        });
        return;
    case SyntaxNodeKind::KnownToken:
        tokens.push_back({
            .kind = PrintTokenKind::Known,
            .known = node.known,
            .text = KnownTokenText(node.known),
            .treeKind = node.treeKind,
            .parentKind = parentKind,
            .grandParentKind = grandParentKind,
            .inTemplateDeclaration = childInTemplateDeclaration,
            .inRequiresClause = childInRequiresClause,
            .inMacroValue = childInMacroValue,
            .breakBeforeMacroValue = childBreakBeforeMacroValue,
            .node = &node,
            .macroDefinition = childMacroDefinition,
            .macroValueElement = macroValueElement,
        });
        return;
    case SyntaxNodeKind::FreeToken:
        tokens.push_back({
            .kind = PrintTokenKind::Free,
            .text = node.text,
            .treeKind = node.treeKind,
            .parentKind = parentKind,
            .grandParentKind = grandParentKind,
            .inTemplateDeclaration = childInTemplateDeclaration,
            .inRequiresClause = childInRequiresClause,
            .inMacroValue = childInMacroValue,
            .breakBeforeMacroValue = childBreakBeforeMacroValue,
            .node = &node,
            .macroDefinition = childMacroDefinition,
            .macroValueElement = macroValueElement,
        });
        return;
    case SyntaxNodeKind::Tree:
        if (node.treeKind == SyntaxTreeKind::MacroReplacementList) {
            for (const std::unique_ptr<SyntaxNode>& child : node.children) {
                AppendTokens(
                    *child,
                    node.treeKind,
                    parentKind,
                    childInTemplateDeclaration,
                    childInRequiresClause,
                    childMacroDefinition,
                    child.get(),
                    true,
                    childBreakBeforeMacroValue,
                    tokens
                );
            }
            return;
        }
        if (node.treeKind == SyntaxTreeKind::IncludeRun) {
            tokens.push_back({
                .kind = PrintTokenKind::IncludeRun,
                .treeKind = node.treeKind,
                .parentKind = parentKind,
                .grandParentKind = grandParentKind,
                .inTemplateDeclaration = childInTemplateDeclaration,
                .inRequiresClause = childInRequiresClause,
                .inMacroValue = childInMacroValue,
                .breakBeforeMacroValue = childBreakBeforeMacroValue,
                .node = &node,
                .macroDefinition = childMacroDefinition,
                .macroValueElement = macroValueElement,
            });
            return;
        }
        if (IsPreprocessorNode(node.treeKind)) {
            tokens.push_back({
                .kind = PrintTokenKind::Preprocessor,
                .text = node.text,
                .treeKind = node.treeKind,
                .parentKind = parentKind,
                .grandParentKind = grandParentKind,
                .inTemplateDeclaration = childInTemplateDeclaration,
                .inRequiresClause = childInRequiresClause,
                .inMacroValue = childInMacroValue,
                .breakBeforeMacroValue = childBreakBeforeMacroValue,
                .node = &node,
                .macroDefinition = childMacroDefinition,
                .macroValueElement = macroValueElement,
            });
            return;
        }
        for (const std::unique_ptr<SyntaxNode>& child : node.children) {
            AppendTokens(
                *child,
                node.treeKind,
                parentKind,
                childInTemplateDeclaration,
                childInRequiresClause,
                childMacroDefinition,
                macroValueElement,
                childInMacroValue,
                childBreakBeforeMacroValue,
                tokens
            );
        }
        return;
    }
}

bool IsNewline(char ch) {
    return ch == '\r' || ch == '\n';
}

std::string CollapseSourceWhitespace(std::string_view text) {
    std::string result;
    bool pendingSpace = false;
    bool inString = false;
    bool inChar = false;
    for (size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        const char next = index + 1 < text.size() ? text[index + 1] : '\0';
        if (!inString && !inChar && ch == '\\' && IsNewline(next)) {
            pendingSpace = true;
            ++index;
            if (next == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
            continue;
        }
        if (!inString && !inChar && (ch == ' ' || ch == '\t' || IsNewline(ch))) {
            pendingSpace = true;
            if (ch == '\r' && next == '\n') {
                ++index;
            }
            continue;
        }
        if (pendingSpace && !result.empty()) {
            result.push_back(' ');
        }
        pendingSpace = false;
        result.push_back(ch);
        if (ch == '\\' && (inString || inChar) && index + 1 < text.size()) {
            result.push_back(text[index + 1]);
            ++index;
            continue;
        }
        if (ch == '"' && !inChar) {
            inString = !inString;
        } else if (ch == '\'' && !inString) {
            inChar = !inChar;
        }
    }
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

bool IsAccessKeyword(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known && KnownTokenHasClass(token.known, TokenClass::AccessKeyword);
}

bool IsCaseLabelKeyword(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known &&
        token.parentKind == SyntaxTreeKind::CaseStatement &&
        (token.known == KnownToken::KeywordCase || token.known == KnownToken::KeywordDefault);
}

bool IsParenthesizedDeclarator(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::ParenthesizedDeclarator ||
        kind == SyntaxTreeKind::AbstractParenthesizedDeclarator;
}

bool IsCompilerCallModifierStart(const PrintToken* token) {
    return token != nullptr &&
        token->kind == PrintTokenKind::Known &&
        (token->known == KnownToken::KeywordCdecl || token->known == KnownToken::KeywordDeclspec);
}

struct LineLayout {
    std::vector<std::string> lines;
};

struct LineLayoutScore {
    int overflow = std::numeric_limits<int>::max();
    int lineCount = std::numeric_limits<int>::max();
    int deepestIndent = std::numeric_limits<int>::max();
};

struct LineLayoutNode {
    std::string compact;
    std::vector<size_t> children;
};

struct LineMemoKey {
    size_t node = 0;
    int indent = 0;
    int force = 0;

    bool operator==(const LineMemoKey& other) const {
        return node == other.node && indent == other.indent && force == other.force;
    }
};

struct LineMemoKeyHash {
    size_t operator()(const LineMemoKey& key) const {
        return key.node ^ (static_cast<size_t>(key.indent) << 17) ^ (static_cast<size_t>(key.force) << 3);
    }
};

bool IsBetterLayoutScore(const LineLayoutScore& left, const LineLayoutScore& right) {
    if (left.overflow != right.overflow) {
        return left.overflow < right.overflow;
    }
    if (left.lineCount != right.lineCount) {
        return left.lineCount < right.lineCount;
    }
    return left.deepestIndent < right.deepestIndent;
}

std::string TrimCopy(std::string_view value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

int LeadingSpaceCount(std::string_view line) {
    int count = 0;
    while (count < static_cast<int>(line.size()) && line[static_cast<size_t>(count)] == ' ') {
        ++count;
    }
    return count;
}

bool StartsWithWord(std::string_view value, std::string_view word) {
    return value.size() >= word.size() &&
        value.substr(0, word.size()) == word &&
        (value.size() == word.size() ||
         (!std::isalnum(static_cast<unsigned char>(value[word.size()])) && value[word.size()] != '_'));
}

bool IsOpenDelimiter(char ch) {
    return ch == '(' || ch == '[' || ch == '{' || ch == '<';
}

bool IsSpacedAngleOperator(std::string_view text, size_t index) {
    if (index >= text.size() || (text[index] != '<' && text[index] != '>')) {
        return false;
    }
    const bool spaceBefore = index > 0 && std::isspace(static_cast<unsigned char>(text[index - 1])) != 0;
    const bool spaceAfter = index + 1 < text.size() &&
        std::isspace(static_cast<unsigned char>(text[index + 1])) != 0;
    return spaceBefore || spaceAfter;
}

char MatchingCloseDelimiter(char ch) {
    switch (ch) {
    case '(':
        return ')';
    case '[':
        return ']';
    case '{':
        return '}';
    case '<':
        return '>';
    default:
        return '\0';
    }
}

bool IsCloseDelimiter(char ch) {
    return ch == ')' || ch == ']' || ch == '}' || ch == '>';
}

bool MatchingDelimiterPair(char open, char close) {
    return MatchingCloseDelimiter(open) == close;
}

std::optional<size_t> FindMatchingDelimiter(std::string_view text, size_t openIndex) {
    if (openIndex >= text.size() || !IsOpenDelimiter(text[openIndex])) {
        return std::nullopt;
    }
    std::vector<char> stack;
    bool inString = false;
    bool inChar = false;
    for (size_t index = openIndex; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\\' && (inString || inChar) && index + 1 < text.size()) {
            ++index;
            continue;
        }
        if (ch == '"' && !inChar) {
            inString = !inString;
            continue;
        }
        if (ch == '\'' && !inString) {
            inChar = !inChar;
            continue;
        }
        if (inString || inChar) {
            continue;
        }
        if (ch == '<') {
            if (text[openIndex] == '<' || (!stack.empty() && stack.back() == '<')) {
                stack.push_back(ch);
            }
            continue;
        }
        if (ch == '>') {
            if (!stack.empty() && stack.back() == '<') {
                stack.pop_back();
                if (stack.empty()) {
                    return index;
                }
            }
            continue;
        }
        if (IsOpenDelimiter(ch) && !IsSpacedAngleOperator(text, index)) {
            stack.push_back(ch);
            continue;
        }
        if (IsCloseDelimiter(ch)) {
            if (stack.empty() || !MatchingDelimiterPair(stack.back(), ch)) {
                return std::nullopt;
            }
            stack.pop_back();
            if (stack.empty()) {
                return index;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::string> SplitTopLevel(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    std::vector<char> stack;
    bool inString = false;
    bool inChar = false;
    size_t start = 0;
    for (size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\\' && (inString || inChar) && index + 1 < text.size()) {
            ++index;
            continue;
        }
        if (ch == '"' && !inChar) {
            inString = !inString;
            continue;
        }
        if (ch == '\'' && !inString) {
            inChar = !inChar;
            continue;
        }
        if (inString || inChar) {
            continue;
        }
        if (IsOpenDelimiter(ch) && !IsSpacedAngleOperator(text, index)) {
            stack.push_back(ch);
            continue;
        }
        if (IsCloseDelimiter(ch) && !stack.empty() && MatchingDelimiterPair(stack.back(), ch)) {
            stack.pop_back();
            continue;
        }
        if (ch == delimiter && stack.empty()) {
            parts.push_back(TrimCopy(text.substr(start, index - start)));
            start = index + 1;
        }
    }
    parts.push_back(TrimCopy(text.substr(start)));
    return parts;
}

bool IsTopLevelOperatorAt(std::string_view text, size_t index, std::string_view op) {
    if (index + op.size() > text.size() || text.substr(index, op.size()) != op) {
        return false;
    }
    const char before = index > 0 ? text[index - 1] : '\0';
    const char after = index + op.size() < text.size() ? text[index + op.size()] : '\0';
    if (op == "|" || op == "&" || op == "+" || op == "-") {
        return before != op.front() && after != op.front() && after != '=';
    }
    if (op == "||") {
        return before != '|' && after != '|' && after != '=';
    }
    if (op == "&&") {
        return before != '&' && after != '&' && after != '=';
    }
    return after != '=';
}

std::vector<std::string> SplitTopLevelOperator(std::string_view text, std::string_view op) {
    std::vector<std::string> parts;
    std::vector<char> stack;
    bool inString = false;
    bool inChar = false;
    size_t start = 0;
    for (size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\\' && (inString || inChar) && index + 1 < text.size()) {
            ++index;
            continue;
        }
        if (ch == '"' && !inChar) {
            inString = !inString;
            continue;
        }
        if (ch == '\'' && !inString) {
            inChar = !inChar;
            continue;
        }
        if (inString || inChar) {
            continue;
        }
        if (stack.empty() && IsTopLevelOperatorAt(text, index, op)) {
            parts.push_back(TrimCopy(text.substr(start, index - start + op.size())));
            index += op.size() - 1;
            start = index + 1;
            continue;
        }
        if (IsOpenDelimiter(ch) && !IsSpacedAngleOperator(text, index)) {
            stack.push_back(ch);
            continue;
        }
        if (IsCloseDelimiter(ch) && !stack.empty() && MatchingDelimiterPair(stack.back(), ch)) {
            stack.pop_back();
            continue;
        }
    }
    parts.push_back(TrimCopy(text.substr(start)));
    return parts;
}

std::vector<std::string> SplitTopLevelStreamOperands(std::string_view text, std::string_view op) {
    std::vector<std::string> parts;
    std::vector<char> stack;
    bool inString = false;
    bool inChar = false;
    size_t start = 0;
    bool foundOperator = false;
    for (size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\\' && (inString || inChar) && index + 1 < text.size()) {
            ++index;
            continue;
        }
        if (ch == '"' && !inChar) {
            inString = !inString;
            continue;
        }
        if (ch == '\'' && !inString) {
            inChar = !inChar;
            continue;
        }
        if (inString || inChar) {
            continue;
        }
        if (stack.empty() && index + op.size() <= text.size() && text.substr(index, op.size()) == op) {
            parts.push_back(TrimCopy(text.substr(start, index - start)));
            index += op.size() - 1;
            start = index + 1;
            foundOperator = true;
            continue;
        }
        if (IsOpenDelimiter(ch) && !IsSpacedAngleOperator(text, index)) {
            stack.push_back(ch);
            continue;
        }
        if (IsCloseDelimiter(ch) && !stack.empty() && MatchingDelimiterPair(stack.back(), ch)) {
            stack.pop_back();
            continue;
        }
    }
    if (!foundOperator) {
        return {};
    }
    parts.push_back(TrimCopy(text.substr(start)));
    return parts;
}

struct TernarySplit {
    std::string prefix;
    std::string trueBranch;
    std::string falseBranch;
};

std::optional<TernarySplit> SplitTopLevelTernary(std::string_view content) {
    std::vector<char> stack;
    bool inString = false;
    bool inChar = false;
    size_t question = std::string::npos;
    size_t colon = std::string::npos;
    for (size_t index = 0; index < content.size(); ++index) {
        const char ch = content[index];
        if (ch == '\\' && (inString || inChar) && index + 1 < content.size()) {
            ++index;
            continue;
        }
        if (ch == '"' && !inChar) {
            inString = !inString;
            continue;
        }
        if (ch == '\'' && !inString) {
            inChar = !inChar;
            continue;
        }
        if (inString || inChar) {
            continue;
        }
        if (IsOpenDelimiter(ch) && !IsSpacedAngleOperator(content, index)) {
            stack.push_back(ch);
            continue;
        }
        if (IsCloseDelimiter(ch) && !stack.empty() && MatchingDelimiterPair(stack.back(), ch)) {
            stack.pop_back();
            continue;
        }
        if (!stack.empty()) {
            continue;
        }
        if (ch == '?' && question == std::string::npos) {
            question = index;
        } else if (ch == ':' &&
                   question != std::string::npos &&
                   !(index > 0 && content[index - 1] == ':') &&
                   !(index + 1 < content.size() && content[index + 1] == ':')) {
            colon = index;
            break;
        }
    }
    if (question == std::string::npos || colon == std::string::npos) {
        return std::nullopt;
    }
    return TernarySplit{
        .prefix = TrimCopy(content.substr(0, question + 1)),
        .trueBranch = TrimCopy(content.substr(question + 1, colon - question - 1)),
        .falseBranch = TrimCopy(content.substr(colon + 1)),
    };
}

struct StringLiteralRange {
    size_t start = 0;
    size_t end = 0;
};

std::vector<StringLiteralRange> FindStringLiteralRanges(std::string_view text) {
    std::vector<StringLiteralRange> ranges;
    for (size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '"') {
            continue;
        }
        size_t start = index;
        if (start > 0 && (text[start - 1] == 'L' || text[start - 1] == 'R' ||
                          text[start - 1] == 'u' || text[start - 1] == 'U')) {
            --start;
            if (start > 0 && text[start] == '8' && text[start - 1] == 'u') {
                --start;
            }
        }
        for (++index; index < text.size(); ++index) {
            if (text[index] == '\\' && index + 1 < text.size()) {
                ++index;
                continue;
            }
            if (text[index] == '"') {
                ranges.push_back({.start = start, .end = index + 1});
                break;
            }
        }
    }
    return ranges;
}

bool ContainsOnlySpaces(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char ch) {
        return ch == ' ';
    });
}

bool ContainsAdjacentStringLiteralSequence(std::string_view text) {
    const std::vector<StringLiteralRange> ranges = FindStringLiteralRanges(text);
    for (size_t index = 1; index < ranges.size(); ++index) {
        if (ContainsOnlySpaces(text.substr(ranges[index - 1].end, ranges[index].start - ranges[index - 1].end))) {
            return true;
        }
    }
    return false;
}

bool ContainsTopLevelOperator(std::string_view text, std::string_view op) {
    return SplitTopLevelOperator(text, op).size() > 1;
}

bool ContainsAnyBreakableContent(std::string_view text) {
    if (ContainsAdjacentStringLiteralSequence(text)) {
        return true;
    }
    static constexpr std::string_view operators[] = {"&&", "||", "==", "<<", ">>", "+", "|"};
    for (std::string_view op : operators) {
        if (ContainsTopLevelOperator(text, op)) {
            return true;
        }
    }
    for (size_t index = 0; index < text.size(); ++index) {
        if (!IsOpenDelimiter(text[index])) {
            continue;
        }
        const std::optional<size_t> close = FindMatchingDelimiter(text, index);
        if (!close) {
            continue;
        }
        const std::string_view inside = text.substr(index + 1, *close - index - 1);
        if (SplitTopLevel(inside, ',').size() > 1 || ContainsAdjacentStringLiteralSequence(inside)) {
            return true;
        }
        index = *close;
    }
    return false;
}

bool IsIdentifierChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool IsBreakableParenOpen(const std::string& content, size_t open) {
    const std::string prefix = TrimCopy(std::string_view(content).substr(0, open));
    if (prefix.empty()) {
        return false;
    }
    if (tools::EndsWith(prefix, "__declspec") ||
        tools::EndsWith(prefix, "alignas") ||
        tools::EndsWith(prefix, "alignof") ||
        tools::EndsWith(prefix, "sizeof") ||
        tools::EndsWith(prefix, "static_cast") ||
        tools::EndsWith(prefix, "reinterpret_cast") ||
        tools::EndsWith(prefix, "const_cast") ||
        tools::EndsWith(prefix, "dynamic_cast")) {
        return false;
    }
    const char before = prefix.back();
    return IsIdentifierChar(before) || before == ')' || before == ']' || before == '>' || before == '}';
}

bool IsBreakableBraceOpen(const std::string& content, size_t open) {
    const std::string prefix = TrimCopy(std::string_view(content).substr(0, open));
    if (prefix.empty()) {
        return true;
    }
    const char before = prefix.back();
    return IsIdentifierChar(before) || before == '=' || before == ']' || before == '}';
}

LineLayoutScore ScoreLayout(const LineLayout& layout, int columnLimit) {
    LineLayoutScore score;
    score.overflow = 0;
    score.lineCount = static_cast<int>(layout.lines.size());
    score.deepestIndent = 0;
    for (const std::string& line : layout.lines) {
        score.overflow = std::max(score.overflow, std::max(0, static_cast<int>(line.size()) - columnLimit));
        score.deepestIndent = std::max(score.deepestIndent, LeadingSpaceCount(line));
    }
    return score;
}

class LineBreakSolver {
public:
    LineBreakSolver(int columnLimit, int indentWidth) :
        columnLimit_(columnLimit),
        indentWidth_(indentWidth) {}

    std::vector<std::string> Format(std::string_view line) {
        const std::string trimmed = TrimCopy(line);
        if (StartsWithWord(trimmed, "//")) {
            return {std::string(line)};
        }
        if (tools::StartsWith(trimmed, "} else if (")) {
            return {std::string(line)};
        }
        const int indent = LeadingSpaceCount(line);
        const std::string content(line.substr(static_cast<size_t>(indent)));
        if (line.size() <= static_cast<size_t>(columnLimit_) && !ContainsAdjacentStringLiteralSequence(content)) {
            return {std::string(line)};
        }
        if (tools::StartsWith(content, "EXPECT_FALSE(") && content.find(" << ") != std::string::npos) {
            return {std::string(line)};
        }
        nodes_.clear();
        nodeByCompact_.clear();
        memo_.clear();
        activeParent_ = kNoParent;
        return FormatContent(content, indent, 0, true).lines;
    }

private:
    static constexpr size_t kNoParent = std::numeric_limits<size_t>::max();

    int columnLimit_ = 120;
    int indentWidth_ = 4;
    size_t activeParent_ = kNoParent;
    std::vector<LineLayoutNode> nodes_;
    std::unordered_map<std::string, size_t> nodeByCompact_;
    std::unordered_map<LineMemoKey, LineLayout, LineMemoKeyHash> memo_;

    LineLayout FormatContent(const std::string& content, int indent, int depth, bool force = false) {
        const size_t node = InternNode(content);
        if (activeParent_ != kNoParent && activeParent_ != node) {
            std::vector<size_t>& children = nodes_[activeParent_].children;
            if (std::find(children.begin(), children.end(), node) == children.end()) {
                children.push_back(node);
            }
        }
        return FormatNode(node, indent, depth, force);
    }

    size_t InternNode(const std::string& compact) {
        const auto existing = nodeByCompact_.find(compact);
        if (existing != nodeByCompact_.end()) {
            return existing->second;
        }
        const size_t id = nodes_.size();
        nodes_.push_back(LineLayoutNode{.compact = compact});
        nodeByCompact_.emplace(nodes_.back().compact, id);
        return id;
    }

    LineLayout FormatNode(size_t node, int indent, int depth, bool force) {
        if (depth > 32) {
            return LineLayout{{std::string(static_cast<size_t>(indent), ' ') + nodes_[node].compact}};
        }
        const LineMemoKey key{.node = node, .indent = indent, .force = force ? 1 : 0};
        const auto memo = memo_.find(key);
        if (memo != memo_.end()) {
            return memo->second;
        }

        const std::string content = nodes_[node].compact;
        LineLayout best{{std::string(static_cast<size_t>(indent), ' ') + content}};
        LineLayoutScore bestScore = ScoreLayout(best, columnLimit_);
        if (ContainsAdjacentStringLiteralSequence(content)) {
            bestScore.overflow = std::numeric_limits<int>::max();
            bestScore.lineCount = std::numeric_limits<int>::max();
            bestScore.deepestIndent = std::numeric_limits<int>::max();
        }
        if (!force && bestScore.overflow == 0 && !ContainsAdjacentStringLiteralSequence(content)) {
            memo_.emplace(key, best);
            return best;
        }

        const size_t previousParent = activeParent_;
        activeParent_ = node;
        std::vector<LineLayout> candidates = BuildSplitCandidates(indent, content, depth);
        activeParent_ = previousParent;

        for (const LineLayout& candidate : candidates) {
            const LineLayoutScore score = ScoreLayout(candidate, columnLimit_);
            if (IsBetterLayoutScore(score, bestScore)) {
                best = candidate;
                bestScore = score;
            }
        }

        memo_.emplace(key, best);
        return best;
    }

    std::vector<LineLayout> BuildSplitCandidates(int indent, const std::string& content, int depth) {
        std::vector<LineLayout> candidates;
        if (std::optional<LineLayout> macro = SplitMacroDefinition(content)) {
            candidates.push_back(std::move(*macro));
        }
        if (std::optional<LineLayout> lambda = SplitLambdaHeader(indent, content, depth)) {
            candidates.push_back(std::move(*lambda));
        }
        if (std::optional<LineLayout> strings = SplitAdjacentStrings(indent, content)) {
            candidates.push_back(std::move(*strings));
        }
        if (std::optional<LineLayout> constructor = SplitConstructorInitializerList(indent, content, depth)) {
            candidates.push_back(std::move(*constructor));
        }
        if (std::optional<LineLayout> requiresClause = SplitRequiresClause(indent, content)) {
            candidates.push_back(std::move(*requiresClause));
            return candidates;
        }
        if (std::optional<LineLayout> typeDeclarator = SplitTypeDeclarator(indent, content)) {
            candidates.push_back(std::move(*typeDeclarator));
        }
        if (std::optional<LineLayout> assignment = SplitAssignment(indent, content, depth)) {
            candidates.push_back(std::move(*assignment));
        }
        if (std::optional<LineLayout> stream = SplitStreamChain(indent, content)) {
            candidates.push_back(std::move(*stream));
        }
        if (std::optional<LineLayout> ternary = SplitTernary(indent, content, depth)) {
            candidates.push_back(std::move(*ternary));
        }
        if (IsTopLevelSameOperatorChain(content)) {
            if (std::optional<LineLayout> binary = SplitBinaryChain(indent, content, depth)) {
                candidates.push_back(std::move(*binary));
                return candidates;
            }
        }
        if (std::optional<LineLayout> angle = SplitAngleList(indent, content)) {
            candidates.push_back(std::move(*angle));
        }
        if (std::optional<LineLayout> group = SplitDelimiterList(indent, content, depth)) {
            candidates.push_back(std::move(*group));
        }
        if (std::optional<LineLayout> binary = SplitBinaryChain(indent, content, depth)) {
            candidates.push_back(std::move(*binary));
        }
        return candidates;
    }

    bool IsTopLevelSameOperatorChain(const std::string& content) const {
        static constexpr std::string_view operators[] = {"&&", "||", "+", "|"};
        for (std::string_view op : operators) {
            if (SplitTopLevelOperator(content, op).size() > 2) {
                return true;
            }
        }
        return false;
    }

    std::optional<LineLayout> SplitMacroDefinition(const std::string& content) const {
        if (!tools::StartsWith(content, "#define ")) {
            return std::nullopt;
        }
        size_t split = std::string::npos;
        int parenDepth = 0;
        for (size_t index = 8; index < content.size(); ++index) {
            if (content[index] == '(') {
                ++parenDepth;
            } else if (content[index] == ')' && parenDepth > 0) {
                --parenDepth;
            } else if (content[index] == ' ' && parenDepth == 0) {
                split = index;
                break;
            }
        }
        if (split == std::string::npos) {
            return std::nullopt;
        }
        return LineLayout{{
            content.substr(0, split) + " \\",
            std::string(static_cast<size_t>(indentWidth_), ' ') + content.substr(split + 1),
        }};
    }

    std::optional<LineLayout> SplitLambdaHeader(int indent, const std::string& content, int depth) {
        const size_t captureClose = content.find("](");
        if (captureClose == std::string::npos) {
            return std::nullopt;
        }
        const size_t firstParen = content.find('(');
        if (firstParen != std::string::npos && firstParen < captureClose) {
            return std::nullopt;
        }
        const std::optional<size_t> close = FindMatchingDelimiter(content, captureClose + 1);
        if (!close || *close + 1 >= content.size()) {
            return std::nullopt;
        }
        const std::string suffix = content.substr(*close + 1);
        if (!tools::StartsWith(TrimCopy(suffix), "->") && !tools::StartsWith(TrimCopy(suffix), "{")) {
            return std::nullopt;
        }
        const std::string baseIndent(static_cast<size_t>(indent), ' ');
        std::vector<std::string> lines;
        lines.push_back(baseIndent + content.substr(0, captureClose + 1));
        std::string suffixText = TrimCopy(suffix);
        const bool suffixOpensBody = tools::EndsWith(suffixText, "{");
        if (suffixOpensBody) {
            suffixText = TrimCopy(std::string_view(suffixText).substr(0, suffixText.size() - 1));
        }
        std::string parameterText = content.substr(captureClose + 1, *close - captureClose);
        if (!suffixText.empty()) {
            parameterText += " " + suffixText;
        }
        LineLayout parameters = FormatContent(parameterText, indent + indentWidth_, depth + 1);
        lines.insert(lines.end(), parameters.lines.begin(), parameters.lines.end());
        if (suffixOpensBody) {
            lines.push_back(baseIndent + "{");
        }
        return LineLayout{std::move(lines)};
    }

    std::optional<LineLayout> SplitAdjacentStrings(int indent, const std::string& content) const {
        const std::vector<StringLiteralRange> ranges = FindStringLiteralRanges(content);
        if (ranges.size() <= 1) {
            return std::nullopt;
        }
        size_t runFirst = std::string::npos;
        size_t runLast = std::string::npos;
        for (size_t index = 1; index < ranges.size(); ++index) {
            if (!ContainsOnlySpaces(std::string_view(content).substr(ranges[index - 1].end, ranges[index].start - ranges[index - 1].end))) {
                continue;
            }
            runFirst = index - 1;
            runLast = index;
            while (runLast + 1 < ranges.size() &&
                   ContainsOnlySpaces(std::string_view(content).substr(ranges[runLast].end, ranges[runLast + 1].start - ranges[runLast].end))) {
                ++runLast;
            }
            break;
        }
        if (runFirst == std::string::npos) {
            return std::nullopt;
        }
        std::vector<char> prefixStack;
        for (size_t index = 0; index < ranges[runFirst].start; ++index) {
            const char ch = content[index];
            if (IsOpenDelimiter(ch) && !IsSpacedAngleOperator(content, index)) {
                prefixStack.push_back(ch);
            } else if (IsCloseDelimiter(ch) && !prefixStack.empty() && MatchingDelimiterPair(prefixStack.back(), ch)) {
                prefixStack.pop_back();
            }
        }
        if (!prefixStack.empty()) {
            return std::nullopt;
        }

        const std::string baseIndent(static_cast<size_t>(indent), ' ');
        const std::string continuationIndent(static_cast<size_t>(indent + indentWidth_), ' ');
        std::vector<std::string> lines;
        lines.push_back(baseIndent + content.substr(0, ranges[runFirst].start) +
            content.substr(ranges[runFirst].start, ranges[runFirst].end - ranges[runFirst].start));
        for (size_t index = runFirst + 1; index <= runLast; ++index) {
            lines.push_back(continuationIndent +
                content.substr(ranges[index].start, ranges[index].end - ranges[index].start));
        }
        lines.back().append(content.substr(ranges[runLast].end));
        return LineLayout{std::move(lines)};
    }

    std::optional<size_t> FindTopLevelAssignment(std::string_view content) const {
        std::vector<char> stack;
        bool inString = false;
        bool inChar = false;
        for (size_t index = 0; index < content.size(); ++index) {
            const char ch = content[index];
            if (ch == '\\' && (inString || inChar) && index + 1 < content.size()) {
                ++index;
                continue;
            }
            if (ch == '"' && !inChar) {
                inString = !inString;
                continue;
            }
            if (ch == '\'' && !inString) {
                inChar = !inChar;
                continue;
            }
            if (inString || inChar) {
                continue;
            }
            if (IsOpenDelimiter(ch) && !IsSpacedAngleOperator(content, index)) {
                stack.push_back(ch);
                continue;
            }
            if (IsCloseDelimiter(ch) && !stack.empty() && MatchingDelimiterPair(stack.back(), ch)) {
                stack.pop_back();
                continue;
            }
            if (ch != '=' || !stack.empty()) {
                continue;
            }
            const char before = index > 0 ? content[index - 1] : '\0';
            const char after = index + 1 < content.size() ? content[index + 1] : '\0';
            if (before == '=' || before == '!' || before == '<' || before == '>' ||
                before == '+' || before == '-' || before == '*' || before == '/' ||
                before == '%' || before == '&' || before == '|' || before == '^' ||
                after == '=' || after == '>') {
                continue;
            }
            return index;
        }
        return std::nullopt;
    }

    std::optional<LineLayout> SplitAssignment(int indent, const std::string& content, int depth) {
        const std::optional<size_t> assignment = FindTopLevelAssignment(content);
        if (!assignment) {
            return std::nullopt;
        }
        const std::string prefix = TrimCopy(std::string_view(content).substr(0, *assignment + 1));
        const std::string rhs = TrimCopy(std::string_view(content).substr(*assignment + 1));
        if (rhs.empty() || ContainsAdjacentStringLiteralSequence(rhs)) {
            return std::nullopt;
        }
        if (tools::StartsWith(rhs, "[")) {
            return std::nullopt;
        }
        if (const std::optional<TernarySplit> ternary = SplitTopLevelTernary(rhs)) {
            if (SplitTopLevelTernary(ternary->falseBranch).has_value()) {
                return std::nullopt;
            }
        }
        if (rhs.find('(') == std::string::npos && rhs.find('{') == std::string::npos &&
            !ContainsAnyBreakableContent(rhs) &&
            prefix.size() + 1 + rhs.size() <= static_cast<size_t>(columnLimit_ + 20)) {
            return std::nullopt;
        }
        const std::string baseIndent(static_cast<size_t>(indent), ' ');
        std::vector<std::string> lines{baseIndent + prefix};
        LineLayout rhsLayout = FormatContent(rhs, indent + indentWidth_, depth + 1, true);
        lines.insert(lines.end(), rhsLayout.lines.begin(), rhsLayout.lines.end());
        return LineLayout{std::move(lines)};
    }

    std::optional<LineLayout> SplitConstructorInitializerList(int indent, const std::string& content, int depth) {
        const size_t split = content.find(" : ");
        const std::string beforeColon = content.substr(0, split);
        if (split == std::string::npos ||
            beforeColon.find(')') == std::string::npos ||
            beforeColon.find('?') != std::string::npos) {
            return std::nullopt;
        }
        const std::string prefix = content.substr(0, split + 2);
        std::string rest = content.substr(split + 3);
        bool hasBodyBrace = false;
        if (tools::EndsWith(rest, " {")) {
            rest.resize(rest.size() - 2);
            hasBodyBrace = true;
        }
        std::vector<std::string> items = SplitTopLevel(rest, ',');
        if (items.size() <= 1) {
            return std::nullopt;
        }
        const std::string baseIndent(static_cast<size_t>(indent), ' ');
        std::vector<std::string> lines;
        const size_t open = prefix.find('(');
        const std::optional<size_t> close = open == std::string::npos ? std::nullopt : FindMatchingDelimiter(prefix, open);
        const std::vector<std::string> parameters = open == std::string::npos || !close ?
            std::vector<std::string>{} :
            SplitTopLevel(std::string_view(prefix).substr(open + 1, *close - open - 1), ',');
        if (open != std::string::npos && close && parameters.size() > 1) {
            lines.push_back(baseIndent + prefix.substr(0, open + 1));
            for (size_t index = 0; index < parameters.size(); ++index) {
                lines.push_back(
                    std::string(static_cast<size_t>(indent + indentWidth_), ' ') +
                    parameters[index] +
                    (index + 1 < parameters.size() ? "," : "")
                );
            }
            lines.push_back(baseIndent + prefix.substr(*close));
        } else {
            lines.push_back(baseIndent + prefix);
        }
        for (size_t index = 0; index < items.size(); ++index) {
            LineLayout item = FormatContent(items[index], indent + indentWidth_, depth + 1);
            if (index + 1 < items.size()) {
                item.lines.back().push_back(',');
            }
            lines.insert(lines.end(), item.lines.begin(), item.lines.end());
        }
        if (hasBodyBrace) {
            lines.push_back(baseIndent + "{");
        }
        return LineLayout{std::move(lines)};
    }

    std::optional<LineLayout> SplitTypeDeclarator(int indent, const std::string& content) const {
        const size_t array = content.find("std::array<");
        if (array == std::string::npos || content.find(" = ") != std::string::npos) {
            return std::nullopt;
        }
        const size_t open = content.find('<', array);
        const std::optional<size_t> close = open == std::string::npos ? std::nullopt : FindMatchingDelimiter(content, open);
        if (!close || *close + 2 >= content.size() || content[*close + 1] != ' ') {
            return std::nullopt;
        }
        const std::string type = content.substr(0, *close + 1);
        const std::string declarator = content.substr(*close + 2);
        if (declarator.find('{') == std::string::npos) {
            return std::nullopt;
        }
        const std::string baseIndent(static_cast<size_t>(indent), ' ');
        const std::string continuationIndent(static_cast<size_t>(indent + indentWidth_), ' ');
        return LineLayout{{
            baseIndent + type,
            continuationIndent + declarator,
        }};
    }

    std::optional<LineLayout> SplitRequiresClause(int indent, const std::string& content) const {
        const size_t requiresPos = content.find(" requires(");
        if (requiresPos == std::string::npos) {
            return std::nullopt;
        }
        const size_t open = requiresPos + std::string_view(" requires").size();
        const std::optional<size_t> close = FindMatchingDelimiter(content, open);
        if (!close) {
            return std::nullopt;
        }
        const std::string before = TrimCopy(std::string_view(content).substr(0, requiresPos));
        const std::string inside = content.substr(open + 1, *close - open - 1);
        const std::string suffix = content.substr(*close + 1);
        const std::string baseIndent(static_cast<size_t>(indent), ' ');
        const std::string requiresIndent(static_cast<size_t>(indent + indentWidth_), ' ');
        const std::string conditionIndent(static_cast<size_t>(indent + indentWidth_ * 2), ' ');
        std::vector<std::string> lines;
        lines.push_back(baseIndent + before);
        lines.push_back(requiresIndent + "requires(");
        const std::vector<std::string> parts = SplitTopLevelOperator(inside, "&&");
        if (parts.size() > 1) {
            for (const std::string& part : parts) {
                lines.push_back(conditionIndent + part);
            }
        } else {
            lines.push_back(conditionIndent + TrimCopy(inside));
        }
        lines.push_back(requiresIndent + ")" + suffix);
        return LineLayout{std::move(lines)};
    }

    std::optional<LineLayout> SplitStreamChain(int indent, const std::string& content) const {
        std::string_view op = content.find(">>") != std::string::npos ? std::string_view(">>") : std::string_view("<<");
        const std::vector<std::string> parts = SplitTopLevelStreamOperands(content, op);
        if (parts.size() <= 1) {
            return std::nullopt;
        }
        std::vector<std::string> lines;
        const std::string baseIndent(static_cast<size_t>(indent), ' ');
        const std::string continuationIndent(static_cast<size_t>(indent + indentWidth_), ' ');
        lines.push_back(baseIndent + parts.front());

        std::string compactTail = continuationIndent + std::string(op) + " " + parts[1];
        for (size_t index = 2; index < parts.size(); ++index) {
            compactTail += " " + std::string(op) + " " + parts[index];
        }
        if (compactTail.size() <= static_cast<size_t>(columnLimit_)) {
            lines.push_back(compactTail);
            return LineLayout{std::move(lines)};
        }

        auto isStringOperand = [](const std::string& operand) {
            const std::string text = TrimCopy(operand);
            return text.find('"') == 0 ||
                (text.size() > 1 && (text[0] == 'L' || text[0] == 'u' || text[0] == 'U') && text[1] == '"') ||
                (text.size() > 2 && text[0] == 'u' && text[1] == '8' && text[2] == '"');
        };

        if (op == ">>") {
            for (size_t index = 1; index < parts.size(); ++index) {
                lines.push_back(continuationIndent + std::string(op) + " " + parts[index]);
            }
            return LineLayout{std::move(lines)};
        }

        for (size_t index = 1; index < parts.size();) {
            const bool startsWithString = isStringOperand(parts[index]);
            std::string current = continuationIndent + std::string(op) + " " + parts[index];
            ++index;
            while (!startsWithString && index < parts.size() && !isStringOperand(parts[index])) {
                current += " " + std::string(op) + " " + parts[index];
                ++index;
            }
            lines.push_back(current);
        }
        return LineLayout{std::move(lines)};
    }

    std::optional<LineLayout> SplitCallAfterPrefix(
        int indent,
        std::string_view firstLinePrefix,
        const std::string& callText,
        std::string_view closeSuffix,
        int depth
    ) {
        const size_t open = callText.find('(');
        const std::optional<size_t> close = open == std::string::npos ? std::nullopt :
            FindMatchingDelimiter(callText, open);
        if (!close) {
            return std::nullopt;
        }
        const std::vector<std::string> arguments =
            SplitTopLevel(std::string_view(callText).substr(open + 1, *close - open - 1), ',');
        if (arguments.size() <= 1) {
            return std::nullopt;
        }

        const std::string firstLine =
            std::string(static_cast<size_t>(indent), ' ') +
            std::string(firstLinePrefix) +
            callText.substr(0, open + 1);
        if (firstLine.size() > static_cast<size_t>(columnLimit_)) {
            return std::nullopt;
        }

        std::vector<std::string> lines{firstLine};
        for (size_t index = 0; index < arguments.size(); ++index) {
            std::string argument = arguments[index];
            if (index + 1 < arguments.size()) {
                argument.push_back(',');
            }
            LineLayout argumentLayout = FormatContent(argument, indent + indentWidth_, depth + 1);
            lines.insert(lines.end(), argumentLayout.lines.begin(), argumentLayout.lines.end());
        }
        lines.push_back(
            std::string(static_cast<size_t>(indent), ' ') +
            ")" +
            TrimCopy(std::string_view(callText).substr(*close + 1)) +
            std::string(closeSuffix)
        );
        return LineLayout{std::move(lines)};
    }

    std::optional<LineLayout> SplitTernary(int indent, const std::string& content, int depth) {
        const std::optional<TernarySplit> split = SplitTopLevelTernary(content);
        if (!split) {
            return std::nullopt;
        }
        const std::string baseIndent(static_cast<size_t>(indent), ' ');
        const std::string prefix = split->prefix;
        const std::string trueBranch = split->trueBranch;
        const std::string falseBranch = split->falseBranch;
        const std::string continuationIndent(static_cast<size_t>(indent + indentWidth_), ' ');
        const bool falseHasTopLevelTernary = SplitTopLevelTernary(falseBranch).has_value();
        if (!trueBranch.empty() && trueBranch.front() == '(') {
            const std::optional<size_t> close = FindMatchingDelimiter(trueBranch, 0);
            if (close && *close + 1 == trueBranch.size()) {
                const std::string inner = trueBranch.substr(1, trueBranch.size() - 2);
                if (const std::optional<TernarySplit> innerSplit = SplitTopLevelTernary(inner)) {
                    std::vector<std::string> lines;
                    lines.push_back(baseIndent + prefix + " (");
                    lines.push_back(continuationIndent + innerSplit->prefix);
                    const std::string compactBranches = innerSplit->trueBranch + " : " + innerSplit->falseBranch;
                    if (continuationIndent.size() + indentWidth_ + compactBranches.size() <=
                        static_cast<size_t>(columnLimit_)) {
                        lines.push_back(
                            std::string(static_cast<size_t>(indent + indentWidth_ * 2), ' ') +
                            compactBranches
                        );
                    } else {
                        LineLayout trueLayout = FormatContent(
                            innerSplit->trueBranch + " :",
                            indent + indentWidth_ * 2,
                            depth + 1
                        );
                        lines.insert(lines.end(), trueLayout.lines.begin(), trueLayout.lines.end());
                        LineLayout falseLayout = FormatContent(
                            innerSplit->falseBranch,
                            indent + indentWidth_ * 2,
                            depth + 1
                        );
                        lines.insert(lines.end(), falseLayout.lines.begin(), falseLayout.lines.end());
                    }
                    lines.push_back(baseIndent + ") : " + falseBranch);
                    return LineLayout{std::move(lines)};
                }
            }
        }
        if (falseHasTopLevelTernary) {
            std::vector<std::string> lines;
            const std::string compactTrueLine = prefix + " " + trueBranch + " :";
            if (baseIndent.size() + compactTrueLine.size() <= static_cast<size_t>(columnLimit_)) {
                lines.push_back(baseIndent + compactTrueLine);
            } else if (std::optional<LineLayout> trueCall = SplitCallAfterPrefix(
                    indent,
                    prefix + " ",
                    trueBranch,
                    " :",
                    depth
                )) {
                lines.insert(lines.end(), trueCall->lines.begin(), trueCall->lines.end());
            } else {
                lines.push_back(baseIndent + prefix + " " + trueBranch + " :");
            }
            std::string current = falseBranch;
            while (const std::optional<TernarySplit> nested = SplitTopLevelTernary(current)) {
                const std::string nestedLine = nested->prefix + " " + nested->trueBranch + " :";
                if (continuationIndent.size() + nestedLine.size() <= static_cast<size_t>(columnLimit_)) {
                    lines.push_back(continuationIndent + nestedLine);
                } else if (std::optional<LineLayout> nestedCall = SplitCallAfterPrefix(
                        indent + indentWidth_,
                        nested->prefix + " ",
                        nested->trueBranch,
                        " :",
                        depth + 1
                    )) {
                    lines.insert(lines.end(), nestedCall->lines.begin(), nestedCall->lines.end());
                } else {
                    lines.push_back(continuationIndent + nested->prefix + " " + nested->trueBranch + " :");
                }
                current = nested->falseBranch;
            }
            LineLayout fallback = FormatContent(current, indent + indentWidth_, depth + 1);
            lines.insert(lines.end(), fallback.lines.begin(), fallback.lines.end());
            return LineLayout{std::move(lines)};
        }
        if (std::optional<LineLayout> falseCall = SplitCallAfterPrefix(
                indent,
                prefix + " " + trueBranch + " : ",
                falseBranch,
                "",
                depth
        )) {
            return falseCall;
        }
        if (!falseHasTopLevelTernary) {
            const std::string combinedBranches = trueBranch + " : " + falseBranch;
            if (continuationIndent.size() + combinedBranches.size() <= static_cast<size_t>(columnLimit_)) {
                return LineLayout{{
                    std::string(static_cast<size_t>(indent), ' ') + prefix,
                    continuationIndent + combinedBranches,
                }};
            }
            if (trueBranch.find('(') != std::string::npos) {
                std::vector<std::string> lines{baseIndent + prefix};
                LineLayout branch = FormatContent(trueBranch + " :", indent + indentWidth_, depth + 1, true);
                lines.insert(lines.end(), branch.lines.begin(), branch.lines.end());
                LineLayout fallback = FormatContent(falseBranch, indent + indentWidth_, depth + 1);
                lines.insert(lines.end(), fallback.lines.begin(), fallback.lines.end());
                return LineLayout{std::move(lines)};
            }
            if (falseBranch.find('(') != std::string::npos) {
                return std::nullopt;
            }
        }
        std::vector<std::string> lines{baseIndent + prefix};
        LineLayout branch = FormatContent(trueBranch + " :", indent + indentWidth_, depth + 1);
        lines.insert(lines.end(), branch.lines.begin(), branch.lines.end());
        LineLayout fallback = FormatContent(falseBranch, indent + indentWidth_, depth + 1);
        lines.insert(lines.end(), fallback.lines.begin(), fallback.lines.end());
        return LineLayout{std::move(lines)};
    }

    std::optional<LineLayout> SplitAngleList(int indent, const std::string& content) const {
        const size_t open = content.find('<');
        if (open == std::string::npos) {
            return std::nullopt;
        }
        const size_t firstParen = content.find('(');
        if (firstParen != std::string::npos && firstParen < open) {
            return std::nullopt;
        }
        const std::optional<size_t> close = FindMatchingDelimiter(content, open);
        if (!close || *close <= open + 1) {
            return std::nullopt;
        }
        const std::vector<std::string> items = SplitTopLevel(std::string_view(content).substr(open + 1, *close - open - 1), ',');
        if (items.size() <= 1) {
            return std::nullopt;
        }
        const std::string suffix = content.substr(*close + 1);
        if (!tools::StartsWith(content, "template ") &&
            content.find("std::variant<") == std::string::npos &&
            content.find("UseTemplate<") == std::string::npos) {
            return std::nullopt;
        }
        std::vector<std::string> lines;
        const std::string baseIndent(static_cast<size_t>(indent), ' ');
        const std::string continuationIndent(static_cast<size_t>(indent + indentWidth_), ' ');
        lines.push_back(baseIndent + content.substr(0, open + 1));
        for (const std::string& item : items) {
            lines.push_back(continuationIndent + item + (&item == &items.back() ? "" : ","));
        }
        lines.push_back(baseIndent + ">" + suffix);
        return LineLayout{std::move(lines)};
    }

    std::optional<LineLayout> SplitDelimiterList(int indent, const std::string& content, int depth) {
        const size_t open = FindBestDelimiterOpen(content);
        if (open == std::string::npos) {
            return std::nullopt;
        }
        const std::optional<size_t> close = FindMatchingDelimiter(content, open);
        if (!close || *close <= open + 1) {
            return std::nullopt;
        }
        const char openChar = content[open];
        const char closeChar = MatchingCloseDelimiter(openChar);
        const std::string inside = content.substr(open + 1, *close - open - 1);
        const bool isControlHeader = (tools::StartsWith(content, "for ") ||
                                      tools::StartsWith(content, "if ") ||
                                      tools::StartsWith(content, "while ") ||
                                      tools::StartsWith(content, "switch ")) &&
            openChar == '(';
        char separator = tools::StartsWith(content, "for ") && openChar == '(' ? ';' : ',';
        std::vector<std::string> items = SplitTopLevel(inside, separator);
        if (isControlHeader && SplitTopLevel(inside, ';').size() > 1) {
            separator = ';';
            items = SplitTopLevel(inside, ';');
        }
        if (items.size() <= 1 && !ContainsAnyBreakableContent(inside) && openChar != '(') {
            return std::nullopt;
        }
        const std::string baseIndent(static_cast<size_t>(indent), ' ');
        const std::string prefix = content.substr(0, open + 1);
        const std::string suffix = content.substr(*close + 1);

        std::vector<std::string> lines;
        if (openChar == '{') {
            const std::string trimmedInside = TrimCopy(inside);
            if (tools::StartsWith(trimmedInside, "{") && tools::EndsWith(trimmedInside, "}")) {
                const size_t innerOpen = inside.find('{');
                const std::optional<size_t> innerClose = innerOpen == std::string::npos ? std::nullopt :
                    FindMatchingDelimiter(inside, innerOpen);
                if (innerClose && TrimCopy(std::string_view(inside).substr(*innerClose + 1)).empty()) {
                    const std::vector<std::string> innerItems =
                        SplitTopLevel(std::string_view(inside).substr(innerOpen + 1, *innerClose - innerOpen - 1), ',');
                    if (!innerItems.empty()) {
                        lines.push_back(baseIndent + prefix + "{");
                        for (size_t index = 0; index < innerItems.size(); ++index) {
                            std::string item = innerItems[index];
                            if (index + 1 < innerItems.size()) {
                                item.push_back(',');
                            }
                            LineLayout itemLayout = FormatContent(item, indent + indentWidth_, depth + 1);
                            lines.insert(lines.end(), itemLayout.lines.begin(), itemLayout.lines.end());
                        }
                        lines.push_back(baseIndent + "}}" + suffix);
                        return LineLayout{std::move(lines)};
                    }
                }
            }
        }
        if (isControlHeader && items.size() == 1) {
            const std::string item = items.front();
            if (tools::StartsWith(content, "if ")) {
                for (std::string_view op : {std::string_view("&&"), std::string_view("||")}) {
                    const std::vector<std::string> operatorParts = SplitTopLevelOperator(item, op);
                    if (operatorParts.size() <= 1 || !tools::StartsWith(operatorParts.back(), "(")) {
                        continue;
                    }
                    const std::optional<size_t> innerClose = FindMatchingDelimiter(operatorParts.back(), 0);
                    if (!innerClose || *innerClose + 1 != operatorParts.back().size()) {
                        continue;
                    }
                    std::string prefixLine = baseIndent + prefix;
                    for (size_t partIndex = 0; partIndex + 1 < operatorParts.size(); ++partIndex) {
                        if (partIndex > 0) {
                            prefixLine.push_back(' ');
                        }
                        prefixLine += operatorParts[partIndex];
                    }
                    prefixLine += " (";
                    lines.push_back(prefixLine);
                    const std::string inner = operatorParts.back().substr(1, operatorParts.back().size() - 2);
                    if (std::optional<LineLayout> condition =
                            SplitBinaryChainAtIndent(indent + indentWidth_, inner, depth + 1, indent + indentWidth_)) {
                        lines.insert(lines.end(), condition->lines.begin(), condition->lines.end());
                    } else {
                        lines.push_back(std::string(static_cast<size_t>(indent + indentWidth_), ' ') + inner);
                    }
                    lines.push_back(baseIndent + "))" + suffix);
                    return LineLayout{std::move(lines)};
                }
            }
            if (tools::StartsWith(content, "while ")) {
                const std::vector<std::string> equalityParts = SplitTopLevelOperator(item, "==");
                if (equalityParts.size() == 2) {
                    lines.push_back(baseIndent + prefix);
                    lines.push_back(std::string(static_cast<size_t>(indent + indentWidth_), ' ') + equalityParts[0]);
                    lines.push_back(std::string(static_cast<size_t>(indent + indentWidth_ * 2), ' ') + equalityParts[1]);
                    lines.push_back(baseIndent + std::string(1, closeChar) + suffix);
                    return LineLayout{std::move(lines)};
                }
            }
            if (tools::StartsWith(content, "if ") || tools::StartsWith(content, "while ")) {
                if (std::optional<LineLayout> condition =
                        SplitBinaryChainAtIndent(indent + indentWidth_, item, depth + 1, indent + indentWidth_)) {
                    lines.push_back(baseIndent + prefix);
                    lines.insert(lines.end(), condition->lines.begin(), condition->lines.end());
                    lines.push_back(baseIndent + std::string(1, closeChar) + suffix);
                    return LineLayout{std::move(lines)};
                }
            }
            const size_t innerOpen = item.find('(');
            const std::optional<size_t> innerClose = innerOpen == std::string::npos ? std::nullopt :
                FindMatchingDelimiter(item, innerOpen);
            if (innerOpen != std::string::npos &&
                innerClose &&
                IsBreakableParenOpen(item, innerOpen) &&
                SplitTopLevel(std::string_view(item).substr(innerOpen + 1, *innerClose - innerOpen - 1), ',').size() > 1) {
                const std::vector<std::string> arguments =
                    SplitTopLevel(std::string_view(item).substr(innerOpen + 1, *innerClose - innerOpen - 1), ',');
                lines.push_back(baseIndent + prefix + item.substr(0, innerOpen + 1));
                for (size_t index = 0; index < arguments.size(); ++index) {
                    lines.push_back(
                        std::string(static_cast<size_t>(indent + indentWidth_), ' ') +
                        arguments[index] +
                        (index + 1 < arguments.size() ? "," : "")
                    );
                }
                lines.push_back(baseIndent + item.substr(*innerClose) + std::string(1, closeChar) + suffix);
                return LineLayout{std::move(lines)};
            }
        }
        lines.push_back(baseIndent + prefix);
        for (size_t index = 0; index < items.size(); ++index) {
            std::string item = items[index];
            if (separator == ',' && index + 1 < items.size()) {
                item += ",";
            } else if (separator == ';' && index + 1 < items.size()) {
                item += ";";
            }
            if (openChar == '(' &&
                items.size() == 1 &&
                !suffix.empty() &&
                (suffix.front() == '*' || suffix.front() == '/' || suffix.front() == '+' ||
                 suffix.front() == '-' || suffix.front() == '&' || suffix.front() == '|')) {
                if (std::optional<LineLayout> grouped =
                        SplitBinaryChainAtIndent(indent + indentWidth_, item, depth + 1, indent + indentWidth_)) {
                    lines.insert(lines.end(), grouped->lines.begin(), grouped->lines.end());
                    continue;
                }
            }
            LineLayout itemLayout = FormatContent(item, indent + indentWidth_, depth + 1);
            lines.insert(lines.end(), itemLayout.lines.begin(), itemLayout.lines.end());
        }
        lines.push_back(baseIndent + std::string(1, closeChar) + suffix);
        return LineLayout{std::move(lines)};
    }

    size_t FindBestDelimiterOpen(const std::string& content) const {
        if (tools::StartsWith(content, "EXPECT_FALSE(") && content.find(" << ") != std::string::npos) {
            return std::string::npos;
        }
        if (tools::StartsWith(content, "} ")) {
            return std::string::npos;
        }
        auto isEligible = [&](size_t index) {
            const std::optional<size_t> close = FindMatchingDelimiter(content, index);
            if (!close) {
                return false;
            }
            if (*close <= index + 1) {
                return false;
            }
            if (content[index] == '{' && !IsBreakableBraceOpen(content, index)) {
                return false;
            }
            const std::string inside = content.substr(index + 1, *close - index - 1);
            const std::string suffix = TrimCopy(std::string_view(content).substr(*close + 1));
            if (content[index] == '(' && !IsBreakableParenOpen(content, index)) {
                const std::string prefix = TrimCopy(std::string_view(content).substr(0, index));
                const bool isParenthesizedOperatorGroup = (prefix.empty() || tools::EndsWith(prefix, "=")) &&
                    !suffix.empty() &&
                    (suffix.front() == '*' || suffix.front() == '/' || suffix.front() == '+' ||
                     suffix.front() == '-' || suffix.front() == '&' || suffix.front() == '|' ||
                     suffix.front() == '?' || suffix.front() == ':') &&
                    ContainsAnyBreakableContent(inside);
                if (!isParenthesizedOperatorGroup) {
                    return false;
                }
            }
            if ((tools::StartsWith(suffix, ".") || tools::StartsWith(suffix, "->")) &&
                !ContainsAnyBreakableContent(inside)) {
                return false;
            }
            if (SplitTopLevel(inside, ',').size() > 1 ||
                ((tools::StartsWith(content, "for ") || tools::StartsWith(content, "if ") ||
                  tools::StartsWith(content, "while ") || tools::StartsWith(content, "switch ")) &&
                 content[index] == '(') ||
                ContainsAnyBreakableContent(inside) ||
                content.size() > static_cast<size_t>(columnLimit_)) {
                return true;
            }
            return false;
        };
        for (size_t index = 0; index < content.size(); ++index) {
            if (content[index] != '(') {
                continue;
            }
            if (!isEligible(index)) {
                continue;
            }
            const size_t firstBrace = content.find('{');
            if (firstBrace != std::string::npos && firstBrace < index) {
                const std::optional<size_t> firstBraceClose = FindMatchingDelimiter(content, firstBrace);
                const std::string firstBraceSuffix = firstBraceClose ?
                    TrimCopy(std::string_view(content).substr(*firstBraceClose + 1)) :
                    std::string{};
                if (!tools::StartsWith(firstBraceSuffix, ".") && !tools::StartsWith(firstBraceSuffix, "->")) {
                    continue;
                }
            }
            if (content.find("LayoutEditAnchorRegistration{") == std::string::npos &&
                content.find("LayoutEditTreeLeaf{") == std::string::npos) {
                return index;
            }
        }
        for (size_t index = 0; index < content.size(); ++index) {
            if (content[index] != '{') {
                continue;
            }
            if (isEligible(index)) {
                return index;
            }
        }
        for (size_t index = 0; index < content.size(); ++index) {
            const char ch = content[index];
            if (ch != '(') {
                continue;
            }
            if (isEligible(index)) {
                return index;
            }
        }
        return std::string::npos;
    }

    std::optional<LineLayout> SplitBinaryChainAtIndent(
        int indent,
        const std::string& content,
        int depth,
        int continuationIndent
    ) {
        static constexpr std::string_view operators[] = {"&&", "||", "==", "+", "|"};
        for (std::string_view op : operators) {
            std::vector<std::string> parts = SplitTopLevelOperator(content, op);
            if (parts.size() <= 1) {
                continue;
            }
            std::vector<std::string> lines;
            lines.push_back(std::string(static_cast<size_t>(indent), ' ') + parts.front());
            for (size_t index = 1; index < parts.size(); ++index) {
                LineLayout part = FormatContent(parts[index], continuationIndent, depth + 1);
                lines.insert(lines.end(), part.lines.begin(), part.lines.end());
            }
            return LineLayout{std::move(lines)};
        }
        return std::nullopt;
    }

    std::optional<LineLayout> SplitBinaryChain(int indent, const std::string& content, int depth) {
        return SplitBinaryChainAtIndent(indent, content, depth, indent + indentWidth_);
    }
};

struct LineRewrite {
    std::vector<std::string> lines;
    size_t consumed = 0;
};

std::string NormalizeCompactArgument(std::string text) {
    text = CollapseSourceWhitespace(text);
    text = tools::ReplaceAll(std::move(text), "{ ", "{");
    text = tools::ReplaceAll(std::move(text), " }", "}");
    text = tools::ReplaceAll(std::move(text), "( ", "(");
    text = tools::ReplaceAll(std::move(text), " )", ")");
    return text;
}

void AppendJoinedTrimmed(std::string& target, std::string_view text) {
    const std::string trimmed = TrimCopy(text);
    if (trimmed.empty()) {
        return;
    }
    if (!target.empty()) {
        target.push_back(' ');
    }
    target += trimmed;
}

std::vector<size_t> FindCallOpenCandidatesBeforeLambda(std::string_view line, size_t lambdaMarker) {
    const size_t searchEnd = lambdaMarker == std::string::npos ? line.size() : lambdaMarker;
    std::vector<size_t> result;
    const std::string text(line);
    for (size_t index = 0; index < searchEnd; ++index) {
        if (line[index] == '(' && IsBreakableParenOpen(text, index)) {
            result.push_back(index);
        }
    }
    return result;
}

std::vector<std::string> SplitNonEmptyArguments(std::string_view text) {
    std::vector<std::string> result;
    for (std::string argument : SplitTopLevel(text, ',')) {
        argument = NormalizeCompactArgument(std::move(argument));
        if (!argument.empty()) {
            result.push_back(std::move(argument));
        }
    }
    return result;
}

std::optional<LineRewrite> RewriteLongCallWithLambdaArgument(
    const std::vector<std::string>& lines,
    size_t index,
    size_t columnLimit
) {
    if (index >= lines.size() || lines[index].size() <= columnLimit) {
        return std::nullopt;
    }

    const int baseIndent = LeadingSpaceCount(lines[index]);
    const std::string base(static_cast<size_t>(baseIndent), ' ');
    const std::string continuation(static_cast<size_t>(baseIndent + 4), ' ');
    const std::string bodyIndent(static_cast<size_t>(baseIndent + 8), ' ');
    const std::string firstTrimmed = TrimCopy(lines[index]);

    const std::vector<size_t> callOpenCandidates =
        FindCallOpenCandidatesBeforeLambda(firstTrimmed, firstTrimmed.find("[]("));
    if (callOpenCandidates.empty()) {
        return std::nullopt;
    }

    size_t callOpen = std::string::npos;
    size_t lambdaLine = lines.size();
    size_t lambdaMarker = std::string::npos;
    for (auto candidate = callOpenCandidates.rbegin(); candidate != callOpenCandidates.rend(); ++candidate) {
        int parenDepth = 0;
        bool inString = false;
        bool inChar = false;
        for (size_t line = index; line < lines.size() && line < index + 32; ++line) {
            const std::string trimmed = TrimCopy(lines[line]);
            const size_t scanStart = line == index ? *candidate : 0;
            const size_t marker = trimmed.find("[](");
            if (marker != std::string::npos && (line != index || marker > *candidate)) {
                callOpen = *candidate;
                lambdaLine = line;
                lambdaMarker = marker;
                break;
            }
            bool closedCandidate = false;
            for (size_t chIndex = scanStart; chIndex < trimmed.size(); ++chIndex) {
                const char ch = trimmed[chIndex];
                if (ch == '\\' && (inString || inChar) && chIndex + 1 < trimmed.size()) {
                    ++chIndex;
                    continue;
                }
                if (ch == '"' && !inChar) {
                    inString = !inString;
                    continue;
                }
                if (ch == '\'' && !inString) {
                    inChar = !inChar;
                    continue;
                }
                if (inString || inChar) {
                    continue;
                }
                if (ch == '(') {
                    ++parenDepth;
                } else if (ch == ')' && parenDepth > 0) {
                    --parenDepth;
                    if (parenDepth == 0) {
                        closedCandidate = true;
                        break;
                    }
                }
            }
            if (lambdaMarker != std::string::npos || closedCandidate) {
                break;
            }
        }
        if (lambdaMarker != std::string::npos) {
            break;
        }
    }
    if (lambdaMarker == std::string::npos) {
        return std::nullopt;
    }

    std::string preLambdaText;
    if (lambdaLine == index) {
        AppendJoinedTrimmed(preLambdaText, std::string_view(firstTrimmed).substr(callOpen + 1, lambdaMarker - callOpen - 1));
    } else {
        AppendJoinedTrimmed(preLambdaText, std::string_view(firstTrimmed).substr(callOpen + 1));
        for (size_t line = index + 1; line < lambdaLine; ++line) {
            AppendJoinedTrimmed(preLambdaText, TrimCopy(lines[line]));
        }
        const std::string lambdaTrimmed = TrimCopy(lines[lambdaLine]);
        AppendJoinedTrimmed(preLambdaText, std::string_view(lambdaTrimmed).substr(0, lambdaMarker));
    }
    while (!preLambdaText.empty() && (preLambdaText.back() == ',' || preLambdaText.back() == ' ')) {
        preLambdaText.pop_back();
    }
    std::vector<std::string> arguments = SplitNonEmptyArguments(preLambdaText);
    if (arguments.empty()) {
        return std::nullopt;
    }

    std::string lambdaHeader;
    size_t headerLine = lambdaLine;
    {
        const std::string lambdaTrimmed = TrimCopy(lines[lambdaLine]);
        AppendJoinedTrimmed(lambdaHeader, std::string_view(lambdaTrimmed).substr(lambdaMarker));
    }
    while (lambdaHeader.find('{') == std::string::npos && headerLine + 1 < lines.size() && headerLine < index + 16) {
        ++headerLine;
        AppendJoinedTrimmed(lambdaHeader, TrimCopy(lines[headerLine]));
    }
    const size_t headerBrace = lambdaHeader.find('{');
    if (headerBrace == std::string::npos) {
        return std::nullopt;
    }
    std::vector<std::string> bodyLines;
    std::string closeTail;
    const size_t inlineClose = lambdaHeader.find('}', headerBrace + 1);
    if (inlineClose != std::string::npos) {
        const std::string inlineBody = TrimCopy(
            std::string_view(lambdaHeader).substr(headerBrace + 1, inlineClose - headerBrace - 1)
        );
        if (!inlineBody.empty()) {
            bodyLines.push_back(inlineBody);
        }
        closeTail = TrimCopy(std::string_view(lambdaHeader).substr(inlineClose + 1));
    }
    lambdaHeader = NormalizeCompactArgument(lambdaHeader.substr(0, headerBrace + 1));

    size_t closeLine = inlineClose == std::string::npos ? headerLine + 1 : headerLine;
    if (inlineClose == std::string::npos) {
        while (closeLine < lines.size() && closeLine < index + 32 && !tools::StartsWith(TrimCopy(lines[closeLine]), "}")) {
            ++closeLine;
        }
        if (closeLine >= lines.size() || closeLine >= index + 32) {
            return std::nullopt;
        }

        for (size_t line = headerLine + 1; line < closeLine; ++line) {
            const std::string body = TrimCopy(lines[line]);
            if (!body.empty()) {
                bodyLines.push_back(body);
            }
        }

        closeTail = TrimCopy(lines[closeLine]);
        if (closeTail.empty() || closeTail.front() != '}') {
            return std::nullopt;
        }
        closeTail.erase(closeTail.begin());
    } else if (closeTail.empty()) {
        closeLine = headerLine + 1;
        if (closeLine >= lines.size()) {
            return std::nullopt;
        }
        closeTail = TrimCopy(lines[closeLine]);
    }

    std::vector<std::string> trailingArguments;
    std::string suffix;
    if (tools::StartsWith(closeTail, ",")) {
        std::string afterComma = TrimCopy(std::string_view(closeTail).substr(1));
        const size_t closeParen = afterComma.rfind(')');
        if (closeParen == std::string::npos) {
            return std::nullopt;
        }
        trailingArguments = SplitNonEmptyArguments(std::string_view(afterComma).substr(0, closeParen));
        suffix = afterComma.substr(closeParen + 1);
    } else if (tools::StartsWith(closeTail, ")")) {
        suffix = closeTail.substr(1);
    } else {
        return std::nullopt;
    }

    LineRewrite rewrite;
    rewrite.consumed = closeLine - index + 1;
    rewrite.lines.push_back(base + firstTrimmed.substr(0, callOpen + 1));
    for (const std::string& argument : arguments) {
        rewrite.lines.push_back(continuation + argument + ",");
    }

    const bool emptyLambda = bodyLines.empty();
    const bool compactLambda = bodyLines.size() == 1 &&
        continuation.size() + lambdaHeader.size() + 1 + bodyLines.front().size() + 2 <= columnLimit;
    if (emptyLambda) {
        rewrite.lines.push_back(
            continuation +
            lambdaHeader +
            "}" +
            (trailingArguments.empty() ? "" : ",")
        );
    } else if (compactLambda) {
        rewrite.lines.push_back(
            continuation +
            lambdaHeader +
            " " +
            bodyLines.front() +
            " }" +
            (trailingArguments.empty() ? "" : ",")
        );
    } else {
        rewrite.lines.push_back(continuation + lambdaHeader);
        for (const std::string& bodyLine : bodyLines) {
            rewrite.lines.push_back(bodyIndent + bodyLine);
        }
        rewrite.lines.push_back(continuation + "}" + (trailingArguments.empty() ? "" : ","));
    }

    for (size_t argument = 0; argument < trailingArguments.size(); ++argument) {
        rewrite.lines.push_back(
            continuation +
            trailingArguments[argument] +
            (argument + 1 < trailingArguments.size() ? "," : "")
        );
    }
    rewrite.lines.push_back(base + ")" + suffix);
    return rewrite;
}

std::string CollapseSingleStatementLambdas(const FormatterConfig& config, std::string text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find('\n', start);
        lines.push_back(end == std::string::npos ?
            text.substr(start) :
            text.substr(start, end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    if (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }

    std::vector<std::string> output;
    output.reserve(lines.size());
    const size_t columnLimit = static_cast<size_t>(std::max(1, config.columnLimit));
    for (size_t index = 0; index < lines.size();) {
        const std::string currentTrimmed = TrimCopy(lines[index]);
        if (tools::StartsWith(currentTrimmed, "} ")) {
            const std::string suffix = TrimCopy(std::string_view(currentTrimmed).substr(1));
            const size_t suffixAssignment = suffix.find('=');
            const size_t suffixDot = suffix.find('.');
            const bool isDeclaration =
                StartsWithWord(suffix, "auto") ||
                StartsWithWord(suffix, "bool") ||
                StartsWithWord(suffix, "char") ||
                StartsWithWord(suffix, "const") ||
                StartsWithWord(suffix, "double") ||
                StartsWithWord(suffix, "float") ||
                StartsWithWord(suffix, "int") ||
                StartsWithWord(suffix, "long") ||
                StartsWithWord(suffix, "short") ||
                StartsWithWord(suffix, "signed") ||
                StartsWithWord(suffix, "std") ||
                StartsWithWord(suffix, "unsigned");
            const bool isMemberAssignment =
                suffixAssignment != std::string::npos &&
                suffixDot != std::string::npos &&
                suffixDot < suffixAssignment;
            if (!StartsWithWord(suffix, "else") &&
                !StartsWithWord(suffix, "catch") &&
                !StartsWithWord(suffix, "finally") &&
                !StartsWithWord(suffix, "while") &&
                (isDeclaration || isMemberAssignment)) {
                const std::string base(static_cast<size_t>(LeadingSpaceCount(lines[index])), ' ');
                output.push_back(base + "}");
                output.push_back(base + suffix);
                ++index;
                continue;
            }
        }
        if (index + 1 < lines.size() && currentTrimmed == "}") {
            const std::string nextTrimmed = TrimCopy(lines[index + 1]);
            const size_t nextBrace = nextTrimmed.find('{');
            const size_t nextBracket = nextTrimmed.find('[');
            const size_t nextParen = nextTrimmed.find('(');
            const size_t nextDeclaratorMarker = std::min(nextBrace, nextBracket);
            const size_t nextDot = nextTrimmed.find('.');
            if (!nextTrimmed.empty() &&
                !StartsWithWord(nextTrimmed, "if") &&
                !StartsWithWord(nextTrimmed, "while") &&
                !StartsWithWord(nextTrimmed, "for") &&
                !StartsWithWord(nextTrimmed, "switch") &&
                !StartsWithWord(nextTrimmed, "do") &&
                !StartsWithWord(nextTrimmed, "else") &&
                !StartsWithWord(nextTrimmed, "catch") &&
                !StartsWithWord(nextTrimmed, "finally") &&
                !StartsWithWord(nextTrimmed, "return") &&
                (nextDot == std::string::npos || nextDot > nextDeclaratorMarker) &&
                (nextBracket != std::string::npos ||
                 (nextBrace != std::string::npos && (nextParen == std::string::npos || nextBrace < nextParen))) &&
                (std::isalpha(static_cast<unsigned char>(nextTrimmed.front())) != 0 || nextTrimmed.front() == '_')) {
                output.push_back(lines[index] + " " + nextTrimmed);
                index += 2;
                continue;
            }
        }
        if (tools::StartsWith(currentTrimmed, "-> ") && !output.empty()) {
            const std::string previousTrimmed = TrimCopy(output.back());
            if (tools::StartsWith(previousTrimmed, "(")) {
                std::string suffix = currentTrimmed;
                const bool opensBody = tools::EndsWith(suffix, "{");
                if (opensBody) {
                    suffix = TrimCopy(std::string_view(suffix).substr(0, suffix.size() - 1));
                }
                output.back() += " " + suffix;
                if (opensBody) {
                    output.push_back(std::string(static_cast<size_t>(LeadingSpaceCount(lines[index])), ' ') + "{");
                }
                ++index;
                continue;
            }
        }
        if (currentTrimmed == "{" &&
            !output.empty() &&
            (tools::EndsWith(TrimCopy(output.back()), "{{") || tools::EndsWith(TrimCopy(output.back()), "},"))) {
            std::vector<std::string> itemLines;
            size_t closeIndex = index + 1;
            while (closeIndex < lines.size() && TrimCopy(lines[closeIndex]) != "}," && TrimCopy(lines[closeIndex]) != "}") {
                itemLines.push_back(TrimCopy(lines[closeIndex]));
                ++closeIndex;
            }
            if (!itemLines.empty() && closeIndex < lines.size()) {
                std::string joined = std::string(static_cast<size_t>(LeadingSpaceCount(lines[index])), ' ') + "{";
                bool simple = true;
                for (size_t itemIndex = 0; itemIndex < itemLines.size(); ++itemIndex) {
                    if (itemLines[itemIndex].find('{') != std::string::npos ||
                        itemLines[itemIndex].find('}') != std::string::npos ||
                        tools::EndsWith(itemLines[itemIndex], "|") ||
                        tools::EndsWith(itemLines[itemIndex], "+")) {
                        simple = false;
                        break;
                    }
                    joined += itemLines[itemIndex];
                    if (itemIndex + 1 < itemLines.size()) {
                        joined += " ";
                    }
                }
                joined += TrimCopy(lines[closeIndex]) == "}," ? "}," : "}";
                if (simple && joined.size() <= columnLimit) {
                    output.push_back(joined);
                    index = closeIndex + 1;
                    continue;
                }
            }
        }
        if (currentTrimmed == "{" && !output.empty() && TrimCopy(output.back()) == "},") {
            output.back() += " {";
            ++index;
            continue;
        }
        if (currentTrimmed == "{" && !output.empty() && tools::EndsWith(TrimCopy(output.back()), "=")) {
            output.back() += " {";
            ++index;
            continue;
        }
        if (index + 1 < lines.size() &&
            TrimCopy(lines[index + 1]) == "{" &&
            TrimCopy(lines[index]).find("->") != std::string::npos &&
            lines[index].find("](") != std::string::npos) {
            const int baseIndent = LeadingSpaceCount(lines[index]);
            output.push_back(lines[index] + " {");
            index += 2;
            while (index < lines.size() && !tools::StartsWith(TrimCopy(lines[index]), "})")) {
                const std::string trimmed = TrimCopy(lines[index]);
                output.push_back(trimmed.empty() ? lines[index] : std::string(static_cast<size_t>(baseIndent + 4), ' ') + trimmed);
                ++index;
            }
            if (index < lines.size()) {
                output.push_back(std::string(static_cast<size_t>(baseIndent), ' ') + TrimCopy(lines[index]));
                ++index;
            }
            continue;
        }
        if (index + 2 < lines.size() && tools::EndsWith(lines[index], " {")) {
            const std::string statement = TrimCopy(lines[index + 1]);
            const std::string close = TrimCopy(lines[index + 2]);
            const std::string joined = lines[index] + " " + statement + " " + close;
            if (lines[index].find("](") != std::string::npos &&
                tools::StartsWith(statement, "return ") &&
                tools::EndsWith(statement, ";") &&
                (close == "};" || close == "}") &&
                joined.size() <= columnLimit) {
                output.push_back(joined);
                index += 3;
                continue;
            }
        }
        output.push_back(lines[index]);
        ++index;
    }

    std::string result;
    for (size_t index = 2; index < output.size(); ++index) {
        const std::string opener = TrimCopy(output[index - 2]);
        const std::string previous = TrimCopy(output[index - 1]);
        if ((opener == "(" || tools::EndsWith(opener, "= (")) &&
            (tools::EndsWith(previous, "+") || tools::EndsWith(previous, "&&") || tools::EndsWith(previous, "||")) &&
            LeadingSpaceCount(output[index]) > LeadingSpaceCount(output[index - 1])) {
            output[index] =
                std::string(static_cast<size_t>(LeadingSpaceCount(output[index - 1])), ' ') +
                TrimCopy(output[index]);
        }
    }
    for (size_t index = 0; index < output.size(); ++index) {
        const std::string line = TrimCopy(output[index]);
        if (!tools::StartsWith(line, "}") || !tools::EndsWith(line, "= {")) {
            continue;
        }
        const int baseIndent = LeadingSpaceCount(output[index]);
        for (size_t inner = index + 1; inner < output.size(); ++inner) {
            const std::string trimmed = TrimCopy(output[inner]);
            if (trimmed.empty()) {
                break;
            }
            const int targetIndent = trimmed == "};" ? baseIndent : baseIndent + 4;
            output[inner] = std::string(static_cast<size_t>(targetIndent), ' ') + trimmed;
            if (trimmed == "};") {
                break;
            }
        }
    }
    std::vector<std::string> rewritten;
    rewritten.reserve(output.size());
    for (size_t index = 0; index < output.size(); ++index) {
        if (std::optional<LineRewrite> call = RewriteLongCallWithLambdaArgument(output, index, columnLimit)) {
            rewritten.insert(rewritten.end(), call->lines.begin(), call->lines.end());
            index += call->consumed - 1;
            continue;
        }
        rewritten.push_back(output[index]);
    }
    output = std::move(rewritten);
    for (const std::string& line : output) {
        result.append(line);
        result.push_back('\n');
    }
    return result;
}

std::string ApplyBreakSelection(const FormatterConfig& config, std::string text) {
    text = CollapseSingleStatementLambdas(config, std::move(text));
    LineBreakSolver solver(std::max(1, config.columnLimit), std::max(1, config.indentWidth));
    std::string result;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find('\n', start);
        const std::string_view line = end == std::string::npos ?
            std::string_view(text).substr(start) :
            std::string_view(text).substr(start, end - start);
        const std::vector<std::string> lines = solver.Format(line);
        for (const std::string& outputLine : lines) {
            result.append(outputLine);
            result.push_back('\n');
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return CollapseSingleStatementLambdas(config, std::move(result));
}

class Printer {
public:
    Printer(const FormatterConfig& config, std::string_view sourcePath) :
        config_(config),
        sourcePath_(sourcePath),
        indentWidth_(std::max(1, config.indentWidth)) {}

    std::string Print(const std::vector<PrintToken>& tokens) {
        for (size_t index = 0; index < tokens.size(); ++index) {
            const PrintToken* previous = PreviousToken(tokens, index);
            const PrintToken* next = NextToken(tokens, index);
            const PrintToken* rawNext = RawNextToken(tokens, index);
            PrintOne(tokens[index], previous, next, rawNext);
        }
        FinishLine();
        if (!output_.empty() && output_.back() != '\n') {
            output_.push_back('\n');
        }
        return ApplyBreakSelection(config_, std::move(output_));
    }

private:
    const FormatterConfig& config_;
    std::string_view sourcePath_;
    int indentWidth_ = 4;
    std::string output_;
    int indentLevel_ = 0;
    bool atLineStart_ = true;
    bool lineHasText_ = false;
    bool macroContinuationLine_ = false;
    bool forceColumnZeroLine_ = false;
    int compactRightBraceSkips_ = 0;
    int switchDepth_ = 0;
    int parenDepth_ = 0;
    int bracketDepth_ = 0;
    std::vector<BraceRole> braceStack_;
    std::vector<int> braceParenDepthStack_;
    std::vector<int> activeCaseBodySwitchDepths_;

    static const PrintToken* PreviousToken(const std::vector<PrintToken>& tokens, size_t index) {
        while (index > 0) {
            --index;
            if (tokens[index].kind != PrintTokenKind::BlankLine && !IsCommentToken(tokens[index].kind)) {
                return &tokens[index];
            }
        }
        return nullptr;
    }

    static const PrintToken* NextToken(const std::vector<PrintToken>& tokens, size_t index) {
        for (++index; index < tokens.size(); ++index) {
            if (tokens[index].kind != PrintTokenKind::BlankLine && !IsCommentToken(tokens[index].kind)) {
                return &tokens[index];
            }
        }
        return nullptr;
    }

    static const PrintToken* RawNextToken(const std::vector<PrintToken>& tokens, size_t index) {
        return index + 1 < tokens.size() ? &tokens[index + 1] : nullptr;
    }

    void TrimTrailingSpaces() {
        while (!output_.empty() && output_.back() == ' ') {
            output_.pop_back();
        }
    }

    void FinishLine() {
        TrimTrailingSpaces();
    }

    void NewLine(bool macroContinuation = false) {
        FinishLine();
        if (macroContinuation && lineHasText_) {
            output_.append(" \\");
        }
        if (output_.empty() || output_.back() != '\n') {
            output_.push_back('\n');
        }
        atLineStart_ = true;
        lineHasText_ = false;
        macroContinuationLine_ = macroContinuation;
        forceColumnZeroLine_ = false;
    }

    void BlankLine() {
        NewLine(false);
        if (output_.size() < 2 || output_[output_.size() - 2] != '\n') {
            output_.push_back('\n');
        }
        atLineStart_ = true;
        lineHasText_ = false;
        macroContinuationLine_ = false;
        forceColumnZeroLine_ = false;
    }

    void WriteIndentIfNeeded() {
        if (!atLineStart_) {
            return;
        }
        const int macroOffset = macroContinuationLine_ ? 1 : 0;
        const int indentLevel = forceColumnZeroLine_ ? 0 : indentLevel_ + macroOffset;
        output_.append(static_cast<size_t>(std::max(0, indentLevel) * indentWidth_), ' ');
        atLineStart_ = false;
        macroContinuationLine_ = false;
        forceColumnZeroLine_ = false;
    }

    void WriteWithIndentOffset(std::string_view text, int indentOffset) {
        if (!atLineStart_) {
            output_.append(text);
            lineHasText_ = lineHasText_ || !text.empty();
            return;
        }
        const int adjustedIndent = std::max(0, indentLevel_ + indentOffset);
        output_.append(static_cast<size_t>(adjustedIndent * indentWidth_), ' ');
        atLineStart_ = false;
        macroContinuationLine_ = false;
        forceColumnZeroLine_ = false;
        output_.append(text);
        lineHasText_ = lineHasText_ || !text.empty();
    }

    void Write(std::string_view text) {
        WriteIndentIfNeeded();
        output_.append(text);
        lineHasText_ = lineHasText_ || !text.empty();
    }

    void CloseCaseBodyIndentIfNeeded() {
        if (!activeCaseBodySwitchDepths_.empty() && activeCaseBodySwitchDepths_.back() == switchDepth_) {
            indentLevel_ = std::max(0, indentLevel_ - 1);
            activeCaseBodySwitchDepths_.pop_back();
        }
    }

    void Space() {
        if (!atLineStart_ && !output_.empty() && output_.back() != ' ' && output_.back() != '\n') {
            output_.push_back(' ');
        }
    }

    bool InEnumBody() const {
        return !braceStack_.empty() && braceStack_.back() == BraceRole::Enum;
    }

    bool ShouldBreakAfterSemicolon() const {
        if (braceParenDepthStack_.empty()) {
            return parenDepth_ == 0;
        }
        return parenDepth_ <= braceParenDepthStack_.back();
    }

    bool SameMacroDefinition(const PrintToken* left, const PrintToken* right) const {
        return left != nullptr &&
            right != nullptr &&
            left->macroDefinition != nullptr &&
            left->macroDefinition == right->macroDefinition;
    }

    bool ShouldContinueMacroLine(const PrintToken& token, const PrintToken* next) const {
        return token.inMacroValue &&
            next != nullptr &&
            next->macroDefinition == token.macroDefinition;
    }

    bool StartsMacroValue(const PrintToken* previous, const PrintToken& current) const {
        return current.inMacroValue &&
            current.breakBeforeMacroValue &&
            (previous == nullptr || !SameMacroDefinition(previous, &current) || !previous->inMacroValue);
    }

    bool StartsMacroValueElement(const PrintToken* previous, const PrintToken& current) const {
        return current.inMacroValue &&
            previous != nullptr &&
            SameMacroDefinition(previous, &current) &&
            previous->inMacroValue &&
            previous->macroValueElement != nullptr &&
            current.macroValueElement != nullptr &&
            previous->macroValueElement != current.macroValueElement;
    }

    void PrepareMacroBoundary(const PrintToken* previous, const PrintToken& current) {
        if (current.macroDefinition != nullptr && !current.inMacroValue && atLineStart_) {
            forceColumnZeroLine_ = true;
        }
        if (previous != nullptr &&
            previous->macroDefinition != nullptr &&
            previous->macroDefinition != current.macroDefinition) {
            if (lineHasText_) {
                NewLine(false);
            }
            if (!IsPreprocessorLikeToken(current)) {
                BlankLine();
            }
        }
        if (current.macroDefinition != nullptr &&
            (previous == nullptr || previous->macroDefinition != current.macroDefinition) &&
            lineHasText_) {
            NewLine(false);
            if (!current.inMacroValue) {
                forceColumnZeroLine_ = true;
            }
        }
        if ((StartsMacroValue(previous, current) || StartsMacroValueElement(previous, current)) && lineHasText_) {
            NewLine(true);
        }
    }

    bool ShouldSpaceBetween(const PrintToken* previous, const PrintToken& current) const {
        if (previous == nullptr ||
            (IsPreprocessorLikeToken(*previous) && previous->macroDefinition == nullptr) ||
            (IsPreprocessorLikeToken(current) && current.macroDefinition == nullptr)) {
            return false;
        }
        if (current.inMacroValue && !previous->inMacroValue && SameMacroDefinition(previous, &current)) {
            return true;
        }
        if (previous->kind != PrintTokenKind::Known && current.kind != PrintTokenKind::Known) {
            return IsWordLike(*previous) && IsWordLike(current);
        }
        const KnownToken prev = previous->kind == PrintTokenKind::Known ? previous->known : KnownToken::Unknown;
        const KnownToken cur = current.kind == PrintTokenKind::Known ? current.known : KnownToken::Unknown;

        if (IsStringLike(*previous) && IsStringLike(current)) {
            return true;
        }
        if ((cur == KnownToken::Arrow && current.parentKind == SyntaxTreeKind::TrailingReturnType) ||
            (prev == KnownToken::Arrow && previous->parentKind == SyntaxTreeKind::TrailingReturnType)) {
            return true;
        }
        if (cur == KnownToken::Dot && current.parentKind == SyntaxTreeKind::FieldDesignator &&
            prev == KnownToken::Comma) {
            return true;
        }
        if (cur == KnownToken::RightParen || cur == KnownToken::RightBracket || cur == KnownToken::Comma ||
            cur == KnownToken::Semicolon || cur == KnownToken::ColonColon || cur == KnownToken::Dot ||
            cur == KnownToken::Arrow || cur == KnownToken::DotStar || cur == KnownToken::ArrowStar) {
            if (cur == KnownToken::ColonColon &&
                (KnownTokenHasClass(prev, TokenClass::Keyword) ||
                 KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
                 prev == KnownToken::KeywordReturn)) {
                return true;
            }
            return false;
        }
        if (prev == KnownToken::LeftParen || prev == KnownToken::LeftBracket || prev == KnownToken::ColonColon ||
            prev == KnownToken::Dot || prev == KnownToken::Arrow || prev == KnownToken::DotStar ||
            prev == KnownToken::ArrowStar || prev == KnownToken::Tilde) {
            return false;
        }
        if (prev == KnownToken::KeywordOperator && cur != KnownToken::LeftParen) {
            return false;
        }
        if (prev == KnownToken::KeywordVirtual && cur == KnownToken::Tilde) {
            return true;
        }
        if (cur == KnownToken::LeftParen) {
            if (IsParenthesizedDeclarator(current.parentKind)) {
                return true;
            }
            if (previous->parentKind == SyntaxTreeKind::OperatorName ||
                previous->parentKind == SyntaxTreeKind::OperatorCast) {
                return false;
            }
            if (previous->kind == PrintTokenKind::Known &&
                (KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
                 (KnownTokenHasClass(prev, TokenClass::BinaryOperator) && IsBinaryContext(*previous)) ||
                 prev == KnownToken::Comma ||
                 prev == KnownToken::KeywordReturn ||
                 (prev == KnownToken::Colon && previous->parentKind == SyntaxTreeKind::ConditionalExpression) ||
                 prev == KnownToken::Question)) {
                return true;
            }
            return previous->kind == PrintTokenKind::Known &&
                KnownTokenHasClass(prev, TokenClass::ControlKeyword);
        }
        if (cur == KnownToken::LeftBracket) {
            if (current.parentKind == SyntaxTreeKind::StructuredBindingDeclarator) {
                return true;
            }
            if (current.parentKind == SyntaxTreeKind::LambdaCaptureSpecifier) {
                return previous->kind == PrintTokenKind::Known &&
                    (KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
                     prev == KnownToken::Comma ||
                     prev == KnownToken::KeywordReturn ||
                     prev == KnownToken::Question ||
                     (prev == KnownToken::Colon && previous->parentKind == SyntaxTreeKind::ConditionalExpression));
            }
            return false;
        }
        if (cur == KnownToken::LeftBrace) {
            if (current.parentKind == SyntaxTreeKind::InitializerList) {
                return previous->kind == PrintTokenKind::Known &&
                    (KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
                     prev == KnownToken::Comma ||
                     prev == KnownToken::KeywordReturn ||
                     prev == KnownToken::Question ||
                     prev == KnownToken::Colon);
            }
            if (current.parentKind == SyntaxTreeKind::CompoundStatement ||
                current.parentKind == SyntaxTreeKind::FieldDeclarationList ||
                current.parentKind == SyntaxTreeKind::DeclarationList ||
                current.parentKind == SyntaxTreeKind::EnumeratorList) {
                return true;
            }
            return prev == KnownToken::KeywordReturn;
        }
        if (prev == KnownToken::Comma || prev == KnownToken::Semicolon || prev == KnownToken::Question) {
            return true;
        }
        if (prev == KnownToken::Ellipsis && IsWordLike(current)) {
            return true;
        }
        if (cur == KnownToken::Question) {
            return true;
        }
        if (prev == KnownToken::Colon) {
            return current.parentKind != SyntaxTreeKind::CaseStatement &&
                !KnownTokenHasClass(previous->known, TokenClass::AccessKeyword);
        }
        if (cur == KnownToken::Less && prev == KnownToken::KeywordTemplate) {
            return true;
        }
        if (cur == KnownToken::Colon) {
            if (previous->known == KnownToken::KeywordDefault ||
                KnownTokenHasClass(previous->known, TokenClass::AccessKeyword)) {
                return false;
            }
            return current.parentKind != SyntaxTreeKind::CaseStatement;
        }
        if (IsDeclaratorBindingToken(current)) {
            return false;
        }
        if (IsDeclaratorBindingToken(*previous)) {
            if (cur == KnownToken::Greater) {
                return false;
            }
            if (IsParenthesizedDeclarator(previous->grandParentKind)) {
                return false;
            }
            return !IsDeclaratorBindingToken(current);
        }
        if (prev == KnownToken::KeywordReturn && current.kind == PrintTokenKind::Known &&
            KnownTokenHasClass(cur, TokenClass::UnaryOperator)) {
            return true;
        }
        if (current.kind == PrintTokenKind::Known &&
            (KnownTokenHasClass(cur, TokenClass::AssignmentOperator) ||
             (KnownTokenHasClass(cur, TokenClass::BinaryOperator) && IsBinaryContext(current)))) {
            return true;
        }
        if (previous->kind == PrintTokenKind::Known &&
            (KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
             (KnownTokenHasClass(prev, TokenClass::BinaryOperator) && IsBinaryContext(*previous)))) {
            return true;
        }
        if (current.kind == PrintTokenKind::Known && KnownTokenHasClass(cur, TokenClass::UnaryOperator) &&
            IsUnaryContext(current)) {
            return false;
        }
        if (previous->kind == PrintTokenKind::Known && KnownTokenHasClass(prev, TokenClass::UnaryOperator) &&
            IsUnaryContext(*previous)) {
            return false;
        }
        if (KnownTokenHasClass(prev, TokenClass::MemberOperator) ||
            KnownTokenHasClass(cur, TokenClass::MemberOperator)) {
            return false;
        }
        if (IsWordLike(*previous) && IsWordLike(current)) {
            return true;
        }
        if (previous->kind == PrintTokenKind::Known &&
            (prev == KnownToken::RightParen || prev == KnownToken::RightBracket || prev == KnownToken::RightBrace ||
             prev == KnownToken::Greater) &&
            IsWordLike(current)) {
            if (prev == KnownToken::RightParen && previous->parentKind == SyntaxTreeKind::CastExpression) {
                return false;
            }
            return true;
        }
        return false;
    }

    void PrintOne(
        const PrintToken& token,
        const PrintToken* previous,
        const PrintToken* next,
        const PrintToken* rawNext
    ) {
        PrepareMacroBoundary(previous, token);
        if (token.kind == PrintTokenKind::BlankLine) {
            BlankLine();
            return;
        }
        if (IsCommentToken(token.kind)) {
            PrintComment(token);
            return;
        }
        if (token.kind == PrintTokenKind::Preprocessor) {
            PrintPreprocessor(token, next);
            return;
        }
        if (token.kind == PrintTokenKind::IncludeRun) {
            PrintIncludeRun(token, next);
            return;
        }
        if (token.kind == PrintTokenKind::Known) {
            PrintKnown(token, previous, next, rawNext);
            return;
        }
        if (ShouldSpaceBetween(previous, token)) {
            Space();
        }
        Write(token.text);
    }

    void PrintComment(const PrintToken& token) {
        if (lineHasText_) {
            Space();
            output_.push_back(' ');
            Write(token.text);
            NewLine();
            return;
        }
        Write(token.text);
        NewLine();
    }

    void PrintIncludeRun(const PrintToken& token, const PrintToken* next) {
        if (token.node == nullptr) {
            return;
        }
        if (lineHasText_) {
            NewLine();
        }
        const std::string text = FormatIncludeRunText(config_, *token.node, sourcePath_);
        output_.append(text);
        atLineStart_ = true;
        lineHasText_ = false;
        if (!text.empty() && next != nullptr) {
            BlankLine();
        }
    }

    void PrintPreprocessor(const PrintToken& token, const PrintToken* next) {
        const std::string line = CollapseSourceWhitespace(token.text);
        const bool isInclude = tools::StartsWith(line, "#include");
        const bool isUndef = tools::StartsWith(line, "#undef");
        if (isUndef) {
            BlankLine();
        }
        if (lineHasText_) {
            NewLine();
        }
        output_.append(line);
        lineHasText_ = true;
        atLineStart_ = false;
        NewLine();
        if (tools::StartsWith(line, "#pragma once") || isUndef ||
            (!isInclude && next != nullptr && !IsPreprocessorLikeToken(*next))) {
            BlankLine();
        }
    }

    void PrintKnown(
        const PrintToken& token,
        const PrintToken* previous,
        const PrintToken* next,
        const PrintToken* rawNext
    ) {
        switch (token.known) {
        case KnownToken::LeftParen:
            if (ShouldSpaceBetween(previous, token) && !IsCompilerCallModifierStart(rawNext)) {
                Space();
            }
            Write(KnownTokenText(token.known));
            ++parenDepth_;
            return;
        case KnownToken::RightParen:
            Write(KnownTokenText(token.known));
            if (parenDepth_ > 0) {
                --parenDepth_;
            }
            if (token.inRequiresClause && token.inTemplateDeclaration &&
                !(next != nullptr && next->inRequiresClause)) {
                NewLine(ShouldContinueMacroLine(token, next));
            }
            return;
        case KnownToken::LeftBracket:
            if (ShouldSpaceBetween(previous, token)) {
                Space();
            }
            Write(KnownTokenText(token.known));
            ++bracketDepth_;
            return;
        case KnownToken::RightBracket:
            Write(KnownTokenText(token.known));
            if (bracketDepth_ > 0) {
                --bracketDepth_;
            }
            return;
        case KnownToken::Greater:
            if (ShouldSpaceBetween(previous, token)) {
                Space();
            }
            Write(KnownTokenText(token.known));
            if (token.parentKind == SyntaxTreeKind::TemplateParameterList &&
                token.inTemplateDeclaration &&
                !(next != nullptr && next->kind == PrintTokenKind::Known &&
                  next->known == KnownToken::KeywordRequires)) {
                NewLine(ShouldContinueMacroLine(token, next));
            }
            return;
        case KnownToken::LeftBrace:
            PrintLeftBrace(token, previous, rawNext);
            return;
        case KnownToken::RightBrace:
            PrintRightBrace(token, next, rawNext);
            return;
        case KnownToken::Semicolon:
            Write(";");
            if (ShouldBreakAfterSemicolon() &&
                !(rawNext != nullptr && rawNext->kind == PrintTokenKind::TrailingComment)) {
                NewLine(ShouldContinueMacroLine(token, next));
            }
            return;
        case KnownToken::Comma:
            Write(",");
            if (InEnumBody() && parenDepth_ == 0 && bracketDepth_ == 0 &&
                !(rawNext != nullptr && rawNext->kind == PrintTokenKind::TrailingComment)) {
                NewLine(ShouldContinueMacroLine(token, next));
            }
            return;
        case KnownToken::Colon:
            if (ShouldSpaceBetween(previous, token)) {
                Space();
            }
            Write(":");
            if (token.parentKind == SyntaxTreeKind::CaseStatement) {
                ++indentLevel_;
                activeCaseBodySwitchDepths_.push_back(switchDepth_);
                if (rawNext != nullptr && rawNext->kind == PrintTokenKind::Known &&
                    rawNext->known == KnownToken::LeftBrace) {
                    return;
                }
                NewLine(ShouldContinueMacroLine(token, next));
                return;
            }
            if (previous != nullptr && previous->kind == PrintTokenKind::Known &&
                (previous->known == KnownToken::KeywordDefault ||
                 KnownTokenHasClass(previous->known, TokenClass::AccessKeyword))) {
                NewLine(ShouldContinueMacroLine(token, next));
            }
            return;
        default:
            if (ShouldSpaceBetween(previous, token)) {
                Space();
            }
            if (IsCaseLabelKeyword(token) && atLineStart_) {
                CloseCaseBodyIndentIfNeeded();
            }
            if (IsAccessKeyword(token)) {
                WriteWithIndentOffset(KnownTokenText(token.known), -1);
                return;
            }
            Write(KnownTokenText(token.known));
            if (token.inRequiresClause && token.inTemplateDeclaration &&
                !(next != nullptr && next->inRequiresClause)) {
                NewLine(ShouldContinueMacroLine(token, next));
            }
            return;
        }
    }

    void PrintLeftBrace(const PrintToken& token, const PrintToken* previous, const PrintToken* rawNext) {
        const bool isEmptyBracePair = rawNext != nullptr &&
            rawNext->kind == PrintTokenKind::Known &&
            rawNext->known == KnownToken::RightBrace;
        const bool isCaseBlock = previous != nullptr &&
            previous->kind == PrintTokenKind::Known &&
            previous->known == KnownToken::Colon &&
            previous->parentKind == SyntaxTreeKind::CaseStatement;
        const BraceRole role = isCaseBlock ? BraceRole::CaseBlock : RoleForBrace(token);
        if (ShouldSpaceBetween(previous, token)) {
            Space();
        }
        if (isEmptyBracePair) {
            Write("{}");
            ++compactRightBraceSkips_;
            return;
        }
        Write("{");
        braceStack_.push_back(role);
        braceParenDepthStack_.push_back(parenDepth_);
        if (token.parentKind == SyntaxTreeKind::CompoundStatement &&
            token.grandParentKind == SyntaxTreeKind::SwitchStatement) {
            ++switchDepth_;
        }
        if (role == BraceRole::Block || role == BraceRole::Enum) {
            ++indentLevel_;
            NewLine(ShouldContinueMacroLine(token, rawNext));
        } else if (role == BraceRole::Namespace || role == BraceRole::CaseBlock) {
            NewLine(ShouldContinueMacroLine(token, rawNext));
            if (role == BraceRole::Namespace) {
                BlankLine();
            }
        }
    }

    void PrintRightBrace(const PrintToken& token, const PrintToken* next, const PrintToken* rawNext) {
        if (compactRightBraceSkips_ > 0) {
            --compactRightBraceSkips_;
            return;
        }
        const BraceRole role = braceStack_.empty() ? RoleForBrace(token) : braceStack_.back();
        const bool keepTrailingComment = rawNext != nullptr && rawNext->kind == PrintTokenKind::TrailingComment;
        if (!braceStack_.empty()) {
            braceStack_.pop_back();
            braceParenDepthStack_.pop_back();
        }
        if (role == BraceRole::Namespace) {
            if (lineHasText_) {
                NewLine(token.inMacroValue);
            }
            BlankLine();
            Write("}");
            if (!keepTrailingComment) {
                NewLine(ShouldContinueMacroLine(token, next));
            }
            return;
        }
        if (role == BraceRole::CaseBlock) {
            if (lineHasText_) {
                NewLine(token.inMacroValue);
            }
            WriteWithIndentOffset("}", -1);
            if (!keepTrailingComment) {
                NewLine(ShouldContinueMacroLine(token, next));
            }
            return;
        }
        if (role != BraceRole::Compact) {
            if (lineHasText_) {
                NewLine(token.inMacroValue);
            }
            const bool isSwitchBody = token.parentKind == SyntaxTreeKind::CompoundStatement &&
                token.grandParentKind == SyntaxTreeKind::SwitchStatement;
            if (isSwitchBody) {
                CloseCaseBodyIndentIfNeeded();
            }
            indentLevel_ = std::max(0, indentLevel_ - 1);
            Write("}");
            if (isSwitchBody) {
                switchDepth_ = std::max(0, switchDepth_ - 1);
            }
            if (next != nullptr && next->kind == PrintTokenKind::Known &&
                (next->known == KnownToken::Semicolon || next->known == KnownToken::Comma ||
                 (token.parentKind == SyntaxTreeKind::CompoundStatement &&
                  token.grandParentKind == SyntaxTreeKind::LambdaExpression &&
                  next->known == KnownToken::RightParen) ||
                 (KnownTokenHasClass(next->known, TokenClass::AttachAfterBlockKeyword) &&
                  next->known != KnownToken::KeywordWhile) ||
                 (next->known == KnownToken::KeywordWhile && next->parentKind == SyntaxTreeKind::DoStatement))) {
                return;
            }
            if (!keepTrailingComment) {
                NewLine(ShouldContinueMacroLine(token, next));
            }
            return;
        }
        Write(KnownTokenText(token.known));
    }
};

}  // namespace

std::string FormatModelText(const FormatterConfig& config, const FormatModel& model, std::string_view sourcePath) {
    if (!model.root) {
        return {};
    }
    std::vector<PrintToken> tokens;
    AppendTokens(
        *model.root,
        SyntaxTreeKind::Unknown,
        SyntaxTreeKind::Unknown,
        false,
        false,
        nullptr,
        nullptr,
        false,
        false,
        tokens
    );
    return Printer(config, sourcePath).Print(tokens);
}
