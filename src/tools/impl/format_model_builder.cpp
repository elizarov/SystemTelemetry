#include "tools/impl/format_model_builder.h"

#include <optional>
#include <string_view>

#include "tools/impl/tools_common.h"

namespace {

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

bool IsLiteralKind(SyntaxNodeKind kind) {
    return SyntaxNodeKindHasClass(kind, TokenClass::Literal);
}

bool KeepWholeNodeAsFreeToken(SyntaxNodeKind kind) {
    return SyntaxNodeKindHasClass(kind, TokenClass::WholeNodeAsFreeToken);
}

bool IsAtomicPreprocessorNode(SyntaxNodeKind kind) {
    return SyntaxNodeKindHasClass(kind, TokenClass::AtomicPreprocessor);
}

std::string_view TrimLeadingWhitespace(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    return value;
}

bool IsPragmaOnceNode(const SyntaxNode& node) {
    return node.kind == SyntaxNodeKind::PreprocCall && StartsWith(TrimLeadingWhitespace(node.text), "#pragma once");
}

SyntaxNode* MakeNode(FormatModel& model) {
    return &model.nodes.emplace_back();
}

void SetParentRecursive(SyntaxNode& node, const SyntaxNode* parent) {
    node.parent = parent;
    node.depth = parent == nullptr ? 0 : parent->depth + 1;
    for (SyntaxNode* child : node.children) {
        if (child != nullptr) {
            SetParentRecursive(*child, &node);
        }
    }
}

void AppendChild(SyntaxNode& parent, SyntaxNode* child) {
    if (child != nullptr) {
        SetParentRecursive(*child, &parent);
    }
    parent.children.push_back(child);
}

bool IsIncludeNode(const SyntaxNode* node) {
    return node != nullptr && node->kind == SyntaxNodeKind::PreprocInclude;
}

bool IsOpeningIncludeSpacer(const SyntaxNode& node) {
    return node.kind == SyntaxNodeKind::BlankLine ||
        node.kind == SyntaxNodeKind::Comment ||
        node.kind == SyntaxNodeKind::TrailingComment;
}

SyntaxNode* MakeBlankLine(FormatModel& model) {
    SyntaxNode* node = MakeNode(model);
    node->kind = SyntaxNodeKind::BlankLine;
    return node;
}

SyntaxNode* MakeTokenNode(FormatModel& model, SyntaxNodeKind token) {
    SyntaxNode* node = MakeNode(model);
    node->kind = token;
    return node;
}

bool IsTriviaNode(const SyntaxNode* node) {
    return node != nullptr && (
        node->kind == SyntaxNodeKind::BlankLine ||
        node->kind == SyntaxNodeKind::Comment ||
        node->kind == SyntaxNodeKind::TrailingComment
    );
}

bool IsCommentNode(const SyntaxNode* node) {
    return node != nullptr && (node->kind == SyntaxNodeKind::Comment || node->kind == SyntaxNodeKind::TrailingComment);
}

bool IsTokenNode(const SyntaxNode* node, SyntaxNodeKind token) {
    return node != nullptr && node->kind == token;
}

bool IsTreeNode(const SyntaxNode* node, SyntaxNodeKind kind) {
    return node != nullptr && node->kind == kind;
}

bool IsMacroLikeInvocationNode(const SyntaxNode* node) {
    return node != nullptr &&
        node->kind == SyntaxNodeKind::Tree &&
        node->children.size() == 2 &&
        IsTreeNode(node->children[0], SyntaxNodeKind::Identifier) &&
        IsTreeNode(node->children[1], SyntaxNodeKind::ArgumentList);
}

std::optional<size_t> PreviousNonTriviaChildIndex(const std::vector<SyntaxNode*>& children, size_t before) {
    while (before > 0) {
        --before;
        if (!IsTriviaNode(children[before])) {
            return before;
        }
    }
    return std::nullopt;
}

std::optional<size_t> NextNonTriviaChildIndex(const std::vector<SyntaxNode*>& children, size_t after) {
    for (size_t index = after; index < children.size(); ++index) {
        if (!IsTriviaNode(children[index])) {
            return index;
        }
    }
    return std::nullopt;
}

void NormalizeTrailingCommas(FormatModel& model, SyntaxNode& node) {
    std::vector<SyntaxNode*>& children = node.children;
    for (size_t index = 0; index < children.size(); ++index) {
        if (
            !IsTokenNode(children[index], SyntaxNodeKind::RightBrace) &&
            !IsTokenNode(children[index], SyntaxNodeKind::RightParen) &&
            !IsTokenNode(children[index], SyntaxNodeKind::RightBracket) &&
            !IsTokenNode(children[index], SyntaxNodeKind::Greater)
        ) {
            continue;
        }
        const std::optional<size_t> previous = PreviousNonTriviaChildIndex(children, index);
        if (!previous) {
            continue;
        }
        if (node.kind == SyntaxNodeKind::EnumeratorList) {
            if (
                !IsTokenNode(children[*previous], SyntaxNodeKind::Comma) &&
                !IsTokenNode(children[*previous], SyntaxNodeKind::LeftBrace) &&
                !IsMacroLikeInvocationNode(children[*previous])
            ) {
                SyntaxNode* comma = MakeTokenNode(model, SyntaxNodeKind::Comma);
                SetParentRecursive(*comma, &node);
                children.insert(children.begin() + static_cast<std::ptrdiff_t>(*previous + 1), comma);
                ++index;
            }
            continue;
        }
        if (IsTokenNode(children[*previous], SyntaxNodeKind::Comma)) {
            children.erase(children.begin() + static_cast<std::ptrdiff_t>(*previous));
            --index;
        }
    }
}

void WrapControlBody(FormatModel& model, SyntaxNode& node, size_t childIndex) {
    if (
        childIndex >= node.children.size() || IsTreeNode(node.children[childIndex], SyntaxNodeKind::CompoundStatement)
    ) {
        return;
    }
    size_t firstBodyIndex = childIndex;
    while (firstBodyIndex > 0 && IsCommentNode(node.children[firstBodyIndex - 1])) {
        --firstBodyIndex;
    }

    SyntaxNode* compound = MakeNode(model);
    compound->kind = SyntaxNodeKind::CompoundStatement;
    compound->children.reserve(childIndex - firstBodyIndex + 3);
    AppendChild(*compound, MakeTokenNode(model, SyntaxNodeKind::LeftBrace));
    for (size_t index = firstBodyIndex; index <= childIndex; ++index) {
        AppendChild(*compound, node.children[index]);
    }
    AppendChild(*compound, MakeTokenNode(model, SyntaxNodeKind::RightBrace));

    node.children.erase(
        node.children.begin() + static_cast<std::ptrdiff_t>(firstBodyIndex),
        node.children.begin() + static_cast<std::ptrdiff_t>(childIndex + 1)
    );
    SetParentRecursive(*compound, &node);
    node.children.insert(node.children.begin() + static_cast<std::ptrdiff_t>(firstBodyIndex), compound);
}

std::optional<size_t> FindOnlyIfInBraceBlock(const SyntaxNode& node) {
    if (node.kind != SyntaxNodeKind::CompoundStatement) {
        return std::nullopt;
    }
    std::optional<size_t> ifIndex;
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (
            IsTokenNode(node.children[index], SyntaxNodeKind::LeftBrace) ||
            IsTokenNode(node.children[index], SyntaxNodeKind::RightBrace)
        ) {
            continue;
        }
        if (IsTreeNode(node.children[index], SyntaxNodeKind::IfStatement) && !ifIndex) {
            ifIndex = index;
            continue;
        }
        return std::nullopt;
    }
    return ifIndex;
}

void NormalizeElseClauseBody(FormatModel& model, SyntaxNode& node) {
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (!IsTokenNode(node.children[index], SyntaxNodeKind::KeywordElse)) {
            continue;
        }
        const std::optional<size_t> bodyIndex = NextNonTriviaChildIndex(node.children, index + 1);
        if (!bodyIndex) {
            return;
        }
        if (IsTreeNode(node.children[*bodyIndex], SyntaxNodeKind::IfStatement)) {
            return;
        }
        if (IsTreeNode(node.children[*bodyIndex], SyntaxNodeKind::CompoundStatement)) {
            std::optional<size_t> ifIndex = FindOnlyIfInBraceBlock(*node.children[*bodyIndex]);
            if (ifIndex) {
                node.children[*bodyIndex] = node.children[*bodyIndex]->children[*ifIndex];
                SetParentRecursive(*node.children[*bodyIndex], &node);
                return;
            }
            return;
        }
        WrapControlBody(model, node, *bodyIndex);
        return;
    }
}

void NormalizeIfStatementBody(FormatModel& model, SyntaxNode& node) {
    size_t before = node.children.size();
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (IsTreeNode(node.children[index], SyntaxNodeKind::ElseClause)) {
            before = index;
            break;
        }
    }
    const std::optional<size_t> consequenceIndex = PreviousNonTriviaChildIndex(node.children, before);
    if (consequenceIndex) {
        WrapControlBody(model, node, *consequenceIndex);
    }
}

void NormalizeDoStatementBody(FormatModel& model, SyntaxNode& node) {
    for (size_t index = 0; index < node.children.size(); ++index) {
        if (!IsTokenNode(node.children[index], SyntaxNodeKind::KeywordWhile)) {
            continue;
        }
        const std::optional<size_t> bodyIndex = PreviousNonTriviaChildIndex(node.children, index);
        if (bodyIndex) {
            WrapControlBody(model, node, *bodyIndex);
        }
        return;
    }
}

void NormalizeLastControlBody(FormatModel& model, SyntaxNode& node) {
    const std::optional<size_t> bodyIndex = PreviousNonTriviaChildIndex(node.children, node.children.size());
    if (bodyIndex) {
        WrapControlBody(model, node, *bodyIndex);
    }
}

void NormalizeControlBodies(FormatModel& model, SyntaxNode& node) {
    switch (node.kind) {
        case SyntaxNodeKind::IfStatement:
            NormalizeIfStatementBody(model, node);
            return;
        case SyntaxNodeKind::ElseClause:
            NormalizeElseClauseBody(model, node);
            return;
        case SyntaxNodeKind::ForStatement:
        case SyntaxNodeKind::WhileStatement:
        case SyntaxNodeKind::SwitchStatement:
            NormalizeLastControlBody(model, node);
            return;
        case SyntaxNodeKind::DoStatement:
            NormalizeDoStatementBody(model, node);
            return;
        default:
            return;
    }
}

void NormalizeSyntaxNode(FormatModel& model, SyntaxNode& node) {
    NormalizeTrailingCommas(model, node);
    NormalizeControlBodies(model, node);
}

SyntaxNode* BuildNode(FormatModel& model, TSNode tsNode, std::string_view source, const SyntaxNode* parent) {
    SyntaxNode* node = MakeNode(model);
    node->parent = parent;
    node->depth = parent == nullptr ? 0 : parent->depth + 1;
    const std::string_view type = ts_node_type(tsNode);
    const std::string_view text = NodeText(tsNode, source);
    const SyntaxNodeKind syntaxKind = SyntaxNodeKindFromTreeType(type);

    if (type == "comment") {
        node->kind = SyntaxNodeKind::Comment;
        std::string_view commentText = text;
        while (!commentText.empty() && (commentText.back() == '\r' || commentText.back() == '\n')) {
            commentText.remove_suffix(1);
        }
        node->text = commentText;
        return node;
    }

    const uint32_t childCount = ts_node_child_count(tsNode);
    const SyntaxNodeKind known = SyntaxNodeKindFromTokenText(text);
    if (SyntaxNodeKindHasClass(known, TokenClass::Known)) {
        node->kind = known;
        return node;
    }

    if (childCount == 0 || KeepWholeNodeAsFreeToken(syntaxKind)) {
        node->kind = syntaxKind == SyntaxNodeKind::Unknown ? SyntaxNodeKind::FreeToken : syntaxKind;
        node->text = text;
        return node;
    }

    if (IsAtomicPreprocessorNode(syntaxKind)) {
        node->kind = syntaxKind;
        node->text = text;
        return node;
    }

    node->kind = syntaxKind == SyntaxNodeKind::Unknown ? SyntaxNodeKind::Tree : syntaxKind;
    node->children.reserve(childCount);
    uint32_t previousEnd = ts_node_start_byte(tsNode);
    uint32_t previousEndRow = StartPoint(tsNode).row;
    for (uint32_t index = 0; index < childCount; ++index) {
        TSNode child = ts_node_child(tsNode, index);
        const uint32_t childStart = ts_node_start_byte(child);
        if (!node->children.empty() && ContainsBlankLine(source, previousEnd, childStart)) {
            AppendChild(*node, MakeBlankLine(model));
        }
        SyntaxNode* childNode = BuildNode(model, child, source, node);
        if (childNode->kind == SyntaxNodeKind::Comment) {
            if (!node->children.empty() && previousEndRow == StartPoint(child).row) {
                childNode->kind = SyntaxNodeKind::TrailingComment;
            }
        }
        node->children.push_back(childNode);
        previousEnd = ts_node_end_byte(child);
        previousEndRow = ts_node_end_point(child).row;
    }
    NormalizeSyntaxNode(model, *node);
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
    std::vector<SyntaxNode*>& sourceChildren,
    size_t& index,
    std::vector<SyntaxNode*>& groupedChildren,
    FormatModel& model
) {
    SyntaxNode* includeRun = MakeNode(model);
    includeRun->kind = SyntaxNodeKind::IncludeRun;

    for (; index < sourceChildren.size(); ++index) {
        if (IsIncludeNode(sourceChildren[index])) {
            AppendChild(*includeRun, sourceChildren[index]);
            continue;
        }
        if (sourceChildren[index] != nullptr && sourceChildren[index]->kind == SyntaxNodeKind::BlankLine) {
            continue;
        }
        break;
    }

    groupedChildren.push_back(includeRun);
}

void GroupOpeningIncludeRuns(FormatModel& model, SyntaxNode& root) {
    if (root.kind != SyntaxNodeKind::TranslationUnit) {
        return;
    }

    std::vector<SyntaxNode*> groupedChildren;
    groupedChildren.reserve(root.children.size());
    bool sawInclude = false;
    bool inOpeningArea = true;
    for (size_t index = 0; index < root.children.size();) {
        if (inOpeningArea && IsIncludeNode(root.children[index])) {
            AppendIncludeRun(root.children, index, groupedChildren, model);
            sawInclude = true;
            continue;
        }
        const bool canRemainInOpeningArea = inOpeningArea &&
            root.children[index] != nullptr &&
            (IsOpeningIncludeSpacer(*root.children[index]) || (!sawInclude && IsPragmaOnceNode(*root.children[index])));
        if (canRemainInOpeningArea) {
            groupedChildren.push_back(root.children[index]);
            ++index;
            continue;
        }

        inOpeningArea = false;
        groupedChildren.push_back(root.children[index]);
        ++index;
    }

    root.children = std::move(groupedChildren);
    for (SyntaxNode* child : root.children) {
        if (child != nullptr && child->parent != &root) {
            SetParentRecursive(*child, &root);
        }
    }
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

    model.root = BuildNode(model, root, source, nullptr);
    GroupOpeningIncludeRuns(model, *model.root);
    model.parse.ok = true;
    return model;
}
