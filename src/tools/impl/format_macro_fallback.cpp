#include "tools/impl/format_macro_fallback.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool IsWordLike(const Token& token) {
    return token.kind == TokenKind::Word ||
        token.kind == TokenKind::Number ||
        token.kind == TokenKind::StringLiteral ||
        token.kind == TokenKind::CharLiteral;
}

bool IsControlKeyword(std::string_view text) {
    return text == "if" || text == "for" || text == "while" || text == "switch" || text == "catch";
}

bool IsBinaryOperatorLike(std::string_view text) {
    static constexpr std::string_view kOperators[] = {
        "=",
        "+",
        "-",
        "*",
        "/",
        "%",
        "==",
        "!=",
        "<",
        ">",
        "<=",
        ">=",
        "&&",
        "||",
        "+=",
        "-=",
        "*=",
        "/=",
        "%=",
        "&=",
        "|=",
        "^=",
        "<<",
        ">>",
        "<<=",
        ">>=",
        "&",
        "|",
        "^",
        "?",
        ":"
    };
    return std::find(std::begin(kOperators), std::end(kOperators), text) != std::end(kOperators);
}

std::optional<size_t> AnnotatedMatchingIndex(TokenSpan tokens, size_t index) {
    if (tokens.empty() || index >= tokens.size() || tokens[index].matchingIndex == kNoTokenIndex) {
        return std::nullopt;
    }
    const size_t base = tokens.front().modelIndex;
    const size_t matching = tokens[index].matchingIndex;
    if (base == kNoTokenIndex || matching < base) {
        return std::nullopt;
    }
    const size_t relative = matching - base;
    if (relative >= tokens.size() || tokens[relative].modelIndex != matching) {
        return std::nullopt;
    }
    return relative;
}

class MacroFallbackLayoutParser {
public:
    LayoutTree Build(TokenSpan tokens) const {
        LayoutTree tree;
        size_t order = 0;
        const auto addNode = [&](LayoutNode node) {
            for (const LayoutNode& existing : tree.breakNodes) {
                if (
                    existing.kind == node.kind &&
                    existing.index == node.index &&
                    existing.group.open == node.group.open &&
                    existing.group.close == node.group.close
                ) {
                    return;
                }
            }
            node.order = order++;
            tree.breakNodes.push_back(node);
        };
        if (IsTemplateDeclarationPrefix(tokens)) {
            addNode({LayoutNodeKind::TemplateDeclaration, 0, {}, 0, 0});
            return tree;
        }
        if (IsRequiresClausePrefix(tokens)) {
            addNode({LayoutNodeKind::TemplateDeclaration, 0, {}, 0, 0});
            return tree;
        }
        if (std::optional<size_t> topLevelLambdaBody = FindTopLevelLambdaBodyOpen(tokens)) {
            addNode({LayoutNodeKind::Lambda, *topLevelLambdaBody, {}, 0, 0});
            return tree;
        }
        if (std::optional<GroupPair> group = FindFirstWrappableGroupPairWithTopLevelSeparatorAndLambda(tokens, ',')) {
            addNode({LayoutNodeKind::Group, 0, *group, 0, 0});
        }
        if (std::optional<size_t> initializerColon = FindConstructorInitializerColon(tokens)) {
            addNode({LayoutNodeKind::ConstructorInitializer, *initializerColon, {}, 0, 0});
        }
        if (std::optional<GroupPair> controlHeader = FindLeadingControlHeaderGroup(tokens)) {
            addNode({LayoutNodeKind::Group, 0, *controlHeader, 0, 0});
        }
        if (
            std::optional<size_t> assignment = FindTopLevelAssignment(tokens);
            assignment && !IsDefaultedDeletedOrPureVirtualMethodDeclaration(tokens)
        ) {
            addNode({LayoutNodeKind::Assignment, *assignment, {}, 0, 0});
            CollectLayoutGroupNodes(tokens, 0, *assignment, 0, tree.breakNodes, order);
            return tree;
        }
        const ChainKind chainKind = SelectChainKind(tokens);
        if (chainKind != ChainKind::None) {
            addNode({LayoutNodeKind::OperatorChain, 0, {}, 0, 0});
            if (chainKind == ChainKind::Shift || IsTrueOperatorChain(tokens, chainKind)) {
                return tree;
            }
        }
        if (IsStringLiteralSequence(tokens)) {
            addNode({LayoutNodeKind::StringLiteralSequence, 0, {}, 0, 0});
            return tree;
        }
        if (std::optional<GroupPair> initializerList = FindTopLevelInitializerListDeclarationGroup(tokens)) {
            if (std::optional<size_t> declarator = InitializerListDeclarationDeclarator(tokens, *initializerList)) {
                addNode({LayoutNodeKind::DeclarationValue, *declarator, {}, 0, 0});
            }
            addNode({LayoutNodeKind::Group, 0, *initializerList, 0, 0});
        }
        if (std::optional<size_t> nestedLambdaBody = FindLambdaBodyOpen(tokens)) {
            addNode({LayoutNodeKind::Lambda, *nestedLambdaBody, {}, 1, 0});
            return tree;
        }
        CollectLayoutGroupNodes(tokens, 0, tokens.size(), chainKind == ChainKind::None ? 0 : 1, tree.breakNodes, order);
        return tree;
    }

private:
    bool IsTemplateDeclarationPrefix(TokenSpan tokens) const {
        if (tokens.empty() || tokens.front().text != "template") {
            return false;
        }
        const size_t open = NextSignificantIndex(tokens, 1);
        return open < tokens.size() && tokens[open].text == "<" && IsTemplateAngleOpen(tokens, open);
    }

    bool IsRequiresClausePrefix(TokenSpan tokens) const {
        const size_t first = NextSignificantIndex(tokens, 0);
        if (first >= tokens.size() || tokens[first].text != "requires") {
            return false;
        }
        const size_t open = NextSignificantIndex(tokens, first + 1);
        if (open >= tokens.size() || tokens[open].text != "(") {
            return false;
        }
        const std::optional<size_t> close = FindMatchingClose(tokens, open);
        if (!close) {
            return false;
        }
        const size_t afterClose = NextSignificantIndex(tokens, *close + 1);
        return afterClose >= tokens.size() || tokens[afterClose].text != "{";
    }

    void CollectLayoutGroupNodes(
        TokenSpan tokens,
        size_t begin,
        size_t end,
        int depth,
        std::vector<LayoutNode>& nodes,
        size_t& order
    ) const {
        for (size_t index = begin; index < end; ++index) {
            if (!IsWrappableGroupOpen(tokens, index)) {
                continue;
            }
            const std::optional<size_t> close = FindWrappableGroupClose(tokens, index);
            if (!close || *close >= end) {
                continue;
            }
            const bool wrappable = !IsEmptyGroupPair(tokens, index, *close) &&
                !IsNonWrappablePrefixGroup(tokens, index, *close) &&
                !IsFunctionPointerDeclaratorGroupOpen(tokens, index);
            if (wrappable) {
                nodes.push_back({LayoutNodeKind::Group, 0, GroupPair{index, *close}, depth, order++});
            }
            if (!GroupOwnsNestedBreaks(tokens, index, *close)) {
                CollectLayoutGroupNodes(tokens, index + 1, *close, depth + 1, nodes, order);
            }
            index = *close;
        }
    }

    bool GroupOwnsNestedBreaks(TokenSpan tokens, size_t open, size_t close) const {
        if (open >= tokens.size() || close > tokens.size()) {
            return false;
        }
        if (IsLambdaBodyOpenToken(tokens, open)) {
            return true;
        }
        if (tokens[open].text == "{") {
            return true;
        }
        TokenSpan inner = TokenSubspan(tokens, open + 1, close);
        if (HasNestedWrappableGroupWithNonClosingTail(tokens, open + 1, close)) {
            return true;
        }
        return ContainsTopLevelSeparator(inner, ',') || ContainsTopLevelSeparator(inner, ';');
    }

    bool HasNestedWrappableGroupWithNonClosingTail(TokenSpan tokens, size_t begin, size_t end) const {
        for (size_t index = begin; index < end; ++index) {
            if (!IsWrappableGroupOpen(tokens, index)) {
                continue;
            }
            const std::optional<size_t> close = FindWrappableGroupClose(tokens, index);
            if (!close || *close >= end) {
                continue;
            }
            if (
                !IsEmptyGroupPair(tokens, index, *close) &&
                !IsNonWrappablePrefixGroup(tokens, index, *close) &&
                !IsFunctionPointerDeclaratorGroupOpen(tokens, index) &&
                HasNonClosingTokenBefore(tokens, *close + 1, end)
            ) {
                return true;
            }
            index = *close;
        }
        return false;
    }

    bool HasNonClosingTokenBefore(TokenSpan tokens, size_t begin, size_t end) const {
        for (size_t index = begin; index < end; ++index) {
            if (tokens[index].kind == TokenKind::Newline) {
                continue;
            }
            if (!IsStackedClosingToken(tokens, index)) {
                return true;
            }
        }
        return false;
    }

    bool IsStackedClosingToken(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size()) {
            return false;
        }
        return tokens[index].text == ")" ||
            tokens[index].text == "]" ||
            tokens[index].text == "}" ||
            IsTemplateAngleCloseToken(tokens, index);
    }

    std::optional<GroupPair> FindLeadingControlHeaderGroup(TokenSpan tokens) const {
        const size_t keyword = NextSignificantIndex(tokens, 0);
        if (keyword >= tokens.size() || !IsControlKeyword(tokens[keyword].text)) {
            return std::nullopt;
        }
        const size_t open = NextSignificantIndex(tokens, keyword + 1);
        if (open >= tokens.size() || tokens[open].text != "(") {
            return std::nullopt;
        }
        const std::optional<size_t> close = FindMatchingClose(tokens, open);
        if (!close || IsEmptyGroupPair(tokens, open, *close)) {
            return std::nullopt;
        }
        return GroupPair{open, *close};
    }

    std::optional<GroupPair> FindTopLevelInitializerListDeclarationGroup(TokenSpan tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (depth == 0 && tokens[index].text == "{") {
                const std::optional<size_t> close = FindMatchingClose(tokens, index);
                if (!close) {
                    return std::nullopt;
                }
                const size_t afterClose = NextSignificantIndex(tokens, *close + 1);
                if (afterClose >= tokens.size() || tokens[afterClose].text != ";") {
                    index = *close;
                    continue;
                }
                const size_t afterSemicolon = NextSignificantIndex(tokens, afterClose + 1);
                if (afterSemicolon < tokens.size()) {
                    index = *close;
                    continue;
                }
                TokenSpan inner = TokenSubspan(tokens, index + 1, *close);
                if (!ContainsTopLevelSeparator(inner, ',')) {
                    index = *close;
                    continue;
                }
                const std::optional<size_t> declarator = PreviousNonNewlineIndex(tokens, index);
                if (declarator && tokens[*declarator].kind == TokenKind::Word && IsLikelyDeclarationTypeBeforeName(
                    tokens,
                    *declarator
                )) {
                    return GroupPair{index, *close};
                }
                index = *close;
                continue;
            }
            UpdateDepth(tokens, index, depth);
        }
        return std::nullopt;
    }

    std::optional<size_t> InitializerListDeclarationDeclarator(TokenSpan tokens, GroupPair initializerList) const {
        if (initializerList.open >= tokens.size()) {
            return std::nullopt;
        }
        const std::optional<size_t> declarator = PreviousNonNewlineIndex(tokens, initializerList.open);
        if (!declarator || tokens[*declarator].kind != TokenKind::Word) {
            return std::nullopt;
        }
        if (!IsLikelyDeclarationTypeBeforeName(tokens, *declarator)) {
            return std::nullopt;
        }
        return declarator;
    }

    bool IsLikelyDeclarationTypeBeforeName(TokenSpan tokens, size_t nameIndex) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, nameIndex);
        if (!previous) {
            return false;
        }
        if (IsPointerOrReferenceDeclaratorToken(tokens[*previous].text)) {
            return IsPointerOrReferenceDeclarator(tokens, *previous) && IsLikelyTypeBeforePointer(tokens, *previous);
        }
        return IsLikelyTypeNameToken(tokens, *previous);
    }

    bool IsStringLiteralSequence(TokenSpan tokens) const {
        int literalCount = 0;
        bool sawTrailingSemicolon = false;
        for (const Token& token : tokens) {
            if (token.kind == TokenKind::Newline) {
                continue;
            }
            if (token.text == ";" && literalCount > 0 && !sawTrailingSemicolon) {
                sawTrailingSemicolon = true;
                continue;
            }
            if (token.kind != TokenKind::StringLiteral || sawTrailingSemicolon) {
                return false;
            }
            ++literalCount;
        }
        return literalCount > 1;
    }

    std::optional<size_t> FindTopLevelAssignment(TokenSpan tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (depth == 0 && IsAssignmentOperator(token.text) && !IsOperatorFunctionNameToken(tokens, index)) {
                return index;
            }
            UpdateDepth(token, depth);
        }
        return std::nullopt;
    }

    std::optional<GroupPair> FindFirstWrappableGroupPairWithTopLevelSeparatorAndLambda(
        TokenSpan tokens,
        char separator
    ) const {
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (IsGroupOpen(tokens[index].text)) {
                if (std::optional<size_t> close = FindWrappableGroupClose(tokens, index)) {
                    if (
                        !IsEmptyGroupPair(tokens, index, *close) &&
                        !IsNonWrappablePrefixGroup(tokens, index, *close) &&
                        !IsFunctionPointerDeclaratorGroupOpen(tokens, index)
                    ) {
                        TokenSpan inner = TokenSubspan(tokens, index + 1, *close);
                        if (ContainsTopLevelSeparator(inner, separator) && FindTopLevelLambdaBodyOpen(inner)) {
                            return GroupPair{index, *close};
                        }
                    }
                }
            }
        }
        return std::nullopt;
    }

    bool IsWrappableGroupOpen(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size()) {
            return false;
        }
        return IsGroupOpen(tokens[index].text) || IsWrappableTemplateAngleOpen(tokens, index);
    }

    bool IsWrappableTemplateAngleOpen(TokenSpan tokens, size_t index) const {
        if (!IsTemplateAngleOpen(tokens, index)) {
            return false;
        }
        const std::optional<size_t> close = FindTemplateAngleClose(tokens, index);
        if (!close || tokens[*close].text != ">") {
            return false;
        }
        TokenSpan inner = TokenSubspan(tokens, index + 1, *close);
        return ContainsTopLevelSeparator(inner, ',');
    }

    std::optional<size_t> FindWrappableGroupClose(TokenSpan tokens, size_t open) const {
        if (open >= tokens.size()) {
            return std::nullopt;
        }
        if (IsTemplateAngleOpen(tokens, open)) {
            const std::optional<size_t> close = FindTemplateAngleClose(tokens, open);
            if (close && tokens[*close].text == ">") {
                return close;
            }
            return std::nullopt;
        }
        return FindMatchingClose(tokens, open);
    }

    bool IsNonWrappablePrefixGroup(TokenSpan tokens, size_t open, size_t close) const {
        return IsParenthesizedCalleeGroup(tokens, open, close) || IsDeclspecGroup(tokens, open);
    }

    bool IsParenthesizedCalleeGroup(TokenSpan tokens, size_t open, size_t close) const {
        if (open >= tokens.size() || tokens[open].text != "(") {
            return false;
        }
        if (IsFunctionPointerDeclaratorGroupOpen(tokens, open)) {
            return false;
        }
        const size_t next = NextSignificantIndex(tokens, close + 1);
        return next < tokens.size() && tokens[next].text == "(";
    }

    bool IsDeclspecGroup(TokenSpan tokens, size_t open) const {
        if (open >= tokens.size() || tokens[open].text != "(") {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, open);
        return previous && tokens[*previous].text == "__declspec";
    }

    bool IsEmptyGroupPair(TokenSpan tokens, size_t open, size_t close) const {
        for (size_t index = open + 1; index < close; ++index) {
            if (tokens[index].kind != TokenKind::Newline) {
                return false;
            }
        }
        return true;
    }

    std::optional<size_t> FindMatchingClose(TokenSpan tokens, size_t openIndex) const {
        if (std::optional<size_t> annotated = AnnotatedMatchingIndex(tokens, openIndex)) {
            return annotated;
        }
        const std::string close = MatchingClose(tokens[openIndex].text);
        int depth = 0;
        for (size_t index = openIndex; index < tokens.size(); ++index) {
            if (tokens[index].text == tokens[openIndex].text) {
                ++depth;
            } else if (tokens[index].text == close) {
                --depth;
                if (depth == 0) {
                    return index;
                }
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> FindMatchingOpen(TokenSpan tokens, size_t closeIndex) const {
        if (std::optional<size_t> annotated = AnnotatedMatchingIndex(tokens, closeIndex)) {
            return annotated;
        }
        const std::string& close = tokens[closeIndex].text;
        std::string open;
        if (close == ")") {
            open = "(";
        } else if (close == "]") {
            open = "[";
        } else if (close == "}") {
            open = "{";
        } else {
            return std::nullopt;
        }
        int depth = 0;
        for (size_t index = closeIndex + 1; index > 0; --index) {
            const size_t current = index - 1;
            if (tokens[current].text == close) {
                ++depth;
            } else if (tokens[current].text == open) {
                --depth;
                if (depth == 0) {
                    return current;
                }
            }
        }
        return std::nullopt;
    }

    bool ContainsTopLevelSeparator(TokenSpan tokens, char separator) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            const Token& token = tokens[index];
            if (depth == 0 && token.text.size() == 1 && token.text[0] == separator) {
                return true;
            }
            UpdateDepth(tokens, index, depth);
        }
        return false;
    }

    bool IsTrueOperatorChain(TokenSpan tokens, ChainKind chainKind) const {
        if (chainKind == ChainKind::None) {
            return false;
        }
        if (chainKind == ChainKind::Ternary) {
            return !SplitSingleTopLevelTernary(tokens).has_value();
        }
        return CountTopLevelChainBreakOperators(tokens, chainKind) > 1;
    }

    size_t CountTopLevelChainBreakOperators(TokenSpan tokens, ChainKind chainKind) const {
        size_t count = 0;
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth == 0 && IsChainBreakOperator(tokens, index, chainKind)) {
                ++count;
            }
        }
        return count;
    }

    ChainKind SelectChainKind(TokenSpan tokens) const {
        int depth = 0;
        bool hasTernary = false;
        bool hasLogical = false;
        bool hasBitwise = false;
        bool hasEquality = false;
        bool hasRelational = false;
        bool hasShift = false;
        bool hasPlus = false;
        bool hasMinus = false;
        bool hasStar = false;
        bool hasDivisionOrModulo = false;
        for (size_t index = 0; index < tokens.size(); ++index) {
            UpdateDepth(tokens[index], depth);
            if (depth != 0 || IsTemplateAngleToken(tokens, index)) {
                continue;
            }
            const std::string& text = tokens[index].text;
            if (text == "?") {
                hasTernary = true;
            } else if (IsPointerOrReferenceDeclaratorToken(text) && IsPointerOrReferenceDeclarator(tokens, index)) {
                continue;
            } else if (text == "&&" || text == "||") {
                hasLogical = true;
            } else if (text == "|" || text == "^") {
                hasBitwise = true;
            } else if (text == "==" || text == "!=") {
                hasEquality = true;
            } else if (text == "<" || text == ">" || text == "<=" || text == ">=") {
                hasRelational = true;
            } else if (text == "<<" || text == ">>") {
                hasShift = true;
            } else if ((text == "+" || text == "-") && IsUnaryPrefixOperator(tokens, index)) {
                continue;
            } else if (text == "+") {
                hasPlus = true;
            } else if (text == "-") {
                hasMinus = true;
            } else if ((text == "*" || text == "&") && IsPointerOrReferenceDeclarator(tokens, index)) {
                continue;
            } else if (text == "*") {
                hasStar = true;
            } else if (text == "/" || text == "%") {
                hasDivisionOrModulo = true;
            }
        }
        if (hasTernary) {
            return ChainKind::Ternary;
        }
        if (hasLogical) {
            return ChainKind::Logical;
        }
        if (hasBitwise) {
            return ChainKind::Bitwise;
        }
        if (hasEquality) {
            return ChainKind::Equality;
        }
        if (hasRelational) {
            return ChainKind::Relational;
        }
        if (hasShift) {
            return ChainKind::Shift;
        }
        if (hasPlus && !hasMinus) {
            return ChainKind::Additive;
        }
        if (hasStar && !hasDivisionOrModulo) {
            return ChainKind::Multiplicative;
        }
        return ChainKind::None;
    }

    bool IsChainBreakOperator(TokenSpan tokens, size_t index, ChainKind chainKind) const {
        if (chainKind == ChainKind::None || IsTemplateAngleToken(tokens, index)) {
            return false;
        }
        if (IsPointerOrReferenceDeclaratorToken(tokens[index].text) && IsPointerOrReferenceDeclarator(tokens, index)) {
            return false;
        }
        if ((tokens[index].text == "+" || tokens[index].text == "-") && IsUnaryPrefixOperator(tokens, index)) {
            return false;
        }
        const std::string& text = tokens[index].text;
        switch (chainKind) {
            case ChainKind::Ternary:
                return text == "?" || text == ":";
            case ChainKind::Logical:
                return text == "&&" || text == "||";
            case ChainKind::Bitwise:
                return text == "|" || text == "^";
            case ChainKind::Equality:
                return text == "==" || text == "!=";
            case ChainKind::Relational:
                return text == "<" || text == ">" || text == "<=" || text == ">=";
            case ChainKind::Shift:
                return text == "<<" || text == ">>";
            case ChainKind::Additive:
                return text == "+";
            case ChainKind::Multiplicative:
                return text == "*";
            case ChainKind::None:
                return false;
        }
        return false;
    }

    struct TernaryExpressionParts {
        TokenSpan condition;
        TokenSpan trueBranch;
        TokenSpan falseBranch;
    };

    std::optional<TernaryExpressionParts> SplitSingleTopLevelTernary(TokenSpan tokens) const {
        int depth = 0;
        std::optional<size_t> question;
        for (size_t index = 0; index < tokens.size(); ++index) {
            UpdateDepth(tokens, index, depth);
            if (depth != 0 || tokens[index].text != "?") {
                continue;
            }
            if (question) {
                return std::nullopt;
            }
            question = index;
        }
        if (!question) {
            return std::nullopt;
        }
        const std::optional<size_t> colon = FindMatchingTernaryColon(tokens, *question);
        if (!colon) {
            return std::nullopt;
        }
        return TernaryExpressionParts{
            TokenSubspan(tokens, 0, *question),
            TokenSubspan(tokens, *question + 1, *colon),
            TokenSubspan(tokens, *colon + 1, tokens.size())
        };
    }

    std::optional<size_t> FindMatchingTernaryColon(TokenSpan tokens, size_t question) const {
        int depth = 0;
        int ternaryDepth = 0;
        for (size_t index = question + 1; index < tokens.size(); ++index) {
            UpdateDepth(tokens, index, depth);
            if (depth != 0) {
                continue;
            }
            if (tokens[index].text == "?") {
                ++ternaryDepth;
            } else if (tokens[index].text == ":") {
                if (ternaryDepth == 0) {
                    return index;
                }
                --ternaryDepth;
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> FindConstructorInitializerColon(TokenSpan tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (depth == 0 && tokens[index].text == ":" && IsConstructorInitializerColon(tokens, index)) {
                return index;
            }
            UpdateDepth(tokens[index], depth);
        }
        return std::nullopt;
    }

    bool IsConstructorInitializerColon(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != ":") {
            return false;
        }
        if (HasTopLevelTokenBefore(tokens, index, "?")) {
            return false;
        }
        const std::optional<size_t> parametersClose = FindConstructorParameterListCloseBeforeColon(tokens, index);
        if (!parametersClose) {
            return false;
        }
        const std::optional<size_t> open = FindMatchingOpen(tokens, *parametersClose);
        if (!open) {
            return false;
        }
        const std::optional<size_t> functionName = PreviousNonNewlineIndex(tokens, *open);
        if (!functionName || tokens[*functionName].kind != TokenKind::Word) {
            return false;
        }
        const size_t afterColon = NextSignificantIndex(tokens, index + 1);
        return afterColon < tokens.size();
    }

    std::optional<size_t> FindConstructorParameterListCloseBeforeColon(TokenSpan tokens, size_t colon) const {
        size_t cursor = colon;
        while (std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, cursor)) {
            if (tokens[*previous].text == ")") {
                const std::optional<size_t> open = FindMatchingOpen(tokens, *previous);
                if (!open) {
                    return std::nullopt;
                }
                const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
                if (beforeOpen && tokens[*beforeOpen].kind == TokenKind::Word && IsConstructorTrailingQualifierGroup(
                    tokens[*beforeOpen].text
                )) {
                    cursor = *beforeOpen;
                    continue;
                }
                return *previous;
            }
            if (
                tokens[*previous].kind == TokenKind::Word && IsConstructorTrailingQualifierWord(tokens[*previous].text)
            ) {
                cursor = *previous;
                continue;
            }
            break;
        }
        return std::nullopt;
    }

    static bool IsConstructorTrailingQualifierWord(std::string_view text) {
        return text == "noexcept";
    }

    static bool IsConstructorTrailingQualifierGroup(std::string_view text) {
        return text == "noexcept" || text == "requires";
    }

    bool HasTopLevelTokenBefore(TokenSpan tokens, size_t before, std::string_view text) const {
        int depth = 0;
        for (size_t index = 0; index < before; ++index) {
            if (depth == 0 && tokens[index].text == text) {
                return true;
            }
            UpdateDepth(tokens[index], depth);
        }
        return false;
    }

    std::optional<size_t> FindLambdaBodyOpen(TokenSpan tokens) const {
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (!IsLambdaBodyOpenToken(tokens, index)) {
                continue;
            }
            const std::optional<size_t> close = FindMatchingClose(tokens, index);
            if (close && IsEmptyGroupPair(tokens, index, *close)) {
                continue;
            }
            if (IsLambdaBodyOpenToken(tokens, index)) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> FindTopLevelLambdaBodyOpen(TokenSpan tokens) const {
        int depth = 0;
        for (size_t index = 0; index < tokens.size(); ++index) {
            if (depth == 0 && IsLambdaBodyOpenToken(tokens, index)) {
                const std::optional<size_t> close = FindMatchingClose(tokens, index);
                if (!close || !IsEmptyGroupPair(tokens, index, *close)) {
                    return index;
                }
            }
            UpdateDepth(tokens, index, depth);
        }
        return std::nullopt;
    }

    bool IsPointerOrReferenceDeclarator(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size() || !IsPointerOrReferenceDeclaratorToken(tokens[index].text)) {
            return false;
        }
        const size_t nextIndex = NextSignificantIndex(tokens, index + 1);
        const Token* next = nextIndex < tokens.size() ? &tokens[nextIndex] : nullptr;
        const bool beforeDeclaratorName = next != nullptr && next->kind == TokenKind::Word;
        const bool beforeTemplateClose = next != nullptr && IsTemplateAngleCloseToken(tokens, nextIndex);
        const bool beforeStructuredBinding = next != nullptr && tokens[index].text != "*" && next->text == "[";
        const bool beforeUnnamedDeclaratorEnd =
            next == nullptr || next->text == ")" || next->text == "," || next->text == "=" || next->text == ";";
        const bool beforePointerOrReferenceDeclarator =
            next != nullptr && IsPointerOrReferenceDeclaratorToken(next->text);
        const bool beforeFunctionPointerDeclarator =
            next != nullptr && tokens[index].text == "*" && IsFunctionPointerDeclaratorGroupOpen(tokens, nextIndex);
        const bool beforeDeclarator = beforeDeclaratorName ||
            beforeTemplateClose ||
            beforeStructuredBinding ||
            beforeUnnamedDeclaratorEnd ||
            beforePointerOrReferenceDeclarator ||
            beforeFunctionPointerDeclarator;
        return beforeDeclarator &&
            IsLikelyTypeBeforePointer(tokens, index) &&
            IsLikelyDeclaratorContextBeforePointer(tokens, index);
    }

    static bool IsPointerOrReferenceDeclaratorToken(std::string_view text) {
        return text == "*" || text == "&" || text == "&&" || text == "^" || text == "%";
    }

    bool IsFunctionPointerDeclaratorGroupOpen(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != "(") {
            return false;
        }
        const std::optional<size_t> close = FindMatchingClose(tokens, index);
        if (!close) {
            return false;
        }
        const size_t afterClose = NextSignificantIndex(tokens, *close + 1);
        if (afterClose >= tokens.size() || tokens[afterClose].text != "(") {
            return false;
        }
        bool sawPointer = false;
        bool sawSignificant = false;
        for (size_t inner = index + 1; inner < *close; ++inner) {
            if (tokens[inner].kind == TokenKind::Newline) {
                continue;
            }
            sawSignificant = true;
            if (
                tokens[inner].kind != TokenKind::Word &&
                tokens[inner].text != "::" &&
                !IsPointerOrReferenceDeclaratorToken(tokens[inner].text)
            ) {
                return false;
            }
            if (IsPointerOrReferenceDeclaratorToken(tokens[inner].text)) {
                sawPointer = true;
            }
        }
        return sawSignificant && sawPointer;
    }

    bool IsDefaultedDeletedOrPureVirtualMethodDeclaration(TokenSpan tokens) const {
        const std::optional<size_t> assignment = FindTopLevelAssignment(tokens);
        if (!assignment) {
            return false;
        }
        const size_t marker = NextSignificantIndex(tokens, *assignment + 1);
        if (marker >= tokens.size()) {
            return false;
        }
        return tokens[marker].text == "default" || tokens[marker].text == "delete" || tokens[marker].text == "0";
    }

    bool IsOperatorFunctionNameToken(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].kind != TokenKind::Symbol) {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        return previous && tokens[*previous].text == "operator";
    }

    std::optional<size_t> PreviousNonNewlineIndex(TokenSpan tokens, size_t index) const {
        while (index > 0) {
            --index;
            if (tokens[index].kind != TokenKind::Newline) {
                return index;
            }
        }
        return std::nullopt;
    }

    bool IsUnaryPrefixOperator(TokenSpan tokens, size_t index) const {
        const std::string& text = tokens[index].text;
        if (
            text != "*" &&
            text != "&" &&
            text != "+" &&
            text != "-" &&
            text != "++" &&
            text != "--" &&
            text != "!" &&
            text != "~"
        ) {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        if (!previous) {
            return true;
        }
        if (IsTemplateAngleCloseToken(tokens, *previous)) {
            return false;
        }
        const std::string& prev = tokens[*previous].text;
        return prev == "(" ||
            prev == "[" ||
            prev == "{" ||
            prev == "," ||
            prev == ";" ||
            prev == "=" ||
            prev == "?" ||
            prev == ":" ||
            IsBinaryOperatorLike(prev) ||
            prev == "return";
    }

    bool IsLikelyTypeBeforePointer(TokenSpan tokens, size_t index) const {
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        if (!previous) {
            return false;
        }
        if (IsPointerOrReferenceDeclaratorToken(tokens[*previous].text) && IsPointerOrReferenceDeclarator(
            tokens,
            *previous
        )) {
            return true;
        }
        return IsLikelyTypeNameToken(tokens, *previous);
    }

    bool IsLikelyTypeNameToken(TokenSpan tokens, size_t index) const {
        const Token& token = tokens[index];
        if (IsTemplateAngleCloseToken(tokens, index)) {
            return IsLikelyTemplateTypeClose(tokens, index);
        }
        if (token.text == ")") {
            return IsDecltypeCloseBeforePointer(tokens, index);
        }
        if (token.text == "]") {
            return false;
        }
        if (token.kind != TokenKind::Word) {
            return false;
        }
        if (IsCallingConventionToken(token.text)) {
            return true;
        }
        static constexpr std::string_view kTypeWords[] = {
            "auto",
            "bool",
            "char",
            "const",
            "double",
            "float",
            "int",
            "long",
            "short",
            "signed",
            "size_t",
            "std",
            "unsigned",
            "void",
            "wchar_t"
        };
        if (std::find(std::begin(kTypeWords), std::end(kTypeWords), token.text) != std::end(kTypeWords)) {
            return true;
        }
        if (IsTypedefStyleTypeName(token.text)) {
            return true;
        }
        if (!token.text.empty() && token.text.front() >= 'A' && token.text.front() <= 'Z') {
            return true;
        }
        const std::optional<size_t> beforeType = PreviousNonNewlineIndex(tokens, index);
        if (beforeType && tokens[*beforeType].kind == TokenKind::Word && IsTypeContextWord(tokens[*beforeType].text)) {
            return true;
        }
        return beforeType && tokens[*beforeType].text == "::";
    }

    static bool IsTypedefStyleTypeName(std::string_view text) {
        return text.size() > 2 && text.ends_with("_t");
    }

    static bool IsCallingConventionToken(std::string_view text) {
        static constexpr std::string_view kCallingConventionTokens[] = {
            "__cdecl",
            "__clrcall",
            "__fastcall",
            "__stdcall",
            "__thiscall",
            "__vectorcall"
        };
        return std::find(std::begin(kCallingConventionTokens), std::end(kCallingConventionTokens), text) !=
            std::end(kCallingConventionTokens);
    }

    bool IsDecltypeCloseBeforePointer(TokenSpan tokens, size_t closeIndex) const {
        const std::optional<size_t> open = FindMatchingOpen(tokens, closeIndex);
        if (!open || *open == 0) {
            return false;
        }
        const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
        return beforeOpen && tokens[*beforeOpen].text == "decltype";
    }

    bool IsLikelyTemplateTypeClose(TokenSpan tokens, size_t closeIndex) const {
        const std::optional<size_t> open = FindTemplateAngleOpen(tokens, closeIndex);
        if (!open) {
            return true;
        }
        const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
        if (!beforeOpen || tokens[*beforeOpen].kind != TokenKind::Word) {
            return true;
        }
        return !tokens[*beforeOpen].text.ends_with("_v");
    }

    bool IsLikelyDeclaratorContextBeforePointer(TokenSpan tokens, size_t index) const {
        const std::optional<size_t> typeStart = TypeNameStartBeforePointer(tokens, index);
        if (!typeStart) {
            return false;
        }
        const std::optional<size_t> beforeType = PreviousNonNewlineIndex(tokens, *typeStart);
        if (!beforeType) {
            return true;
        }
        const Token& token = tokens[*beforeType];
        if (token.kind == TokenKind::Word) {
            return IsTypeContextWord(token.text);
        }
        const std::string& text = token.text;
        if (text == "=") {
            return HasWordBefore(tokens, *beforeType, "using");
        }
        if (text == "::") {
            return IsTypeQualifierStartInDeclaratorContext(tokens, *beforeType);
        }
        return text == "(" ||
            text == "[" ||
            text == "{" ||
            text == "," ||
            text == "<" ||
            IsPointerOrReferenceDeclaratorToken(text) ||
            text == ":";
    }

    bool IsTypeQualifierStartInDeclaratorContext(TokenSpan tokens, size_t qualifier) const {
        const std::optional<size_t> beforeQualifier = PreviousNonNewlineIndex(tokens, qualifier);
        if (!beforeQualifier) {
            return true;
        }
        const std::string& text = tokens[*beforeQualifier].text;
        if (tokens[*beforeQualifier].kind == TokenKind::Word) {
            return IsTypeContextWord(text);
        }
        return text == "(" ||
            text == "[" ||
            text == "{" ||
            text == "," ||
            text == "<" ||
            text == "=" ||
            IsPointerOrReferenceDeclaratorToken(text) ||
            text == ":";
    }

    bool HasWordBefore(TokenSpan tokens, size_t before, std::string_view word) const {
        for (size_t index = 0; index < before; ++index) {
            if (tokens[index].kind == TokenKind::Word && tokens[index].text == word) {
                return true;
            }
        }
        return false;
    }

    std::optional<size_t> TypeNameStartBeforePointer(TokenSpan tokens, size_t index) const {
        std::optional<size_t> start = PreviousNonNewlineIndex(tokens, index);
        if (!start) {
            return std::nullopt;
        }
        start = UnwrapTemplateTypeNameStart(tokens, *start);
        if (!start) {
            return std::nullopt;
        }
        while (IsPointerOrReferenceDeclaratorToken(tokens[*start].text)) {
            if (!IsPointerOrReferenceDeclarator(tokens, *start)) {
                return std::nullopt;
            }
            start = PreviousNonNewlineIndex(tokens, *start);
            if (!start) {
                return std::nullopt;
            }
            start = UnwrapTemplateTypeNameStart(tokens, *start);
            if (!start) {
                return std::nullopt;
            }
        }
        while (*start > 1) {
            const std::optional<size_t> before = PreviousNonNewlineIndex(tokens, *start);
            if (!before || tokens[*before].text != "::") {
                break;
            }
            const std::optional<size_t> qualifier = PreviousNonNewlineIndex(tokens, *before);
            if (!qualifier || (tokens[*qualifier].kind != TokenKind::Word && tokens[*qualifier].text != ">")) {
                break;
            }
            start = UnwrapTemplateTypeNameStart(tokens, *qualifier);
            if (!start) {
                return std::nullopt;
            }
        }
        return start;
    }

    std::optional<size_t> UnwrapTemplateTypeNameStart(TokenSpan tokens, size_t index) const {
        if (!IsTemplateAngleCloseToken(tokens, index)) {
            return index;
        }
        const std::optional<size_t> open = FindTemplateAngleOpen(tokens, index);
        if (!open) {
            return index;
        }
        const std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open);
        if (!beforeOpen) {
            return std::nullopt;
        }
        return beforeOpen;
    }

    bool IsTypeContextWord(std::string_view text) const {
        static constexpr std::string_view kWords[] = {
            "class",
            "const",
            "enum",
            "long",
            "mutable",
            "short",
            "signed",
            "static",
            "struct",
            "typename",
            "unsigned",
            "virtual",
            "volatile"
        };
        return std::find(std::begin(kWords), std::end(kWords), text) != std::end(kWords);
    }

    bool IsLambdaBodyOpenToken(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != "{") {
            return false;
        }
        for (size_t current = index; current > 0; --current) {
            const size_t candidate = current - 1;
            if (tokens[candidate].text == "{" || tokens[candidate].text == "}" || tokens[candidate].text == ";") {
                return false;
            }
            if (IsLambdaIntroducerClose(tokens, candidate)) {
                return true;
            }
        }
        return false;
    }

    bool IsLambdaIntroducerClose(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != "]") {
            return false;
        }
        const std::optional<size_t> open = FindMatchingOpen(tokens, index);
        if (!open) {
            const size_t afterClose = NextSignificantIndex(tokens, index + 1);
            return index == 0 && afterClose < tokens.size() && tokens[afterClose].text == "(";
        }
        if (std::optional<size_t> beforeOpen = PreviousNonNewlineIndex(tokens, *open)) {
            if (IsWordLike(tokens[*beforeOpen]) || tokens[*beforeOpen].text == ")" || tokens[*beforeOpen].text == "]") {
                return false;
            }
        }
        const size_t afterClose = NextSignificantIndex(tokens, index + 1);
        if (afterClose >= tokens.size()) {
            return false;
        }
        const std::string& next = tokens[afterClose].text;
        return next == "(" || next == "{" || next == "mutable" || next == "noexcept" || next == "->";
    }

    bool IsTemplateAngleToken(TokenSpan tokens, size_t index) const {
        return IsTemplateAngleOpen(tokens, index) || IsTemplateAngleClose(tokens, index);
    }

    bool IsTemplateAngleCloseToken(TokenSpan tokens, size_t index) const {
        return index < tokens.size() &&
            (tokens[index].text == ">" || tokens[index].text == ">>") &&
            IsTemplateAngleClose(tokens, index);
    }

    bool IsTemplateAngleOpen(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size() || tokens[index].text != "<") {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        if (!previous) {
            return false;
        }
        const Token& before = tokens[*previous];
        if (before.text != "template" && before.kind != TokenKind::Word && before.text != ">") {
            return false;
        }
        return FindTemplateAngleClose(tokens, index).has_value();
    }

    bool IsTemplateAngleClose(TokenSpan tokens, size_t index) const {
        if (index >= tokens.size() || (tokens[index].text != ">" && tokens[index].text != ">>")) {
            return false;
        }
        return FindTemplateAngleOpen(tokens, index).has_value();
    }

    int TemplateCloseWidth(std::string_view text) const {
        if (text == ">>") {
            return 2;
        }
        if (text == ">") {
            return 1;
        }
        return 0;
    }

    std::optional<size_t> FindTemplateAngleOpen(TokenSpan tokens, size_t close) const {
        if (close >= tokens.size()) {
            return std::nullopt;
        }
        const int closeWidth = TemplateCloseWidth(tokens[close].text);
        if (closeWidth == 0) {
            return std::nullopt;
        }
        int depth = closeWidth - 1;
        for (size_t current = close; current > 0; --current) {
            const size_t candidate = current - 1;
            const int candidateCloseWidth = TemplateCloseWidth(tokens[candidate].text);
            if (candidateCloseWidth > 0) {
                depth += candidateCloseWidth;
            } else if (tokens[candidate].text == "<" && IsTemplateAngleOpen(tokens, candidate)) {
                if (depth == 0) {
                    return candidate;
                }
                --depth;
            } else if (depth == 0 && IsTemplateArgumentReferenceToken(tokens, candidate)) {
                continue;
            } else if (depth == 0 && IsTemplateScanBoundary(tokens[candidate].text)) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> FindTemplateAngleClose(TokenSpan tokens, size_t open) const {
        int depth = 0;
        for (size_t index = open + 1; index < tokens.size(); ++index) {
            const std::string& text = tokens[index].text;
            if (text == "<") {
                ++depth;
            } else if (text == ">") {
                if (depth == 0) {
                    return index;
                }
                --depth;
            } else if (text == ">>") {
                if (depth <= 1) {
                    return index;
                }
                depth -= 2;
            } else if (depth == 0 && IsTemplateArgumentReferenceToken(tokens, index)) {
                continue;
            } else if (depth == 0 && IsTemplateScanBoundary(text)) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    bool IsTemplateArgumentReferenceToken(TokenSpan tokens, size_t index) const {
        if (tokens[index].text != "&&") {
            return false;
        }
        const std::optional<size_t> previous = PreviousNonNewlineIndex(tokens, index);
        const size_t next = NextSignificantIndex(tokens, index + 1);
        if (!previous || next >= tokens.size()) {
            return false;
        }
        const std::string& before = tokens[*previous].text;
        const std::string& after = tokens[next].text;
        return (tokens[*previous].kind == TokenKind::Word || before == ">" || before == ">>") &&
            (after == "," || after == ">" || after == ">>");
    }

    bool IsTemplateScanBoundary(std::string_view text) const {
        return text == ";" ||
            text == "{" ||
            text == "}" ||
            text == "=" ||
            text == "?" ||
            text == ":" ||
            text == "&&" ||
            text == "||";
    }

    size_t NextSignificantIndex(TokenSpan tokens, size_t index) const {
        while (index < tokens.size() && tokens[index].kind == TokenKind::Newline) {
            ++index;
        }
        return index;
    }

    void UpdateDepth(TokenSpan tokens, size_t index, int& depth) const {
        if (index >= tokens.size()) {
            return;
        }
        const Token& token = tokens[index];
        if (IsGroupOpen(token.text)) {
            ++depth;
        } else if (token.text == ")" || token.text == "]" || token.text == "}") {
            depth = std::max(0, depth - 1);
        } else if (IsTemplateAngleOpen(tokens, index)) {
            ++depth;
        } else if (IsTemplateAngleCloseToken(tokens, index)) {
            depth = std::max(0, depth - TemplateCloseWidth(token.text));
        }
    }

    static bool IsGroupOpen(std::string_view text) {
        return text == "(" || text == "[" || text == "{";
    }

    static std::string MatchingClose(std::string_view text) {
        if (text == "(") {
            return ")";
        }
        if (text == "[") {
            return "]";
        }
        if (text == "{") {
            return "}";
        }
        return "}";
    }

    static void UpdateDepth(const Token& token, int& depth) {
        if (IsGroupOpen(token.text)) {
            ++depth;
        } else if (token.text == ")" || token.text == "]" || token.text == "}") {
            depth = std::max(0, depth - 1);
        }
    }

    static bool IsAssignmentOperator(std::string_view text) {
        static constexpr std::string_view kOperators[] = {
            "=",
            "+=",
            "-=",
            "*=",
            "/=",
            "%=",
            "&=",
            "|=",
            "^=",
            "<<=",
            ">>="
        };
        return std::find(std::begin(kOperators), std::end(kOperators), text) != std::end(kOperators);
    }
};

}  // namespace

LayoutTree BuildMacroFallbackLayoutTree(TokenSpan tokens) {
    return MacroFallbackLayoutParser{}.Build(tokens);
}
