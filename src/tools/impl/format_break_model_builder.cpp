#include "tools/impl/format_break_model_builder.h"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

using SyntaxChildList = std::vector<const SyntaxNode*>;

bool IsTemplateAngleToken(const FormatBreakToken& token) {
    return token.token.parentKind == SyntaxTreeKind::TemplateArgumentList ||
        token.token.parentKind == SyntaxTreeKind::TemplateParameterList;
}

bool HasSyntaxAncestor(const FormatBreakToken& token, SyntaxTreeKind kind) {
    return std::any_of(
        token.token.syntaxPath.begin(),
        token.token.syntaxPath.end(),
        [kind](const SyntaxNode* node) {
            return node != nullptr && node->kind == SyntaxNodeKind::Tree && node->treeKind == kind;
        }
    );
}

FormatBreakDelimiterKind OpeningDelimiter(const FormatBreakToken& token) {
    if (token.token.kind != PrintTokenKind::Known) {
        return FormatBreakDelimiterKind::None;
    }
    if (
        token.token.parentKind == SyntaxTreeKind::MsCallModifier ||
        token.token.grandParentKind == SyntaxTreeKind::MsCallModifier ||
        token.token.parentKind == SyntaxTreeKind::MsDeclspecModifier ||
        token.token.grandParentKind == SyntaxTreeKind::MsDeclspecModifier ||
        HasSyntaxAncestor(token, SyntaxTreeKind::MsCallModifier) ||
        HasSyntaxAncestor(token, SyntaxTreeKind::MsDeclspecModifier)
    ) {
        return FormatBreakDelimiterKind::None;
    }
    switch (token.token.known) {
        case KnownToken::LeftParen:
            return FormatBreakDelimiterKind::Paren;
        case KnownToken::LeftBracket:
            return FormatBreakDelimiterKind::Bracket;
        case KnownToken::LeftBrace:
            return FormatBreakDelimiterKind::Brace;
        case KnownToken::Less:
            return IsTemplateAngleToken(token) ? FormatBreakDelimiterKind::Angle : FormatBreakDelimiterKind::None;
        default:
            return FormatBreakDelimiterKind::None;
    }
}

FormatBreakDelimiterKind ClosingDelimiter(const FormatBreakToken& token) {
    if (token.token.kind != PrintTokenKind::Known) {
        return FormatBreakDelimiterKind::None;
    }
    switch (token.token.known) {
        case KnownToken::RightParen:
            return FormatBreakDelimiterKind::Paren;
        case KnownToken::RightBracket:
            return FormatBreakDelimiterKind::Bracket;
        case KnownToken::RightBrace:
            return FormatBreakDelimiterKind::Brace;
        case KnownToken::Greater:
            return IsTemplateAngleToken(token) ? FormatBreakDelimiterKind::Angle : FormatBreakDelimiterKind::None;
        default:
            return FormatBreakDelimiterKind::None;
    }
}

bool IsSelectedSeparator(const FormatBreakToken& token) {
    return token.token.kind == PrintTokenKind::Known &&
        (token.token.known == KnownToken::Comma || token.token.known == KnownToken::Semicolon);
}

bool IsBinaryOperatorForNode(const FormatBreakToken& token) {
    return token.token.kind == PrintTokenKind::Known &&
        KnownTokenHasClass(token.token.known, TokenClass::BinaryOperator) &&
        token.token.parentKind == SyntaxTreeKind::BinaryExpression;
}

bool IsAssignmentOperatorForNode(const FormatBreakToken& token) {
    return token.token.kind == PrintTokenKind::Known &&
        KnownTokenHasClass(token.token.known, TokenClass::AssignmentOperator) && (
            token.token.parentKind == SyntaxTreeKind::AssignmentExpression ||
            token.token.parentKind == SyntaxTreeKind::InitDeclarator ||
            token.token.parentKind == SyntaxTreeKind::FieldDeclaration ||
            token.token.parentKind == SyntaxTreeKind::AliasDeclaration
        );
}

bool IsConditionalOperatorForNode(const FormatBreakToken& token) {
    return token.token.kind == PrintTokenKind::Known &&
        token.token.parentKind == SyntaxTreeKind::ConditionalExpression &&
        (token.token.known == KnownToken::Question || token.token.known == KnownToken::Colon);
}

bool IsCommaOperatorForNode(const FormatBreakToken& token) {
    return token.token.kind == PrintTokenKind::Known &&
        token.token.parentKind == SyntaxTreeKind::CommaExpression &&
        token.token.known == KnownToken::Comma;
}

bool IsControlHeaderKind(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::ConditionClause ||
        kind == SyntaxTreeKind::ForStatement ||
        kind == SyntaxTreeKind::IfStatement ||
        kind == SyntaxTreeKind::WhileStatement ||
        kind == SyntaxTreeKind::SwitchStatement;
}

bool IsForHeaderDelimiter(const FormatBreakToken& open) {
    return open.token.parentKind == SyntaxTreeKind::ForStatement ||
        open.token.grandParentKind == SyntaxTreeKind::ForStatement;
}

bool IsFlatLogicalHeaderKind(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::ConditionClause ||
        kind == SyntaxTreeKind::IfStatement ||
        kind == SyntaxTreeKind::WhileStatement ||
        kind == SyntaxTreeKind::SwitchStatement;
}

bool IsFlattenableBinaryOperator(KnownToken token) {
    return token == KnownToken::Plus ||
        token == KnownToken::Star ||
        token == KnownToken::Pipe ||
        token == KnownToken::AmpersandAmpersand ||
        token == KnownToken::PipePipe ||
        token == KnownToken::LessLess ||
        token == KnownToken::GreaterGreater;
}

bool IsHexDigit(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
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

bool EndsWithOpenHexEscape(std::string_view text) {
    const size_t quote = text.rfind('"');
    if (quote == std::string_view::npos || quote == 0) {
        return false;
    }
    size_t cursor = quote;
    while (cursor > 0 && IsHexDigit(text[cursor - 1])) {
        --cursor;
    }
    return cursor < quote && cursor >= 2 && text[cursor - 2] == '\\' && text[cursor - 1] == 'x';
}

bool ForcesStringBoundarySplit(const FormatBreakToken& token) {
    const std::string_view text = FormatTokenText(token.token);
    return EndsWithEscapedLineFragment(text) || EndsWithOpenHexEscape(text);
}

size_t CountFieldInitializers(const SyntaxNode& node) {
    if (node.kind == SyntaxNodeKind::Tree && node.treeKind == SyntaxTreeKind::FieldInitializerList) {
        return static_cast<size_t>(std::count_if(
            node.children.begin(),
            node.children.end(),
            [](const std::unique_ptr<SyntaxNode>& child) {
                return child &&
                    child->kind == SyntaxNodeKind::Tree &&
                    child->treeKind == SyntaxTreeKind::FieldInitializer;
            }
        ));
    }
    size_t count = 0;
    for (const std::unique_ptr<SyntaxNode>& child : node.children) {
        if (child) {
            count += CountFieldInitializers(*child);
        }
    }
    return count;
}

bool IsConstructorParameterListWithInitializerList(const FormatBreakToken& open) {
    if (
        open.token.parentKind != SyntaxTreeKind::ParameterList ||
        open.token.grandParentKind != SyntaxTreeKind::FunctionDeclarator
    ) {
        return false;
    }
    for (const SyntaxNode* ancestor : open.token.syntaxPath) {
        if (
            ancestor != nullptr &&
            ancestor->kind == SyntaxNodeKind::Tree &&
            ancestor->treeKind == SyntaxTreeKind::FunctionDefinition
        ) {
            return CountFieldInitializers(*ancestor) > 2;
        }
    }
    return false;
}

bool IsLogicalOperatorToken(const FormatBreakToken& token) {
    return token.token.kind == PrintTokenKind::Known &&
        (token.token.known == KnownToken::AmpersandAmpersand || token.token.known == KnownToken::PipePipe);
}

bool IsLogicalChain(const FormatBreakNode& node) {
    return node.kind == FormatBreakNodeKind::Chain &&
        node.chainKind == FormatBreakChainKind::AfterOperator &&
        !node.operators.empty() &&
        std::all_of(node.operators.begin(), node.operators.end(), IsLogicalOperatorToken);
}

bool UsesFlatLogicalContinuation(const FormatBreakToken& open, const FormatBreakNode& item) {
    if (!IsLogicalChain(item)) {
        return false;
    }
    if (open.token.parentKind == SyntaxTreeKind::RequiresClause) {
        return true;
    }
    if (
        open.token.parentKind == SyntaxTreeKind::ForStatement ||
        open.token.grandParentKind == SyntaxTreeKind::ForStatement
    ) {
        return false;
    }
    return IsFlatLogicalHeaderKind(open.token.parentKind) || IsFlatLogicalHeaderKind(open.token.grandParentKind);
}

class BreakModelBuilder {
public:
    BreakModelBuilder(std::span<const PrintToken> tokens, const FormatBreakModelContext& context) : context_(context)
    {
        const PrintToken* previous = nullptr;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const PrintToken& token = tokens[index];
            bool spaceBefore = FormatTokenNeedsSpace(previous, token);
            if (
                token.kind == PrintTokenKind::Known &&
                token.known == KnownToken::LeftParen &&
                index + 1 < tokens.size() &&
                IsCompilerCallModifierStart(&tokens[index + 1])
            ) {
                spaceBefore = false;
            }
            FormatBreakToken breakToken{token, spaceBefore};
            if (token.node != nullptr) {
                tokensByNode_[token.node] = breakToken;
            }
            for (const SyntaxNode* ancestor : token.syntaxPath) {
                if (ancestor != nullptr) {
                    selectedNodes_.insert(ancestor);
                }
            }
            previous = &token;
        }
        root_ = CommonRoot(tokens);
    }

    FormatBreakModel Build() {
        FormatBreakModel model;
        if (root_ != nullptr) {
            model.root = BuildSyntaxNode(*root_, 0);
        }
        if (!model.root) {
            model.root = MakeNode(FormatBreakNodeKind::Sequence, 0);
        }
        return model;
    }

private:
    const FormatBreakModelContext& context_;
    std::unordered_map<const SyntaxNode*, FormatBreakToken> tokensByNode_;
    std::unordered_set<const SyntaxNode*> selectedNodes_;
    const SyntaxNode* root_ = nullptr;
    int nextId_ = 1;

    static const SyntaxNode* CommonRoot(std::span<const PrintToken> tokens) {
        if (tokens.empty() || tokens.front().syntaxPath.empty()) {
            return nullptr;
        }
        size_t commonLength = tokens.front().syntaxPath.size();
        for (const PrintToken& token : tokens.subspan(1)) {
            commonLength = std::min(commonLength, token.syntaxPath.size());
            size_t index = 0;
            while (index < commonLength && tokens.front().syntaxPath[index] == token.syntaxPath[index]) {
                ++index;
            }
            commonLength = index;
        }
        return commonLength == 0 ? nullptr : tokens.front().syntaxPath[commonLength - 1];
    }

    bool ContainsSelected(const SyntaxNode& node) const {
        return selectedNodes_.find(&node) != selectedNodes_.end();
    }

    const FormatBreakToken* TokenForNode(const SyntaxNode& node) const {
        const auto found = tokensByNode_.find(&node);
        return found == tokensByNode_.end() ? nullptr : &found->second;
    }

    const FormatBreakToken* FirstSelectedToken(const SyntaxNode& node) const {
        if (!ContainsSelected(node)) {
            return nullptr;
        }
        if (const FormatBreakToken* token = TokenForNode(node)) {
            return token;
        }
        for (const std::unique_ptr<SyntaxNode>& child : node.children) {
            if (!child) {
                continue;
            }
            if (const FormatBreakToken* token = FirstSelectedToken(*child)) {
                return token;
            }
        }
        return nullptr;
    }

    std::unique_ptr<FormatBreakNode> MakeNode(FormatBreakNodeKind kind, int depth) {
        auto node = std::make_unique<FormatBreakNode>();
        node->id = nextId_++;
        node->kind = kind;
        node->structuralDepth = depth;
        return node;
    }

    std::unique_ptr<FormatBreakNode> BuildToken(const FormatBreakToken& token, int depth) {
        auto node = MakeNode(FormatBreakNodeKind::Token, depth);
        node->token = token;
        return node;
    }

    static const FormatBreakToken* TokenChild(const std::unique_ptr<FormatBreakNode>& node) {
        if (!node || node->kind != FormatBreakNodeKind::Token) {
            return nullptr;
        }
        return &node->token;
    }

    static bool IsStringTokenChild(const std::unique_ptr<FormatBreakNode>& node) {
        const FormatBreakToken* token = TokenChild(node);
        return token != nullptr && IsStringLike(token->token);
    }

    void AppendDelimitedItem(
        FormatBreakNode& delimited,
        SyntaxChildList& itemChildren,
        const FormatBreakToken& open,
        int depth
    ) {
        if (itemChildren.empty()) {
            return;
        }
        std::unique_ptr<FormatBreakNode> item = BuildSequenceFromPointers(itemChildren, depth + 1);
        if (
            delimited.delimiterKind == FormatBreakDelimiterKind::Paren &&
            item &&
            item->kind == FormatBreakNodeKind::Chain &&
            item->chainKind != FormatBreakChainKind::Ternary &&
            (open.token.parentKind == SyntaxTreeKind::Unknown || UsesFlatLogicalContinuation(open, *item))
        ) {
            item->flatSplitIndent = true;
        }
        delimited.items.push_back(std::move(item));
        itemChildren.clear();
    }

    void AppendEmptyDelimitedItem(FormatBreakNode& delimited, int depth) {
        delimited.items.push_back(MakeNode(FormatBreakNodeKind::Sequence, depth + 1));
    }

    void GroupAdjacentStrings(FormatBreakNode& sequence, int depth) {
        std::vector<std::unique_ptr<FormatBreakNode>> grouped;
        grouped.reserve(sequence.children.size());
        for (size_t index = 0; index < sequence.children.size();) {
            if (!IsStringTokenChild(sequence.children[index])) {
                grouped.push_back(std::move(sequence.children[index]));
                ++index;
                continue;
            }

            const size_t begin = index;
            while (index < sequence.children.size() && IsStringTokenChild(sequence.children[index])) {
                ++index;
            }
            if (index - begin == 1) {
                grouped.push_back(std::move(sequence.children[begin]));
                continue;
            }

            auto strings = MakeNode(FormatBreakNodeKind::AdjacentStrings, depth + 1);
            for (size_t cursor = begin; cursor < index; ++cursor) {
                if (cursor + 1 < index && ForcesStringBoundarySplit(*TokenChild(sequence.children[cursor]))) {
                    strings->forceSplit = true;
                }
                strings->operands.push_back(std::move(sequence.children[cursor]));
            }
            grouped.push_back(std::move(strings));
        }
        sequence.children = std::move(grouped);
    }

    std::unique_ptr<FormatBreakNode> BuildSyntaxNode(const SyntaxNode& node, int depth) {
        if (!ContainsSelected(node)) {
            return nullptr;
        }
        if (const FormatBreakToken* token = TokenForNode(node)) {
            return BuildToken(*token, depth);
        }
        if (node.kind != SyntaxNodeKind::Tree) {
            return nullptr;
        }
        if (node.treeKind == SyntaxTreeKind::Declaration) {
            if (auto declaration = BuildDirectInitializedDeclaration(node, depth)) {
                return declaration;
            }
            if (auto declaration = BuildAssignedDeclaration(node, depth)) {
                return declaration;
            }
        }
        if (
            node.treeKind == SyntaxTreeKind::BinaryExpression ||
            node.treeKind == SyntaxTreeKind::AssignmentExpression ||
            node.treeKind == SyntaxTreeKind::InitDeclarator ||
            node.treeKind == SyntaxTreeKind::FieldDeclaration ||
            node.treeKind == SyntaxTreeKind::AliasDeclaration ||
            node.treeKind == SyntaxTreeKind::CommaExpression ||
            node.treeKind == SyntaxTreeKind::ConditionalExpression
        ) {
            if (auto expression = BuildOperatorExpression(node, depth)) {
                return expression;
            }
        }
        if (node.treeKind == SyntaxTreeKind::FieldInitializerList) {
            if (auto list = BuildPrefixList(node, depth)) {
                return list;
            }
        }
        return BuildSequenceFromChildren(node.children, 0, node.children.size(), depth);
    }

    static bool IsDirectInitializedDeclarator(const SyntaxNode& node) {
        if (node.kind != SyntaxNodeKind::Tree || node.treeKind != SyntaxTreeKind::InitDeclarator) {
            return false;
        }
        bool hasAssignment = false;
        bool hasInitializer = false;
        for (const std::unique_ptr<SyntaxNode>& child : node.children) {
            if (!child) {
                continue;
            }
            if (
                child->kind == SyntaxNodeKind::KnownToken &&
                KnownTokenHasClass(child->known, TokenClass::AssignmentOperator)
            ) {
                hasAssignment = true;
            }
            if (child->kind == SyntaxNodeKind::Tree && child->treeKind == SyntaxTreeKind::InitializerList) {
                hasInitializer = true;
            }
        }
        return hasInitializer && !hasAssignment;
    }

    std::unique_ptr<FormatBreakNode> BuildDirectInitializedDeclaration(const SyntaxNode& node, int depth) {
        std::optional<size_t> declaratorIndex;
        for (size_t index = 0; index < node.children.size(); ++index) {
            if (node.children[index] && IsDirectInitializedDeclarator(*node.children[index])) {
                declaratorIndex = index;
                break;
            }
        }
        if (!declaratorIndex || *declaratorIndex == 0) {
            return nullptr;
        }

        auto chain = MakeNode(FormatBreakNodeKind::Chain, depth);
        chain->operands.push_back(BuildSequenceFromChildren(node.children, 0, *declaratorIndex, depth + 1));
        chain->operators.push_back({});
        chain->operands.push_back(
            BuildSequenceFromChildren(node.children, *declaratorIndex, node.children.size(), depth + 1)
        );
        return chain;
    }

    std::unique_ptr<FormatBreakNode> BuildAssignedDeclaration(const SyntaxNode& node, int depth) {
        std::optional<size_t> declaratorIndex;
        std::optional<size_t> operatorIndex;
        for (size_t index = 0; index < node.children.size(); ++index) {
            if (
                !node.children[index] ||
                node.children[index]->kind != SyntaxNodeKind::Tree ||
                node.children[index]->treeKind != SyntaxTreeKind::InitDeclarator
            ) {
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
        const FormatBreakToken* op = TokenForNode(*declarator.children[*operatorIndex]);
        if (op == nullptr || !IsAssignmentOperatorForNode(*op)) {
            return nullptr;
        }

        SyntaxChildList leftChildren;
        for (size_t index = 0; index < *declaratorIndex; ++index) {
            leftChildren.push_back(node.children[index].get());
        }
        for (size_t index = 0; index < *operatorIndex; ++index) {
            leftChildren.push_back(declarator.children[index].get());
        }

        auto chain = MakeNode(FormatBreakNodeKind::Chain, depth);
        chain->operands.push_back(BuildSequenceFromPointers(leftChildren, depth + 1));
        chain->operators.push_back(*op);
        chain->operands.push_back(
            BuildSequenceFromChildren(declarator.children, *operatorIndex + 1, declarator.children.size(), depth + 1)
        );

        auto sequence = MakeNode(FormatBreakNodeKind::Sequence, depth);
        sequence->children.push_back(std::move(chain));
        for (size_t index = *declaratorIndex + 1; index < node.children.size(); ++index) {
            if (node.children[index] && ContainsSelected(*node.children[index])) {
                if (std::unique_ptr<FormatBreakNode> built = BuildSyntaxNode(*node.children[index], depth + 1)) {
                    sequence->children.push_back(std::move(built));
                }
            }
        }
        return sequence->children.size() == 1 ? std::move(sequence->children.front()) : std::move(sequence);
    }

    std::unique_ptr<FormatBreakNode> BuildSequenceFromPointers(const SyntaxChildList& children, int depth) {
        std::vector<std::unique_ptr<FormatBreakNode>> builtChildren;
        builtChildren.reserve(children.size());
        for (const SyntaxNode* child : children) {
            if (child == nullptr) {
                continue;
            }
            if (std::unique_ptr<FormatBreakNode> built = BuildSyntaxNode(*child, depth + 1)) {
                builtChildren.push_back(std::move(built));
            }
        }
        if (builtChildren.size() == 1) {
            return std::move(builtChildren.front());
        }
        auto sequence = MakeNode(FormatBreakNodeKind::Sequence, depth);
        sequence->children = std::move(builtChildren);
        GroupAdjacentStrings(*sequence, depth);
        return sequence;
    }

    std::unique_ptr<FormatBreakNode> BuildSequenceFromChildren(
        const std::vector<std::unique_ptr<SyntaxNode>>& children,
        size_t begin,
        size_t end,
        int depth
    ) {
        std::vector<std::unique_ptr<FormatBreakNode>> builtChildren;
        builtChildren.reserve(end - begin);
        for (size_t index = begin; index < end;) {
            if (!children[index] || !ContainsSelected(*children[index])) {
                ++index;
                continue;
            }
            size_t afterDelimited = index;
            if (
                std::unique_ptr<
                    FormatBreakNode
                > delimited = BuildDirectDelimited(children, index, end, depth + 1, afterDelimited)
            ) {
                builtChildren.push_back(std::move(delimited));
                index = afterDelimited;
                continue;
            }
            if (std::unique_ptr<FormatBreakNode> built = BuildSyntaxNode(*children[index], depth + 1)) {
                builtChildren.push_back(std::move(built));
            }
            ++index;
        }
        if (builtChildren.size() == 1) {
            return std::move(builtChildren.front());
        }
        auto sequence = MakeNode(FormatBreakNodeKind::Sequence, depth);
        sequence->children = std::move(builtChildren);
        GroupAdjacentStrings(*sequence, depth);
        return sequence;
    }

    std::optional<size_t> DirectOperatorIndex(const SyntaxNode& node) const {
        for (size_t index = 0; index < node.children.size(); ++index) {
            if (!node.children[index]) {
                continue;
            }
            const FormatBreakToken* token = TokenForNode(*node.children[index]);
            if (token == nullptr) {
                continue;
            }
            if (
                (node.treeKind == SyntaxTreeKind::BinaryExpression && IsBinaryOperatorForNode(*token)) ||
                (node.treeKind == SyntaxTreeKind::CommaExpression && IsCommaOperatorForNode(*token)) || (
                    (
                        node.treeKind == SyntaxTreeKind::AssignmentExpression ||
                        node.treeKind == SyntaxTreeKind::InitDeclarator ||
                        node.treeKind == SyntaxTreeKind::FieldDeclaration ||
                        node.treeKind == SyntaxTreeKind::AliasDeclaration
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
            const FormatBreakToken* token = TokenForNode(*node.children[index]);
            if (token == nullptr || !IsConditionalOperatorForNode(*token)) {
                continue;
            }
            if (token->token.known == KnownToken::Question) {
                question = index;
            } else if (token->token.known == KnownToken::Colon) {
                colon = index;
            }
        }
        if (!question || !colon || *question >= *colon) {
            return std::nullopt;
        }
        return std::pair<size_t, size_t>{*question, *colon};
    }

    bool HasSameDirectBinaryOperator(const SyntaxNode& node, KnownToken op) const {
        if (node.treeKind != SyntaxTreeKind::BinaryExpression) {
            return false;
        }
        const std::optional<size_t> opIndex = DirectOperatorIndex(node);
        if (!opIndex || !node.children[*opIndex]) {
            return false;
        }
        const FormatBreakToken* token = TokenForNode(*node.children[*opIndex]);
        return token != nullptr && token->token.known == op;
    }

    void AppendBinaryChainOperand(
        FormatBreakNode& chain,
        const std::vector<std::unique_ptr<SyntaxNode>>& children,
        size_t begin,
        size_t end,
        KnownToken op,
        int depth
    ) {
        if (
            end == begin + 1 &&
            children[begin] &&
            IsFlattenableBinaryOperator(op) &&
            HasSameDirectBinaryOperator(*children[begin], op)
        ) {
            AppendBinaryChain(*children[begin], op, chain, depth);
            return;
        }
        chain.operands.push_back(BuildSequenceFromChildren(children, begin, end, depth + 1));
    }

    void AppendBinaryChain(const SyntaxNode& node, KnownToken op, FormatBreakNode& chain, int depth) {
        const std::optional<size_t> opIndex = DirectOperatorIndex(node);
        if (!opIndex || !node.children[*opIndex]) {
            chain.operands.push_back(BuildSyntaxNode(node, depth + 1));
            return;
        }

        AppendBinaryChainOperand(chain, node.children, 0, *opIndex, op, depth);
        const FormatBreakToken* token = TokenForNode(*node.children[*opIndex]);
        if (token != nullptr) {
            chain.operators.push_back(*token);
        }
        AppendBinaryChainOperand(chain, node.children, *opIndex + 1, node.children.size(), op, depth);
    }

    std::unique_ptr<FormatBreakNode> BuildBinaryOrAssignmentExpression(const SyntaxNode& node, int depth) {
        const std::optional<size_t> opIndex = DirectOperatorIndex(node);
        if (!opIndex || !node.children[*opIndex]) {
            return nullptr;
        }
        const FormatBreakToken* token = TokenForNode(*node.children[*opIndex]);
        if (token == nullptr) {
            return nullptr;
        }

        auto chain = MakeNode(FormatBreakNodeKind::Chain, depth);
        chain->chainKind = (
            token->token.known == KnownToken::LessLess || token->token.known == KnownToken::GreaterGreater
        ) ? FormatBreakChainKind::StreamBeforeOperator : FormatBreakChainKind::AfterOperator;
        if (node.treeKind == SyntaxTreeKind::BinaryExpression && IsFlattenableBinaryOperator(token->token.known)) {
            AppendBinaryChain(node, token->token.known, *chain, depth);
            return chain;
        }

        chain->operands.push_back(BuildSequenceFromChildren(node.children, 0, *opIndex, depth + 1));
        chain->operators.push_back(*token);
        chain->operands.push_back(
            BuildSequenceFromChildren(node.children, *opIndex + 1, node.children.size(), depth + 1)
        );
        return chain;
    }

    void AppendConditionalChain(const SyntaxNode& node, FormatBreakNode& chain, int depth) {
        const std::optional<std::pair<size_t, size_t>> operators = DirectConditionalOperatorIndices(node);
        if (!operators) {
            chain.operands.push_back(BuildSyntaxNode(node, depth + 1));
            return;
        }
        const size_t question = operators->first;
        const size_t colon = operators->second;
        chain.operands.push_back(BuildSequenceFromChildren(node.children, 0, question, depth + 1));
        if (const FormatBreakToken* token = TokenForNode(*node.children[question])) {
            chain.operators.push_back(*token);
        }
        chain.operands.push_back(BuildSequenceFromChildren(node.children, question + 1, colon, depth + 1));
        if (const FormatBreakToken* token = TokenForNode(*node.children[colon])) {
            chain.operators.push_back(*token);
        }
        if (
            node.children.size() == colon + 2 &&
            node.children[colon + 1] &&
            node.children[colon + 1]->treeKind == SyntaxTreeKind::ConditionalExpression
        ) {
            AppendConditionalChain(*node.children[colon + 1], chain, depth);
        } else {
            chain.operands.push_back(
                BuildSequenceFromChildren(node.children, colon + 1, node.children.size(), depth + 1)
            );
        }
    }

    std::unique_ptr<FormatBreakNode> BuildConditionalExpression(const SyntaxNode& node, int depth) {
        if (!DirectConditionalOperatorIndices(node)) {
            return nullptr;
        }
        auto chain = MakeNode(FormatBreakNodeKind::Chain, depth);
        chain->chainKind = FormatBreakChainKind::Ternary;
        AppendConditionalChain(node, *chain, depth);
        return chain;
    }

    std::unique_ptr<FormatBreakNode> BuildOperatorExpression(const SyntaxNode& node, int depth) {
        if (node.treeKind == SyntaxTreeKind::ConditionalExpression) {
            return BuildConditionalExpression(node, depth);
        }
        return BuildBinaryOrAssignmentExpression(node, depth);
    }

    std::unique_ptr<FormatBreakNode> BuildPrefixList(const SyntaxNode& node, int depth) {
        std::optional<size_t> prefixIndex;
        for (size_t index = 0; index < node.children.size(); ++index) {
            if (!node.children[index]) {
                continue;
            }
            const FormatBreakToken* token = TokenForNode(*node.children[index]);
            if (
                token != nullptr &&
                token->token.kind == PrintTokenKind::Known &&
                token->token.known == KnownToken::Colon
            ) {
                prefixIndex = index;
                break;
            }
        }
        if (!prefixIndex || !node.children[*prefixIndex]) {
            return nullptr;
        }
        const FormatBreakToken* prefix = TokenForNode(*node.children[*prefixIndex]);
        if (prefix == nullptr) {
            return nullptr;
        }

        auto list = MakeNode(FormatBreakNodeKind::PrefixList, depth);
        list->children.push_back(BuildToken(*prefix, depth + 1));

        SyntaxChildList itemChildren;
        for (size_t index = *prefixIndex + 1; index < node.children.size(); ++index) {
            const SyntaxNode* child = node.children[index].get();
            if (child == nullptr || !ContainsSelected(*child)) {
                continue;
            }
            const FormatBreakToken* token = TokenForNode(*child);
            if (
                token != nullptr &&
                token->token.kind == PrintTokenKind::Known &&
                token->token.known == KnownToken::Comma
            ) {
                if (!itemChildren.empty()) {
                    list->items.push_back(BuildSequenceFromPointers(itemChildren, depth + 1));
                    itemChildren.clear();
                }
                list->separators.push_back(*token);
                continue;
            }
            itemChildren.push_back(child);
        }
        if (!itemChildren.empty()) {
            list->items.push_back(BuildSequenceFromPointers(itemChildren, depth + 1));
            list->separators.push_back({});
        }
        return list->items.empty() ? nullptr : std::move(list);
    }

    std::optional<std::pair<size_t, FormatBreakDelimiterKind>> FindDirectClose(
        const std::vector<std::unique_ptr<SyntaxNode>>& children,
        size_t openIndex,
        size_t end,
        FormatBreakDelimiterKind delimiter
    ) const {
        for (size_t index = openIndex + 1; index < end; ++index) {
            if (!children[index]) {
                continue;
            }
            const FormatBreakToken* token = TokenForNode(*children[index]);
            if (token != nullptr && ClosingDelimiter(*token) == delimiter) {
                return std::pair<size_t, FormatBreakDelimiterKind>{index, delimiter};
            }
        }
        return std::nullopt;
    }

    std::unique_ptr<FormatBreakNode> BuildDirectDelimited(
        const std::vector<std::unique_ptr<SyntaxNode>>& children,
        size_t openIndex,
        size_t end,
        int depth,
        size_t& afterDelimited
    ) {
        afterDelimited = openIndex + 1;
        if (!children[openIndex]) {
            return nullptr;
        }
        const FormatBreakToken* open = TokenForNode(*children[openIndex]);
        if (open == nullptr) {
            return nullptr;
        }
        const FormatBreakDelimiterKind delimiter = OpeningDelimiter(*open);
        if (delimiter == FormatBreakDelimiterKind::None) {
            return nullptr;
        }
        const std::optional<std::pair<size_t, FormatBreakDelimiterKind>> closeMatch =
            FindDirectClose(children, openIndex, end, delimiter);
        const bool hasVirtualClose = !closeMatch &&
            context_.virtualDelimiterOpen == open->token.node &&
            ClosingDelimiter(context_.virtualDelimiterClose) == delimiter;
        if (!closeMatch && !hasVirtualClose) {
            return nullptr;
        }
        const size_t closeIndex = closeMatch ? closeMatch->first : end;
        const FormatBreakToken* close =
            closeMatch ? TokenForNode(*children[closeIndex]) : &context_.virtualDelimiterClose;

        auto delimited = MakeNode(FormatBreakNodeKind::Delimited, depth);
        delimited->delimiterKind = delimiter;
        delimited->children.push_back(BuildToken(*open, depth + 1));
        delimited->children.push_back(BuildToken(*close, depth + 1));

        SyntaxChildList itemChildren;
        for (size_t index = openIndex + 1; index < closeIndex; ++index) {
            const SyntaxNode* child = children[index].get();
            if (child == nullptr || !ContainsSelected(*child)) {
                continue;
            }
            const FormatBreakToken* token = TokenForNode(*child);
            if (token != nullptr && IsSelectedSeparator(*token)) {
                if (
                    itemChildren.empty() &&
                    IsForHeaderDelimiter(*open) &&
                    token->token.kind == PrintTokenKind::Known &&
                    token->token.known == KnownToken::Semicolon
                ) {
                    AppendEmptyDelimitedItem(*delimited, depth);
                } else {
                    AppendDelimitedItem(*delimited, itemChildren, *open, depth);
                }
                delimited->separators.push_back(*token);
                continue;
            }
            itemChildren.push_back(child);
            if (
                (IsControlHeaderKind(open->token.parentKind) || IsControlHeaderKind(open->token.grandParentKind)) &&
                itemChildren.size() == 1 &&
                child->kind == SyntaxNodeKind::Tree &&
                (child->treeKind == SyntaxTreeKind::Declaration || child->treeKind == SyntaxTreeKind::InitStatement)
            ) {
                AppendDelimitedItem(*delimited, itemChildren, *open, depth);
                delimited->separators.push_back({});
            }
        }
        AppendDelimitedItem(*delimited, itemChildren, *open, depth);
        if (
            itemChildren.empty() &&
            IsForHeaderDelimiter(*open) &&
            !delimited->separators.empty() &&
            delimited->items.size() == delimited->separators.size()
        ) {
            AppendEmptyDelimitedItem(*delimited, depth);
        }
        if (delimited->items.empty() && !delimited->separators.empty()) {
            return nullptr;
        }
        if (!delimited->items.empty()) {
            delimited->separators.push_back({});
        }
        delimited->forceSplit = (IsConstructorParameterListWithInitializerList(*open) && delimited->items.size() > 1) ||
            (hasVirtualClose && context_.forceSplitVirtualDelimiter);
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
