#include "tools/impl/format_break_solver.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace {

struct SolveKey {
    int nodeId = 0;
    int column = 0;
    int indentLevel = 0;
    bool lineHasText = false;

    friend bool operator<(const SolveKey& left, const SolveKey& right) {
        if (left.nodeId != right.nodeId) {
            return left.nodeId < right.nodeId;
        }
        if (left.column != right.column) {
            return left.column < right.column;
        }
        if (left.indentLevel != right.indentLevel) {
            return left.indentLevel < right.indentLevel;
        }
        return left.lineHasText < right.lineHasText;
    }
};

struct NodeResult {
    bool valid = false;
    int endColumn = 0;
    int endIndentLevel = 0;
    bool endLineHasText = false;
    int extraLines = 0;
    int maxOverflow = 0;
    int deepestBreakIndent = -1;
    int deepestBreakDepth = -1;
    std::map<int, FormatBreakChoice> choices;
};

class Solver {
public:
    Solver(const FormatterConfig& config, int indentWidth) : config_(config), indentWidth_(indentWidth) {}

    NodeResult Solve(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        const SolveKey key{node.id, column, indentLevel, lineHasText};
        const auto found = memo_.find(key);
        if (found != memo_.end()) {
            return found->second;
        }

        NodeResult result;
        switch (node.kind) {
            case FormatBreakNodeKind::Token:
                result = SolveToken(node.token, column, indentLevel, lineHasText);
                break;
            case FormatBreakNodeKind::Sequence:
                result = SolveChildren(node.children, column, indentLevel, lineHasText);
                break;
            case FormatBreakNodeKind::Delimited:
                result = SolveDelimited(node, column, indentLevel, lineHasText);
                break;
            case FormatBreakNodeKind::PrefixList:
                result = SolvePrefixList(node, column, indentLevel, lineHasText);
                break;
            case FormatBreakNodeKind::Chain:
                result = SolveChain(node, column, indentLevel, lineHasText);
                break;
            case FormatBreakNodeKind::AdjacentStrings:
                result = SolveAdjacentStrings(node, column, indentLevel, lineHasText);
                break;
        }
        memo_.emplace(key, result);
        return result;
    }

private:
    const FormatterConfig& config_;
    int indentWidth_ = 4;
    std::map<SolveKey, NodeResult> memo_;

    int IndentColumn(int indentLevel) const {
        return std::max(0, indentLevel) * indentWidth_;
    }

    NodeResult SolveToken(const FormatBreakToken& token, int column, int indentLevel, bool lineHasText) const {
        const int space = token.spaceBefore && lineHasText ? 1 : 0;
        const int endColumn = column + space + FormatTokenWidth(token.token);
        return {
            .valid = true,
            .endColumn = endColumn,
            .endIndentLevel = indentLevel,
            .endLineHasText = lineHasText || FormatTokenWidth(token.token) > 0,
            .maxOverflow = std::max(0, endColumn - config_.columnLimit)
        };
    }

    static void Merge(NodeResult& left, const NodeResult& right) {
        left.valid = left.valid && right.valid;
        left.endColumn = right.endColumn;
        left.endIndentLevel = right.endIndentLevel;
        left.endLineHasText = right.endLineHasText;
        left.extraLines += right.extraLines;
        left.maxOverflow = std::max(left.maxOverflow, right.maxOverflow);
        left.deepestBreakIndent = std::max(left.deepestBreakIndent, right.deepestBreakIndent);
        left.deepestBreakDepth = std::max(left.deepestBreakDepth, right.deepestBreakDepth);
        left.choices.insert(right.choices.begin(), right.choices.end());
    }

    std::vector<
        NodeResult
    > SolveAlternatives(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        switch (node.kind) {
            case FormatBreakNodeKind::Token:
                return {SolveToken(node.token, column, indentLevel, lineHasText)};
            case FormatBreakNodeKind::Sequence:
                return SolveChildrenAlternatives(node.children, column, indentLevel, lineHasText);
            case FormatBreakNodeKind::Delimited: {
                std::vector<NodeResult> alternatives;
                NodeResult compact = SolveDelimitedCompact(node, column, indentLevel, lineHasText);
                if (
                    !node.forceSplit &&
                    !(compact.valid && compact.extraLines > 0 && !CanKeepDelimiterStackCompact(node, compact))
                ) {
                    alternatives.push_back(compact);
                }
                NodeResult split = SolveDelimitedSplit(node, column, indentLevel, lineHasText);
                if (split.valid) {
                    alternatives.push_back(split);
                }
                return alternatives;
            }
            case FormatBreakNodeKind::PrefixList: {
                std::vector<NodeResult> alternatives;
                NodeResult compact = SolvePrefixListCompact(node, column, indentLevel, lineHasText);
                if (!(compact.valid && compact.extraLines > 0)) {
                    alternatives.push_back(compact);
                }
                NodeResult split = SolvePrefixListSplit(node, column, indentLevel, lineHasText);
                if (split.valid) {
                    alternatives.push_back(split);
                }
                return alternatives;
            }
            case FormatBreakNodeKind::Chain: {
                std::vector<NodeResult> alternatives;
                for (NodeResult compact : SolveChainCompactAlternatives(node, column, indentLevel, lineHasText)) {
                    const bool compactKeepsSplitChild = compact.valid &&
                        compact.extraLines > 0 &&
                        (
                            IsCommaChain(node) ||
                            (node.chainKind != FormatBreakChainKind::Ternary && node.operators.size() > 1) ||
                            (node.chainKind == FormatBreakChainKind::Ternary && node.operators.size() > 2)
                        );
                    if (!compactKeepsSplitChild) {
                        alternatives.push_back(std::move(compact));
                    }
                }
                if (node.chainKind == FormatBreakChainKind::StreamBeforeOperator) {
                    alternatives.push_back(
                        SolveStreamSplit(node, column, indentLevel, lineHasText, FormatBreakChoice::StreamCompactTail)
                    );
                    alternatives.push_back(
                        SolveStreamSplit(node, column, indentLevel, lineHasText, FormatBreakChoice::Split)
                    );
                } else if (node.chainKind == FormatBreakChainKind::Ternary && node.operators.size() > 2) {
                    alternatives.push_back(SolveTernaryChainSplit(node, column, indentLevel, lineHasText));
                } else if (node.chainKind == FormatBreakChainKind::Ternary && node.operators.size() == 2) {
                    alternatives.push_back(SolveSingleTernary(
                        node,
                        column,
                        indentLevel,
                        lineHasText,
                        FormatBreakChoice::TernaryBreakAfterQuestion
                    ));
                    alternatives.push_back(SolveSingleTernary(
                        node,
                        column,
                        indentLevel,
                        lineHasText,
                        FormatBreakChoice::TernaryBreakAfterColon
                    ));
                    alternatives.push_back(
                        SolveSingleTernary(node, column, indentLevel, lineHasText, FormatBreakChoice::Split)
                    );
                } else {
                    alternatives.push_back(SolveChainSplitAfterOperator(node, column, indentLevel, lineHasText));
                }
                return alternatives;
            }
            case FormatBreakNodeKind::AdjacentStrings: {
                std::vector<NodeResult> alternatives;
                if (!node.forceSplit) {
                    alternatives.push_back(SolveAdjacentStringsCompact(node, column, indentLevel, lineHasText));
                }
                NodeResult split = SolveAdjacentStringsSplit(node, column, indentLevel, lineHasText);
                if (split.valid) {
                    alternatives.push_back(split);
                }
                return alternatives;
            }
        }
        return {};
    }

    NodeResult SolveChildren(
        const std::vector<std::unique_ptr<FormatBreakNode>>& children,
        int column,
        int indentLevel,
        bool lineHasText
    ) {
        NodeResult best;
        for (const NodeResult& candidate : SolveChildrenAlternatives(children, column, indentLevel, lineHasText)) {
            if (Better(candidate, best)) {
                best = candidate;
            }
        }
        return best;
    }

    std::vector<NodeResult> SolveChildrenAlternatives(
        const std::vector<std::unique_ptr<FormatBreakNode>>& children,
        int column,
        int indentLevel,
        bool lineHasText
    ) {
        std::vector<const FormatBreakNode*> sequenceChildren;
        AppendSequenceChildren(children, sequenceChildren);
        using SequenceKey = std::tuple<size_t, int, int, bool>;
        std::map<SequenceKey, std::vector<NodeResult>> memo;
        const auto solveFrom = [
            &,
            this
        ](auto&& self, size_t index, int currentColumn, int currentIndent, bool currentLineHasText) -> std::vector<
            NodeResult
        > {
            if (index == sequenceChildren.size()) {
                return {{
                    .valid = true,
                    .endColumn = currentColumn,
                    .endIndentLevel = currentIndent,
                    .endLineHasText = currentLineHasText
                }};
            }
            const SequenceKey key{index, currentColumn, currentIndent, currentLineHasText};
            const auto found = memo.find(key);
            if (found != memo.end()) {
                return found->second;
            }

            std::vector<NodeResult> results;
            for (const NodeResult& childResult : SolveAlternatives(
                *sequenceChildren[index],
                currentColumn,
                currentIndent,
                currentLineHasText
            )) {
                if (!childResult.valid) {
                    continue;
                }
                std::vector<NodeResult> tails = self(
                    self,
                    index + 1,
                    childResult.endColumn,
                    childResult.endIndentLevel,
                    childResult.endLineHasText
                );
                for (const NodeResult& tail : tails) {
                    if (!tail.valid) {
                        continue;
                    }
                    NodeResult candidate = childResult;
                    Merge(candidate, tail);
                    results.push_back(std::move(candidate));
                }
            }
            memo.emplace(key, results);
            return results;
        };
        return solveFrom(solveFrom, 0, column, indentLevel, lineHasText);
    }

    static void AppendSequenceChildren(
        const std::vector<std::unique_ptr<FormatBreakNode>>& children,
        std::vector<const FormatBreakNode*>& output
    ) {
        for (const std::unique_ptr<FormatBreakNode>& child : children) {
            if (!child) {
                continue;
            }
            if (child->kind == FormatBreakNodeKind::Sequence) {
                AppendSequenceChildren(child->children, output);
                continue;
            }
            output.push_back(child.get());
        }
    }

    NodeResult AddBreak(NodeResult result, int indentLevel, int structuralDepth) const {
        ++result.extraLines;
        result.endIndentLevel = indentLevel;
        result.endColumn = IndentColumn(indentLevel);
        result.endLineHasText = false;
        result.deepestBreakIndent = std::max(result.deepestBreakIndent, result.endColumn);
        result.deepestBreakDepth = std::max(result.deepestBreakDepth, structuralDepth);
        return result;
    }

    NodeResult AddToken(NodeResult result, const FormatBreakToken& token) const {
        NodeResult tokenResult = SolveToken(token, result.endColumn, result.endIndentLevel, result.endLineHasText);
        Merge(result, tokenResult);
        return result;
    }

    bool Better(const NodeResult& candidate, const NodeResult& incumbent) const {
        if (!candidate.valid) {
            return false;
        }
        if (!incumbent.valid) {
            return true;
        }
        if ((candidate.maxOverflow == 0) != (incumbent.maxOverflow == 0)) {
            return candidate.maxOverflow == 0;
        }
        if (candidate.maxOverflow != incumbent.maxOverflow) {
            return candidate.maxOverflow < incumbent.maxOverflow;
        }
        if (candidate.extraLines != incumbent.extraLines) {
            return candidate.extraLines < incumbent.extraLines;
        }
        if (candidate.deepestBreakIndent != incumbent.deepestBreakIndent) {
            return candidate.deepestBreakIndent < incumbent.deepestBreakIndent;
        }
        if (candidate.deepestBreakDepth != incumbent.deepestBreakDepth) {
            return candidate.deepestBreakDepth < incumbent.deepestBreakDepth;
        }
        return false;
    }

    bool CompactLineEndsOverLimit(const NodeResult& compact) const {
        return compact.endLineHasText && compact.endColumn > config_.columnLimit;
    }

    NodeResult SolveDelimitedCompact(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        result.choices.emplace(node.id, FormatBreakChoice::Compact);
        result = AddToken(result, node.children[0]->token);
        for (size_t index = 0; index < node.items.size(); ++index) {
            NodeResult item = Solve(*node.items[index], result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
            if (index < node.separators.size() && node.separators[index].token.kind == PrintTokenKind::Known) {
                result = AddToken(result, node.separators[index]);
            }
        }
        result = AddToken(result, node.children[1]->token);
        return result;
    }

    NodeResult SolveDelimitedSplit(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        if (node.items.empty()) {
            return {};
        }
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        result.choices.emplace(node.id, FormatBreakChoice::Split);
        result = AddToken(result, node.children[0]->token);
        result = AddBreak(result, indentLevel + 1, node.structuralDepth);
        for (size_t index = 0; index < node.items.size(); ++index) {
            NodeResult item = Solve(*node.items[index], result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
            if (index < node.separators.size() && node.separators[index].token.kind == PrintTokenKind::Known) {
                result = AddToken(result, node.separators[index]);
            }
            result =
                AddBreak(result, index + 1 < node.items.size() ? indentLevel + 1 : indentLevel, node.structuralDepth);
        }
        result = AddToken(result, node.children[1]->token);
        return result;
    }

    NodeResult SolveDelimited(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult compact = SolveDelimitedCompact(node, column, indentLevel, lineHasText);
        NodeResult split = SolveDelimitedSplit(node, column, indentLevel, lineHasText);
        if (node.forceSplit && split.valid) {
            return split;
        }
        if (compact.valid && split.valid && CompactLineEndsOverLimit(compact) && split.maxOverflow == 0) {
            return split;
        }
        if (
            compact.valid &&
            split.valid &&
            compact.maxOverflow == 0 &&
            split.maxOverflow == 0 &&
            compact.endColumn >= config_.columnLimit
        ) {
            return split;
        }
        if (compact.valid && split.valid && compact.extraLines > 0 && !CanKeepDelimiterStackCompact(node, compact)) {
            return split;
        }
        return Better(split, compact) ? split : compact;
    }

    static bool IsControlDelimiter(const FormatBreakNode& node) {
        if (node.children.empty() || node.children.front()->kind != FormatBreakNodeKind::Token) {
            return false;
        }
        const PrintToken& open = node.children.front()->token.token;
        return open.parentKind == SyntaxTreeKind::ConditionClause ||
            open.parentKind == SyntaxTreeKind::ForStatement ||
            open.grandParentKind == SyntaxTreeKind::IfStatement ||
            open.grandParentKind == SyntaxTreeKind::WhileStatement ||
            open.grandParentKind == SyntaxTreeKind::SwitchStatement ||
            open.grandParentKind == SyntaxTreeKind::ForStatement;
    }

    static FormatBreakChoice ChoiceFor(const NodeResult& result, const FormatBreakNode& node) {
        const auto found = result.choices.find(node.id);
        return found == result.choices.end() ? FormatBreakChoice::Compact : found->second;
    }

    static bool EndsWithCompactPrefixAndSplitDelimiter(const FormatBreakNode& node, const NodeResult& result) {
        if (node.kind == FormatBreakNodeKind::Delimited) {
            return ChoiceFor(result, node) == FormatBreakChoice::Split;
        }
        if (node.kind == FormatBreakNodeKind::Chain) {
            return node.operators.size() == 1 &&
                !node.operands.empty() &&
                ChoiceFor(result, node) == FormatBreakChoice::Compact &&
                EndsWithCompactPrefixAndSplitDelimiter(*node.operands.back(), result);
        }
        if (node.kind != FormatBreakNodeKind::Sequence || node.children.empty()) {
            return false;
        }
        for (size_t index = 0; index + 1 < node.children.size(); ++index) {
            if (ChoiceFor(result, *node.children[index]) != FormatBreakChoice::Compact) {
                return false;
            }
        }
        return EndsWithCompactPrefixAndSplitDelimiter(*node.children.back(), result);
    }

    static bool CanKeepDelimiterStackCompact(const FormatBreakNode& node, const NodeResult& compact) {
        if (node.items.size() != 1) {
            return false;
        }
        if (node.children.empty() || node.children.front()->kind != FormatBreakNodeKind::Token) {
            return false;
        }
        const PrintToken& open = node.children.front()->token.token;
        if (open.parentKind == SyntaxTreeKind::ForStatement || open.grandParentKind == SyntaxTreeKind::ForStatement) {
            return false;
        }
        if (!EndsWithCompactPrefixAndSplitDelimiter(*node.items.front(), compact)) {
            return false;
        }
        return std::none_of(
            node.separators.begin(),
            node.separators.end(),
            [](const FormatBreakToken& separator) { return separator.token.kind == PrintTokenKind::Known; }
        );
    }

    static bool IsCommaChain(const FormatBreakNode& node) {
        return !node.operators.empty() && std::all_of(
            node.operators.begin(),
            node.operators.end(),
            [](const FormatBreakToken& token) {
                return token.token.kind == PrintTokenKind::Known && token.token.known == KnownToken::Comma;
            }
        );
    }

    NodeResult SolvePrefixListCompact(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        result.choices.emplace(node.id, FormatBreakChoice::Compact);
        result = AddToken(result, node.children[0]->token);
        for (size_t index = 0; index < node.items.size(); ++index) {
            NodeResult item = Solve(*node.items[index], result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
            if (index < node.separators.size() && node.separators[index].token.kind == PrintTokenKind::Known) {
                result = AddToken(result, node.separators[index]);
            }
        }
        return result;
    }

    NodeResult SolvePrefixListSplit(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        result.choices.emplace(node.id, FormatBreakChoice::Split);
        result = AddToken(result, node.children[0]->token);
        result = AddBreak(result, indentLevel + 1, node.structuralDepth);
        for (size_t index = 0; index < node.items.size(); ++index) {
            NodeResult item = Solve(*node.items[index], result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
            if (index < node.separators.size() && node.separators[index].token.kind == PrintTokenKind::Known) {
                result = AddToken(result, node.separators[index]);
            }
            if (index + 1 < node.items.size()) {
                result = AddBreak(result, indentLevel + 1, node.structuralDepth);
            }
        }
        return result;
    }

    NodeResult SolvePrefixList(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult compact = SolvePrefixListCompact(node, column, indentLevel, lineHasText);
        NodeResult split = SolvePrefixListSplit(node, column, indentLevel, lineHasText);
        if (compact.valid && split.valid && compact.extraLines > 0) {
            return split;
        }
        if (compact.valid && split.valid && CompactLineEndsOverLimit(compact) && split.maxOverflow == 0) {
            return split;
        }
        return Better(split, compact) ? split : compact;
    }

    NodeResult SolveChainCompact(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult best;
        for (const NodeResult& candidate : SolveChainCompactAlternatives(node, column, indentLevel, lineHasText)) {
            if (Better(candidate, best)) {
                best = candidate;
            }
        }
        return best;
    }

    std::vector<
        NodeResult
    > SolveChainCompactAlternatives(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        result.choices.emplace(node.id, FormatBreakChoice::Compact);
        std::vector<NodeResult> candidates;
        const auto solveFrom = [&, this](auto&& self, size_t index, const NodeResult& current) -> void {
            if (index == node.operands.size()) {
                candidates.push_back(current);
                return;
            }
            for (const NodeResult& operand : SolveAlternatives(
                *node.operands[index],
                current.endColumn,
                current.endIndentLevel,
                current.endLineHasText
            )) {
                if (!operand.valid) {
                    continue;
                }
                NodeResult next = current;
                Merge(next, operand);
                if (index < node.operators.size()) {
                    next = AddToken(next, node.operators[index]);
                }
                self(self, index + 1, next);
            }
        };
        solveFrom(solveFrom, 0, result);
        return candidates;
    }

    NodeResult SolveChainSplitAfterOperator(
        const FormatBreakNode& node,
        int column,
        int indentLevel,
        bool lineHasText
    ) {
        const int continuationIndent = node.flatSplitIndent ? indentLevel : indentLevel + 1;
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        result.choices.emplace(node.id, FormatBreakChoice::Split);
        for (size_t index = 0; index < node.operands.size(); ++index) {
            NodeResult operand =
                Solve(*node.operands[index], result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, operand);
            if (index < node.operators.size()) {
                result = AddToken(result, node.operators[index]);
                result = AddBreak(result, continuationIndent, node.structuralDepth);
            }
        }
        return result;
    }

    NodeResult SolveStreamSplit(
        const FormatBreakNode& node,
        int column,
        int indentLevel,
        bool lineHasText,
        FormatBreakChoice choice
    ) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        result.choices.emplace(node.id, choice);
        NodeResult receiver =
            Solve(*node.operands.front(), result.endColumn, result.endIndentLevel, result.endLineHasText);
        Merge(result, receiver);
        result = AddBreak(result, indentLevel + 1, node.structuralDepth);
        for (size_t index = 0; index < node.operators.size(); ++index) {
            result = AddToken(result, node.operators[index]);
            NodeResult operand =
                Solve(*node.operands[index + 1], result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, operand);
            if (
                choice == FormatBreakChoice::Split &&
                index + 1 < node.operators.size() &&
                !IsFormatBreakStreamConfigurationOperand(
                    *node.operands[index + 1],
                    config_.streamShiftConfigurationMethods
                )
            ) {
                result = AddBreak(result, indentLevel + 1, node.structuralDepth);
            }
        }
        return result;
    }

    NodeResult SolveTernaryChainSplit(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        result.choices.emplace(node.id, FormatBreakChoice::Split);
        for (size_t index = 0; index < node.operands.size(); ++index) {
            NodeResult operand =
                Solve(*node.operands[index], result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, operand);
            if (index < node.operators.size()) {
                result = AddToken(result, node.operators[index]);
                if (
                    node.operators[index].token.kind == PrintTokenKind::Known &&
                    node.operators[index].token.known == KnownToken::Colon
                ) {
                    result = AddBreak(result, indentLevel + 1, node.structuralDepth);
                }
            }
        }
        return result;
    }

    NodeResult SolveSingleTernary(
        const FormatBreakNode& node,
        int column,
        int indentLevel,
        bool lineHasText,
        FormatBreakChoice choice
    ) {
        const bool breakAfterQuestion =
            choice == FormatBreakChoice::TernaryBreakAfterQuestion || choice == FormatBreakChoice::Split;
        const bool breakAfterColon =
            choice == FormatBreakChoice::TernaryBreakAfterColon || choice == FormatBreakChoice::Split;
        const int continuationIndent = node.flatSplitIndent ? indentLevel : indentLevel + 1;
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        result.choices.emplace(node.id, choice);
        for (size_t index = 0; index < node.operands.size(); ++index) {
            NodeResult operand =
                Solve(*node.operands[index], result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, operand);
            if (index < node.operators.size()) {
                result = AddToken(result, node.operators[index]);
                if ((index == 0 && breakAfterQuestion) || (index == 1 && breakAfterColon)) {
                    result = AddBreak(result, continuationIndent, node.structuralDepth);
                }
            }
        }
        return result;
    }

    NodeResult SolveChain(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult compact = SolveChainCompact(node, column, indentLevel, lineHasText);
        if (node.chainKind == FormatBreakChainKind::StreamBeforeOperator) {
            NodeResult compactTail =
                SolveStreamSplit(node, column, indentLevel, lineHasText, FormatBreakChoice::StreamCompactTail);
            NodeResult split = SolveStreamSplit(node, column, indentLevel, lineHasText, FormatBreakChoice::Split);
            NodeResult best = Better(compactTail, compact) ? compactTail : compact;
            if (compact.valid && best.valid && CompactLineEndsOverLimit(compact) && best.maxOverflow == 0) {
                if (compactTail.valid && compactTail.maxOverflow == 0) {
                    return compactTail;
                }
                return best;
            }
            return Better(split, best) ? split : best;
        }
        if (node.chainKind == FormatBreakChainKind::Ternary && node.operators.size() > 2) {
            NodeResult split = SolveTernaryChainSplit(node, column, indentLevel, lineHasText);
            if (compact.valid && split.valid && compact.extraLines > 0) {
                return split;
            }
            return Better(split, compact) ? split : compact;
        }
        if (node.chainKind == FormatBreakChainKind::Ternary && node.operators.size() == 2) {
            NodeResult best = compact;
            std::vector<NodeResult>
                alternatives{
                    SolveSingleTernary(
                        node,
                        column,
                        indentLevel,
                        lineHasText,
                        FormatBreakChoice::TernaryBreakAfterQuestion
                    ),
                    SolveSingleTernary(
                        node,
                        column,
                        indentLevel,
                        lineHasText,
                        FormatBreakChoice::TernaryBreakAfterColon
                    ),
                    SolveSingleTernary(node, column, indentLevel, lineHasText, FormatBreakChoice::Split)
                };
            for (const NodeResult& alternative : alternatives) {
                if (Better(alternative, best)) {
                    best = alternative;
                }
            }
            if (compact.valid && best.valid && CompactLineEndsOverLimit(compact) && best.maxOverflow == 0) {
                return best;
            }
            return best;
        }
        NodeResult split = SolveChainSplitAfterOperator(node, column, indentLevel, lineHasText);
        if (
            compact.valid &&
            split.valid &&
            compact.extraLines > 0 &&
            (IsCommaChain(node) || (node.chainKind != FormatBreakChainKind::Ternary && node.operators.size() > 1))
        ) {
            return split;
        }
        if (compact.valid && split.valid && CompactLineEndsOverLimit(compact) && split.maxOverflow == 0) {
            return split;
        }
        return Better(split, compact) ? split : compact;
    }

    NodeResult SolveAdjacentStringsCompact(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        result.choices.emplace(node.id, FormatBreakChoice::Compact);
        for (const std::unique_ptr<FormatBreakNode>& operand : node.operands) {
            NodeResult item = Solve(*operand, result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
        }
        return result;
    }

    NodeResult SolveAdjacentStringsSplit(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        result.choices.emplace(node.id, FormatBreakChoice::Split);
        for (size_t index = 0; index < node.operands.size(); ++index) {
            if (index > 0) {
                result = AddBreak(result, indentLevel + 1, node.structuralDepth);
            }
            NodeResult item =
                Solve(*node.operands[index], result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
        }
        return result;
    }

    NodeResult SolveAdjacentStrings(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult compact = SolveAdjacentStringsCompact(node, column, indentLevel, lineHasText);
        NodeResult split = SolveAdjacentStringsSplit(node, column, indentLevel, lineHasText);
        if (node.forceSplit && split.valid) {
            return split;
        }
        return Better(split, compact) ? split : compact;
    }
};

}  // namespace

FormatBreakSolution SolveFormatBreaks(
    const FormatterConfig& config,
    const FormatBreakModel& model,
    int startColumn,
    int indentLevel,
    int indentWidth
) {
    FormatBreakSolution solution;
    if (!model.root) {
        return solution;
    }
    Solver solver(config, indentWidth);
    NodeResult result = solver.Solve(*model.root, startColumn, indentLevel, startColumn > indentLevel * indentWidth);
    solution.choices = std::move(result.choices);
    return solution;
}
