#include "tools/impl/format_break_solver.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "tools/impl/format_break_model_inline_helpers.h"

namespace {

struct SolveKey {
    int nodeId = 0;
    int column = 0;
    int indentLevel = 0;
    bool lineHasText = false;

    friend bool operator==(const SolveKey& left, const SolveKey& right) {
        return left.nodeId == right.nodeId &&
            left.column == right.column &&
            left.indentLevel == right.indentLevel &&
            left.lineHasText == right.lineHasText;
    }
};

struct SolveKeyHash {
    size_t operator()(const SolveKey& key) const {
        size_t hash = static_cast<size_t>(key.nodeId);
        hash = hash * 131u + static_cast<size_t>(key.column);
        hash = hash * 131u + static_cast<size_t>(key.indentLevel);
        hash = hash * 2u + static_cast<size_t>(key.lineHasText);
        return hash;
    }
};

struct NodeResult {
    bool valid = false;
    int endColumn = 0;
    int endIndentLevel = 0;
    bool endLineHasText = false;
    int extraLines = 0;
    int maxOverflow = 0;
    int overflowLines = 0;
    int deepestBreakIndent = -1;
    int deepestBreakDepth = -1;
    const struct ChoiceTree* choices = nullptr;
};

class NodeResults {
public:
    using iterator = NodeResult*;
    using const_iterator = const NodeResult*;

    NodeResults() = default;

    NodeResults(std::initializer_list<NodeResult> values) {
        for (NodeResult value : values) {
            push_back(std::move(value));
        }
    }

    NodeResults(const NodeResults& other) {
        for (const NodeResult& value : other) {
            push_back(value);
        }
    }

    NodeResults(NodeResults&& other) noexcept {
        MoveFrom(std::move(other));
    }

    ~NodeResults() {
        clear();
    }

    NodeResults& operator=(const NodeResults& other) {
        if (this != &other) {
            clear();
            usingHeap_ = false;
            for (const NodeResult& value : other) {
                push_back(value);
            }
        }
        return *this;
    }

    NodeResults& operator=(NodeResults&& other) noexcept {
        if (this != &other) {
            clear();
            usingHeap_ = false;
            MoveFrom(std::move(other));
        }
        return *this;
    }

    iterator begin() {
        return usingHeap_ ? heap_.data() : InlineData();
    }

    iterator end() {
        return begin() + size();
    }

    const_iterator begin() const {
        return usingHeap_ ? heap_.data() : InlineData();
    }

    const_iterator end() const {
        return begin() + size();
    }

    bool empty() const {
        return size() == 0;
    }

    size_t size() const {
        return usingHeap_ ? heap_.size() : inlineSize_;
    }

    NodeResult& operator[](size_t index) {
        return begin()[index];
    }

    const NodeResult& operator[](size_t index) const {
        return begin()[index];
    }

    void push_back(NodeResult value) {
        if (usingHeap_) {
            heap_.push_back(std::move(value));
            return;
        }
        if (inlineSize_ < kInlineCapacity) {
            std::construct_at(InlineData() + inlineSize_, std::move(value));
            ++inlineSize_;
            return;
        }
        MoveInlineToHeap();
        heap_.push_back(std::move(value));
    }

    iterator erase(iterator it) {
        const size_t index = static_cast<size_t>(it - begin());
        if (usingHeap_) {
            heap_.erase(heap_.begin() + static_cast<std::ptrdiff_t>(index));
            return heap_.data() + index;
        }
        for (size_t cursor = index + 1; cursor < inlineSize_; ++cursor) {
            InlineData()[cursor - 1] = std::move(InlineData()[cursor]);
        }
        --inlineSize_;
        std::destroy_at(InlineData() + inlineSize_);
        return InlineData() + index;
    }

    void clear() {
        if (usingHeap_) {
            heap_.clear();
            return;
        }
        for (size_t index = 0; index < inlineSize_; ++index) {
            std::destroy_at(InlineData() + index);
        }
        inlineSize_ = 0;
    }

private:
    static constexpr size_t kInlineCapacity = 8;

    NodeResult* InlineData() {
        return std::launder(reinterpret_cast<NodeResult*>(inlineStorage_));
    }

    const NodeResult* InlineData() const {
        return std::launder(reinterpret_cast<const NodeResult*>(inlineStorage_));
    }

    void MoveFrom(NodeResults&& other) {
        if (other.usingHeap_) {
            heap_ = std::move(other.heap_);
            usingHeap_ = true;
            other.usingHeap_ = false;
            return;
        }
        for (NodeResult& value : other) {
            push_back(std::move(value));
        }
        other.clear();
    }

    void MoveInlineToHeap() {
        heap_.reserve(kInlineCapacity * 2);
        for (size_t index = 0; index < inlineSize_; ++index) {
            heap_.push_back(std::move(InlineData()[index]));
            std::destroy_at(InlineData() + index);
        }
        inlineSize_ = 0;
        usingHeap_ = true;
    }

    alignas(NodeResult) std::byte inlineStorage_[sizeof(NodeResult) * kInlineCapacity];
    size_t inlineSize_ = 0;
    bool usingHeap_ = false;
    std::vector<NodeResult> heap_;
};

struct ChoiceTree {
    const ChoiceTree* left = nullptr;
    const ChoiceTree* right = nullptr;
    int nodeId = 0;
    FormatBreakChoice choice = FormatBreakChoice::Compact;
    bool leaf = false;
};

struct DelimiterStackView {
    std::vector<const FormatBreakNode*> delimiters;
    const FormatBreakNode* leaf = nullptr;
};

struct DelimiterStackRun {
    size_t begin = 0;
    size_t end = 0;
    int indentLevel = 0;
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

        if (std::optional<NodeResult> compact = SolveCompactOneLine(node, column, indentLevel, lineHasText)) {
            memo_.emplace(key, *compact);
            return *compact;
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
            case FormatBreakNodeKind::FunctionSignature:
                result = SolveFunctionSignature(node, column, indentLevel, lineHasText);
                break;
            case FormatBreakNodeKind::BodyHeader:
                result = SolveBodyHeader(node, column, indentLevel, lineHasText);
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
    std::unordered_map<SolveKey, NodeResult, SolveKeyHash> memo_;
    std::deque<ChoiceTree> choiceArena_;

    int IndentColumn(int indentLevel) const {
        return std::max(0, indentLevel) * indentWidth_;
    }

    static bool HasTrailingComment(const FormatBreakNode& node, size_t index) {
        return index < node.items.size() && IsCommentToken(FormatBreakTokenKind(node.items[index].trailingComment));
    }

    static bool HasBlankLineBeforeItem(const FormatBreakNode& node, size_t index) {
        return index < node.items.size() && node.items[index].blankLineBefore;
    }

    static int SpaceBeforeToken(const FormatBreakToken& token, bool lineHasText) {
        if (!lineHasText) {
            return 0;
        }
        if (IsCommentToken(FormatBreakTokenKind(token))) {
            return 2;
        }
        return token.spaceBefore ? 1 : 0;
    }

    NodeResult SolveToken(const FormatBreakToken& token, int column, int indentLevel, bool lineHasText) const {
        if (token.contextOnly) {
            return {.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        }
        const int space = SpaceBeforeToken(token, lineHasText);
        const int width = FormatTokenWidth(FormatBreakTokenValue(token));
        const int endColumn = column + space + width;
        const bool endLineHasText = lineHasText || width > 0;
        const bool lineWasOverLimit = lineHasText && column > config_.columnLimit;
        const bool lineIsOverLimit = endLineHasText && endColumn > config_.columnLimit;
        return {
            .valid = true,
            .endColumn = endColumn,
            .endIndentLevel = indentLevel,
            .endLineHasText = endLineHasText,
            .maxOverflow = std::max(0, endColumn - config_.columnLimit),
            .overflowLines = !lineWasOverLimit && lineIsOverLimit ? 1 : 0
        };
    }

    const ChoiceTree* MakeChoice(int nodeId, FormatBreakChoice choice) {
        choiceArena_.push_back(ChoiceTree{.nodeId = nodeId, .choice = choice, .leaf = true});
        return &choiceArena_.back();
    }

    const ChoiceTree* ConcatChoices(const ChoiceTree* left, const ChoiceTree* right) {
        if (left == nullptr) {
            return right;
        }
        if (right == nullptr) {
            return left;
        }
        choiceArena_.push_back(ChoiceTree{.left = left, .right = right});
        return &choiceArena_.back();
    }

    void AddChoice(NodeResult& result, int nodeId, FormatBreakChoice choice) {
        result.choices = ConcatChoices(result.choices, MakeChoice(nodeId, choice));
    }

    void Merge(NodeResult& left, const NodeResult& right) {
        left.valid = left.valid && right.valid;
        left.endColumn = right.endColumn;
        left.endIndentLevel = right.endIndentLevel;
        left.endLineHasText = right.endLineHasText;
        left.extraLines += right.extraLines;
        left.maxOverflow = std::max(left.maxOverflow, right.maxOverflow);
        left.overflowLines += right.overflowLines;
        left.deepestBreakIndent = std::max(left.deepestBreakIndent, right.deepestBreakIndent);
        left.deepestBreakDepth = std::max(left.deepestBreakDepth, right.deepestBreakDepth);
        left.choices = ConcatChoices(left.choices, right.choices);
    }

    NodeResults SolveAlternatives(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        switch (node.kind) {
            case FormatBreakNodeKind::Token:
                return {SolveToken(node.token, column, indentLevel, lineHasText)};
            case FormatBreakNodeKind::Sequence:
                return SolveChildrenAlternatives(node.children, column, indentLevel, lineHasText);
            case FormatBreakNodeKind::Delimited: {
                NodeResults alternatives;
                for (NodeResult compact : SolveDelimitedCompactAlternatives(node, column, indentLevel, lineHasText)) {
                    if (!node.forceSplit && !(compact.valid && compact.extraLines > 0 && (
                        !lineHasText ||
                        ContainsForceSplitAdjacentStrings(node) ||
                        !CanKeepDelimitedCompactWithExtraLines(node, compact)
                    ))) {
                        alternatives.push_back(std::move(compact));
                    }
                }
                NodeResult split = SolveDelimitedSplit(node, column, indentLevel, lineHasText);
                if (split.valid) {
                    alternatives.push_back(split);
                }
                return alternatives;
            }
            case FormatBreakNodeKind::PrefixList: {
                NodeResults alternatives;
                NodeResult compact = SolvePrefixListCompact(node, column, indentLevel, lineHasText);
                if (!node.forceSplit && !(compact.valid && compact.extraLines > 0)) {
                    alternatives.push_back(compact);
                }
                NodeResult split = SolvePrefixListSplit(node, column, indentLevel, lineHasText);
                if (split.valid) {
                    alternatives.push_back(split);
                }
                return alternatives;
            }
            case FormatBreakNodeKind::FunctionSignature:
                return SolveFunctionSignatureAlternatives(node, column, indentLevel, lineHasText);
            case FormatBreakNodeKind::BodyHeader:
                return SolveBodyHeaderAlternatives(node, column, indentLevel, lineHasText);
            case FormatBreakNodeKind::Chain: {
                NodeResults alternatives;
                for (NodeResult compact : SolveChainCompactAlternatives(node, column, indentLevel, lineHasText)) {
                    if (
                        !compact.valid ||
                        compact.extraLines == 0 ||
                        !RequiresChainCompactExtraLinesGuard(node) ||
                        CanKeepChainCompactWithExtraLines(node, compact)
                    ) {
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
                NodeResults alternatives;
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

    NodeResult
        SolveChildren(std::span<FormatBreakNode* const> children, int column, int indentLevel, bool lineHasText)
    {
        NodeResult best;
        for (const NodeResult& candidate : SolveChildrenAlternatives(children, column, indentLevel, lineHasText)) {
            if (Better(candidate, best)) {
                best = candidate;
            }
        }
        return best;
    }

    NodeResults SolveChildrenAlternatives(
        std::span<FormatBreakNode* const> children,
        int column,
        int indentLevel,
        bool lineHasText
    ) {
        std::vector<const FormatBreakNode*> sequenceChildren;
        AppendSequenceChildren(children, sequenceChildren);
        NodeResults
            current{{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText}};
        for (const FormatBreakNode* child : sequenceChildren) {
            NodeResults next;
            for (const NodeResult& prefix : current) {
                for (const NodeResult& childResult : SolveAlternatives(
                    *child,
                    prefix.endColumn,
                    prefix.endIndentLevel,
                    prefix.endLineHasText
                )) {
                    if (!childResult.valid) {
                        continue;
                    }
                    NodeResult candidate = prefix;
                    Merge(candidate, childResult);
                    AddPrunedResult(next, std::move(candidate));
                }
            }
            SortPrunedResults(next);
            current = std::move(next);
        }
        return current;
    }

    static void
        AppendSequenceChildren(std::span<FormatBreakNode* const> children, std::vector<const FormatBreakNode*>& output)
    {
        for (const FormatBreakNode* child : children) {
            if (!child) {
                continue;
            }
            if (child->kind == FormatBreakNodeKind::Sequence) {
                AppendSequenceChildren(child->children, output);
                continue;
            }
            output.push_back(child);
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

    NodeResult AddListBreak(NodeResult result, int indentLevel, int structuralDepth, bool blankLine) const {
        result = AddBreak(result, indentLevel, structuralDepth);
        if (blankLine) {
            result = AddBreak(result, indentLevel, structuralDepth);
        }
        return result;
    }

    NodeResult AddToken(NodeResult result, const FormatBreakToken& token) {
        NodeResult tokenResult = SolveToken(token, result.endColumn, result.endIndentLevel, result.endLineHasText);
        Merge(result, tokenResult);
        return result;
    }

    NodeResult
        SolveListItemWithSuffix(const FormatBreakListItem& listItem, int column, int indentLevel, bool lineHasText)
    {
        if (listItem.node == nullptr) {
            return {};
        }
        NodeResult best;
        for (NodeResult item : SolveAlternatives(*listItem.node, column, indentLevel, lineHasText)) {
            if (!item.valid) {
                continue;
            }
            if (FormatBreakTokenKind(listItem.separator) == PrintTokenKind::Known) {
                item = AddToken(item, listItem.separator);
            }
            if (IsCommentToken(FormatBreakTokenKind(listItem.trailingComment))) {
                item = AddToken(item, listItem.trailingComment);
            }
            if (Better(item, best)) {
                best = item;
            }
        }
        return best;
    }

    bool Better(const NodeResult& candidate, const NodeResult& incumbent) const {
        if (!candidate.valid) {
            return false;
        }
        if (!incumbent.valid) {
            return true;
        }
        if (candidate.maxOverflow != incumbent.maxOverflow) {
            return candidate.maxOverflow < incumbent.maxOverflow;
        }
        if (candidate.overflowLines != incumbent.overflowLines) {
            return candidate.overflowLines < incumbent.overflowLines;
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

    static bool SameResultState(const NodeResult& left, const NodeResult& right) {
        return left.endColumn == right.endColumn &&
            left.endIndentLevel == right.endIndentLevel &&
            left.endLineHasText == right.endLineHasText;
    }

    static bool DominatesResult(const NodeResult& left, const NodeResult& right) {
        if (left.endIndentLevel != right.endIndentLevel || left.endLineHasText != right.endLineHasText) {
            return false;
        }
        if (left.endColumn > right.endColumn) {
            return false;
        }
        if (
            left.maxOverflow > right.maxOverflow ||
            left.overflowLines > right.overflowLines ||
            left.extraLines > right.extraLines ||
            left.deepestBreakIndent > right.deepestBreakIndent ||
            left.deepestBreakDepth > right.deepestBreakDepth
        ) {
            return false;
        }
        return left.maxOverflow < right.maxOverflow ||
            left.overflowLines < right.overflowLines ||
            left.extraLines < right.extraLines ||
            left.deepestBreakIndent < right.deepestBreakIndent ||
            left.deepestBreakDepth < right.deepestBreakDepth;
    }

    static bool ResultStateLess(const NodeResult& left, const NodeResult& right) {
        if (left.endColumn != right.endColumn) {
            return left.endColumn < right.endColumn;
        }
        if (left.endIndentLevel != right.endIndentLevel) {
            return left.endIndentLevel < right.endIndentLevel;
        }
        return left.endLineHasText < right.endLineHasText;
    }

    void AddPrunedResult(NodeResults& results, NodeResult candidate) const {
        if (!candidate.valid) {
            return;
        }
        for (auto it = results.begin(); it != results.end();) {
            if (SameResultState(*it, candidate)) {
                if (Better(candidate, *it)) {
                    *it = std::move(candidate);
                }
                return;
            }
            if (DominatesResult(*it, candidate)) {
                return;
            }
            if (DominatesResult(candidate, *it)) {
                it = results.erase(it);
                continue;
            }
            ++it;
        }
        results.push_back(std::move(candidate));
    }

    static void SortPrunedResults(NodeResults& results) {
        std::sort(results.begin(), results.end(), ResultStateLess);
    }

    bool CompactLineEndsOverLimit(const NodeResult& compact) const {
        return compact.endLineHasText && compact.endColumn > config_.columnLimit;
    }

    std::optional<NodeResult>
        SolveCompactOneLine(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) const
    {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        if (!AppendCompactOneLine(node, result)) {
            return std::nullopt;
        }
        if (result.endLineHasText && result.endColumn > config_.columnLimit) {
            return std::nullopt;
        }
        return result;
    }

    bool AddCompactToken(NodeResult& result, const FormatBreakToken& token) const {
        if (token.contextOnly) {
            return true;
        }
        const PrintToken& printToken = FormatBreakTokenValue(token);
        if (printToken.kind == PrintTokenKind::BlankLine || IsCommentToken(printToken.kind)) {
            return false;
        }
        const int space = SpaceBeforeToken(token, result.endLineHasText);
        const int width = FormatTokenWidth(printToken);
        result.endColumn += space + width;
        result.endLineHasText = result.endLineHasText || width > 0;
        return !result.endLineHasText || result.endColumn <= config_.columnLimit;
    }

    bool AppendCompactOneLine(const FormatBreakNode& node, NodeResult& result) const {
        if (node.forceSplit) {
            return false;
        }
        switch (node.kind) {
            case FormatBreakNodeKind::Token:
                return AddCompactToken(result, node.token);
            case FormatBreakNodeKind::Sequence:
            case FormatBreakNodeKind::FunctionSignature:
            case FormatBreakNodeKind::BodyHeader:
                for (const FormatBreakNode* child : node.children) {
                    if (child != nullptr && !AppendCompactOneLine(*child, result)) {
                        return false;
                    }
                }
                return true;
            case FormatBreakNodeKind::Delimited:
                if (node.children.size() < 2 || !AppendCompactOneLine(*node.children[0], result)) {
                    return false;
                }
                for (size_t index = 0; index < node.items.size(); ++index) {
                    const FormatBreakListItem& item = node.items[index];
                    if (item.node != nullptr && !AppendCompactOneLine(*item.node, result)) {
                        return false;
                    }
                    if (
                        FormatBreakTokenKind(item.separator) == PrintTokenKind::Known &&
                        !AddCompactToken(result, item.separator)
                    ) {
                        return false;
                    }
                    if (HasTrailingComment(node, index)) {
                        return false;
                    }
                }
                return AppendCompactOneLine(*node.children[1], result);
            case FormatBreakNodeKind::PrefixList:
                if (node.children.empty() || !AppendCompactOneLine(*node.children[0], result)) {
                    return false;
                }
                for (size_t index = 0; index < node.items.size(); ++index) {
                    const FormatBreakListItem& item = node.items[index];
                    if (item.node != nullptr && !AppendCompactOneLine(*item.node, result)) {
                        return false;
                    }
                    if (
                        FormatBreakTokenKind(item.separator) == PrintTokenKind::Known &&
                        !AddCompactToken(result, item.separator)
                    ) {
                        return false;
                    }
                    if (HasTrailingComment(node, index)) {
                        return false;
                    }
                }
                return true;
            case FormatBreakNodeKind::Chain:
                for (size_t index = 0; index < node.operands.size(); ++index) {
                    if (node.operands[index] != nullptr && !AppendCompactOneLine(*node.operands[index], result)) {
                        return false;
                    }
                    if (index < node.operators.size() && !AddCompactToken(result, node.operators[index])) {
                        return false;
                    }
                }
                return true;
            case FormatBreakNodeKind::AdjacentStrings:
                for (const FormatBreakNode* operand : node.operands) {
                    if (operand != nullptr && !AppendCompactOneLine(*operand, result)) {
                        return false;
                    }
                }
                return true;
        }
        return false;
    }

    static bool HasRealSeparators(const FormatBreakNode& node) {
        return std::any_of(node.items.begin(), node.items.end(), [](const FormatBreakListItem& item) {
            return FormatBreakTokenKind(item.separator) == PrintTokenKind::Known;
        });
    }

    static bool IsDelimiterStackItem(const FormatBreakNode& node) {
        return node.kind == FormatBreakNodeKind::Delimited && node.items.size() == 1 && !HasRealSeparators(node);
    }

    static std::optional<DelimiterStackView> CollectDelimiterStack(const FormatBreakNode& node) {
        if (!IsDelimiterStackItem(node) || node.children.size() < 2 || node.forceSplit) {
            return std::nullopt;
        }
        DelimiterStackView stack;
        const FormatBreakNode* current = &node;
        while (current != nullptr) {
            stack.delimiters.push_back(current);
            const FormatBreakNode* item = current->items.front().node;
            if (
                item == nullptr ||
                !IsDelimiterStackItem(*item) ||
                item->delimiterKind != node.delimiterKind ||
                item->children.size() < 2 ||
                item->forceSplit
            ) {
                stack.leaf = item;
                break;
            }
            current = item;
        }
        return stack.leaf != nullptr && stack.delimiters.size() > 1 ? std::optional(stack) : std::nullopt;
    }

    int TokenColumnAdvance(const FormatBreakToken& token, bool lineHasText) const {
        return SpaceBeforeToken(token, lineHasText) + FormatTokenWidth(FormatBreakTokenValue(token));
    }

    bool TokenWouldOverflow(const NodeResult& result, const FormatBreakToken& token) const {
        return result.endLineHasText &&
            result.endColumn <= config_.columnLimit &&
            result.endColumn + TokenColumnAdvance(token, result.endLineHasText) > config_.columnLimit;
    }

    NodeResults SolveDelimitedCompactItemAlternatives(
        const FormatBreakNode& item,
        int column,
        int indentLevel,
        bool lineHasText
    ) {
        if (IsDelimiterStackItem(item)) {
            return {Solve(item, column, indentLevel, lineHasText)};
        }
        return SolveAlternatives(item, column, indentLevel, lineHasText);
    }

    NodeResults
        SolveDelimitedCompactAlternatives(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText)
    {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::Compact);
        result = AddToken(result, node.children[0]->token);
        NodeResults current{result};
        for (size_t index = 0; index < node.items.size(); ++index) {
            const FormatBreakListItem& listItem = node.items[index];
            NodeResults nextByState;
            const bool canKeepMultilineItem = node.items.size() == 1 || index + 1 == node.items.size();
            for (const NodeResult& prefix : current) {
                for (const NodeResult& item : SolveDelimitedCompactItemAlternatives(
                    *listItem.node,
                    prefix.endColumn,
                    prefix.endIndentLevel,
                    prefix.endLineHasText
                )) {
                    if (!item.valid) {
                        continue;
                    }
                    if (!canKeepMultilineItem && item.extraLines > 0) {
                        continue;
                    }
                    NodeResult next = prefix;
                    Merge(next, item);
                    if (FormatBreakTokenKind(listItem.separator) == PrintTokenKind::Known) {
                        next = AddToken(next, listItem.separator);
                    }
                    if (HasTrailingComment(node, index)) {
                        next = AddToken(next, listItem.trailingComment);
                    }
                    AddPrunedResult(nextByState, std::move(next));
                }
            }
            SortPrunedResults(nextByState);
            current = std::move(nextByState);
        }

        NodeResults alternatives;
        for (const NodeResult& candidate : current) {
            AddPrunedResult(alternatives, AddToken(candidate, node.children[1]->token));
        }
        SortPrunedResults(alternatives);
        return alternatives;
    }

    NodeResult SolveDelimitedCompact(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult best;
        for (const NodeResult& candidate : SolveDelimitedCompactAlternatives(node, column, indentLevel, lineHasText)) {
            if (Better(candidate, best)) {
                best = candidate;
            }
        }
        return best;
    }

    NodeResult SolveDelimitedSplit(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        if (node.items.empty()) {
            return {};
        }
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::Split);
        result = AddToken(result, node.children[0]->token);
        result = AddListBreak(result, indentLevel + 1, node.structuralDepth, HasBlankLineBeforeItem(node, 0));
        for (size_t index = 0; index < node.items.size(); ++index) {
            const FormatBreakListItem& listItem = node.items[index];
            NodeResult item =
                SolveListItemWithSuffix(listItem, result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
            const bool hasNextItem = index + 1 < node.items.size();
            result = AddListBreak(
                result,
                hasNextItem ? indentLevel + 1 : indentLevel,
                node.structuralDepth,
                hasNextItem && HasBlankLineBeforeItem(node, index + 1)
            );
        }
        result = AddToken(result, node.children[1]->token);
        return result;
    }

    NodeResult SolveDelimited(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        if (std::optional<DelimiterStackView> stack = CollectDelimiterStack(node)) {
            return SolveDelimiterStack(node, *stack, column, indentLevel, lineHasText);
        }
        NodeResult compact = SolveDelimitedCompact(node, column, indentLevel, lineHasText);
        NodeResult split = SolveDelimitedSplit(node, column, indentLevel, lineHasText);
        if (node.forceSplit && split.valid) {
            return split;
        }
        if (!lineHasText && compact.valid && split.valid && compact.extraLines > 0) {
            return split;
        }
        if (compact.valid && split.valid && CompactLineEndsOverLimit(compact) && split.maxOverflow == 0) {
            return split;
        }
        if (compact.valid && split.valid && compact.extraLines > 0 && ContainsForceSplitAdjacentStrings(node)) {
            return split;
        }
        if (compact.valid && split.valid && compact.extraLines > 0 && !CanKeepDelimitedCompactWithExtraLines(
            node,
            compact
        )) {
            return split;
        }
        return Better(split, compact) ? split : compact;
    }

    NodeResult SolveDelimiterStack(
        const FormatBreakNode& node,
        const DelimiterStackView& stack,
        int column,
        int indentLevel,
        bool lineHasText
    ) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::SplitDelimiterStack);

        int currentLineIndent = indentLevel;
        int nextOpenIndent = indentLevel + 1;
        std::vector<DelimiterStackRun> delimiterRuns;
        delimiterRuns.reserve(stack.delimiters.size());
        for (size_t index = 0; index < stack.delimiters.size(); ++index) {
            const FormatBreakNode* delimiter = stack.delimiters[index];
            const FormatBreakToken& open = delimiter->children.front()->token;
            // Avoid starting a new overflow line for an opener. If this line is
            // already over the limit, keeping the opener avoids another overflow line.
            if (TokenWouldOverflow(result, open)) {
                currentLineIndent = nextOpenIndent;
                result = AddBreak(result, currentLineIndent, node.structuralDepth);
                ++nextOpenIndent;
            }
            if (delimiterRuns.empty() || delimiterRuns.back().indentLevel != currentLineIndent) {
                delimiterRuns.push_back(
                    DelimiterStackRun{.begin = index, .end = index, .indentLevel = currentLineIndent}
                );
            }
            delimiterRuns.back().end = index + 1;
            result = AddToken(result, open);
        }

        NodeResult leaf = Solve(*stack.leaf, result.endColumn, result.endIndentLevel, result.endLineHasText);
        if (leaf.valid && result.endLineHasText && (leaf.extraLines > 0 || leaf.maxOverflow > 0)) {
            NodeResult broken = AddBreak(result, nextOpenIndent, node.structuralDepth);
            leaf = Solve(*stack.leaf, broken.endColumn, broken.endIndentLevel, broken.endLineHasText);
            result = broken;
        }
        if (!leaf.valid) {
            return {};
        }
        const bool leafHasBreaks = leaf.valid && leaf.extraLines > 0;
        Merge(result, leaf);

        for (size_t runIndex = delimiterRuns.size(); runIndex-- > 0;) {
            const DelimiterStackRun& run = delimiterRuns[runIndex];
            const bool firstClosingRun = runIndex + 1 == delimiterRuns.size();
            if (result.endLineHasText && (leafHasBreaks || !firstClosingRun)) {
                result = AddBreak(result, run.indentLevel, node.structuralDepth);
            }
            for (size_t index = run.end; index-- > run.begin;) {
                result = AddToken(result, stack.delimiters[index]->children.back()->token);
            }
        }
        return result;
    }

    static FormatBreakChoice ChoiceFor(const NodeResult& result, const FormatBreakNode& node) {
        const std::optional<FormatBreakChoice> choice = FindChoice(result.choices, node.id);
        return choice.value_or(FormatBreakChoice::Compact);
    }

    static bool IsBreakingChoice(FormatBreakChoice choice) {
        return choice != FormatBreakChoice::Compact;
    }

    static std::optional<FormatBreakChoice> FindChoice(const ChoiceTree* tree, int nodeId) {
        if (tree == nullptr) {
            return std::nullopt;
        }
        if (tree->leaf) {
            return tree->nodeId == nodeId ? std::optional(tree->choice) : std::nullopt;
        }
        if (std::optional<FormatBreakChoice> choice = FindChoice(tree->right, nodeId)) {
            return choice;
        }
        return FindChoice(tree->left, nodeId);
    }

    static bool HasSelectedBreak(const FormatBreakNode& node, const NodeResult& result) {
        if (node.kind != FormatBreakNodeKind::Token && IsBreakingChoice(ChoiceFor(result, node))) {
            return true;
        }
        for (const FormatBreakNode* child : node.children) {
            if (child != nullptr && HasSelectedBreak(*child, result)) {
                return true;
            }
        }
        for (const FormatBreakListItem& item : node.items) {
            if (item.node != nullptr && HasSelectedBreak(*item.node, result)) {
                return true;
            }
        }
        for (const FormatBreakNode* operand : node.operands) {
            if (operand != nullptr && HasSelectedBreak(*operand, result)) {
                return true;
            }
        }
        return false;
    }

    static bool IsTailExpansionBreak(const FormatBreakNode& node, const NodeResult& compact) {
        return (node.kind == FormatBreakNodeKind::Delimited || node.kind == FormatBreakNodeKind::BodyHeader) &&
            IsBreakingChoice(ChoiceFor(compact, node));
    }

    static bool CanKeepCompactPrefixEndingInTailExpansion(const FormatBreakNode& node, const NodeResult& compact) {
        if (IsTailExpansionBreak(node, compact)) {
            return true;
        }
        if (node.kind == FormatBreakNodeKind::Delimited) {
            if (IsBreakingChoice(ChoiceFor(compact, node))) {
                return true;
            }
            if (node.items.size() != 1 || HasRealSeparators(node)) {
                return false;
            }
            const FormatBreakNode* item = node.items.front().node;
            return item != nullptr && CanKeepCompactPrefixEndingInTailExpansion(*item, compact);
        }
        if (node.kind == FormatBreakNodeKind::Chain) {
            if (node.operands.empty() || ChoiceFor(compact, node) != FormatBreakChoice::Compact) {
                return false;
            }
            for (size_t index = 0; index + 1 < node.operands.size(); ++index) {
                if (node.operands[index] != nullptr && HasSelectedBreak(*node.operands[index], compact)) {
                    return false;
                }
            }
            return CanKeepCompactPrefixEndingInTailExpansion(*node.operands.back(), compact);
        }
        if (node.kind == FormatBreakNodeKind::BodyHeader) {
            if (node.children.empty() || ChoiceFor(compact, node) != FormatBreakChoice::Compact) {
                return false;
            }
            for (size_t index = 0; index + 1 < node.children.size(); ++index) {
                if (node.children[index] != nullptr && HasSelectedBreak(*node.children[index], compact)) {
                    return false;
                }
            }
            return CanKeepCompactPrefixEndingInTailExpansion(*node.children.back(), compact);
        }
        if (node.kind != FormatBreakNodeKind::Sequence || node.children.empty()) {
            return false;
        }
        for (size_t index = 0; index + 1 < node.children.size(); ++index) {
            if (node.children[index] != nullptr && HasSelectedBreak(*node.children[index], compact)) {
                return false;
            }
        }
        return CanKeepCompactPrefixEndingInTailExpansion(*node.children.back(), compact);
    }

    static bool CanKeepDelimitedCompactWithExtraLines(const FormatBreakNode& node, const NodeResult& compact) {
        if (node.items.empty()) {
            return false;
        }
        if (node.items.size() == 1 && !HasRealSeparators(node)) {
            const FormatBreakNode* item = node.items.front().node;
            return item != nullptr && CanKeepCompactPrefixEndingInTailExpansion(*item, compact);
        }
        if (!HasRealSeparators(node)) {
            return false;
        }
        for (size_t index = 0; index + 1 < node.items.size(); ++index) {
            if (node.items[index].node != nullptr && HasSelectedBreak(*node.items[index].node, compact)) {
                return false;
            }
        }
        const FormatBreakNode* tail = node.items.back().node;
        if (tail == nullptr) {
            return false;
        }
        return CanKeepCompactPrefixEndingInTailExpansion(*tail, compact);
    }

    static bool CanKeepChainCompactWithExtraLines(const FormatBreakNode& node, const NodeResult& compact) {
        if (node.operands.empty() || ChoiceFor(compact, node) != FormatBreakChoice::Compact) {
            return false;
        }
        for (size_t index = 0; index + 1 < node.operands.size(); ++index) {
            if (node.operands[index] != nullptr && HasSelectedBreak(*node.operands[index], compact)) {
                return false;
            }
        }
        const FormatBreakNode* tail = node.operands.back();
        return tail != nullptr && CanKeepCompactPrefixEndingInTailExpansion(*tail, compact);
    }

    static bool IsFormatterOwnedChainOperator(const FormatBreakToken& token) {
        return FormatBreakTokenKind(token) == PrintTokenKind::Known &&
            SyntaxNodeKindHasClass(FormatBreakTokenSyntaxKind(token), TokenClass::ChainOperator);
    }

    static bool IsFormatterOwnedChain(const FormatBreakNode& node) {
        if (
            node.chainKind == FormatBreakChainKind::StreamBeforeOperator ||
            node.chainKind == FormatBreakChainKind::Ternary
        ) {
            return true;
        }
        return !node.operators.empty() &&
            std::all_of(node.operators.begin(), node.operators.end(), IsFormatterOwnedChainOperator);
    }

    static bool IsAssignmentChain(const FormatBreakNode& node) {
        return node.operators.size() == 1 &&
            FormatBreakTokenKind(node.operators.front()) == PrintTokenKind::Known &&
            SyntaxNodeKindHasClass(FormatBreakTokenSyntaxKind(node.operators.front()), TokenClass::AssignmentOperator);
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

    static bool RequiresChainCompactExtraLinesGuard(const FormatBreakNode& node) {
        if (IsAssignmentChain(node) && !node.operands.empty() && IsDirectForceSplitAdjacentStringsInitializer(
            *node.operands.back()
        )) {
            return true;
        }
        if (node.chainKind == FormatBreakChainKind::Ternary) {
            return node.operators.size() > 2;
        }
        if (node.chainKind == FormatBreakChainKind::StreamBeforeOperator) {
            return true;
        }
        return IsFormatterOwnedChain(node) && node.operators.size() > 1;
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

    static bool ContainsNonSingleStatementBodyHeader(const FormatBreakNode& node) {
        if (node.kind == FormatBreakNodeKind::BodyHeader && !node.bodyHeaderSingleStatementBody) {
            return true;
        }
        for (const FormatBreakNode* child : node.children) {
            if (child && ContainsNonSingleStatementBodyHeader(*child)) {
                return true;
            }
        }
        for (const FormatBreakListItem& item : node.items) {
            if (item.node && ContainsNonSingleStatementBodyHeader(*item.node)) {
                return true;
            }
        }
        for (const FormatBreakNode* operand : node.operands) {
            if (operand && ContainsNonSingleStatementBodyHeader(*operand)) {
                return true;
            }
        }
        return false;
    }

    NodeResult SolvePrefixListCompact(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::Compact);
        result = AddToken(result, node.children[0]->token);
        for (size_t index = 0; index < node.items.size(); ++index) {
            const FormatBreakListItem& listItem = node.items[index];
            NodeResult item =
                SolveListItemWithSuffix(listItem, result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
        }
        return result;
    }

    NodeResult SolvePrefixListSplit(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::Split);
        result = AddToken(result, node.children[0]->token);
        result = AddListBreak(result, indentLevel + 1, node.structuralDepth, HasBlankLineBeforeItem(node, 0));
        for (size_t index = 0; index < node.items.size(); ++index) {
            const FormatBreakListItem& listItem = node.items[index];
            NodeResult item =
                SolveListItemWithSuffix(listItem, result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
            if (index + 1 < node.items.size()) {
                result = AddListBreak(result, indentLevel + 1, node.structuralDepth, HasBlankLineBeforeItem(
                    node,
                    index + 1
                ));
            }
        }
        return result;
    }

    NodeResult SolvePrefixList(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult compact = SolvePrefixListCompact(node, column, indentLevel, lineHasText);
        NodeResult split = SolvePrefixListSplit(node, column, indentLevel, lineHasText);
        if (node.forceSplit && split.valid) {
            return split;
        }
        if (compact.valid && split.valid && compact.extraLines > 0) {
            return split;
        }
        if (compact.valid && split.valid && CompactLineEndsOverLimit(compact) && split.maxOverflow == 0) {
            return split;
        }
        return Better(split, compact) ? split : compact;
    }

    NodeResult
        SolveFunctionSignatureCompact(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText)
    {
        if (node.children.size() < 2) {
            return {};
        }
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::Compact);
        for (const FormatBreakNode* child : node.children) {
            NodeResult item = Solve(*child, result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
        }
        return result;
    }

    NodeResult SolveFunctionSignatureSplit(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        if (node.children.size() < 2) {
            return {};
        }
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::Split);
        NodeResult returnType =
            Solve(*node.children[0], result.endColumn, result.endIndentLevel, result.endLineHasText);
        Merge(result, returnType);
        result = AddBreak(result, indentLevel + 1, node.structuralDepth);
        NodeResult declarator =
            Solve(*node.children[1], result.endColumn, result.endIndentLevel, result.endLineHasText);
        Merge(result, declarator);
        if (node.children.size() > 2) {
            if (node.functionSignatureHasBody) {
                result = AddBreak(result, indentLevel, node.structuralDepth);
            }
            NodeResult tail = Solve(*node.children[2], result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, tail);
        }
        return result;
    }

    NodeResults
        SolveFunctionSignatureAlternatives(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText)
    {
        NodeResults alternatives;
        NodeResult compact = SolveFunctionSignatureCompact(node, column, indentLevel, lineHasText);
        if (compact.valid) {
            alternatives.push_back(compact);
        }
        NodeResult split = SolveFunctionSignatureSplit(node, column, indentLevel, lineHasText);
        if (split.valid) {
            alternatives.push_back(split);
        }
        return alternatives;
    }

    NodeResult SolveFunctionSignature(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult compact = SolveFunctionSignatureCompact(node, column, indentLevel, lineHasText);
        NodeResult split = SolveFunctionSignatureSplit(node, column, indentLevel, lineHasText);
        NodeResult returnType =
            node.children.empty() ? NodeResult{}: Solve(*node.children[0], column, indentLevel, lineHasText);
        if (compact.valid && split.valid && returnType.valid && returnType.extraLines > 0) {
            return split;
        }
        if (compact.valid && split.valid && CompactLineEndsOverLimit(compact) && split.maxOverflow == 0) {
            return split;
        }
        return Better(split, compact) ? split : compact;
    }

    NodeResult SolveBodyHeaderCompact(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        if (node.children.size() < 2) {
            return {};
        }
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::Compact);
        for (const FormatBreakNode* child : node.children) {
            NodeResult item = Solve(*child, result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
        }
        return result;
    }

    NodeResults
        SolveBodyHeaderAlternatives(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText)
    {
        NodeResults alternatives;
        NodeResult compact = SolveBodyHeaderCompact(node, column, indentLevel, lineHasText);
        NodeResult header =
            node.children.empty() ? NodeResult{}: Solve(*node.children[0], column, indentLevel, lineHasText);
        const bool lineStartParentIndentBody = !lineHasText &&
            node.bodyHeaderSplitAtParentIndentWhenLineStarts &&
            (!node.bodyHeaderSingleStatementBody || (header.valid && header.extraLines > 0));
        if (compact.valid && !(header.valid && header.extraLines > 0) && !lineStartParentIndentBody) {
            alternatives.push_back(compact);
        }
        NodeResult split = SolveBodyHeaderSplit(node, column, indentLevel, lineHasText);
        if (split.valid) {
            alternatives.push_back(split);
        }
        if (lineStartParentIndentBody) {
            NodeResult parentIndent = SolveBodyHeaderSplitAtParentIndent(node, column, indentLevel, lineHasText);
            if (parentIndent.valid) {
                alternatives.push_back(parentIndent);
            }
        }
        return alternatives;
    }

    NodeResult SolveBodyHeaderSplitWithChoice(
        const FormatBreakNode& node,
        int column,
        int indentLevel,
        bool lineHasText,
        FormatBreakChoice choice,
        int bodyIndentLevel
    ) {
        if (node.children.size() < 2) {
            return {};
        }
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, choice);
        NodeResult header = Solve(*node.children[0], result.endColumn, result.endIndentLevel, result.endLineHasText);
        Merge(result, header);
        if (choice == FormatBreakChoice::BodyHeaderSplitAtParentIndent) {
            result = AddBreak(result, bodyIndentLevel, node.structuralDepth);
        }
        NodeResult body =
            SolveBodyHeaderSplitBody(*node.children[1], result.endColumn, result.endIndentLevel, result.endLineHasText);
        Merge(result, body);
        return result;
    }

    NodeResult SolveBodyHeaderSplitBody(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        if (node.kind == FormatBreakNodeKind::Delimited) {
            return SolveDelimitedSplit(node, column, indentLevel, lineHasText);
        }
        return Solve(node, column, indentLevel, lineHasText);
    }

    NodeResult SolveBodyHeaderSplit(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        return SolveBodyHeaderSplitWithChoice(
            node,
            column,
            indentLevel,
            lineHasText,
            FormatBreakChoice::Split,
            indentLevel
        );
    }

    NodeResult
        SolveBodyHeaderSplitAtParentIndent(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText)
    {
        return SolveBodyHeaderSplitWithChoice(
            node,
            column,
            indentLevel,
            lineHasText,
            FormatBreakChoice::BodyHeaderSplitAtParentIndent,
            std::max(0, indentLevel - 1)
        );
    }

    NodeResult SolveTrailingBodyHeaderSplitAtParentIndent(
        const FormatBreakNode& node,
        int column,
        int indentLevel,
        bool lineHasText
    ) {
        if (node.kind == FormatBreakNodeKind::BodyHeader) {
            return SolveBodyHeaderSplitAtParentIndent(node, column, indentLevel, lineHasText);
        }
        if (node.kind != FormatBreakNodeKind::Sequence) {
            return {};
        }

        std::vector<const FormatBreakNode*> sequenceChildren;
        AppendSequenceChildren(node.children, sequenceChildren);
        if (sequenceChildren.empty()) {
            return {};
        }

        NodeResults
            current{{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText}};
        for (size_t index = 0; index < sequenceChildren.size(); ++index) {
            NodeResults next;
            const bool last = index + 1 == sequenceChildren.size();
            for (const NodeResult& prefix : current) {
                NodeResults childResults = last ? NodeResults{SolveTrailingBodyHeaderSplitAtParentIndent(
                    *sequenceChildren[index],
                    prefix.endColumn,
                    prefix.endIndentLevel,
                    prefix.endLineHasText
                )} : SolveAlternatives(
                    *sequenceChildren[index],
                    prefix.endColumn,
                    prefix.endIndentLevel,
                    prefix.endLineHasText
                );
                for (const NodeResult& child : childResults) {
                    if (!child.valid) {
                        continue;
                    }
                    NodeResult candidate = prefix;
                    Merge(candidate, child);
                    AddPrunedResult(next, std::move(candidate));
                }
            }
            SortPrunedResults(next);
            current = std::move(next);
        }

        NodeResult best;
        for (const NodeResult& candidate : current) {
            if (Better(candidate, best)) {
                best = candidate;
            }
        }
        return best;
    }

    NodeResult SolveBodyHeader(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult compact = SolveBodyHeaderCompact(node, column, indentLevel, lineHasText);
        NodeResult split = SolveBodyHeaderSplit(node, column, indentLevel, lineHasText);
        NodeResult parentIndent = !lineHasText && node.bodyHeaderSplitAtParentIndentWhenLineStarts ?
            SolveBodyHeaderSplitAtParentIndent(node, column, indentLevel, lineHasText) : NodeResult{};
        if (compact.valid && compact.extraLines > 0 && parentIndent.valid) {
            return parentIndent;
        }
        NodeResult header =
            node.children.empty() ? NodeResult{}: Solve(*node.children[0], column, indentLevel, lineHasText);
        const bool lineStartParentIndentBody = !lineHasText &&
            node.bodyHeaderSplitAtParentIndentWhenLineStarts &&
            (!node.bodyHeaderSingleStatementBody || (header.valid && header.extraLines > 0));
        if (lineStartParentIndentBody && parentIndent.valid) {
            return parentIndent;
        }
        if (compact.valid && split.valid && header.valid && header.extraLines > 0) {
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

    NodeResults
        SolveChainCompactAlternatives(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText)
    {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::Compact);
        NodeResults current{result};
        for (size_t index = 0; index < node.operands.size(); ++index) {
            NodeResults nextByState;
            for (const NodeResult& prefix : current) {
                for (const NodeResult& operand : SolveAlternatives(
                    *node.operands[index],
                    prefix.endColumn,
                    prefix.endIndentLevel,
                    prefix.endLineHasText
                )) {
                    if (!operand.valid) {
                        continue;
                    }
                    NodeResult next = prefix;
                    Merge(next, operand);
                    if (index < node.operators.size()) {
                        next = AddToken(next, node.operators[index]);
                    }
                    AddPrunedResult(nextByState, std::move(next));
                }
            }
            SortPrunedResults(nextByState);
            current = std::move(nextByState);
        }
        return current;
    }

    NodeResult SolveDelimitedSplitAttachedOpen(const FormatBreakNode& node, NodeResult result, int baseIndent) {
        if (node.kind != FormatBreakNodeKind::Delimited || node.items.empty()) {
            return {};
        }
        AddChoice(result, node.id, FormatBreakChoice::SplitAttachedOpen);
        result = AddToken(result, node.children[0]->token);
        result = AddListBreak(result, baseIndent + 1, node.structuralDepth, HasBlankLineBeforeItem(node, 0));
        for (size_t index = 0; index < node.items.size(); ++index) {
            const FormatBreakListItem& listItem = node.items[index];
            NodeResult item = Solve(*listItem.node, result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
            if (FormatBreakTokenKind(listItem.separator) == PrintTokenKind::Known) {
                result = AddToken(result, listItem.separator);
            }
            if (HasTrailingComment(node, index)) {
                result = AddToken(result, listItem.trailingComment);
            }
            const bool hasNextItem = index + 1 < node.items.size();
            result = AddListBreak(
                result,
                hasNextItem ? baseIndent + 1 : baseIndent,
                node.structuralDepth,
                hasNextItem && HasBlankLineBeforeItem(node, index + 1)
            );
        }
        result = AddToken(result, node.children[1]->token);
        return result;
    }

    static bool CanAttachSplitOpenAfterOperator(const FormatBreakToken& op, const FormatBreakNode& operand) {
        const PrintToken& opToken = FormatBreakTokenValue(op);
        return opToken.kind == PrintTokenKind::Known &&
            opToken.parentKind == SyntaxNodeKind::BinaryExpression &&
            SyntaxNodeKindHasClass(opToken.syntaxKind, TokenClass::BinaryOperator) &&
            operand.kind == FormatBreakNodeKind::Delimited &&
            operand.delimiterKind == FormatBreakDelimiterKind::Paren;
    }

    NodeResult
        SolveChainSplitAfterOperator(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText)
    {
        const int continuationIndent = node.flatSplitIndent ? indentLevel : indentLevel + 1;
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::Split);
        if (node.operands.empty()) {
            return result;
        }
        NodeResult first =
            Solve(*node.operands.front(), result.endColumn, result.endIndentLevel, result.endLineHasText);
        Merge(result, first);
        for (size_t index = 0; index < node.operators.size(); ++index) {
            result = AddToken(result, node.operators[index]);
            NodeResult normal = AddBreak(result, continuationIndent, node.structuralDepth);
            const bool splitTrailingBodyHeaderAtParentIndent =
                node.splitTrailingBodyHeaderAtParentIndent && index + 1 == node.operands.size() - 1;
            NodeResult operand =
                Solve(*node.operands[index + 1], normal.endColumn, normal.endIndentLevel, normal.endLineHasText);
            if (splitTrailingBodyHeaderAtParentIndent) {
                NodeResult parentIndentOperand = SolveTrailingBodyHeaderSplitAtParentIndent(
                    *node.operands[index + 1],
                    normal.endColumn,
                    normal.endIndentLevel,
                    normal.endLineHasText
                );
                if (
                    (operand.extraLines > 0 || ContainsNonSingleStatementBodyHeader(*node.operands[index + 1])) &&
                    parentIndentOperand.valid
                ) {
                    operand = parentIndentOperand;
                } else {
                    operand = Better(parentIndentOperand, operand) ? parentIndentOperand : operand;
                }
            }
            Merge(normal, operand);

            NodeResult attached;
            if (CanAttachSplitOpenAfterOperator(node.operators[index], *node.operands[index + 1])) {
                attached = SolveDelimitedSplitAttachedOpen(*node.operands[index + 1], result, continuationIndent);
            }
            result = Better(attached, normal) ? attached : normal;
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
        AddChoice(result, node.id, choice);
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
        AddChoice(result, node.id, FormatBreakChoice::Split);
        for (size_t index = 0; index < node.operands.size(); ++index) {
            NodeResult operand =
                Solve(*node.operands[index], result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, operand);
            if (index < node.operators.size()) {
                result = AddToken(result, node.operators[index]);
                if (
                    FormatBreakTokenKind(node.operators[index]) == PrintTokenKind::Known &&
                    FormatBreakTokenSyntaxKind(node.operators[index]) == SyntaxNodeKind::Colon
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
        AddChoice(result, node.id, choice);
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
        if (
            compact.valid &&
            compact.extraLines > 0 &&
            RequiresChainCompactExtraLinesGuard(node) &&
            !CanKeepChainCompactWithExtraLines(node, compact)
        ) {
            compact = {};
        }
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
            return Better(split, compact) ? split : compact;
        }
        if (node.chainKind == FormatBreakChainKind::Ternary && node.operators.size() == 2) {
            NodeResult best = compact;
            NodeResults alternatives{
                SolveSingleTernary(
                    node,
                    column,
                    indentLevel,
                    lineHasText,
                    FormatBreakChoice::TernaryBreakAfterQuestion
                ),
                SolveSingleTernary(node, column, indentLevel, lineHasText, FormatBreakChoice::TernaryBreakAfterColon),
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
        if (compact.valid && split.valid && CompactLineEndsOverLimit(compact) && split.maxOverflow == 0) {
            return split;
        }
        return Better(split, compact) ? split : compact;
    }

    NodeResult SolveAdjacentStringsCompact(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::Compact);
        for (const FormatBreakNode* operand : node.operands) {
            NodeResult item = Solve(*operand, result.endColumn, result.endIndentLevel, result.endLineHasText);
            Merge(result, item);
        }
        return result;
    }

    NodeResult SolveAdjacentStringsSplit(const FormatBreakNode& node, int column, int indentLevel, bool lineHasText) {
        NodeResult
            result{.valid = true, .endColumn = column, .endIndentLevel = indentLevel, .endLineHasText = lineHasText};
        AddChoice(result, node.id, FormatBreakChoice::Split);
        const int continuationIndent = node.flatSplitIndent ? indentLevel : indentLevel + 1;
        for (size_t index = 0; index < node.operands.size(); ++index) {
            if (index > 0) {
                result = AddBreak(result, continuationIndent, node.structuralDepth);
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

void AppendChoices(const ChoiceTree* tree, std::vector<FormatBreakChoice>& output, std::vector<bool>& assigned) {
    if (tree == nullptr) {
        return;
    }
    if (tree->leaf) {
        const size_t index = static_cast<size_t>(tree->nodeId);
        if (index < output.size() && !assigned[index]) {
            output[index] = tree->choice;
            assigned[index] = true;
        }
        return;
    }
    AppendChoices(tree->left, output, assigned);
    AppendChoices(tree->right, output, assigned);
}

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
    if (!result.valid) {
        return solution;
    }
    const size_t choiceCount = model.nodes == nullptr ? 0 : model.nodes->size() + 1;
    solution.choices.assign(choiceCount, FormatBreakChoice::Compact);
    std::vector<bool> assigned(choiceCount, false);
    AppendChoices(result.choices, solution.choices, assigned);
    return solution;
}
