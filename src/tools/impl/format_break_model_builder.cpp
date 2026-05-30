#include "tools/impl/format_break_model_builder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <utility>

#include "tools/impl/format_break_model_inline_helpers.h"

namespace {

using SyntaxChildList = std::vector<const SyntaxNode*>;

bool IsTemplateAngleToken(const FormatBreakToken& token) {
    const PrintToken& printToken = FormatBreakTokenValue(token);
    return printToken.parentKind == SyntaxNodeKind::TemplateArgumentList ||
        printToken.parentKind == SyntaxNodeKind::TemplateParameterList;
}

FormatBreakDelimiterKind OpeningDelimiter(const FormatBreakToken& token) {
    const PrintToken& printToken = FormatBreakTokenValue(token);
    if (printToken.kind != PrintTokenKind::Known) {
        return FormatBreakDelimiterKind::None;
    }
    FormatBreakDelimiterKind delimiter = FormatBreakDelimiterKind::None;
    switch (printToken.syntaxKind) {
        case SyntaxNodeKind::LeftParen:
            delimiter = FormatBreakDelimiterKind::Paren;
            break;
        case SyntaxNodeKind::LeftBracket:
            delimiter = FormatBreakDelimiterKind::Bracket;
            break;
        case SyntaxNodeKind::LeftBrace:
            delimiter = FormatBreakDelimiterKind::Brace;
            break;
        case SyntaxNodeKind::Less:
            delimiter = IsTemplateAngleToken(token) ? FormatBreakDelimiterKind::Angle : FormatBreakDelimiterKind::None;
            break;
        default:
            break;
    }
    if (delimiter == FormatBreakDelimiterKind::None) {
        return FormatBreakDelimiterKind::None;
    }
    if (printToken.inCompilerCallModifier) {
        return FormatBreakDelimiterKind::None;
    }
    return delimiter;
}

FormatBreakDelimiterKind ClosingDelimiter(const FormatBreakToken& token) {
    const PrintToken& printToken = FormatBreakTokenValue(token);
    if (printToken.kind != PrintTokenKind::Known) {
        return FormatBreakDelimiterKind::None;
    }
    switch (printToken.syntaxKind) {
        case SyntaxNodeKind::RightParen:
            return FormatBreakDelimiterKind::Paren;
        case SyntaxNodeKind::RightBracket:
            return FormatBreakDelimiterKind::Bracket;
        case SyntaxNodeKind::RightBrace:
            return FormatBreakDelimiterKind::Brace;
        case SyntaxNodeKind::Greater:
            return IsTemplateAngleToken(token) ? FormatBreakDelimiterKind::Angle : FormatBreakDelimiterKind::None;
        default:
            return FormatBreakDelimiterKind::None;
    }
}

bool IsSelectedSeparator(const FormatBreakToken& token) {
    return FormatBreakTokenKind(token) == PrintTokenKind::Known && (
        FormatBreakTokenSyntaxKind(token) == SyntaxNodeKind::Comma ||
        FormatBreakTokenSyntaxKind(token) == SyntaxNodeKind::Semicolon
    );
}

bool IsBinaryOperatorForNode(const FormatBreakToken& token) {
    const PrintToken& printToken = FormatBreakTokenValue(token);
    return printToken.kind == PrintTokenKind::Known &&
        SyntaxNodeKindHasClass(printToken.syntaxKind, TokenClass::BinaryOperator) &&
        printToken.parentKind == SyntaxNodeKind::BinaryExpression;
}

bool IsAssignmentOperatorForNode(const FormatBreakToken& token) {
    const PrintToken& printToken = FormatBreakTokenValue(token);
    return printToken.kind == PrintTokenKind::Known &&
        SyntaxNodeKindHasClass(printToken.syntaxKind, TokenClass::AssignmentOperator) && (
            printToken.parentKind == SyntaxNodeKind::AssignmentExpression ||
            printToken.parentKind == SyntaxNodeKind::InitDeclarator ||
            printToken.parentKind == SyntaxNodeKind::FieldDeclaration ||
            printToken.parentKind == SyntaxNodeKind::AliasDeclaration
        );
}

bool IsConditionalOperatorForNode(const FormatBreakToken& token) {
    const PrintToken& printToken = FormatBreakTokenValue(token);
    return printToken.kind == PrintTokenKind::Known &&
        printToken.parentKind == SyntaxNodeKind::ConditionalExpression &&
        (printToken.syntaxKind == SyntaxNodeKind::Question || printToken.syntaxKind == SyntaxNodeKind::Colon);
}

bool IsCommaOperatorForNode(const FormatBreakToken& token) {
    const PrintToken& printToken = FormatBreakTokenValue(token);
    return printToken.kind == PrintTokenKind::Known &&
        printToken.parentKind == SyntaxNodeKind::CommaExpression &&
        SyntaxNodeKindHasClass(printToken.syntaxKind, TokenClass::ChainOperator);
}

bool IsControlHeaderKind(SyntaxNodeKind kind) {
    return SyntaxNodeKindHasClass(kind, TokenClass::ControlHeader);
}

bool IsForHeaderDelimiter(const FormatBreakToken& open) {
    const PrintToken& printToken = FormatBreakTokenValue(open);
    return printToken.parentKind == SyntaxNodeKind::ForStatement ||
        printToken.grandParentKind == SyntaxNodeKind::ForStatement;
}

bool IsFlatLogicalHeaderKind(SyntaxNodeKind kind) {
    return SyntaxNodeKindHasClass(kind, TokenClass::FlatLogicalHeader);
}

bool IsChainOperatorKind(SyntaxNodeKind token) {
    return SyntaxNodeKindHasClass(token, TokenClass::ChainOperator);
}

bool IsChainOperatorToken(const FormatBreakToken& token) {
    return FormatBreakTokenKind(token) == PrintTokenKind::Known &&
        IsChainOperatorKind(FormatBreakTokenSyntaxKind(token));
}

bool EndsWithEscapedLineFragment(std::string_view text) {
    const size_t quote = text.rfind('"');
    if (quote == std::string_view::npos || quote == 0) {
        return false;
    }
    if (quote >= 2 && text.substr(quote - 2, 2) == "\\n") {
        return true;
    }
    return quote >= 4 && text.substr(quote - 4, 4) == "\\r\\n";
}

bool ForcesStringBoundarySplit(const FormatBreakToken& token) {
    const std::string_view text = FormatTokenText(FormatBreakTokenValue(token));
    return EndsWithEscapedLineFragment(text);
}

bool IsLogicalOperatorToken(const FormatBreakToken& token) {
    return IsChainOperatorToken(token) && (
        FormatBreakTokenSyntaxKind(token) == SyntaxNodeKind::AmpersandAmpersand ||
        FormatBreakTokenSyntaxKind(token) == SyntaxNodeKind::PipePipe
    );
}

bool IsLogicalChain(const FormatBreakNode& node) {
    return node.kind == FormatBreakNodeKind::Chain &&
        node.chainKind == FormatBreakChainKind::AfterOperator &&
        !node.operators.empty() &&
        std::all_of(node.operators.begin(), node.operators.end(), IsLogicalOperatorToken);
}

bool IsFlatChainOperator(const FormatBreakToken& token) {
    return IsChainOperatorToken(token);
}

bool IsFlatParenthesizedChain(const FormatBreakNode& node) {
    return node.kind == FormatBreakNodeKind::Chain &&
        node.chainKind == FormatBreakChainKind::AfterOperator &&
        !node.operators.empty() &&
        std::all_of(node.operators.begin(), node.operators.end(), IsFlatChainOperator);
}

bool UsesFlatLogicalContinuation(const FormatBreakToken& open, const FormatBreakNode& item) {
    if (!IsLogicalChain(item)) {
        return false;
    }
    const PrintToken& printToken = FormatBreakTokenValue(open);
    if (printToken.parentKind == SyntaxNodeKind::RequiresClause) {
        return true;
    }
    if (
        printToken.parentKind == SyntaxNodeKind::ForStatement ||
        printToken.grandParentKind == SyntaxNodeKind::ForStatement
    ) {
        return false;
    }
    return IsFlatLogicalHeaderKind(printToken.parentKind) || IsFlatLogicalHeaderKind(printToken.grandParentKind);
}

bool UsesFlatNonCallParenthesisContinuation(const FormatBreakToken& open) {
    const PrintToken& printToken = FormatBreakTokenValue(open);
    if (printToken.kind != PrintTokenKind::Known || printToken.syntaxKind != SyntaxNodeKind::LeftParen) {
        return false;
    }
    return printToken.parentKind != SyntaxNodeKind::ArgumentList &&
        printToken.parentKind != SyntaxNodeKind::ParameterList &&
        printToken.parentKind != SyntaxNodeKind::ForStatement &&
        printToken.grandParentKind != SyntaxNodeKind::ForStatement;
}

bool IsListForceSplitMarker(SyntaxNodeKind kind) {
    return kind == SyntaxNodeKind::BlankLine ||
        kind == SyntaxNodeKind::Comment ||
        kind == SyntaxNodeKind::TrailingComment;
}

bool IsPrefixListNodeKind(SyntaxNodeKind kind) {
    return kind == SyntaxNodeKind::BaseClassClause || kind == SyntaxNodeKind::FieldInitializerList;
}

bool IsDeclarationNodeKind(SyntaxNodeKind kind) {
    return kind == SyntaxNodeKind::Declaration || kind == SyntaxNodeKind::FieldDeclaration;
}

class BreakModelBuilder {
public:
    BreakModelBuilder(std::span<const PrintToken> tokens, const FormatBreakModelContext& context) : context_(context) {
        model_.nodes = std::make_unique<std::deque<FormatBreakNode>>();
        selectionMark_ = NextSelectionMark();
        const PrintToken* previous = nullptr;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const PrintToken& token = tokens[index];
            bool spaceBefore = FormatTokenNeedsSpace(previous, token);
            if (token.node != nullptr) {
                token.node->formatPrintToken = &token;
                token.node->formatSpaceBefore = spaceBefore;
                token.node->formatTokenMark = selectionMark_;
            }
            for (
                const SyntaxNode* ancestor = token.node;
                ancestor != nullptr && ancestor->formatSelectionMark != selectionMark_;
                ancestor = ancestor->parent
            ) {
                ancestor->formatSelectionMark = selectionMark_;
            }
            previous = &token;
        }
        root_ = CommonRoot(tokens);
    }

    FormatBreakModel Build() {
        if (root_ != nullptr) {
            model_.root = BuildSyntaxNode(*root_, 0);
        }
        if (!model_.root) {
            model_.root = MakeNode(FormatBreakNodeKind::Sequence, 0);
        }
        return std::move(model_);
    }

private:
    const FormatBreakModelContext& context_;
    FormatBreakModel model_;
    const SyntaxNode* root_ = nullptr;
    std::uint32_t selectionMark_ = 0;
    int nextId_ = 1;

    static size_t AncestorCount(const SyntaxNode* node) {
        return node == nullptr ? 0 : node->depth + 1;
    }

    static const SyntaxNode* CommonAncestor(const SyntaxNode* left, const SyntaxNode* right) {
        size_t leftDepth = AncestorCount(left);
        size_t rightDepth = AncestorCount(right);
        while (leftDepth > rightDepth && left != nullptr) {
            left = left->parent;
            --leftDepth;
        }
        while (rightDepth > leftDepth && right != nullptr) {
            right = right->parent;
            --rightDepth;
        }
        while (left != right) {
            if (left == nullptr || right == nullptr) {
                return nullptr;
            }
            left = left->parent;
            right = right->parent;
        }
        return left;
    }

    static const SyntaxNode* CommonRoot(std::span<const PrintToken> tokens) {
        if (tokens.empty() || tokens.front().node == nullptr) {
            return nullptr;
        }
        const SyntaxNode* common = tokens.front().node;
        for (const PrintToken& token : tokens.subspan(1)) {
            common = CommonAncestor(common, token.node);
            if (common == nullptr) {
                return nullptr;
            }
        }
        return common;
    }

    static std::uint32_t NextSelectionMark() {
        thread_local std::uint32_t next = 1;
        const std::uint32_t mark = next++;
        if (next == 0) {
            next = 1;
        }
        return mark;
    }

    bool ContainsSelected(const SyntaxNode& node) const {
        return node.formatSelectionMark == selectionMark_;
    }

    SyntaxNodeKind ParentKind(const SyntaxNode& node) const {
        return node.parent == nullptr ? SyntaxNodeKind::Unknown : node.parent->kind;
    }

    std::optional<FormatBreakToken> TokenForNode(const SyntaxNode& node) const {
        if (node.formatTokenMark != selectionMark_ || node.formatPrintToken == nullptr) {
            return std::nullopt;
        }
        return FormatBreakToken{.token = node.formatPrintToken, .spaceBefore = node.formatSpaceBefore};
    }

    std::span<FormatBreakNode*> StoreNodePointers(std::span<FormatBreakNode* const> nodes) {
        return model_.nodePointers.Append(nodes);
    }

    std::span<FormatBreakNode*> StoreNodePointers(const std::vector<FormatBreakNode*>& nodes) {
        return StoreNodePointers(std::span<FormatBreakNode* const>{nodes.data(), nodes.size()});
    }

    std::span<FormatBreakNode*> StoreNodePointers(std::initializer_list<FormatBreakNode*> nodes) {
        return StoreNodePointers(std::span<FormatBreakNode* const>{nodes.begin(), nodes.size()});
    }

    std::span<FormatBreakToken> StoreTokens(std::span<const FormatBreakToken> tokens) {
        return model_.tokens.Append(tokens);
    }

    std::span<FormatBreakToken> StoreTokens(std::initializer_list<FormatBreakToken> tokens) {
        return StoreTokens(std::span<const FormatBreakToken>{tokens.begin(), tokens.size()});
    }

    FormatBreakNode* MakeNode(FormatBreakNodeKind kind, int depth) {
        model_.nodes->emplace_back();
        FormatBreakNode& node = model_.nodes->back();
        node.id = nextId_++;
        node.kind = kind;
        node.structuralDepth = depth;
        return &node;
    }

    FormatBreakNode* BuildToken(const FormatBreakToken& token, int depth) {
        auto node = MakeNode(FormatBreakNodeKind::Token, depth);
        node->token = token;
        return node;
    }

    static const FormatBreakToken* TokenChild(const FormatBreakNode* node) {
        if (!node || node->kind != FormatBreakNodeKind::Token) {
            return nullptr;
        }
        return &node->token;
    }

    static bool IsStringTokenChild(const FormatBreakNode* node) {
        const FormatBreakToken* token = TokenChild(node);
        return token != nullptr && IsStringLike(FormatBreakTokenValue(*token));
    }

    static bool IsStandaloneCommentItem(const FormatBreakNode& node, size_t index) {
        if (index >= node.items.size()) {
            return false;
        }
        const FormatBreakToken* token = TokenChild(node.items[index].node);
        return token != nullptr && FormatBreakTokenKind(*token) == PrintTokenKind::Comment;
    }

    static bool ShouldPreservePendingBlankLine(const FormatBreakNode& list, bool pendingBlankLine, bool beforeComment) {
        return pendingBlankLine && (beforeComment || IsStandaloneCommentItem(list, list.items.size() - 1));
    }

    void AppendListItem(FormatBreakNode& list, FormatBreakNode* item, bool blankLineBefore) {
        list.items.push_back(FormatBreakListItem{.node = item, .blankLineBefore = blankLineBefore});
    }

    void AppendDelimitedItem(
        FormatBreakNode& delimited,
        SyntaxChildList& itemChildren,
        const FormatBreakToken& open,
        int depth,
        bool blankLineBefore = false
    ) {
        if (itemChildren.empty()) {
            return;
        }
        FormatBreakNode* item = BuildSequenceFromPointers(itemChildren, depth + 1);
        const bool virtualDelimiter = FormatBreakTokenValue(open).parentKind == SyntaxNodeKind::Unknown;
        if (
            delimited.delimiterKind == FormatBreakDelimiterKind::Paren &&
            item &&
            item->kind == FormatBreakNodeKind::Chain &&
            item->chainKind != FormatBreakChainKind::Ternary && (
                virtualDelimiter || (IsFlatParenthesizedChain(*item) && (
                    UsesFlatLogicalContinuation(open, *item) || UsesFlatNonCallParenthesisContinuation(open)
                ))
            )
        ) {
            item->flatSplitIndent = true;
        }
        AppendListItem(delimited, item, blankLineBefore);
        itemChildren.clear();
    }

    void AppendEmptyDelimitedItem(FormatBreakNode& delimited, int depth, bool blankLineBefore = false) {
        AppendListItem(delimited, MakeNode(FormatBreakNodeKind::Sequence, depth + 1), blankLineBefore);
    }

    void AppendStandaloneCommentItem(
        FormatBreakNode& list,
        const FormatBreakToken& comment,
        int depth,
        bool blankLineBefore = false
    ) {
        AppendListItem(list, BuildToken(comment, depth + 1), blankLineBefore);
    }

    void AttachTrailingCommentToPreviousItem(FormatBreakNode& list, const FormatBreakToken& comment) {
        if (list.items.empty()) {
            return;
        }
        list.items.back().trailingComment = comment;
    }

    static bool AttachSeparatorToPreviousItem(FormatBreakNode& list, const FormatBreakToken& separator) {
        if (list.items.empty()) {
            return false;
        }
        list.items.back().separator = separator;
        return true;
    }

    static bool ContainsBodyHeader(const FormatBreakNode& node) {
        if (node.kind == FormatBreakNodeKind::BodyHeader) {
            return true;
        }
        for (const FormatBreakNode* child : node.children) {
            if (child && ContainsBodyHeader(*child)) {
                return true;
            }
        }
        for (const FormatBreakListItem& item : node.items) {
            if (item.node && ContainsBodyHeader(*item.node)) {
                return true;
            }
        }
        for (const FormatBreakNode* operand : node.operands) {
            if (operand && ContainsBodyHeader(*operand)) {
                return true;
            }
        }
        return false;
    }

    static void MarkBodyHeaderSplitAtParentIndentWhenLineStarts(FormatBreakNode& node) {
        if (node.kind == FormatBreakNodeKind::BodyHeader) {
            node.bodyHeaderSplitAtParentIndentWhenLineStarts = true;
        }
        for (FormatBreakNode* child : node.children) {
            if (child) {
                MarkBodyHeaderSplitAtParentIndentWhenLineStarts(*child);
            }
        }
        for (FormatBreakListItem& item : node.items) {
            if (item.node) {
                MarkBodyHeaderSplitAtParentIndentWhenLineStarts(*item.node);
            }
        }
        for (FormatBreakNode* operand : node.operands) {
            if (operand) {
                MarkBodyHeaderSplitAtParentIndentWhenLineStarts(*operand);
            }
        }
    }

    static bool ContainsForceSplitAdjacentStrings(const FormatBreakNode& node) {
        if (node.kind == FormatBreakNodeKind::AdjacentStrings && node.forceSplit) {
            return true;
        }
        for (const FormatBreakNode* child : node.children) {
            if (child && ContainsForceSplitAdjacentStrings(*child)) {
                return true;
            }
        }
        for (const FormatBreakListItem& item : node.items) {
            if (item.node && ContainsForceSplitAdjacentStrings(*item.node)) {
                return true;
            }
        }
        for (const FormatBreakNode* operand : node.operands) {
            if (operand && ContainsForceSplitAdjacentStrings(*operand)) {
                return true;
            }
        }
        return false;
    }

    static bool ContainsDelimitedNode(const FormatBreakNode& node) {
        if (node.kind == FormatBreakNodeKind::Delimited) {
            return true;
        }
        for (const FormatBreakNode* child : node.children) {
            if (child && ContainsDelimitedNode(*child)) {
                return true;
            }
        }
        for (const FormatBreakListItem& item : node.items) {
            if (item.node && ContainsDelimitedNode(*item.node)) {
                return true;
            }
        }
        for (const FormatBreakNode* operand : node.operands) {
            if (operand && ContainsDelimitedNode(*operand)) {
                return true;
            }
        }
        return false;
    }

    static bool IsDirectForceSplitAdjacentStringsInitializer(const FormatBreakNode& node) {
        return ContainsForceSplitAdjacentStrings(node) && !ContainsDelimitedNode(node);
    }

    static void MarkForceSplitAdjacentStringsFlat(FormatBreakNode& node) {
        if (node.kind == FormatBreakNodeKind::AdjacentStrings && node.forceSplit) {
            node.flatSplitIndent = true;
        }
        for (FormatBreakNode* child : node.children) {
            if (child) {
                MarkForceSplitAdjacentStringsFlat(*child);
            }
        }
        for (FormatBreakListItem& item : node.items) {
            if (item.node) {
                MarkForceSplitAdjacentStringsFlat(*item.node);
            }
        }
        for (FormatBreakNode* operand : node.operands) {
            if (operand) {
                MarkForceSplitAdjacentStringsFlat(*operand);
            }
        }
    }

    void GroupAdjacentStrings(FormatBreakNode& sequence, int depth) {
        std::vector<FormatBreakNode*> grouped;
        grouped.reserve(sequence.children.size());
        for (size_t index = 0; index < sequence.children.size();) {
            if (!IsStringTokenChild(sequence.children[index])) {
                grouped.push_back(sequence.children[index]);
                ++index;
                continue;
            }

            const size_t begin = index;
            while (index < sequence.children.size() && IsStringTokenChild(sequence.children[index])) {
                ++index;
            }
            if (index - begin == 1) {
                grouped.push_back(sequence.children[begin]);
                continue;
            }

            auto strings = MakeNode(FormatBreakNodeKind::AdjacentStrings, depth + 1);
            std::vector<FormatBreakNode*> operands;
            operands.reserve(index - begin);
            for (size_t cursor = begin; cursor < index; ++cursor) {
                if (cursor + 1 < index && ForcesStringBoundarySplit(*TokenChild(sequence.children[cursor]))) {
                    strings->forceSplit = true;
                }
                operands.push_back(sequence.children[cursor]);
            }
            strings->operands = StoreNodePointers(operands);
            grouped.push_back(strings);
        }
        sequence.children = StoreNodePointers(grouped);
    }

    FormatBreakNode* BuildSyntaxNode(const SyntaxNode& node, int depth) {
        if (!ContainsSelected(node)) {
            return nullptr;
        }
        if (std::optional<FormatBreakToken> token = TokenForNode(node)) {
            return BuildToken(*token, depth);
        }
        if (!SyntaxNodeKindHasClass(node.kind, TokenClass::Tree)) {
            return nullptr;
        }
        if (IsDeclarationNodeKind(node.kind)) {
            if (auto declaration = BuildDirectInitializedDeclaration(node, depth)) {
                return declaration;
            }
        }
        if (node.kind == SyntaxNodeKind::Declaration) {
            if (auto declaration = BuildAssignedDeclaration(node, depth)) {
                return declaration;
            }
        }
        if (node.kind == SyntaxNodeKind::Declaration || node.kind == SyntaxNodeKind::FunctionDefinition) {
            if (auto signature = BuildFunctionSignature(node, depth)) {
                return signature;
            }
        }
        if (node.kind == SyntaxNodeKind::LambdaExpression) {
            if (auto header = BuildBodyHeader(node, depth)) {
                return header;
            }
        }
        if (
            node.kind == SyntaxNodeKind::BinaryExpression ||
            node.kind == SyntaxNodeKind::AssignmentExpression ||
            node.kind == SyntaxNodeKind::InitDeclarator ||
            node.kind == SyntaxNodeKind::FieldDeclaration ||
            node.kind == SyntaxNodeKind::AliasDeclaration ||
            node.kind == SyntaxNodeKind::CommaExpression ||
            node.kind == SyntaxNodeKind::ConditionalExpression
        ) {
            if (auto expression = BuildOperatorExpression(node, depth)) {
                return expression;
            }
        }
        if (IsPrefixListNodeKind(node.kind)) {
            if (auto list = BuildPrefixList(node, depth)) {
                return list;
            }
        }
        return BuildSequenceFromChildren(node.children, 0, node.children.size(), depth);
    }

    FormatBreakNode* BuildBodyHeader(const SyntaxNode& node, int depth) {
        std::optional<size_t> bodyIndex;
        for (size_t index = 0; index < node.children.size(); ++index) {
            if (node.children[index] && node.children[index]->kind == SyntaxNodeKind::CompoundStatement) {
                bodyIndex = index;
                break;
            }
        }
        if (!bodyIndex || *bodyIndex == 0 || !ContainsSelected(*node.children[*bodyIndex])) {
            return nullptr;
        }
        FormatBreakNode* header = BuildSequenceFromChildren(node.children, 0, *bodyIndex, depth + 1);
        FormatBreakNode* body = BuildSequenceFromChildren(node.children, *bodyIndex, node.children.size(), depth + 1);
        if (!header || !body) {
            return nullptr;
        }

        auto result = MakeNode(FormatBreakNodeKind::BodyHeader, depth);
        result->bodyHeaderSingleStatementBody = IsSingleStatementLambdaBody(*node.children[*bodyIndex]);
        const SyntaxNode* parent = node.parent;
        result->bodyHeaderSplitAtParentIndentWhenLineStarts = parent != nullptr &&
            (parent->kind == SyntaxNodeKind::AssignmentExpression || parent->kind == SyntaxNodeKind::InitDeclarator);
        result->children = StoreNodePointers({header, body});
        return result;
    }

    static bool IsSingleStatementLambdaBody(const SyntaxNode& node) {
        size_t statementCount = 0;
        for (const SyntaxNode* child : node.children) {
            if (
                child == nullptr ||
                child->kind == SyntaxNodeKind::BlankLine ||
                child->kind == SyntaxNodeKind::Comment ||
                child->kind == SyntaxNodeKind::TrailingComment ||
                SyntaxNodeKindHasClass(child->kind, TokenClass::Known)
            ) {
                continue;
            }
            ++statementCount;
            if (statementCount > 1) {
                return false;
            }
        }
        return statementCount == 1;
    }

    FormatBreakNode* BuildFunctionSignature(const SyntaxNode& node, int depth) {
        std::optional<size_t> declaratorIndex;
        for (size_t index = 0; index < node.children.size(); ++index) {
            if (node.children[index] && node.children[index]->kind == SyntaxNodeKind::FunctionDeclarator) {
                declaratorIndex = index;
                break;
            }
        }
        if (!declaratorIndex || *declaratorIndex == 0) {
            return nullptr;
        }
        if (!ContainsSelected(*node.children[*declaratorIndex])) {
            return nullptr;
        }

        FormatBreakNode* returnType = BuildSequenceFromChildren(node.children, 0, *declaratorIndex, depth + 1);
        FormatBreakNode* declarator =
            BuildSequenceFromChildren(node.children, *declaratorIndex, *declaratorIndex + 1, depth + 1);
        if (!returnType || !declarator) {
            return nullptr;
        }

        auto signature = MakeNode(FormatBreakNodeKind::FunctionSignature, depth);
        signature->functionSignatureHasBody = node.kind == SyntaxNodeKind::FunctionDefinition;
        std::array<FormatBreakNode*, 3> signatureChildren{returnType, declarator, nullptr};
        size_t signatureChildCount = 2;
        if (*declaratorIndex + 1 < node.children.size()) {
            FormatBreakNode* tail =
                BuildSequenceFromChildren(node.children, *declaratorIndex + 1, node.children.size(), depth + 1);
            if (tail) {
                signatureChildren[signatureChildCount++] = tail;
            }
        }
        signature->children =
            StoreNodePointers(std::span<FormatBreakNode* const>{signatureChildren.data(), signatureChildCount});
        return signature;
    }

    static bool IsDirectInitializedDeclarator(const SyntaxNode& node) {
        if (node.kind != SyntaxNodeKind::InitDeclarator) {
            return false;
        }
        bool hasAssignment = false;
        bool hasInitializer = false;
        for (const SyntaxNode* child : node.children) {
            if (!child) {
                continue;
            }
            if (
                SyntaxNodeKindHasClass(child->kind, TokenClass::Known) &&
                SyntaxNodeKindHasClass(child->kind, TokenClass::AssignmentOperator)
            ) {
                hasAssignment = true;
            }
            if (child->kind == SyntaxNodeKind::InitializerList) {
                hasInitializer = true;
            }
            if (child->kind == SyntaxNodeKind::ArgumentList) {
                hasInitializer = true;
            }
        }
        return hasInitializer && !hasAssignment;
    }

    static bool IsAssignmentOperatorNode(const SyntaxNode& node) {
        return SyntaxNodeKindHasClass(node.kind, TokenClass::Known) &&
            SyntaxNodeKindHasClass(node.kind, TokenClass::AssignmentOperator);
    }

    std::optional<size_t> DirectInitializedDeclaratorIndex(const SyntaxNode& node) const {
        bool hasAssignment = false;
        for (size_t index = 0; index < node.children.size(); ++index) {
            const SyntaxNode* child = node.children[index];
            if (child == nullptr) {
                continue;
            }
            hasAssignment = hasAssignment || IsAssignmentOperatorNode(*child);
            if (IsDirectInitializedDeclarator(*child)) {
                return index;
            }
            // Some direct initializer forms attach the initializer list as a declaration child after the declarator.
            if (child->kind == SyntaxNodeKind::InitializerList && index > 0 && !hasAssignment) {
                return index - 1;
            }
        }
        return std::nullopt;
    }

    FormatBreakNode* BuildDirectInitializedDeclaration(const SyntaxNode& node, int depth) {
        const std::optional<size_t> declaratorIndex = DirectInitializedDeclaratorIndex(node);
        if (!declaratorIndex || *declaratorIndex == 0) {
            return nullptr;
        }

        auto chain = MakeNode(FormatBreakNodeKind::Chain, depth);
        FormatBreakNode* left = BuildSequenceFromChildren(node.children, 0, *declaratorIndex, depth + 1);
        FormatBreakNode* right =
            BuildSequenceFromChildren(node.children, *declaratorIndex, node.children.size(), depth + 1);
        chain->operands = StoreNodePointers({left, right});
        chain->operators = StoreTokens({{}});
        return chain;
    }

    FormatBreakNode* BuildAssignedDeclaration(const SyntaxNode& node, int depth) {
        std::optional<size_t> declaratorIndex;
        std::optional<size_t> operatorIndex;
        for (size_t index = 0; index < node.children.size(); ++index) {
            if (!node.children[index] || node.children[index]->kind != SyntaxNodeKind::InitDeclarator) {
                continue;
            }
            if (declaratorIndex) {
                return nullptr;
            }
            const std::optional<size_t> directOperator = DirectOperatorIndex(*node.children[index]);
            if (!directOperator || !node.children[index]->children[*directOperator]) {
                return nullptr;
            }
            declaratorIndex = index;
            operatorIndex = *directOperator;
        }
        if (!declaratorIndex || !operatorIndex) {
            return nullptr;
        }

        const SyntaxNode& declarator = *node.children[*declaratorIndex];
        const std::optional<FormatBreakToken> op = TokenForNode(*declarator.children[*operatorIndex]);
        if (!op || !IsAssignmentOperatorForNode(*op)) {
            return nullptr;
        }

        SyntaxChildList leftChildren;
        leftChildren.reserve(*declaratorIndex + *operatorIndex);
        for (size_t index = 0; index < *declaratorIndex; ++index) {
            leftChildren.push_back(node.children[index]);
        }
        for (size_t index = 0; index < *operatorIndex; ++index) {
            leftChildren.push_back(declarator.children[index]);
        }

        auto chain = MakeNode(FormatBreakNodeKind::Chain, depth);
        FormatBreakNode* left = BuildSequenceFromPointers(leftChildren, depth + 1);
        FormatBreakNode* right =
            BuildSequenceFromChildren(declarator.children, *operatorIndex + 1, declarator.children.size(), depth + 1);
        chain->operands = StoreNodePointers({left, right});
        chain->operators = StoreTokens({*op});
        chain->splitTrailingBodyHeaderAtParentIndent = right != nullptr && ContainsBodyHeader(*right);
        if (right != nullptr && chain->splitTrailingBodyHeaderAtParentIndent) {
            MarkBodyHeaderSplitAtParentIndentWhenLineStarts(*right);
        }
        if (right != nullptr && IsDirectForceSplitAdjacentStringsInitializer(*right)) {
            MarkForceSplitAdjacentStringsFlat(*right);
        }

        std::vector<FormatBreakNode*> tailChildren;
        tailChildren.reserve(node.children.size() - *declaratorIndex - 1);
        for (size_t index = *declaratorIndex + 1; index < node.children.size(); ++index) {
            if (node.children[index] && ContainsSelected(*node.children[index])) {
                if (FormatBreakNode* built = BuildSyntaxNode(*node.children[index], depth + 1)) {
                    tailChildren.push_back(built);
                }
            }
        }
        if (tailChildren.empty()) {
            return chain;
        }
        auto sequence = MakeNode(FormatBreakNodeKind::Sequence, depth);
        std::vector<FormatBreakNode*> sequenceChildren;
        sequenceChildren.reserve(tailChildren.size() + 1);
        sequenceChildren.push_back(chain);
        sequenceChildren.insert(sequenceChildren.end(), tailChildren.begin(), tailChildren.end());
        sequence->children = StoreNodePointers(sequenceChildren);
        return sequence;
    }

    FormatBreakNode* BuildSequenceFromPointers(const SyntaxChildList& children, int depth) {
        std::vector<FormatBreakNode*> builtChildren;
        builtChildren.reserve(children.size());
        for (const SyntaxNode* child : children) {
            if (child == nullptr) {
                continue;
            }
            if (FormatBreakNode* built = BuildSyntaxNode(*child, depth + 1)) {
                builtChildren.push_back(built);
            }
        }
        if (builtChildren.size() == 1) {
            return builtChildren.front();
        }
        auto sequence = MakeNode(FormatBreakNodeKind::Sequence, depth);
        sequence->children = StoreNodePointers(builtChildren);
        GroupAdjacentStrings(*sequence, depth);
        return sequence;
    }

    FormatBreakNode* BuildSequenceFromChildren(
        const std::vector<SyntaxNode*>& children,
        size_t begin,
        size_t end,
        int depth
    ) {
        std::vector<FormatBreakNode*> builtChildren;
        builtChildren.reserve(end - begin);
        for (size_t index = begin; index < end;) {
            if (!children[index] || !ContainsSelected(*children[index])) {
                ++index;
                continue;
            }
            size_t afterDelimited = index;
            if (FormatBreakNode* delimited = BuildDirectDelimited(children, index, end, depth + 1, afterDelimited)) {
                builtChildren.push_back(delimited);
                index = afterDelimited;
                continue;
            }
            if (FormatBreakNode* built = BuildSyntaxNode(*children[index], depth + 1)) {
                builtChildren.push_back(built);
            }
            ++index;
        }
        if (builtChildren.size() == 1) {
            return builtChildren.front();
        }
        auto sequence = MakeNode(FormatBreakNodeKind::Sequence, depth);
        sequence->children = StoreNodePointers(builtChildren);
        GroupAdjacentStrings(*sequence, depth);
        return sequence;
    }

    std::optional<size_t> DirectOperatorIndex(const SyntaxNode& node) const {
        for (size_t index = 0; index < node.children.size(); ++index) {
            if (!node.children[index]) {
                continue;
            }
            const std::optional<FormatBreakToken> token = TokenForNode(*node.children[index]);
            if (!token) {
                continue;
            }
            if (
                (node.kind == SyntaxNodeKind::BinaryExpression && IsBinaryOperatorForNode(*token)) ||
                (node.kind == SyntaxNodeKind::CommaExpression && IsCommaOperatorForNode(*token)) || (
                    (
                        node.kind == SyntaxNodeKind::AssignmentExpression ||
                        node.kind == SyntaxNodeKind::InitDeclarator ||
                        node.kind == SyntaxNodeKind::FieldDeclaration ||
                        node.kind == SyntaxNodeKind::AliasDeclaration
                    ) && IsAssignmentOperatorForNode(*token)
                )
            ) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::optional<std::pair<size_t, size_t>> DirectConditionalOperatorIndices(const SyntaxNode& node) const {
        std::optional<size_t> question;
        std::optional<size_t> colon;
        for (size_t index = 0; index < node.children.size(); ++index) {
            if (!node.children[index]) {
                continue;
            }
            const std::optional<FormatBreakToken> token = TokenForNode(*node.children[index]);
            if (!token || !IsConditionalOperatorForNode(*token)) {
                continue;
            }
            if (FormatBreakTokenSyntaxKind(*token) == SyntaxNodeKind::Question) {
                question = index;
            } else if (FormatBreakTokenSyntaxKind(*token) == SyntaxNodeKind::Colon) {
                colon = index;
            }
        }
        if (!question || !colon || *question >= *colon) {
            return std::nullopt;
        }
        return std::pair<size_t, size_t>{*question, *colon};
    }

    bool HasSameDirectBinaryOperator(const SyntaxNode& node, SyntaxNodeKind op) const {
        if (node.kind != SyntaxNodeKind::BinaryExpression) {
            return false;
        }
        const std::optional<size_t> opIndex = DirectOperatorIndex(node);
        if (!opIndex || !node.children[*opIndex]) {
            return false;
        }
        const std::optional<FormatBreakToken> token = TokenForNode(*node.children[*opIndex]);
        return token && FormatBreakTokenSyntaxKind(*token) == op;
    }

    void AppendBinaryChainOperand(
        std::vector<FormatBreakNode*>& operands,
        std::vector<FormatBreakToken>& operators,
        const std::vector<SyntaxNode*>& children,
        size_t begin,
        size_t end,
        SyntaxNodeKind op,
        int depth
    ) {
        if (end == begin + 1 && children[begin] && IsChainOperatorKind(op) && HasSameDirectBinaryOperator(
            *children[begin],
            op
        )) {
            AppendBinaryChain(*children[begin], op, operands, operators, depth);
            return;
        }
        operands.push_back(BuildSequenceFromChildren(children, begin, end, depth + 1));
    }

    void AppendBinaryChain(
        const SyntaxNode& node,
        SyntaxNodeKind op,
        std::vector<FormatBreakNode*>& operands,
        std::vector<FormatBreakToken>& operators,
        int depth
    ) {
        const std::optional<size_t> opIndex = DirectOperatorIndex(node);
        if (!opIndex || !node.children[*opIndex]) {
            operands.push_back(BuildSyntaxNode(node, depth + 1));
            return;
        }

        AppendBinaryChainOperand(operands, operators, node.children, 0, *opIndex, op, depth);
        if (std::optional<FormatBreakToken> token = TokenForNode(*node.children[*opIndex])) {
            operators.push_back(*token);
        }
        AppendBinaryChainOperand(operands, operators, node.children, *opIndex + 1, node.children.size(), op, depth);
    }

    FormatBreakNode* BuildBinaryOrAssignmentExpression(const SyntaxNode& node, int depth) {
        const std::optional<size_t> opIndex = DirectOperatorIndex(node);
        if (!opIndex || !node.children[*opIndex]) {
            return nullptr;
        }
        const std::optional<FormatBreakToken> token = TokenForNode(*node.children[*opIndex]);
        if (!token) {
            return nullptr;
        }

        auto chain = MakeNode(FormatBreakNodeKind::Chain, depth);
        const SyntaxNodeKind operatorKind = FormatBreakTokenSyntaxKind(*token);
        chain->chainKind = (
            operatorKind == SyntaxNodeKind::LessLess || operatorKind == SyntaxNodeKind::GreaterGreater
        ) ? FormatBreakChainKind::StreamBeforeOperator : FormatBreakChainKind::AfterOperator;
        if (node.kind == SyntaxNodeKind::BinaryExpression && IsChainOperatorKind(operatorKind)) {
            std::vector<FormatBreakNode*> operands;
            std::vector<FormatBreakToken> operators;
            operands.reserve(2);
            operators.reserve(1);
            AppendBinaryChain(node, operatorKind, operands, operators, depth);
            chain->operands = StoreNodePointers(operands);
            chain->operators = StoreTokens(operators);
            return chain;
        }

        FormatBreakNode* left = BuildSequenceFromChildren(node.children, 0, *opIndex, depth + 1);
        FormatBreakNode* right =
            BuildSequenceFromChildren(node.children, *opIndex + 1, node.children.size(), depth + 1);
        chain->operands = StoreNodePointers({left, right});
        chain->operators = StoreTokens({*token});
        chain->splitTrailingBodyHeaderAtParentIndent =
            IsAssignmentOperatorForNode(*token) && right != nullptr && ContainsBodyHeader(*right);
        if (right != nullptr && chain->splitTrailingBodyHeaderAtParentIndent) {
            MarkBodyHeaderSplitAtParentIndentWhenLineStarts(*right);
        }
        if (IsAssignmentOperatorForNode(*token) && right != nullptr && IsDirectForceSplitAdjacentStringsInitializer(
            *right
        )) {
            MarkForceSplitAdjacentStringsFlat(*right);
        }
        return chain;
    }

    void AppendConditionalChain(
        const SyntaxNode& node,
        std::vector<FormatBreakNode*>& operands,
        std::vector<FormatBreakToken>& operators,
        int depth
    ) {
        const std::optional<std::pair<size_t, size_t>> operatorIndices = DirectConditionalOperatorIndices(node);
        if (!operatorIndices) {
            operands.push_back(BuildSyntaxNode(node, depth + 1));
            return;
        }
        const size_t question = operatorIndices->first;
        const size_t colon = operatorIndices->second;
        operands.push_back(BuildSequenceFromChildren(node.children, 0, question, depth + 1));
        if (std::optional<FormatBreakToken> token = TokenForNode(*node.children[question])) {
            operators.push_back(*token);
        }
        AppendConditionalBranch(node.children, question + 1, colon, operands, operators, depth);
        if (std::optional<FormatBreakToken> token = TokenForNode(*node.children[colon])) {
            operators.push_back(*token);
        }
        AppendConditionalBranch(node.children, colon + 1, node.children.size(), operands, operators, depth);
    }

    void AppendConditionalBranch(
        const std::vector<SyntaxNode*>& children,
        size_t begin,
        size_t end,
        std::vector<FormatBreakNode*>& operands,
        std::vector<FormatBreakToken>& operators,
        int depth
    ) {
        if (
            end == begin + 1 &&
            begin < children.size() &&
            children[begin] != nullptr &&
            children[begin]->kind == SyntaxNodeKind::ConditionalExpression
        ) {
            AppendConditionalChain(*children[begin], operands, operators, depth);
        } else {
            operands.push_back(BuildSequenceFromChildren(children, begin, end, depth + 1));
        }
    }

    FormatBreakNode* BuildConditionalExpression(const SyntaxNode& node, int depth) {
        if (!DirectConditionalOperatorIndices(node)) {
            return nullptr;
        }
        auto chain = MakeNode(FormatBreakNodeKind::Chain, depth);
        chain->chainKind = FormatBreakChainKind::Ternary;
        std::vector<FormatBreakNode*> operands;
        std::vector<FormatBreakToken> operators;
        operands.reserve(3);
        operators.reserve(2);
        AppendConditionalChain(node, operands, operators, depth);
        chain->operands = StoreNodePointers(operands);
        chain->operators = StoreTokens(operators);
        return chain;
    }

    FormatBreakNode* BuildOperatorExpression(const SyntaxNode& node, int depth) {
        if (node.kind == SyntaxNodeKind::ConditionalExpression) {
            return BuildConditionalExpression(node, depth);
        }
        return BuildBinaryOrAssignmentExpression(node, depth);
    }

    FormatBreakNode* BuildPrefixList(const SyntaxNode& node, int depth) {
        std::optional<size_t> prefixIndex;
        for (size_t index = 0; index < node.children.size(); ++index) {
            if (!node.children[index]) {
                continue;
            }
            const std::optional<FormatBreakToken> token = TokenForNode(*node.children[index]);
            if (
                token &&
                FormatBreakTokenKind(*token) == PrintTokenKind::Known &&
                FormatBreakTokenSyntaxKind(*token) == SyntaxNodeKind::Colon
            ) {
                prefixIndex = index;
                break;
            }
        }
        if (!prefixIndex || !node.children[*prefixIndex]) {
            return nullptr;
        }
        const std::optional<FormatBreakToken> prefix = TokenForNode(*node.children[*prefixIndex]);
        if (!prefix) {
            return nullptr;
        }

        auto list = MakeNode(FormatBreakNodeKind::PrefixList, depth);
        list->children = StoreNodePointers({BuildToken(*prefix, depth + 1)});

        SyntaxChildList itemChildren;
        bool pendingBlankLine = false;
        for (size_t index = *prefixIndex + 1; index < node.children.size(); ++index) {
            const SyntaxNode* child = node.children[index];
            if (child == nullptr) {
                continue;
            }
            if (IsListForceSplitMarker(child->kind)) {
                list->forceSplit = true;
                if (child->kind == SyntaxNodeKind::BlankLine) {
                    pendingBlankLine = true;
                    continue;
                }
                const std::optional<FormatBreakToken> comment = TokenForNode(*child);
                if (!comment) {
                    continue;
                }
                if (child->kind == SyntaxNodeKind::TrailingComment) {
                    if (!itemChildren.empty()) {
                        const bool blankLineBefore = ShouldPreservePendingBlankLine(*list, pendingBlankLine, false);
                        AppendListItem(*list, BuildSequenceFromPointers(itemChildren, depth + 1), blankLineBefore);
                        itemChildren.clear();
                    }
                    AttachTrailingCommentToPreviousItem(*list, *comment);
                } else {
                    if (!itemChildren.empty()) {
                        const bool blankLineBefore = ShouldPreservePendingBlankLine(*list, pendingBlankLine, false);
                        AppendListItem(*list, BuildSequenceFromPointers(itemChildren, depth + 1), blankLineBefore);
                        itemChildren.clear();
                        pendingBlankLine = false;
                    }
                    const bool blankLineBefore = ShouldPreservePendingBlankLine(*list, pendingBlankLine, true);
                    AppendStandaloneCommentItem(*list, *comment, depth + 1, blankLineBefore);
                }
                pendingBlankLine = false;
                continue;
            }
            if (!ContainsSelected(*child)) {
                continue;
            }
            const std::optional<FormatBreakToken> token = TokenForNode(*child);
            if (
                token &&
                FormatBreakTokenKind(*token) == PrintTokenKind::Known &&
                FormatBreakTokenSyntaxKind(*token) == SyntaxNodeKind::Comma
            ) {
                if (!itemChildren.empty()) {
                    const bool blankLineBefore = ShouldPreservePendingBlankLine(*list, pendingBlankLine, false);
                    AppendListItem(*list, BuildSequenceFromPointers(itemChildren, depth + 1), blankLineBefore);
                    itemChildren.clear();
                    pendingBlankLine = false;
                }
                AttachSeparatorToPreviousItem(*list, *token);
                continue;
            }
            itemChildren.push_back(child);
        }
        if (!itemChildren.empty()) {
            const bool blankLineBefore = ShouldPreservePendingBlankLine(*list, pendingBlankLine, false);
            AppendListItem(*list, BuildSequenceFromPointers(itemChildren, depth + 1), blankLineBefore);
        }
        return list->items.empty() ? nullptr : list;
    }

    std::optional<std::pair<size_t, FormatBreakDelimiterKind>> FindDirectClose(
        const std::vector<SyntaxNode*>& children,
        size_t openIndex,
        size_t end,
        FormatBreakDelimiterKind delimiter
    ) const {
        for (size_t index = openIndex + 1; index < end; ++index) {
            if (!children[index]) {
                continue;
            }
            const std::optional<FormatBreakToken> token = TokenForNode(*children[index]);
            if (token && ClosingDelimiter(*token) == delimiter) {
                return std::pair<size_t, FormatBreakDelimiterKind>{index, delimiter};
            }
        }
        return std::nullopt;
    }

    FormatBreakNode* BuildDirectDelimited(
        const std::vector<SyntaxNode*>& children,
        size_t openIndex,
        size_t end,
        int depth,
        size_t& afterDelimited
    ) {
        afterDelimited = openIndex + 1;
        if (!children[openIndex]) {
            return nullptr;
        }
        const std::optional<FormatBreakToken> open = TokenForNode(*children[openIndex]);
        if (!open) {
            return nullptr;
        }
        const FormatBreakDelimiterKind delimiter = OpeningDelimiter(*open);
        if (delimiter == FormatBreakDelimiterKind::None) {
            return nullptr;
        }
        const std::optional<std::pair<size_t, FormatBreakDelimiterKind>> closeMatch =
            FindDirectClose(children, openIndex, end, delimiter);
        const bool hasVirtualClose = !closeMatch &&
            context_.virtualDelimiterOpen != nullptr &&
            context_.virtualDelimiterOpen == FormatBreakTokenValue(*open).node &&
            ClosingDelimiter(context_.virtualDelimiterClose) == delimiter;
        if (!closeMatch && !hasVirtualClose) {
            return nullptr;
        }
        const size_t closeIndex = closeMatch ? closeMatch->first : end;
        std::optional<FormatBreakToken> closeToken;
        const FormatBreakToken* close = &context_.virtualDelimiterClose;
        if (closeMatch) {
            closeToken = TokenForNode(*children[closeIndex]);
            if (!closeToken) {
                return nullptr;
            }
            close = &*closeToken;
        }

        auto delimited = MakeNode(FormatBreakNodeKind::Delimited, depth);
        delimited->delimiterKind = delimiter;
        delimited->children = StoreNodePointers({BuildToken(*open, depth + 1), BuildToken(*close, depth + 1)});

        SyntaxChildList itemChildren;
        bool pendingBlankLine = false;
        for (size_t index = openIndex + 1; index < closeIndex; ++index) {
            const SyntaxNode* child = children[index];
            if (child == nullptr) {
                continue;
            }
            if (IsListForceSplitMarker(child->kind)) {
                delimited->forceSplit = true;
                if (child->kind == SyntaxNodeKind::BlankLine) {
                    pendingBlankLine = true;
                    continue;
                }
                const std::optional<FormatBreakToken> comment = TokenForNode(*child);
                if (!comment) {
                    continue;
                }
                if (child->kind == SyntaxNodeKind::TrailingComment) {
                    if (!itemChildren.empty()) {
                        const bool blankLineBefore =
                            ShouldPreservePendingBlankLine(*delimited, pendingBlankLine, false);
                        AppendDelimitedItem(*delimited, itemChildren, *open, depth, blankLineBefore);
                    }
                    AttachTrailingCommentToPreviousItem(*delimited, *comment);
                } else {
                    if (!itemChildren.empty()) {
                        const bool blankLineBefore =
                            ShouldPreservePendingBlankLine(*delimited, pendingBlankLine, false);
                        AppendDelimitedItem(*delimited, itemChildren, *open, depth, blankLineBefore);
                        pendingBlankLine = false;
                    }
                    const bool blankLineBefore = ShouldPreservePendingBlankLine(*delimited, pendingBlankLine, true);
                    AppendStandaloneCommentItem(*delimited, *comment, depth, blankLineBefore);
                }
                pendingBlankLine = false;
                continue;
            }
            if (!ContainsSelected(*child)) {
                continue;
            }
            const std::optional<FormatBreakToken> token = TokenForNode(*child);
            if (token && IsSelectedSeparator(*token)) {
                if (
                    itemChildren.empty() &&
                    IsForHeaderDelimiter(*open) &&
                    FormatBreakTokenKind(*token) == PrintTokenKind::Known &&
                    FormatBreakTokenSyntaxKind(*token) == SyntaxNodeKind::Semicolon
                ) {
                    const bool blankLineBefore = ShouldPreservePendingBlankLine(*delimited, pendingBlankLine, false);
                    AppendEmptyDelimitedItem(*delimited, depth, blankLineBefore);
                } else {
                    const bool blankLineBefore = ShouldPreservePendingBlankLine(*delimited, pendingBlankLine, false);
                    AppendDelimitedItem(*delimited, itemChildren, *open, depth, blankLineBefore);
                }
                pendingBlankLine = false;
                AttachSeparatorToPreviousItem(*delimited, *token);
                continue;
            }
            itemChildren.push_back(child);
            if (
                (
                    IsControlHeaderKind(FormatBreakTokenValue(*open).parentKind) ||
                    IsControlHeaderKind(FormatBreakTokenValue(*open).grandParentKind)
                ) &&
                itemChildren.size() == 1 &&
                (child->kind == SyntaxNodeKind::Declaration || child->kind == SyntaxNodeKind::InitStatement)
            ) {
                const bool blankLineBefore = ShouldPreservePendingBlankLine(*delimited, pendingBlankLine, false);
                AppendDelimitedItem(*delimited, itemChildren, *open, depth, blankLineBefore);
                pendingBlankLine = false;
            }
        }
        const bool blankLineBefore = ShouldPreservePendingBlankLine(*delimited, pendingBlankLine, false);
        AppendDelimitedItem(*delimited, itemChildren, *open, depth, blankLineBefore);
        if (
            itemChildren.empty() &&
            IsForHeaderDelimiter(*open) &&
            !delimited->items.empty() &&
            FormatBreakTokenKind(delimited->items.back().separator) == PrintTokenKind::Known
        ) {
            AppendEmptyDelimitedItem(*delimited, depth);
        }
        delimited->forceSplit = delimited->forceSplit || (hasVirtualClose && context_.forceSplitVirtualDelimiter);
        afterDelimited = hasVirtualClose ? end : closeIndex + 1;
        return delimited;
    }
};

}  // namespace

FormatBreakModel BuildFormatBreakModel(std::span<const PrintToken> tokens) {
    return BuildFormatBreakModel(tokens, {});
}

FormatBreakModel BuildFormatBreakModel(std::span<const PrintToken> tokens, const FormatBreakModelContext& context) {
    return BreakModelBuilder(tokens, context).Build();
}
