#include "tools/impl/format_model_builder.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace {

struct TreeMapping {
    std::string_view text;
    SyntaxTreeKind kind;
};

constexpr auto kTreeKinds = std::to_array<TreeMapping>({
    {"translation_unit", SyntaxTreeKind::TranslationUnit},
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
    {"preproc_if_in_field_declaration_list", SyntaxTreeKind::PreprocIf},
    {"preproc_ifdef_in_field_declaration_list", SyntaxTreeKind::PreprocIfdef},
    {"preproc_else_in_field_declaration_list", SyntaxTreeKind::PreprocElse},
    {"preproc_elif_in_field_declaration_list", SyntaxTreeKind::PreprocElif},
    {"preproc_if_in_enumerator_list", SyntaxTreeKind::PreprocIf},
    {"preproc_ifdef_in_enumerator_list", SyntaxTreeKind::PreprocIfdef},
    {"preproc_else_in_enumerator_list", SyntaxTreeKind::PreprocElse},
    {"preproc_elif_in_enumerator_list", SyntaxTreeKind::PreprocElif},
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
    {"raw_string_literal", SyntaxTreeKind::RawStringLiteral},
    {"string_literal", SyntaxTreeKind::StringLiteral},
    {"char_literal", SyntaxTreeKind::CharacterLiteral},
});

SyntaxTreeKind TreeKindFromType(std::string_view type) {
    if (type == "identifier" || type == "field_identifier" || type == "namespace_identifier" ||
        type == "type_identifier" || type == "qualified_identifier") {
        return SyntaxTreeKind::Identifier;
    }
    if (type == "number_literal") {
        return SyntaxTreeKind::NumberLiteral;
    }
    if (type == "string_literal") {
        return SyntaxTreeKind::StringLiteral;
    }
    if (type == "raw_string_literal") {
        return SyntaxTreeKind::RawStringLiteral;
    }
    if (type == "char_literal") {
        return SyntaxTreeKind::CharacterLiteral;
    }
    for (const TreeMapping& mapping : kTreeKinds) {
        if (mapping.text == type) {
            return mapping.kind;
        }
    }
    return SyntaxTreeKind::Unknown;
}

TSPoint StartPoint(TSNode node) {
    return ts_node_start_point(node);
}

TSPoint EndPoint(TSNode node) {
    return ts_node_end_point(node);
}

std::string_view NodeText(TSNode node, std::string_view source) {
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (start > end || end > source.size()) {
        return {};
    }
    return source.substr(start, end - start);
}

std::string LineSnippet(std::string_view source, uint32_t byte) {
    size_t start = std::min<size_t>(byte, source.size());
    while (start > 0 && source[start - 1] != '\n' && source[start - 1] != '\r') {
        --start;
    }
    size_t end = std::min<size_t>(byte, source.size());
    while (end < source.size() && source[end] != '\n' && source[end] != '\r') {
        ++end;
    }
    return std::string(source.substr(start, end - start));
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

bool KeepWholeNodeAsFreeToken(SyntaxTreeKind kind, std::string_view type) {
    return IsLiteralKind(kind) ||
        type == "preproc_arg" ||
        type == "system_lib_string" ||
        type == "ms_call_modifier";
}

bool IsAtomicPreprocessorNode(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::PreprocCall ||
        kind == SyntaxTreeKind::PreprocDef ||
        kind == SyntaxTreeKind::PreprocFunctionDef ||
        kind == SyntaxTreeKind::PreprocInclude ||
        kind == SyntaxTreeKind::PreprocUsing;
}

std::unique_ptr<SyntaxNode> MakeBlankLine() {
    auto node = std::make_unique<SyntaxNode>();
    node->kind = SyntaxNodeKind::BlankLine;
    return node;
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

    if (childCount == 0 || KeepWholeNodeAsFreeToken(treeKind, type)) {
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

ParseResult ParseFailure(TSNode root, std::string_view source) {
    ProblemNode problem = FindFirstProblem(root);
    if (!problem.found) {
        problem = {.found = true, .missing = false, .node = root};
    }
    const TSPoint point = StartPoint(problem.node);
    ParseResult parse;
    parse.ok = false;
    parse.hasErrors = !problem.missing;
    parse.hasMissingNodes = problem.missing;
    parse.errorNodeType = problem.missing ? "missing " + std::string(ts_node_type(problem.node)) :
                                            std::string(ts_node_type(problem.node));
    parse.errorLine = static_cast<int>(point.row) + 1;
    parse.errorColumn = static_cast<int>(point.column) + 1;
    parse.errorSnippet = LineSnippet(source, ts_node_start_byte(problem.node));
    parse.error = "tree-sitter parse failed at " +
        std::to_string(parse.errorLine) +
        ":" +
        std::to_string(parse.errorColumn) +
        " near " +
        parse.errorNodeType;
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
        model.parse = ParseFailure(root, source);
        return model;
    }

    model.root = BuildNode(root, source);
    model.parse.ok = true;
    return model;
}
