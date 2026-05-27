#include "tools/impl/format_model_builder.h"

#include <array>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "tools/impl/tools_common.h"

namespace {

struct TreeMapping {
    std::string_view text;
    SyntaxTreeKind kind;
};

constexpr auto kTreeKinds = std::to_array<TreeMapping>({
    {"translation_unit", SyntaxTreeKind::TranslationUnit},
    {"macro_replacement_list", SyntaxTreeKind::MacroReplacementList},
    {"declaration", SyntaxTreeKind::Declaration},
    {"field_declaration", SyntaxTreeKind::FieldDeclaration},
    {"function_definition", SyntaxTreeKind::FunctionDefinition},
    {"compound_statement", SyntaxTreeKind::CompoundStatement},
    {"field_declaration_list", SyntaxTreeKind::FieldDeclarationList},
    {"enumerator_list", SyntaxTreeKind::EnumeratorList},
    {"initializer_list", SyntaxTreeKind::InitializerList},
    {"declaration_list", SyntaxTreeKind::DeclarationList},
    {"namespace_definition", SyntaxTreeKind::NamespaceDefinition},
    {"enum_specifier", SyntaxTreeKind::EnumSpecifier},
    {"class_specifier", SyntaxTreeKind::ClassSpecifier},
    {"struct_specifier", SyntaxTreeKind::StructSpecifier},
    {"if_statement", SyntaxTreeKind::IfStatement},
    {"else_clause", SyntaxTreeKind::ElseClause},
    {"for_statement", SyntaxTreeKind::ForStatement},
    {"while_statement", SyntaxTreeKind::WhileStatement},
    {"do_statement", SyntaxTreeKind::DoStatement},
    {"switch_statement", SyntaxTreeKind::SwitchStatement},
    {"case_statement", SyntaxTreeKind::CaseStatement},
    {"preproc_call", SyntaxTreeKind::PreprocCall},
    {"preproc_def", SyntaxTreeKind::PreprocDef},
    {"preproc_function_def", SyntaxTreeKind::PreprocFunctionDef},
    {"preproc_include", SyntaxTreeKind::PreprocInclude},
    {"preproc_if", SyntaxTreeKind::PreprocIf},
    {"preproc_ifdef", SyntaxTreeKind::PreprocIfdef},
    {"preproc_else", SyntaxTreeKind::PreprocElse},
    {"preproc_elif", SyntaxTreeKind::PreprocElif},
    {"preproc_using", SyntaxTreeKind::PreprocUsing},
    {"preproc_params", SyntaxTreeKind::PreprocParams},
    {"preproc_arg", SyntaxTreeKind::PreprocArg},
    {"preproc_if_in_field_declaration_list", SyntaxTreeKind::PreprocIf},
    {"preproc_ifdef_in_field_declaration_list", SyntaxTreeKind::PreprocIfdef},
    {"preproc_else_in_field_declaration_list", SyntaxTreeKind::PreprocElse},
    {"preproc_elif_in_field_declaration_list", SyntaxTreeKind::PreprocElif},
    {"preproc_if_in_enumerator_list", SyntaxTreeKind::PreprocIf},
    {"preproc_ifdef_in_enumerator_list", SyntaxTreeKind::PreprocIfdef},
    {"preproc_else_in_enumerator_list", SyntaxTreeKind::PreprocElse},
    {"preproc_elif_in_enumerator_list", SyntaxTreeKind::PreprocElif},
    {"preproc_elifdef", SyntaxTreeKind::PreprocElif},
    {"binary_expression", SyntaxTreeKind::BinaryExpression},
    {"unary_expression", SyntaxTreeKind::UnaryExpression},
    {"conditional_expression", SyntaxTreeKind::ConditionalExpression},
    {"assignment_expression", SyntaxTreeKind::AssignmentExpression},
    {"pointer_declarator", SyntaxTreeKind::PointerDeclarator},
    {"abstract_pointer_declarator", SyntaxTreeKind::AbstractPointerDeclarator},
    {"reference_declarator", SyntaxTreeKind::ReferenceDeclarator},
    {"abstract_reference_declarator", SyntaxTreeKind::AbstractReferenceDeclarator},
    {"handle_declarator", SyntaxTreeKind::HandleDeclarator},
    {"abstract_handle_declarator", SyntaxTreeKind::AbstractHandleDeclarator},
    {"member_pointer_declarator", SyntaxTreeKind::MemberPointerDeclarator},
    {"function_declarator", SyntaxTreeKind::FunctionDeclarator},
    {"abstract_function_declarator", SyntaxTreeKind::AbstractFunctionDeclarator},
    {"parenthesized_declarator", SyntaxTreeKind::ParenthesizedDeclarator},
    {"abstract_parenthesized_declarator", SyntaxTreeKind::AbstractParenthesizedDeclarator},
    {"parameter_list", SyntaxTreeKind::ParameterList},
    {"argument_list", SyntaxTreeKind::ArgumentList},
    {"template_parameter_list", SyntaxTreeKind::TemplateParameterList},
    {"template_argument_list", SyntaxTreeKind::TemplateArgumentList},
    {"template_declaration", SyntaxTreeKind::TemplateDeclaration},
    {"requires_clause", SyntaxTreeKind::RequiresClause},
    {"lambda_expression", SyntaxTreeKind::LambdaExpression},
    {"lambda_capture_specifier", SyntaxTreeKind::LambdaCaptureSpecifier},
    {"structured_binding_declarator", SyntaxTreeKind::StructuredBindingDeclarator},
    {"field_designator", SyntaxTreeKind::FieldDesignator},
    {"field_expression", SyntaxTreeKind::FieldExpression},
    {"trailing_return_type", SyntaxTreeKind::TrailingReturnType},
    {"operator_name", SyntaxTreeKind::OperatorName},
    {"operator_cast", SyntaxTreeKind::OperatorCast},
    {"ms_call_modifier", SyntaxTreeKind::MsCallModifier},
    {"raw_string_literal", SyntaxTreeKind::RawStringLiteral},
    {"string_literal", SyntaxTreeKind::StringLiteral},
    {"system_lib_string", SyntaxTreeKind::SystemLibString},
    {"char_literal", SyntaxTreeKind::CharacterLiteral},
    {"number_literal", SyntaxTreeKind::NumberLiteral},
    {"identifier", SyntaxTreeKind::Identifier},
    {"field_identifier", SyntaxTreeKind::Identifier},
    {"namespace_identifier", SyntaxTreeKind::Identifier},
    {"type_identifier", SyntaxTreeKind::Identifier},
    {"qualified_identifier", SyntaxTreeKind::Identifier},
});

const std::unordered_map<std::string_view, SyntaxTreeKind>& TreeKindByType() {
    static const std::unordered_map<std::string_view, SyntaxTreeKind> treeKinds = [] {
        std::unordered_map<std::string_view, SyntaxTreeKind> result;
        result.reserve(kTreeKinds.size());
        for (const TreeMapping& mapping : kTreeKinds) {
            result.emplace(mapping.text, mapping.kind);
        }
        return result;
    }();
    return treeKinds;
}

SyntaxTreeKind TreeKindFromType(std::string_view type) {
    const auto& treeKinds = TreeKindByType();
    const auto found = treeKinds.find(type);
    return found == treeKinds.end() ? SyntaxTreeKind::Unknown : found->second;
}

TSPoint StartPoint(TSNode node) {
    return ts_node_start_point(node);
}

std::string_view NodeText(TSNode node, std::string_view source) {
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (start > end || end > source.size()) {
        return {};
    }
    return source.substr(start, end - start);
}

bool ContainsBlankLine(std::string_view source, uint32_t firstEnd, uint32_t secondStart) {
    if (firstEnd >= secondStart || secondStart > source.size()) {
        return false;
    }
    int lineBreaks = 0;
    bool sawNonWhitespace = false;
    for (size_t index = firstEnd; index < secondStart; ++index) {
        const char ch = source[index];
        if (ch == '\r' || ch == '\n') {
            ++lineBreaks;
            if (ch == '\r' && index + 1 < secondStart && source[index + 1] == '\n') {
                ++index;
            }
            if (lineBreaks >= 2 && !sawNonWhitespace) {
                return true;
            }
            continue;
        }
        if (ch != ' ' && ch != '\t' && ch != '\v' && ch != '\f') {
            sawNonWhitespace = true;
        }
    }
    return lineBreaks >= 2 && !sawNonWhitespace;
}

bool IsLiteralKind(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::StringLiteral ||
        kind == SyntaxTreeKind::RawStringLiteral ||
        kind == SyntaxTreeKind::CharacterLiteral ||
        kind == SyntaxTreeKind::NumberLiteral;
}

bool KeepWholeNodeAsFreeToken(SyntaxTreeKind kind) {
    return IsLiteralKind(kind) ||
        kind == SyntaxTreeKind::PreprocArg ||
        kind == SyntaxTreeKind::SystemLibString ||
        kind == SyntaxTreeKind::MsCallModifier;
}

bool IsAtomicPreprocessorNode(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::PreprocCall ||
        kind == SyntaxTreeKind::PreprocInclude ||
        kind == SyntaxTreeKind::PreprocUsing;
}

std::string_view TrimLeadingWhitespace(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    return value;
}

bool IsPragmaOnceNode(const SyntaxNode& node) {
    return node.kind == SyntaxNodeKind::Tree &&
        node.treeKind == SyntaxTreeKind::PreprocCall &&
        tools::StartsWith(TrimLeadingWhitespace(node.text), "#pragma once");
}

bool IsIncludeNode(const std::unique_ptr<SyntaxNode>& node) {
    return node != nullptr &&
        node->kind == SyntaxNodeKind::Tree &&
        node->treeKind == SyntaxTreeKind::PreprocInclude;
}

bool IsOpeningIncludeSpacer(const SyntaxNode& node) {
    return node.kind == SyntaxNodeKind::BlankLine ||
        node.kind == SyntaxNodeKind::Comment ||
        node.kind == SyntaxNodeKind::TrailingComment;
}

std::unique_ptr<SyntaxNode> MakeBlankLine() {
    auto node = std::make_unique<SyntaxNode>();
    node->kind = SyntaxNodeKind::BlankLine;
    return node;
}

std::unique_ptr<SyntaxNode> MakeKnownToken(KnownToken token) {
    auto node = std::make_unique<SyntaxNode>();
    node->kind = SyntaxNodeKind::KnownToken;
    node->known = token;
    return node;
}

bool IsTriviaNode(const std::unique_ptr<SyntaxNode>& node) {
    return node != nullptr &&
        (node->kind == SyntaxNodeKind::BlankLine ||
         node->kind == SyntaxNodeKind::Comment ||
         node->kind == SyntaxNodeKind::TrailingComment);
}

bool IsCommentNode(const std::unique_ptr<SyntaxNode>& node) {
    return node != nullptr &&
        (node->kind == SyntaxNodeKind::Comment ||
         node->kind == SyntaxNodeKind::TrailingComment);
}

bool IsKnownTokenNode(const std::unique_ptr<SyntaxNode>& node, KnownToken token) {
    return node != nullptr &&
        node->kind == SyntaxNodeKind::KnownToken &&
        node->known == token;
}

bool IsTreeNode(const std::unique_ptr<SyntaxNode>& node, SyntaxTreeKind kind) {
    return node != nullptr &&
        node->kind == SyntaxNodeKind::Tree &&
        node->treeKind == kind;
}

std::optional<size_t> PreviousNonTriviaChildIndex(
    const std::vector<std::unique_ptr<SyntaxNode>>& children,
    size_t before
) {
    while (before > 0) {
        --before;
        if (!IsTriviaNode(children[before])) {
            return before;
        }
    }
    return std::nullopt;
}

std::optional<size_t> NextNonTriviaChildIndex(
    const std::vector<std::unique_ptr<SyntaxNode>>& children,
    size_t after
) {
    for (size_t index = after; index < children.size(); ++index) {
        if (!IsTriviaNode(children[index])) {
            return index;
        }
    }
    return std::nullopt;
}

void NormalizeTrailingCommas(SyntaxNode& node) {
    std::vector<std::unique_ptr<SyntaxNode>>& children = node.children;
    for (size_t index = 0; index < children.size(); ++index) {
        if (!IsKnownTokenNode(children[index], KnownToken::RightBrace) &&
            !IsKnownTokenNode(children[index], KnownToken::RightParen) &&
            !IsKnownTokenNode(children[index], KnownToken::RightBracket) &&
            !IsKnownTokenNode(children[index], KnownToken::Greater)) {
            continue;
        }
        const std::optional<size_t> previous = PreviousNonTriviaChildIndex(children, index);
        if (!previous) {
            continue;
        }
        if (node.treeKind == SyntaxTreeKind::EnumeratorList) {
            if (!IsKnownTokenNode(children[*previous], KnownToken::Comma) &&
                !IsKnownTokenNode(children[*previous], KnownToken::LeftBrace)) {
                children.insert(children.begin() + static_cast<std::ptrdiff_t>(*previous + 1), MakeKnownToken(KnownToken::Comma));
                ++index;
            }
            continue;
        }
        if (IsKnownTokenNode(children[*previous], KnownToken::Comma)) {
            children.erase(children.begin() + static_cast<std::ptrdiff_t>(*previous));
            --index;
        }
    }
}

void WrapControlBody(SyntaxNode& node, size_t childIndex) {
    if (childIndex >= node.children.size() ||
        IsTreeNode(node.children[childIndex], SyntaxTreeKind::CompoundStatement)) {
        return;
    }
    size_t firstBodyIndex = childIndex;
    while (firstBodyIndex > 0 && IsCommentNode(node.children[firstBodyIndex - 1])) {
        --firstBodyIndex;
    }

    auto compound = std::make_unique<SyntaxNode>();
    compound->kind = SyntaxNodeKind::Tree;
    compound->treeKind = SyntaxTreeKind::CompoundStatement;
    compound->children.reserve(childIndex - firstBodyIndex + 3);
    compound->children.push_back(MakeKnownToken(KnownToken::LeftBrace));
    for (size_t index = firstBodyIndex; index <= childIndex; ++index) {
        compound->children.push_back(std::move(node.children[index]));
    }
    compound->children.push_back(MakeKnownToken(KnownToken::RightBrace));

    node.children.erase(
        node.children.begin() + static_cast<std::ptrdiff_t>(firstBodyIndex),
        node.children.begin() + static_cast<std::ptrdiff_t>(childIndex + 1)
    );
    node.children.insert(node.children.begin() + static_cast<std::ptrdiff_t>(firstBodyIndex), std::move(compound));
}

std::optional<size_t> FindOnlyIfInBraceBlock(const SyntaxNode& node) {
    if (node.kind != SyntaxNodeKind::Tree || node.treeKind != SyntaxTreeKind::CompoundStatement) {
        return std::nullopt;
    }
    std::optional<size_t> ifIndex;
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (IsKnownTokenNode(node.children[index], KnownToken::LeftBrace) ||
            IsKnownTokenNode(node.children[index], KnownToken::RightBrace)) {
            continue;
        }
        if (IsTreeNode(node.children[index], SyntaxTreeKind::IfStatement) && !ifIndex) {
            ifIndex = index;
            continue;
        }
        return std::nullopt;
    }
    return ifIndex;
}

void NormalizeElseClauseBody(SyntaxNode& node) {
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (!IsKnownTokenNode(node.children[index], KnownToken::KeywordElse)) {
            continue;
        }
        const std::optional<size_t> bodyIndex = NextNonTriviaChildIndex(node.children, index + 1);
        if (!bodyIndex) {
            return;
        }
        if (IsTreeNode(node.children[*bodyIndex], SyntaxTreeKind::IfStatement)) {
            return;
        }
        if (IsTreeNode(node.children[*bodyIndex], SyntaxTreeKind::CompoundStatement)) {
            std::optional<size_t> ifIndex = FindOnlyIfInBraceBlock(*node.children[*bodyIndex]);
            if (ifIndex) {
                node.children[*bodyIndex] = std::move(node.children[*bodyIndex]->children[*ifIndex]);
                return;
            }
            return;
        }
        WrapControlBody(node, *bodyIndex);
        return;
    }
}

void NormalizeIfStatementBody(SyntaxNode& node) {
    size_t before = node.children.size();
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (IsTreeNode(node.children[index], SyntaxTreeKind::ElseClause)) {
            before = index;
            break;
        }
    }
    const std::optional<size_t> consequenceIndex = PreviousNonTriviaChildIndex(node.children, before);
    if (consequenceIndex) {
        WrapControlBody(node, *consequenceIndex);
    }
}

void NormalizeDoStatementBody(SyntaxNode& node) {
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (!IsKnownTokenNode(node.children[index], KnownToken::KeywordWhile)) {
            continue;
        }
        const std::optional<size_t> bodyIndex = PreviousNonTriviaChildIndex(node.children, index);
        if (bodyIndex) {
            WrapControlBody(node, *bodyIndex);
        }
        return;
    }
}

void NormalizeLastControlBody(SyntaxNode& node) {
    const std::optional<size_t> bodyIndex = PreviousNonTriviaChildIndex(node.children, node.children.size());
    if (bodyIndex) {
        WrapControlBody(node, *bodyIndex);
    }
}

void NormalizeControlBodies(SyntaxNode& node) {
    switch (node.treeKind) {
    case SyntaxTreeKind::IfStatement:
        NormalizeIfStatementBody(node);
        return;
    case SyntaxTreeKind::ElseClause:
        NormalizeElseClauseBody(node);
        return;
    case SyntaxTreeKind::ForStatement:
    case SyntaxTreeKind::WhileStatement:
    case SyntaxTreeKind::SwitchStatement:
        NormalizeLastControlBody(node);
        return;
    case SyntaxTreeKind::DoStatement:
        NormalizeDoStatementBody(node);
        return;
    default:
        return;
    }
}

void NormalizeSyntaxNode(SyntaxNode& node) {
    NormalizeTrailingCommas(node);
    NormalizeControlBodies(node);
}

std::unique_ptr<SyntaxNode> BuildNode(TSNode tsNode, std::string_view source) {
    auto node = std::make_unique<SyntaxNode>();
    const std::string_view type = ts_node_type(tsNode);
    const std::string_view text = NodeText(tsNode, source);
    const SyntaxTreeKind treeKind = TreeKindFromType(type);

    if (type == "comment") {
        node->kind = SyntaxNodeKind::Comment;
        node->treeKind = SyntaxTreeKind::Unknown;
        std::string_view commentText = text;
        while (!commentText.empty() && (commentText.back() == '\r' || commentText.back() == '\n')) {
            commentText.remove_suffix(1);
        }
        node->text = commentText;
        return node;
    }

    const uint32_t childCount = ts_node_child_count(tsNode);
    const KnownToken known = KnownTokenFromText(text);
    if (known != KnownToken::Unknown) {
        node->kind = SyntaxNodeKind::KnownToken;
        node->known = known;
        node->treeKind = treeKind;
        return node;
    }

    if (childCount == 0 || KeepWholeNodeAsFreeToken(treeKind)) {
        node->kind = SyntaxNodeKind::FreeToken;
        node->treeKind = treeKind;
        node->text = text;
        return node;
    }

    if (IsAtomicPreprocessorNode(treeKind)) {
        node->kind = SyntaxNodeKind::Tree;
        node->treeKind = treeKind;
        node->text = text;
        return node;
    }

    node->kind = SyntaxNodeKind::Tree;
    node->treeKind = treeKind;
    node->children.reserve(childCount);
    uint32_t previousEnd = ts_node_start_byte(tsNode);
    uint32_t previousEndRow = StartPoint(tsNode).row;
    for (uint32_t index = 0; index < childCount; ++index) {
        TSNode child = ts_node_child(tsNode, index);
        const uint32_t childStart = ts_node_start_byte(child);
        if (!node->children.empty() && ContainsBlankLine(source, previousEnd, childStart)) {
            node->children.push_back(MakeBlankLine());
        }
        std::unique_ptr<SyntaxNode> childNode = BuildNode(child, source);
        if (childNode->kind == SyntaxNodeKind::Comment) {
            if (!node->children.empty() && previousEndRow == StartPoint(child).row) {
                childNode->kind = SyntaxNodeKind::TrailingComment;
            }
        }
        node->children.push_back(std::move(childNode));
        previousEnd = ts_node_end_byte(child);
        previousEndRow = ts_node_end_point(child).row;
    }
    NormalizeSyntaxNode(*node);
    return node;
}

struct ProblemNode {
    bool found = false;
    bool missing = false;
    TSNode node = {};
};

ProblemNode FindFirstProblem(TSNode node) {
    if (ts_node_is_missing(node)) {
        return {.found = true, .missing = true, .node = node};
    }
    if (std::string_view(ts_node_type(node)) == "ERROR") {
        return {.found = true, .missing = false, .node = node};
    }

    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        TSNode child = ts_node_child(node, index);
        if (!ts_node_has_error(child) && !ts_node_is_missing(child)) {
            continue;
        }
        ProblemNode problem = FindFirstProblem(child);
        if (problem.found) {
            return problem;
        }
    }
    return {};
}

void AppendIncludeRun(
    std::vector<std::unique_ptr<SyntaxNode>>& sourceChildren,
    size_t& index,
    std::vector<std::unique_ptr<SyntaxNode>>& groupedChildren
) {
    auto includeRun = std::make_unique<SyntaxNode>();
    includeRun->kind = SyntaxNodeKind::Tree;
    includeRun->treeKind = SyntaxTreeKind::IncludeRun;

    for (; index < sourceChildren.size(); ++index) {
        if (IsIncludeNode(sourceChildren[index])) {
            includeRun->children.push_back(std::move(sourceChildren[index]));
            continue;
        }
        if (sourceChildren[index] != nullptr && sourceChildren[index]->kind == SyntaxNodeKind::BlankLine) {
            continue;
        }
        break;
    }

    groupedChildren.push_back(std::move(includeRun));
}

void GroupOpeningIncludeRuns(SyntaxNode& root) {
    if (root.kind != SyntaxNodeKind::Tree || root.treeKind != SyntaxTreeKind::TranslationUnit) {
        return;
    }

    std::vector<std::unique_ptr<SyntaxNode>> groupedChildren;
    groupedChildren.reserve(root.children.size());
    bool sawInclude = false;
    bool inOpeningArea = true;
    for (size_t index = 0; index < root.children.size();) {
        if (inOpeningArea && IsIncludeNode(root.children[index])) {
            AppendIncludeRun(root.children, index, groupedChildren);
            sawInclude = true;
            continue;
        }
        const bool canRemainInOpeningArea = inOpeningArea &&
            root.children[index] != nullptr &&
            (IsOpeningIncludeSpacer(*root.children[index]) || (!sawInclude && IsPragmaOnceNode(*root.children[index])));
        if (canRemainInOpeningArea) {
            groupedChildren.push_back(std::move(root.children[index]));
            ++index;
            continue;
        }

        inOpeningArea = false;
        groupedChildren.push_back(std::move(root.children[index]));
        ++index;
    }

    root.children = std::move(groupedChildren);
}

ParseResult ParseFailure(TSNode root) {
    ProblemNode problem = FindFirstProblem(root);
    if (!problem.found) {
        problem = {.found = true, .missing = false, .node = root};
    }
    const TSPoint point = StartPoint(problem.node);
    const std::string nodeType = problem.missing ? "missing " + std::string(ts_node_type(problem.node)) :
                                                   std::string(ts_node_type(problem.node));
    ParseResult parse;
    parse.ok = false;
    parse.error = "tree-sitter parse failed at " +
        std::to_string(static_cast<int>(point.row) + 1) +
        ":" +
        std::to_string(static_cast<int>(point.column) + 1) +
        " near " +
        nodeType;
    return parse;
}

}  // namespace

FormatModel BuildFormatModel(TSNode root, std::unique_ptr<std::string> sourceText) {
    FormatModel model;
    model.sourceText = std::move(sourceText);
    if (!model.sourceText) {
        model.parse.error = "formatter source ownership setup failed";
        return model;
    }

    const std::string_view source(*model.sourceText);
    if (ts_node_has_error(root) || ts_node_is_missing(root)) {
        model.parse = ParseFailure(root);
        return model;
    }

    model.root = BuildNode(root, source);
    GroupOpeningIncludeRuns(*model.root);
    model.parse.ok = true;
    return model;
}
