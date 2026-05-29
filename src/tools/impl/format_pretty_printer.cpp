#include "tools/impl/format_pretty_printer.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "tools/impl/format_break_model_builder.h"
#include "tools/impl/format_break_model_inline_helpers.h"
#include "tools/impl/format_break_solver.h"
#include "tools/impl/format_include_sort.h"
#include "tools/impl/format_spacing.h"
#include "tools/impl/tools_common.h"

namespace {

enum class BraceRole {
    Compact,
    Block,
    Enum,
    Namespace,
    CaseBlock,
};

struct BraceFrame {
    BraceRole role = BraceRole::Compact;
    int parenDepth = 0;
    int indentRestore = 0;
    int closeIndent = 0;
};

bool IsPreprocessorNode(SyntaxNodeKind kind) {
    return SyntaxNodeKindHasClass(kind, TokenClass::AtomicPreprocessor);
}

BraceRole RoleForBraceParent(SyntaxNodeKind parentKind) {
    switch (parentKind) {
        case SyntaxNodeKind::CompoundStatement:
        case SyntaxNodeKind::FieldDeclarationList:
        case SyntaxNodeKind::DeclarationList:
            return BraceRole::Block;
        case SyntaxNodeKind::EnumeratorList:
            return BraceRole::Enum;
        default:
            return BraceRole::Compact;
    }
}

BraceRole RoleForBrace(const PrintToken& token) {
    if (
        token.inSingleStatementLambdaBody &&
        token.parentKind == SyntaxNodeKind::CompoundStatement &&
        token.grandParentKind == SyntaxNodeKind::LambdaExpression
    ) {
        return BraceRole::Compact;
    }
    if (
        token.parentKind == SyntaxNodeKind::DeclarationList &&
        token.grandParentKind == SyntaxNodeKind::NamespaceDefinition
    ) {
        return BraceRole::Namespace;
    }
    return RoleForBraceParent(token.parentKind);
}

bool IsMacroDefinitionNode(SyntaxNodeKind kind) {
    return SyntaxNodeKindHasClass(kind, TokenClass::MacroDefinition);
}

bool IsMacroDeclarationFragment(SyntaxNodeKind kind) {
    return SyntaxNodeKindHasClass(kind, TokenClass::MacroDeclarationFragment);
}

bool RequiresMacroValueBreak(const SyntaxNode& node) {
    size_t topLevelElementCount = 0;
    for (const SyntaxNode* child : node.children) {
        if (!child || child->kind == SyntaxNodeKind::BlankLine) {
            continue;
        }
        ++topLevelElementCount;
        if (topLevelElementCount > 1) {
            return true;
        }
        if (IsMacroDeclarationFragment(child->kind)) {
            return true;
        }
    }
    return false;
}

bool PreservesBlankLineToken(SyntaxNodeKind parentKind) {
    return parentKind == SyntaxNodeKind::TranslationUnit ||
        parentKind == SyntaxNodeKind::DeclarationList ||
        parentKind == SyntaxNodeKind::CompoundStatement ||
        parentKind == SyntaxNodeKind::FieldDeclarationList ||
        parentKind == SyntaxNodeKind::EnumeratorList;
}

bool KeepsListCommentInBreakModel(const PrintToken& token) {
    if (!IsCommentToken(token.kind)) {
        return false;
    }
    // Keep list comments in the same optimization segment so they can force all comma breaks together.
    switch (token.parentKind) {
        case SyntaxNodeKind::InitializerList:
        case SyntaxNodeKind::FieldInitializerList:
        case SyntaxNodeKind::ParameterList:
        case SyntaxNodeKind::ArgumentList:
        case SyntaxNodeKind::TemplateParameterList:
        case SyntaxNodeKind::TemplateArgumentList:
        case SyntaxNodeKind::LambdaCaptureSpecifier:
        case SyntaxNodeKind::PreprocParams:
            return true;
        default:
            return false;
    }
}

bool ContainsLogicalOperator(const SyntaxNode& node) {
    if (node.kind == SyntaxNodeKind::AmpersandAmpersand || node.kind == SyntaxNodeKind::PipePipe) {
        return true;
    }
    for (const SyntaxNode* child : node.children) {
        if (child && ContainsLogicalOperator(*child)) {
            return true;
        }
    }
    return false;
}

bool IsListDelimiter(SyntaxNodeKind token) {
    return token == SyntaxNodeKind::LeftParen ||
        token == SyntaxNodeKind::RightParen ||
        token == SyntaxNodeKind::LeftBracket ||
        token == SyntaxNodeKind::RightBracket ||
        token == SyntaxNodeKind::Comma;
}

bool HasDelimitedListItemContent(const SyntaxNode& node) {
    if (
        node.kind == SyntaxNodeKind::BlankLine ||
        node.kind == SyntaxNodeKind::Comment ||
        node.kind == SyntaxNodeKind::TrailingComment
    ) {
        return false;
    }
    if (SyntaxNodeKindHasClass(node.kind, TokenClass::Known) && IsListDelimiter(node.kind)) {
        return false;
    }
    return true;
}

size_t CountDirectDelimitedListItems(const SyntaxNode& node) {
    if (!SyntaxNodeKindHasClass(node.kind, TokenClass::Tree)) {
        return 0;
    }
    bool hasItem = false;
    size_t commaCount = 0;
    for (const SyntaxNode* child : node.children) {
        if (!child) {
            continue;
        }
        if (child->kind == SyntaxNodeKind::Comma) {
            ++commaCount;
            continue;
        }
        hasItem = hasItem || HasDelimitedListItemContent(*child);
    }
    return hasItem ? commaCount + 1 : 0;
}

const SyntaxNode* FindDirectChild(const SyntaxNode& node, SyntaxNodeKind targetKind) {
    if (!SyntaxNodeKindHasClass(node.kind, TokenClass::Tree)) {
        return nullptr;
    }
    for (const SyntaxNode* child : node.children) {
        if (child && child->kind == targetKind) {
            return child;
        }
    }
    return nullptr;
}

const SyntaxNode* FindLambdaHeaderChild(const SyntaxNode& node, SyntaxNodeKind targetKind) {
    if (!SyntaxNodeKindHasClass(node.kind, TokenClass::Tree)) {
        return nullptr;
    }
    for (const SyntaxNode* child : node.children) {
        if (!child || !SyntaxNodeKindHasClass(child->kind, TokenClass::Tree)) {
            continue;
        }
        if (child->kind == SyntaxNodeKind::CompoundStatement) {
            continue;
        }
        if (child->kind == targetKind) {
            return child;
        }
        if (const SyntaxNode* nested = FindLambdaHeaderChild(*child, targetKind)) {
            return nested;
        }
    }
    return nullptr;
}

bool HasSimpleLambdaHeader(const SyntaxNode* lambda) {
    if (lambda == nullptr || lambda->kind != SyntaxNodeKind::LambdaExpression) {
        return false;
    }
    const SyntaxNode* captures = FindDirectChild(*lambda, SyntaxNodeKind::LambdaCaptureSpecifier);
    const SyntaxNode* parameters = FindLambdaHeaderChild(*lambda, SyntaxNodeKind::ParameterList);
    const size_t captureCount = captures == nullptr ? 0 : CountDirectDelimitedListItems(*captures);
    const size_t parameterCount = parameters == nullptr ? 0 : CountDirectDelimitedListItems(*parameters);
    return captureCount <= 1 && parameterCount <= 1;
}

bool IsSingleStatementLambdaBody(const SyntaxNode& node, SyntaxNodeKind parentKind, const SyntaxNode* parentNode) {
    if (
        node.kind != SyntaxNodeKind::CompoundStatement ||
        parentKind != SyntaxNodeKind::LambdaExpression ||
        !HasSimpleLambdaHeader(parentNode)
    ) {
        return false;
    }
    size_t statementCount = 0;
    for (const SyntaxNode* child : node.children) {
        if (
            !child ||
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

void AppendTokens(
    const SyntaxNode& node,
    SyntaxNodeKind parentKind,
    SyntaxNodeKind grandParentKind,
    bool inTemplateDeclaration,
    bool inRequiresClause,
    bool splitRequiresClause,
    bool inCompilerCallModifier,
    bool inSingleStatementLambdaBody,
    const SyntaxNode* macroDefinition,
    const SyntaxNode* macroValueElement,
    bool inMacroValue,
    bool breakBeforeMacroValue,
    std::vector<PrintToken>& tokens
) {
    const SyntaxNode* parentNode = node.parent;
    const SyntaxNodeKind nodeKind = node.kind;
    const bool childInTemplateDeclaration = inTemplateDeclaration || nodeKind == SyntaxNodeKind::TemplateDeclaration;
    const bool childInRequiresClause = inRequiresClause || nodeKind == SyntaxNodeKind::RequiresClause;
    const bool childSplitRequiresClause =
        splitRequiresClause || (nodeKind == SyntaxNodeKind::RequiresClause && ContainsLogicalOperator(node));
    const bool childInCompilerCallModifier = inCompilerCallModifier ||
        nodeKind == SyntaxNodeKind::MsCallModifier ||
        nodeKind == SyntaxNodeKind::MsDeclspecModifier;
    const bool childInSingleStatementLambdaBody =
        inSingleStatementLambdaBody || IsSingleStatementLambdaBody(node, parentKind, parentNode);
    const SyntaxNode* childMacroDefinition =
        macroDefinition != nullptr ? macroDefinition : (IsMacroDefinitionNode(nodeKind) ? &node : nullptr);
    const bool childInMacroValue = inMacroValue || nodeKind == SyntaxNodeKind::MacroReplacementList;
    const bool childBreakBeforeMacroValue =
        breakBeforeMacroValue || (nodeKind == SyntaxNodeKind::MacroReplacementList && RequiresMacroValueBreak(node));

    if (nodeKind == SyntaxNodeKind::BlankLine) {
        if (!PreservesBlankLineToken(parentKind)) {
            return;
        }
        tokens.push_back({
            .kind = PrintTokenKind::BlankLine,
            .inMacroValue = childInMacroValue,
            .breakBeforeMacroValue = childBreakBeforeMacroValue,
            .node = &node,
            .macroDefinition = childMacroDefinition,
            .macroValueElement = macroValueElement
        });
        return;
    }
    if (nodeKind == SyntaxNodeKind::Comment || nodeKind == SyntaxNodeKind::TrailingComment) {
        tokens.push_back({
            .kind = nodeKind == SyntaxNodeKind::TrailingComment ? PrintTokenKind::TrailingComment :
                PrintTokenKind::Comment,
            .syntaxKind = nodeKind,
            .text = node.text,
            .parentKind = parentKind,
            .grandParentKind = grandParentKind,
            .inTemplateDeclaration = childInTemplateDeclaration,
            .inRequiresClause = childInRequiresClause,
            .splitRequiresClause = childSplitRequiresClause,
            .inCompilerCallModifier = childInCompilerCallModifier,
            .inSingleStatementLambdaBody = childInSingleStatementLambdaBody,
            .inMacroValue = childInMacroValue,
            .breakBeforeMacroValue = childBreakBeforeMacroValue,
            .node = &node,
            .macroDefinition = childMacroDefinition,
            .macroValueElement = macroValueElement
        });
        return;
    }
    if (SyntaxNodeKindHasClass(nodeKind, TokenClass::Known)) {
        tokens.push_back({
            .kind = PrintTokenKind::Known,
            .syntaxKind = nodeKind,
            .text = SyntaxNodeKindTokenText(nodeKind),
            .parentKind = parentKind,
            .grandParentKind = grandParentKind,
            .inTemplateDeclaration = childInTemplateDeclaration,
            .inRequiresClause = childInRequiresClause,
            .splitRequiresClause = childSplitRequiresClause,
            .inCompilerCallModifier = childInCompilerCallModifier,
            .inSingleStatementLambdaBody = childInSingleStatementLambdaBody,
            .inMacroValue = childInMacroValue,
            .breakBeforeMacroValue = childBreakBeforeMacroValue,
            .node = &node,
            .macroDefinition = childMacroDefinition,
            .macroValueElement = macroValueElement
        });
        return;
    }
    if (nodeKind == SyntaxNodeKind::IncludeRun) {
        tokens.push_back({
            .kind = PrintTokenKind::IncludeRun,
            .syntaxKind = nodeKind,
            .parentKind = parentKind,
            .grandParentKind = grandParentKind,
            .inTemplateDeclaration = childInTemplateDeclaration,
            .inRequiresClause = childInRequiresClause,
            .splitRequiresClause = childSplitRequiresClause,
            .inCompilerCallModifier = childInCompilerCallModifier,
            .inSingleStatementLambdaBody = childInSingleStatementLambdaBody,
            .inMacroValue = childInMacroValue,
            .breakBeforeMacroValue = childBreakBeforeMacroValue,
            .node = &node,
            .macroDefinition = childMacroDefinition,
            .macroValueElement = macroValueElement
        });
        return;
    }
    if (IsPreprocessorNode(nodeKind)) {
        tokens.push_back({
            .kind = PrintTokenKind::Preprocessor,
            .syntaxKind = nodeKind,
            .text = node.text,
            .parentKind = parentKind,
            .grandParentKind = grandParentKind,
            .inTemplateDeclaration = childInTemplateDeclaration,
            .inRequiresClause = childInRequiresClause,
            .splitRequiresClause = childSplitRequiresClause,
            .inCompilerCallModifier = childInCompilerCallModifier,
            .inSingleStatementLambdaBody = childInSingleStatementLambdaBody,
            .inMacroValue = childInMacroValue,
            .breakBeforeMacroValue = childBreakBeforeMacroValue,
            .node = &node,
            .macroDefinition = childMacroDefinition,
            .macroValueElement = macroValueElement
        });
        return;
    }
    if (nodeKind == SyntaxNodeKind::FreeToken || node.children.empty()) {
        tokens.push_back({
            .kind = PrintTokenKind::Free,
            .syntaxKind = nodeKind,
            .text = node.text,
            .parentKind = parentKind,
            .grandParentKind = grandParentKind,
            .inTemplateDeclaration = childInTemplateDeclaration,
            .inRequiresClause = childInRequiresClause,
            .splitRequiresClause = childSplitRequiresClause,
            .inCompilerCallModifier = childInCompilerCallModifier,
            .inSingleStatementLambdaBody = childInSingleStatementLambdaBody,
            .inMacroValue = childInMacroValue,
            .breakBeforeMacroValue = childBreakBeforeMacroValue,
            .node = &node,
            .macroDefinition = childMacroDefinition,
            .macroValueElement = macroValueElement
        });
        return;
    }
    if (nodeKind == SyntaxNodeKind::MacroReplacementList) {
        for (const SyntaxNode* child : node.children) {
            AppendTokens(
                *child,
                nodeKind,
                parentKind,
                childInTemplateDeclaration,
                childInRequiresClause,
                childSplitRequiresClause,
                childInCompilerCallModifier,
                childInSingleStatementLambdaBody,
                childMacroDefinition,
                child,
                true,
                childBreakBeforeMacroValue,
                tokens
            );
        }
        return;
    }
    if (SyntaxNodeKindHasClass(nodeKind, TokenClass::Tree)) {
        for (const SyntaxNode* child : node.children) {
            AppendTokens(
                *child,
                nodeKind,
                parentKind,
                childInTemplateDeclaration,
                childInRequiresClause,
                childSplitRequiresClause,
                childInCompilerCallModifier,
                childInSingleStatementLambdaBody,
                childMacroDefinition,
                macroValueElement,
                childInMacroValue,
                childBreakBeforeMacroValue,
                tokens
            );
        }
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

void AnnotateMacroValueWidths(std::vector<PrintToken>& tokens) {
    for (size_t index = 0; index < tokens.size(); ++index) {
        if (!tokens[index].inMacroValue || tokens[index].macroValueRemainingWidth != 0) {
            continue;
        }
        const SyntaxNode* macroDefinition = tokens[index].macroDefinition;
        size_t end = index;
        int width = 0;
        const PrintToken* previous = index > 0 ? &tokens[index - 1] : nullptr;
        while (end < tokens.size() && tokens[end].inMacroValue && tokens[end].macroDefinition == macroDefinition) {
            width += (FormatTokenNeedsSpace(previous, tokens[end]) ? 1 : 0) + FormatTokenWidth(tokens[end]);
            previous = &tokens[end];
            ++end;
        }
        for (size_t cursor = index; cursor < end; ++cursor) {
            tokens[cursor].macroValueRemainingWidth = width;
        }
        index = end == 0 ? 0 : end - 1;
    }
}

struct DeferredSplitCallContext {
    const SyntaxNode* argumentList = nullptr;
    const SyntaxNode* lambdaRightBrace = nullptr;
    const SyntaxNode* closeToken = nullptr;
    int argumentIndent = 0;
    int closeIndent = 0;
    bool afterLambdaClose = false;
};

struct LambdaSplitCallPlan {
    FormatBreakModelContext breakContext;
    DeferredSplitCallContext deferredContext;
};

class Printer {
public:
    Printer(
        const FormatterConfig& config,
        std::string_view sourcePath,
        FormatModelTextStats* stats
    ) : config_(config), sourcePath_(sourcePath), stats_(stats), indentWidth_(std::max(1, config.indentWidth)) {}

    std::string Print(const std::vector<PrintToken>& tokens) {
        activeTokens_ = &tokens;
        output_.reserve(tokens.size() * 8);
        pendingTokens_.reserve(64);
        for (size_t index = 0; index < tokens.size(); ++index) {
            currentTokenIndex_ = index;
            const PrintToken* previous = PreviousToken(tokens, index);
            const PrintToken* next = NextToken(tokens, index);
            const PrintToken* rawNext = RawNextToken(tokens, index);
            PrintOne(tokens[index], previous, next, rawNext);
        }
        activeTokens_ = nullptr;
        FlushPendingTokens();
        FinishLine();
        if (!output_.empty() && output_.back() != '\n') {
            output_.push_back('\n');
        }
        return output_;
    }

private:
    const FormatterConfig& config_;
    std::string_view sourcePath_;
    FormatModelTextStats* stats_ = nullptr;
    int indentWidth_ = 4;
    std::string output_;
    std::vector<PrintToken> pendingTokens_;
    int indentLevel_ = 0;
    bool atLineStart_ = true;
    bool lineHasText_ = false;
    int currentColumn_ = 0;
    bool macroContinuationLine_ = false;
    bool forceColumnZeroLine_ = false;
    bool emittingMacroValue_ = false;
    const std::vector<PrintToken>* activeTokens_ = nullptr;
    size_t currentTokenIndex_ = 0;
    std::optional<int> pendingIndentLevel_;
    int compactRightBraceSkips_ = 0;
    int switchDepth_ = 0;
    int parenDepth_ = 0;
    int bracketDepth_ = 0;
    std::vector<BraceFrame> braceStack_;
    std::vector<int> activeCaseBodySwitchDepths_;
    std::vector<DeferredSplitCallContext> deferredSplitCallContexts_;
    std::optional<int> pendingIndentRestoreAfterFlush_;

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

    static bool SyntaxPathContains(const PrintToken& token, const SyntaxNode* node) {
        for (const SyntaxNode* cursor = token.node; cursor != nullptr; cursor = cursor->parent) {
            if (cursor == node) {
                return true;
            }
        }
        return false;
    }

    static const SyntaxNode* DirectTokenChild(const SyntaxNode& node, SyntaxNodeKind known) {
        for (const SyntaxNode* child : node.children) {
            if (child && child->kind == known) {
                return child;
            }
        }
        return nullptr;
    }

    static bool HasDirectTokenChild(const SyntaxNode& node, SyntaxNodeKind known) {
        return DirectTokenChild(node, known) != nullptr;
    }

    static const SyntaxNode* NearestAncestorBefore(
        const PrintToken& token,
        SyntaxNodeKind kind,
        const SyntaxNode* before
    ) {
        for (const SyntaxNode* cursor = token.node; cursor != nullptr; cursor = cursor->parent) {
            if (cursor == before) {
                for (cursor = cursor->parent; cursor != nullptr; cursor = cursor->parent) {
                    if (cursor->kind == kind) {
                        return cursor;
                    }
                }
                return nullptr;
            }
        }
        return nullptr;
    }

    static const SyntaxNode* NearestAncestor(const PrintToken& token, SyntaxNodeKind kind) {
        for (const SyntaxNode* cursor = token.node; cursor != nullptr; cursor = cursor->parent) {
            if (cursor->kind == kind) {
                return cursor;
            }
        }
        return nullptr;
    }

    static bool IsAccessLabel(const PrintToken& token, const PrintToken* next) {
        return IsAccessKeyword(token) &&
            next != nullptr &&
            next->kind == PrintTokenKind::Known &&
            next->syntaxKind == SyntaxNodeKind::Colon;
    }

    std::optional<size_t> FindTokenIndex(const SyntaxNode* node, size_t begin) const {
        if (node == nullptr || activeTokens_ == nullptr) {
            return std::nullopt;
        }
        for (size_t index = begin; index < activeTokens_->size(); ++index) {
            if ((*activeTokens_)[index].node == node) {
                return index;
            }
        }
        return std::nullopt;
    }

    void TrimTrailingSpaces() {
        while (!output_.empty() && output_.back() == ' ') {
            output_.pop_back();
            currentColumn_ = std::max(0, currentColumn_ - 1);
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
        currentColumn_ = 0;
        macroContinuationLine_ = macroContinuation;
        forceColumnZeroLine_ = false;
        pendingIndentLevel_.reset();
    }

    void NewLineWithIndent(int indentLevel) {
        NewLine(emittingMacroValue_);
        pendingIndentLevel_ = std::max(0, indentLevel);
    }

    void BlankLineWithIndent(int indentLevel) {
        BlankLine();
        pendingIndentLevel_ = std::max(0, indentLevel);
    }

    void BreakListLine(int indentLevel, bool blankLine) {
        if (blankLine) {
            BlankLineWithIndent(indentLevel);
            return;
        }
        NewLineWithIndent(indentLevel);
    }

    void BlankLine() {
        NewLine(false);
        if (output_.size() < 2 || output_[output_.size() - 2] != '\n') {
            output_.push_back('\n');
        }
        atLineStart_ = true;
        lineHasText_ = false;
        currentColumn_ = 0;
        macroContinuationLine_ = false;
        forceColumnZeroLine_ = false;
        pendingIndentLevel_.reset();
    }

    void WriteIndentIfNeeded() {
        if (!atLineStart_) {
            return;
        }
        const int macroOffset = macroContinuationLine_ ? 1 : 0;
        const int indentLevel = pendingIndentLevel_.value_or(forceColumnZeroLine_ ? 0 : indentLevel_ + macroOffset);
        currentColumn_ = std::max(0, indentLevel) * indentWidth_;
        output_.append(static_cast<size_t>(currentColumn_), ' ');
        atLineStart_ = false;
        macroContinuationLine_ = false;
        forceColumnZeroLine_ = false;
        pendingIndentLevel_.reset();
    }

    void WriteWithIndentOffset(std::string_view text, int indentOffset) {
        if (!atLineStart_) {
            output_.append(text);
            AdvanceCurrentColumn(text);
            lineHasText_ = lineHasText_ || !text.empty();
            return;
        }
        const int adjustedIndent = std::max(0, indentLevel_ + indentOffset);
        currentColumn_ = adjustedIndent * indentWidth_;
        output_.append(static_cast<size_t>(currentColumn_), ' ');
        atLineStart_ = false;
        macroContinuationLine_ = false;
        forceColumnZeroLine_ = false;
        pendingIndentLevel_.reset();
        output_.append(text);
        AdvanceCurrentColumn(text);
        lineHasText_ = lineHasText_ || !text.empty();
    }

    void Write(std::string_view text) {
        WriteIndentIfNeeded();
        output_.append(text);
        AdvanceCurrentColumn(text);
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
            ++currentColumn_;
        }
    }

    int CurrentColumn() const {
        if (atLineStart_) {
            const int macroOffset = macroContinuationLine_ ? 1 : 0;
            const int indentLevel = pendingIndentLevel_.value_or(forceColumnZeroLine_ ? 0 : indentLevel_ + macroOffset);
            return std::max(0, indentLevel) * indentWidth_;
        }
        return currentColumn_;
    }

    void AdvanceCurrentColumn(std::string_view text) {
        const size_t newline = text.find_last_of('\n');
        if (newline == std::string_view::npos) {
            currentColumn_ += static_cast<int>(text.size());
            return;
        }
        currentColumn_ = static_cast<int>(text.size() - newline - 1);
    }

    int CurrentLineIndentLevel() const {
        const size_t lineStart = output_.find_last_of('\n');
        size_t cursor = lineStart == std::string::npos ? 0 : lineStart + 1;
        int spaces = 0;
        while (cursor < output_.size() && output_[cursor] == ' ') {
            ++spaces;
            ++cursor;
        }
        return spaces / indentWidth_;
    }

    int PendingCompactWidth() const {
        int width = 0;
        bool hasText = lineHasText_;
        const PrintToken* previous = nullptr;
        for (const PrintToken& token : pendingTokens_) {
            if (FormatTokenNeedsSpace(previous, token) && hasText) {
                ++width;
            }
            width += FormatTokenWidth(token);
            hasText = hasText || FormatTokenWidth(token) > 0;
            previous = &token;
        }
        return width;
    }

    bool CanFlushPendingTokensCompact(const FormatBreakModelContext& context) const {
        if (
            context.virtualDelimiterOpen != nullptr ||
            context.virtualDelimiterClose.token != nullptr ||
            context.forceSplitVirtualDelimiter
        ) {
            return false;
        }
        int width = 0;
        bool hasText = lineHasText_;
        const PrintToken* previous = nullptr;
        bool previousStringLike = false;
        for (const PrintToken& token : pendingTokens_) {
            if (token.kind != PrintTokenKind::Known && token.kind != PrintTokenKind::Free) {
                return false;
            }
            const bool stringLike = IsStringLike(token);
            if (
                token.inMacroValue ||
                token.breakBeforeMacroValue ||
                token.macroDefinition != nullptr ||
                (stringLike && previousStringLike) ||
                token.parentKind == SyntaxNodeKind::FieldInitializerList ||
                token.grandParentKind == SyntaxNodeKind::FieldInitializerList
            ) {
                return false;
            }
            if (FormatTokenNeedsSpace(previous, token) && hasText) {
                ++width;
            }
            const int tokenWidth = FormatTokenWidth(token);
            width += tokenWidth;
            hasText = hasText || tokenWidth > 0;
            previous = &token;
            previousStringLike = stringLike;
        }
        return CurrentColumn() + width <= config_.columnLimit;
    }

    void FlushPendingTokensCompact() {
        const PrintToken* previous = nullptr;
        for (const PrintToken& token : pendingTokens_) {
            if (FormatTokenNeedsSpace(previous, token) && !atLineStart_) {
                Space();
            }
            Write(FormatTokenText(token));
            previous = &token;
        }
        pendingTokens_.clear();
    }

    void BufferToken(const PrintToken& token) {
        pendingTokens_.push_back(token);
    }

    FormatBreakChoice ChoiceFor(const FormatBreakSolution& solution, int nodeId) const {
        if (nodeId < 0 || static_cast<size_t>(nodeId) >= solution.choices.size()) {
            return FormatBreakChoice::Compact;
        }
        return solution.choices[static_cast<size_t>(nodeId)];
    }

    static bool IsSplitChoice(FormatBreakChoice choice) {
        return choice == FormatBreakChoice::Split ||
            choice == FormatBreakChoice::BodyHeaderSplitAtParentIndent ||
            choice == FormatBreakChoice::SplitAttachedOpen ||
            choice == FormatBreakChoice::SplitDelimiterStack;
    }

    static bool IsBodyHeaderSplitChoice(FormatBreakChoice choice) {
        return choice == FormatBreakChoice::Split || choice == FormatBreakChoice::BodyHeaderSplitAtParentIndent;
    }

    void WriteBreakToken(const FormatBreakToken& token) {
        if (token.contextOnly) {
            return;
        }
        const PrintToken& printToken = FormatBreakTokenValue(token);
        if (printToken.macroDefinition != nullptr && !printToken.inMacroValue && atLineStart_) {
            forceColumnZeroLine_ = true;
            pendingIndentLevel_.reset();
        }
        if (IsCommentToken(printToken.kind)) {
            if (!atLineStart_) {
                Space();
                output_.push_back(' ');
            }
            Write(FormatTokenText(printToken));
            return;
        }
        if (token.spaceBefore && !atLineStart_) {
            Space();
        }
        Write(FormatTokenText(printToken));
    }

    void EmitBreakNode(const FormatBreakNode& node, const FormatBreakSolution& solution, int baseIndent) {
        switch (node.kind) {
            case FormatBreakNodeKind::Token:
                WriteBreakToken(node.token);
                return;
            case FormatBreakNodeKind::Sequence:
                for (const FormatBreakNode* child : node.children) {
                    EmitBreakNode(*child, solution, baseIndent);
                }
                return;
            case FormatBreakNodeKind::Delimited:
                EmitDelimitedNode(node, solution, baseIndent);
                return;
            case FormatBreakNodeKind::PrefixList:
                EmitPrefixListNode(node, solution, baseIndent);
                return;
            case FormatBreakNodeKind::FunctionSignature:
                EmitFunctionSignatureNode(node, solution, baseIndent);
                return;
            case FormatBreakNodeKind::BodyHeader:
                EmitBodyHeaderNode(node, solution, baseIndent);
                return;
            case FormatBreakNodeKind::Chain:
                EmitChainNode(node, solution, baseIndent);
                return;
            case FormatBreakNodeKind::AdjacentStrings:
                EmitAdjacentStringsNode(node, solution, baseIndent);
                return;
        }
    }

    bool ShouldCombineSplitBracedItemBoundary(
        const FormatBreakNode& node,
        const FormatBreakSolution& solution,
        size_t index
    ) const {
        if (node.delimiterKind != FormatBreakDelimiterKind::Brace || index + 1 >= node.items.size()) {
            return false;
        }
        const FormatBreakListItem& item = node.items[index];
        const FormatBreakListItem& nextItem = node.items[index + 1];
        return item.node != nullptr &&
            nextItem.node != nullptr &&
            FormatBreakTokenKind(item.separator) == PrintTokenKind::Known &&
            FormatBreakTokenSyntaxKind(item.separator) == SyntaxNodeKind::Comma &&
            item.node->kind == FormatBreakNodeKind::Delimited &&
            item.node->delimiterKind == FormatBreakDelimiterKind::Brace &&
            IsSplitChoice(ChoiceFor(solution, item.node->id)) &&
            nextItem.node->kind == FormatBreakNodeKind::Delimited &&
            nextItem.node->delimiterKind == FormatBreakDelimiterKind::Brace &&
            IsSplitChoice(ChoiceFor(solution, nextItem.node->id)) &&
            !HasTrailingComment(node, index) &&
            !HasBlankLineBeforeItem(node, index + 1);
    }

    struct DelimiterStackEmitView {
        std::vector<const FormatBreakNode*> delimiters;
        const FormatBreakNode* leaf = nullptr;
    };

    static bool HasRealSeparators(const FormatBreakNode& node) {
        return std::any_of(
            node.items.begin(),
            node.items.end(),
            [](const FormatBreakListItem& item)
            { return FormatBreakTokenKind(item.separator) == PrintTokenKind::Known; }
        );
    }

    static bool HasTrailingComment(const FormatBreakNode& node, size_t index) {
        return index < node.items.size() && IsCommentToken(FormatBreakTokenKind(node.items[index].trailingComment));
    }

    static bool HasBlankLineBeforeItem(const FormatBreakNode& node, size_t index) {
        return index < node.items.size() && node.items[index].blankLineBefore;
    }

    static bool IsDelimiterStackItem(const FormatBreakNode& node) {
        return node.kind == FormatBreakNodeKind::Delimited && node.items.size() == 1 && !HasRealSeparators(node);
    }

    static std::optional<DelimiterStackEmitView> CollectDelimiterStack(const FormatBreakNode& node) {
        if (!IsDelimiterStackItem(node) || node.children.size() < 2 || node.forceSplit) {
            return std::nullopt;
        }
        DelimiterStackEmitView stack;
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
        return stack.leaf != nullptr ? std::optional(stack) : std::nullopt;
    }

    void WritePackedBreakToken(const FormatBreakToken& token, int overflowIndent) {
        const int space = token.spaceBefore && !atLineStart_ ? 1 : 0;
        if (
            !atLineStart_ &&
            CurrentColumn() + space + FormatTokenWidth(FormatBreakTokenValue(token)) > config_.columnLimit
        ) {
            NewLineWithIndent(overflowIndent);
        }
        WriteBreakToken(token);
    }

    void EmitDelimiterStackNode(const FormatBreakNode& node, const FormatBreakSolution& solution, int baseIndent) {
        const std::optional<DelimiterStackEmitView> stack = CollectDelimiterStack(node);
        if (!stack) {
            return;
        }
        int currentLineIndent = baseIndent;
        int nextOpenIndent = baseIndent + 1;
        std::vector<int> delimiterIndents;
        delimiterIndents.reserve(stack->delimiters.size());
        for (const FormatBreakNode* delimiter : stack->delimiters) {
            const FormatBreakToken& open = delimiter->children.front()->token;
            const int space = open.spaceBefore && !atLineStart_ ? 1 : 0;
            if (
                !atLineStart_ &&
                CurrentColumn() + space + FormatTokenWidth(FormatBreakTokenValue(open)) > config_.columnLimit
            ) {
                currentLineIndent = nextOpenIndent;
                NewLineWithIndent(currentLineIndent);
                ++nextOpenIndent;
            }
            delimiterIndents.push_back(currentLineIndent);
            WriteBreakToken(open);
        }
        EmitBreakNode(*stack->leaf, solution, nextOpenIndent);
        for (size_t index = stack->delimiters.size(); index-- > 0;) {
            WritePackedBreakToken(stack->delimiters[index]->children.back()->token, delimiterIndents[index]);
        }
    }

    void EmitDelimitedNode(const FormatBreakNode& node, const FormatBreakSolution& solution, int baseIndent) {
        const FormatBreakChoice choice = ChoiceFor(solution, node.id);
        if (choice == FormatBreakChoice::SplitDelimiterStack) {
            EmitDelimiterStackNode(node, solution, baseIndent);
            return;
        }
        if (!IsSplitChoice(choice) || node.items.empty()) {
            EmitBreakNode(*node.children[0], solution, baseIndent);
            for (size_t index = 0; index < node.items.size(); ++index) {
                const FormatBreakListItem& item = node.items[index];
                EmitBreakNode(*item.node, solution, baseIndent);
                if (FormatBreakTokenKind(item.separator) == PrintTokenKind::Known) {
                    WriteBreakToken(item.separator);
                }
                if (HasTrailingComment(node, index)) {
                    WriteBreakToken(item.trailingComment);
                }
            }
            EmitBreakNode(*node.children[1], solution, baseIndent);
            return;
        }

        EmitBreakNode(*node.children[0], solution, baseIndent);
        const bool closesInContext = node.children.size() > 1 &&
            node.children[1]->kind == FormatBreakNodeKind::Token &&
            node.children[1]->token.contextOnly;
        BreakListLine(baseIndent + 1, HasBlankLineBeforeItem(node, 0));
        for (size_t index = 0; index < node.items.size(); ++index) {
            const FormatBreakListItem& item = node.items[index];
            EmitBreakNode(*item.node, solution, baseIndent + 1);
            if (FormatBreakTokenKind(item.separator) == PrintTokenKind::Known) {
                WriteBreakToken(item.separator);
            }
            if (HasTrailingComment(node, index)) {
                WriteBreakToken(item.trailingComment);
            }
            if (ShouldCombineSplitBracedItemBoundary(node, solution, index)) {
                Space();
            } else if (closesInContext && index + 1 == node.items.size()) {
                continue;
            } else {
                const bool hasNextItem = index + 1 < node.items.size();
                BreakListLine(
                    hasNextItem ? baseIndent + 1 : baseIndent,
                    hasNextItem && HasBlankLineBeforeItem(node, index + 1)
                );
            }
        }
        EmitBreakNode(*node.children[1], solution, baseIndent);
    }

    void EmitPrefixListNode(const FormatBreakNode& node, const FormatBreakSolution& solution, int baseIndent) {
        const FormatBreakChoice choice = ChoiceFor(solution, node.id);
        if (choice != FormatBreakChoice::Split) {
            EmitBreakNode(*node.children[0], solution, baseIndent);
            for (size_t index = 0; index < node.items.size(); ++index) {
                const FormatBreakListItem& item = node.items[index];
                EmitBreakNode(*item.node, solution, baseIndent);
                if (FormatBreakTokenKind(item.separator) == PrintTokenKind::Known) {
                    WriteBreakToken(item.separator);
                }
                if (HasTrailingComment(node, index)) {
                    WriteBreakToken(item.trailingComment);
                }
            }
            return;
        }

        EmitBreakNode(*node.children[0], solution, baseIndent);
        BreakListLine(baseIndent + 1, HasBlankLineBeforeItem(node, 0));
        for (size_t index = 0; index < node.items.size(); ++index) {
            const FormatBreakListItem& item = node.items[index];
            EmitBreakNode(*item.node, solution, baseIndent + 1);
            if (FormatBreakTokenKind(item.separator) == PrintTokenKind::Known) {
                WriteBreakToken(item.separator);
            }
            if (HasTrailingComment(node, index)) {
                WriteBreakToken(item.trailingComment);
            }
            if (index + 1 < node.items.size()) {
                BreakListLine(baseIndent + 1, HasBlankLineBeforeItem(node, index + 1));
            }
        }
    }

    void EmitDelimitedNodeAfterAttachedOpen(
        const FormatBreakNode& node,
        const FormatBreakSolution& solution,
        int baseIndent
    ) {
        EmitBreakNode(*node.children[0], solution, baseIndent);
        const bool closesInContext = node.children.size() > 1 &&
            node.children[1]->kind == FormatBreakNodeKind::Token &&
            node.children[1]->token.contextOnly;
        BreakListLine(baseIndent + 1, HasBlankLineBeforeItem(node, 0));
        for (size_t index = 0; index < node.items.size(); ++index) {
            const FormatBreakListItem& item = node.items[index];
            EmitBreakNode(*item.node, solution, baseIndent + 1);
            if (FormatBreakTokenKind(item.separator) == PrintTokenKind::Known) {
                WriteBreakToken(item.separator);
            }
            if (HasTrailingComment(node, index)) {
                WriteBreakToken(item.trailingComment);
            }
            if (ShouldCombineSplitBracedItemBoundary(node, solution, index)) {
                Space();
            } else if (closesInContext && index + 1 == node.items.size()) {
                continue;
            } else {
                const bool hasNextItem = index + 1 < node.items.size();
                BreakListLine(
                    hasNextItem ? baseIndent + 1 : baseIndent,
                    hasNextItem && HasBlankLineBeforeItem(node, index + 1)
                );
            }
        }
        EmitBreakNode(*node.children[1], solution, baseIndent);
    }

    void EmitFunctionSignatureNode(const FormatBreakNode& node, const FormatBreakSolution& solution, int baseIndent) {
        const FormatBreakChoice choice = ChoiceFor(solution, node.id);
        if (choice != FormatBreakChoice::Split || node.children.size() < 2) {
            for (const FormatBreakNode* child : node.children) {
                EmitBreakNode(*child, solution, baseIndent);
            }
            return;
        }
        EmitBreakNode(*node.children[0], solution, baseIndent);
        NewLineWithIndent(baseIndent + 1);
        EmitBreakNode(*node.children[1], solution, baseIndent + 1);
        if (node.children.size() > 2) {
            if (node.functionSignatureHasBody) {
                NewLineWithIndent(baseIndent);
                EmitBreakNode(*node.children[2], solution, baseIndent);
            } else {
                EmitBreakNode(*node.children[2], solution, baseIndent + 1);
            }
        }
    }

    void EmitBodyHeaderNode(const FormatBreakNode& node, const FormatBreakSolution& solution, int baseIndent) {
        const FormatBreakChoice choice = ChoiceFor(solution, node.id);
        if (!IsBodyHeaderSplitChoice(choice) || node.children.size() < 2) {
            for (const FormatBreakNode* child : node.children) {
                EmitBreakNode(*child, solution, baseIndent);
            }
            return;
        }
        EmitBreakNode(*node.children[0], solution, baseIndent);
        const int bodyIndent =
            choice == FormatBreakChoice::BodyHeaderSplitAtParentIndent ? std::max(0, baseIndent - 1) : baseIndent;
        NewLineWithIndent(bodyIndent);
        EmitBreakNode(*node.children[1], solution, bodyIndent);
    }

    void EmitChainNode(const FormatBreakNode& node, const FormatBreakSolution& solution, int baseIndent) {
        const FormatBreakChoice choice = ChoiceFor(solution, node.id);
        if (choice == FormatBreakChoice::Compact) {
            for (size_t index = 0; index < node.operands.size(); ++index) {
                EmitBreakNode(*node.operands[index], solution, baseIndent);
                if (index < node.operators.size()) {
                    WriteBreakToken(node.operators[index]);
                }
            }
            return;
        }

        if (node.chainKind == FormatBreakChainKind::StreamBeforeOperator) {
            EmitBreakNode(*node.operands.front(), solution, baseIndent);
            NewLineWithIndent(baseIndent + 1);
            for (size_t index = 0; index < node.operators.size(); ++index) {
                WriteBreakToken(node.operators[index]);
                EmitBreakNode(*node.operands[index + 1], solution, baseIndent + 1);
                if (
                    choice == FormatBreakChoice::Split &&
                    index + 1 < node.operators.size() &&
                    !IsFormatBreakStreamConfigurationOperand(
                        *node.operands[index + 1],
                        config_.streamShiftConfigurationMethods
                    )
                ) {
                    NewLineWithIndent(baseIndent + 1);
                }
            }
            return;
        }

        if (node.chainKind == FormatBreakChainKind::Ternary && node.operators.size() > 2) {
            for (size_t index = 0; index < node.operands.size(); ++index) {
                EmitBreakNode(*node.operands[index], solution, index == 0 ? baseIndent : baseIndent + 1);
                if (index < node.operators.size()) {
                    WriteBreakToken(node.operators[index]);
                    if (
                        FormatBreakTokenKind(node.operators[index]) == PrintTokenKind::Known &&
                        FormatBreakTokenSyntaxKind(node.operators[index]) == SyntaxNodeKind::Colon
                    ) {
                        NewLineWithIndent(baseIndent + 1);
                    }
                }
            }
            return;
        }

        if (node.chainKind == FormatBreakChainKind::Ternary && node.operators.size() == 2) {
            const int continuationIndent = node.flatSplitIndent ? baseIndent : baseIndent + 1;
            const bool breakAfterQuestion =
                choice == FormatBreakChoice::TernaryBreakAfterQuestion || choice == FormatBreakChoice::Split;
            const bool breakAfterColon =
                choice == FormatBreakChoice::TernaryBreakAfterColon || choice == FormatBreakChoice::Split;
            for (size_t index = 0; index < node.operands.size(); ++index) {
                EmitBreakNode(*node.operands[index], solution, index == 0 ? baseIndent : continuationIndent);
                if (index < node.operators.size()) {
                    WriteBreakToken(node.operators[index]);
                    if ((index == 0 && breakAfterQuestion) || (index == 1 && breakAfterColon)) {
                        NewLineWithIndent(continuationIndent);
                    }
                }
            }
            return;
        }

        const int continuationIndent = node.flatSplitIndent ? baseIndent : baseIndent + 1;
        for (size_t index = 0; index < node.operands.size(); ++index) {
            EmitBreakNode(*node.operands[index], solution, index == 0 ? baseIndent : continuationIndent);
            if (index < node.operators.size()) {
                WriteBreakToken(node.operators[index]);
                if (
                    index + 1 < node.operands.size() &&
                    node.operands[index + 1]->kind == FormatBreakNodeKind::Delimited &&
                    ChoiceFor(solution, node.operands[index + 1]->id) == FormatBreakChoice::SplitAttachedOpen
                ) {
                    const size_t attachedOperandIndex = index + 1;
                    EmitDelimitedNodeAfterAttachedOpen(
                        *node.operands[attachedOperandIndex],
                        solution,
                        continuationIndent
                    );
                    if (attachedOperandIndex < node.operators.size()) {
                        WriteBreakToken(node.operators[attachedOperandIndex]);
                        NewLineWithIndent(continuationIndent);
                    }
                    ++index;
                    continue;
                }
                NewLineWithIndent(continuationIndent);
            }
        }
    }

    void EmitAdjacentStringsNode(const FormatBreakNode& node, const FormatBreakSolution& solution, int baseIndent) {
        const FormatBreakChoice choice = ChoiceFor(solution, node.id);
        const int continuationIndent = node.flatSplitIndent ? baseIndent : baseIndent + 1;
        for (size_t index = 0; index < node.operands.size(); ++index) {
            if (choice == FormatBreakChoice::Split && index > 0) {
                NewLineWithIndent(continuationIndent);
            }
            EmitBreakNode(*node.operands[index], solution, index == 0 ? baseIndent : continuationIndent);
        }
    }

    bool UsesSplitContextClose(const FormatBreakNode& node, const FormatBreakSolution& solution) const {
        if (
            node.kind == FormatBreakNodeKind::Delimited &&
            node.children.size() > 1 &&
            node.children[1]->kind == FormatBreakNodeKind::Token &&
            node.children[1]->token.contextOnly &&
            IsSplitChoice(ChoiceFor(solution, node.id))
        ) {
            return true;
        }
        for (const FormatBreakNode* child : node.children) {
            if (child && UsesSplitContextClose(*child, solution)) {
                return true;
            }
        }
        for (const FormatBreakListItem& item : node.items) {
            if (item.node && UsesSplitContextClose(*item.node, solution)) {
                return true;
            }
        }
        for (const FormatBreakNode* operand : node.operands) {
            if (operand && UsesSplitContextClose(*operand, solution)) {
                return true;
            }
        }
        return false;
    }

    bool FlushPendingTokens(const FormatBreakModelContext& context = {}) {
        if (pendingTokens_.empty()) {
            return false;
        }
        if (CanFlushPendingTokensCompact(context)) {
            FlushPendingTokensCompact();
            if (pendingIndentRestoreAfterFlush_) {
                indentLevel_ = *pendingIndentRestoreAfterFlush_;
                pendingIndentRestoreAfterFlush_.reset();
            }
            return false;
        }
        const auto modelStart = std::chrono::steady_clock::now();
        FormatBreakModel model = BuildFormatBreakModel(pendingTokens_, context);
        if (stats_ != nullptr) {
            stats_->breakModel += std::chrono::steady_clock::now() - modelStart;
        }
        const bool previousEmittingMacroValue = emittingMacroValue_;
        emittingMacroValue_ = std::any_of(
            pendingTokens_.begin(),
            pendingTokens_.end(),
            [](const PrintToken& token) { return token.inMacroValue; }
        );
        if (emittingMacroValue_ && pendingTokens_.front().inMacroValue && atLineStart_) {
            pendingIndentLevel_ = std::max(pendingIndentLevel_.value_or(0), indentLevel_ + 1);
        }
        const int baseIndentLevel = pendingIndentLevel_.value_or(indentLevel_);
        const auto solveStart = std::chrono::steady_clock::now();
        FormatBreakSolution solution =
            SolveFormatBreaks(config_, model, CurrentColumn(), baseIndentLevel, indentWidth_);
        if (stats_ != nullptr) {
            stats_->solve += std::chrono::steady_clock::now() - solveStart;
        }
        bool splitContextClose = false;
        if (model.root) {
            splitContextClose = UsesSplitContextClose(*model.root, solution);
            const auto emitStart = std::chrono::steady_clock::now();
            EmitBreakNode(*model.root, solution, baseIndentLevel);
            if (stats_ != nullptr) {
                stats_->emit += std::chrono::steady_clock::now() - emitStart;
            }
        }
        emittingMacroValue_ = previousEmittingMacroValue;
        pendingTokens_.clear();
        if (pendingIndentRestoreAfterFlush_) {
            indentLevel_ = *pendingIndentRestoreAfterFlush_;
            pendingIndentRestoreAfterFlush_.reset();
        }
        return splitContextClose;
    }

    bool HasBufferedLineText() const {
        return lineHasText_ || !pendingTokens_.empty();
    }

    std::optional<LambdaSplitCallPlan> BuildLambdaSplitCallPlan(const PrintToken& token) const {
        if (
            activeTokens_ == nullptr ||
            token.kind != PrintTokenKind::Known ||
            token.syntaxKind != SyntaxNodeKind::LeftBrace ||
            token.inSingleStatementLambdaBody ||
            token.parentKind != SyntaxNodeKind::CompoundStatement ||
            token.grandParentKind != SyntaxNodeKind::LambdaExpression
        ) {
            return std::nullopt;
        }
        const SyntaxNode* compound = NearestAncestor(token, SyntaxNodeKind::CompoundStatement);
        const SyntaxNode* lambda = NearestAncestor(token, SyntaxNodeKind::LambdaExpression);
        if (compound == nullptr || lambda == nullptr) {
            return std::nullopt;
        }
        const SyntaxNode* argumentList = NearestAncestorBefore(token, SyntaxNodeKind::ArgumentList, lambda);
        if (argumentList == nullptr) {
            return std::nullopt;
        }
        const SyntaxNode* argumentOpen = DirectTokenChild(*argumentList, SyntaxNodeKind::LeftParen);
        const SyntaxNode* argumentClose = DirectTokenChild(*argumentList, SyntaxNodeKind::RightParen);
        const SyntaxNode* lambdaClose = DirectTokenChild(*compound, SyntaxNodeKind::RightBrace);
        if (
            argumentOpen == nullptr ||
            argumentClose == nullptr ||
            lambdaClose == nullptr ||
            !HasDirectTokenChild(*argumentList, SyntaxNodeKind::Comma)
        ) {
            return std::nullopt;
        }
        const std::optional<size_t> closeIndex = FindTokenIndex(argumentClose, currentTokenIndex_ + 1);
        const std::optional<size_t> lambdaCloseIndex = FindTokenIndex(lambdaClose, currentTokenIndex_ + 1);
        if (!closeIndex || !lambdaCloseIndex || *lambdaCloseIndex >= *closeIndex) {
            return std::nullopt;
        }

        FormatBreakToken virtualClose{&(*activeTokens_)[*closeIndex], false, true};
        const int baseIndentLevel = pendingIndentLevel_.value_or(indentLevel_);
        bool hasCommaAfterLambda = false;
        for (size_t index = *lambdaCloseIndex + 1; index < *closeIndex; ++index) {
            const PrintToken& candidate = (*activeTokens_)[index];
            if (
                candidate.kind == PrintTokenKind::Known &&
                candidate.syntaxKind == SyntaxNodeKind::Comma &&
                SyntaxPathContains(candidate, argumentList)
            ) {
                hasCommaAfterLambda = true;
                break;
            }
        }

        return LambdaSplitCallPlan{
            .breakContext = {
                .virtualDelimiterOpen = argumentOpen,
                .virtualDelimiterClose = virtualClose,
                .forceSplitVirtualDelimiter = hasCommaAfterLambda
            },
            .deferredContext = {
                .argumentList = argumentList,
                .lambdaRightBrace = lambdaClose,
                .closeToken = argumentClose,
                .argumentIndent = baseIndentLevel + 1,
                .closeIndent = baseIndentLevel
            }
        };
    }

    bool InEnumBody() const {
        return !braceStack_.empty() && braceStack_.back().role == BraceRole::Enum;
    }

    bool ShouldBreakAfterSemicolon() const {
        if (braceStack_.empty()) {
            return parenDepth_ == 0;
        }
        return parenDepth_ <= braceStack_.back().parenDepth;
    }

    bool ShouldContinueMacroLine(const PrintToken& token, const PrintToken* next) const {
        return token.inMacroValue && next != nullptr && next->macroDefinition == token.macroDefinition;
    }

    const PrintToken* RawTokenAfterCurrent(size_t offset) const {
        if (activeTokens_ == nullptr || currentTokenIndex_ + offset >= activeTokens_->size()) {
            return nullptr;
        }
        return &(*activeTokens_)[currentTokenIndex_ + offset];
    }

    bool ShouldBreakAfterCompactEmptyBlock(const PrintToken& token) const {
        if (RoleForBrace(token) != BraceRole::Block) {
            return false;
        }
        const PrintToken* afterClose = RawTokenAfterCurrent(2);
        return afterClose != nullptr &&
            afterClose->kind == PrintTokenKind::Known &&
            afterClose->syntaxKind != SyntaxNodeKind::KeywordWhile &&
            SyntaxNodeKindHasClass(afterClose->syntaxKind, TokenClass::AttachAfterBlockKeyword);
    }

    DeferredSplitCallContext* ActiveDeferredSplitCallContext() {
        return deferredSplitCallContexts_.empty() ? nullptr : &deferredSplitCallContexts_.back();
    }

    void MarkDeferredSplitLambdaClosed(const PrintToken& token) {
        DeferredSplitCallContext* context = ActiveDeferredSplitCallContext();
        if (context != nullptr && context->lambdaRightBrace == token.node) {
            context->afterLambdaClose = true;
        }
    }

    bool TryPrintDeferredSplitCallComma(const PrintToken& token) {
        DeferredSplitCallContext* context = ActiveDeferredSplitCallContext();
        if (
            context == nullptr ||
            !context->afterLambdaClose ||
            token.kind != PrintTokenKind::Known ||
            token.syntaxKind != SyntaxNodeKind::Comma ||
            !SyntaxPathContains(token, context->argumentList)
        ) {
            return false;
        }
        BufferToken(token);
        FlushPendingTokens();
        NewLineWithIndent(context->argumentIndent);
        return true;
    }

    bool TryPrintDeferredSplitCallClose(const PrintToken& token) {
        DeferredSplitCallContext* context = ActiveDeferredSplitCallContext();
        if (
            context == nullptr ||
            !context->afterLambdaClose ||
            token.kind != PrintTokenKind::Known ||
            token.syntaxKind != SyntaxNodeKind::RightParen ||
            context->closeToken != token.node
        ) {
            return false;
        }
        if (HasBufferedLineText()) {
            FlushPendingTokens();
        }
        NewLineWithIndent(context->closeIndent);
        BufferToken(token);
        deferredSplitCallContexts_.pop_back();
        return true;
    }

    bool StartsMacroValueRun(const PrintToken* previous, const PrintToken& current) const {
        return current.inMacroValue &&
            (previous == nullptr || !FormatTokensShareMacroDefinition(previous, &current) || !previous->inMacroValue);
    }

    bool StartsMacroValueElement(const PrintToken* previous, const PrintToken& current) const {
        return current.inMacroValue &&
            previous != nullptr &&
            FormatTokensShareMacroDefinition(previous, &current) &&
            previous->inMacroValue &&
            previous->macroValueElement != nullptr &&
            current.macroValueElement != nullptr &&
            previous->macroValueElement != current.macroValueElement;
    }

    void PrepareMacroBoundary(const PrintToken* previous, const PrintToken& current) {
        if (current.macroDefinition != nullptr && !current.inMacroValue && atLineStart_) {
            forceColumnZeroLine_ = true;
            pendingIndentLevel_.reset();
        }
        if (
            previous != nullptr &&
            previous->macroDefinition != nullptr &&
            previous->macroDefinition != current.macroDefinition
        ) {
            if (HasBufferedLineText()) {
                FlushPendingTokens();
                NewLine(false);
            }
            if (current.macroDefinition != nullptr && !current.inMacroValue) {
                forceColumnZeroLine_ = true;
                pendingIndentLevel_.reset();
            }
            if (!IsPreprocessorLikeToken(current)) {
                BlankLine();
            }
        }
        if (
            current.macroDefinition != nullptr &&
            (previous == nullptr || previous->macroDefinition != current.macroDefinition) &&
            HasBufferedLineText()
        ) {
            FlushPendingTokens();
            NewLine(false);
            if (!current.inMacroValue) {
                forceColumnZeroLine_ = true;
                pendingIndentLevel_.reset();
            }
        }
        const bool startsMacroValue = StartsMacroValueRun(previous, current);
        const bool macroValueWouldOverflow = startsMacroValue &&
            CurrentColumn() + PendingCompactWidth() + 1 + current.macroValueRemainingWidth > config_.columnLimit;
        if (
            (
                (startsMacroValue && (current.breakBeforeMacroValue || macroValueWouldOverflow)) ||
                StartsMacroValueElement(previous, current)
            ) && HasBufferedLineText()
        ) {
            FlushPendingTokens();
            NewLine(true);
        }
    }

    void PrintOne(
        const PrintToken& token,
        const PrintToken* previous,
        const PrintToken* next,
        const PrintToken* rawNext
    ) {
        PrepareMacroBoundary(previous, token);
        if (token.kind == PrintTokenKind::BlankLine) {
            FlushPendingTokens();
            BlankLine();
            return;
        }
        if (IsCommentToken(token.kind)) {
            if (KeepsListCommentInBreakModel(token)) {
                BufferToken(token);
                return;
            }
            FlushPendingTokens();
            PrintComment(token);
            return;
        }
        if (token.kind == PrintTokenKind::Preprocessor) {
            FlushPendingTokens();
            PrintPreprocessor(token, next);
            return;
        }
        if (token.kind == PrintTokenKind::IncludeRun) {
            FlushPendingTokens();
            PrintIncludeRun(token, next);
            return;
        }
        if (token.kind == PrintTokenKind::Known) {
            PrintKnown(token, previous, next, rawNext);
            return;
        }
        BufferToken(token);
    }

    void PrintComment(const PrintToken& token) {
        if (lineHasText_) {
            Space();
            output_.push_back(' ');
            ++currentColumn_;
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
        currentColumn_ = 0;
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
        AdvanceCurrentColumn(line);
        lineHasText_ = true;
        atLineStart_ = false;
        NewLine();
        if (
            tools::StartsWith(line, "#pragma once") ||
            isUndef ||
            (!isInclude && next != nullptr && !IsPreprocessorLikeToken(*next))
        ) {
            BlankLine();
        }
    }

    void PrintKnown(
        const PrintToken& token,
        const PrintToken* previous,
        const PrintToken* next,
        const PrintToken* rawNext
    ) {
        switch (token.syntaxKind) {
            case SyntaxNodeKind::LeftParen:
                BufferToken(token);
                ++parenDepth_;
                return;
            case SyntaxNodeKind::RightParen:
                if (TryPrintDeferredSplitCallClose(token)) {
                    if (parenDepth_ > 0) {
                        --parenDepth_;
                    }
                    return;
                }
                BufferToken(token);
                if (parenDepth_ > 0) {
                    --parenDepth_;
                }
                if (
                    token.inRequiresClause &&
                    token.inTemplateDeclaration &&
                    !(next != nullptr && next->inRequiresClause)
                ) {
                    FlushPendingTokens();
                    NewLine(ShouldContinueMacroLine(token, next));
                }
                return;
            case SyntaxNodeKind::LeftBracket:
                BufferToken(token);
                ++bracketDepth_;
                return;
            case SyntaxNodeKind::RightBracket:
                BufferToken(token);
                if (bracketDepth_ > 0) {
                    --bracketDepth_;
                }
                return;
            case SyntaxNodeKind::Greater:
                BufferToken(token);
                if (
                    token.parentKind == SyntaxNodeKind::TemplateParameterList &&
                    token.inTemplateDeclaration &&
                    !(
                        next != nullptr &&
                        next->kind == PrintTokenKind::Known &&
                        next->syntaxKind == SyntaxNodeKind::KeywordRequires
                    )
                ) {
                    FlushPendingTokens();
                    NewLine(ShouldContinueMacroLine(token, next));
                }
                return;
            case SyntaxNodeKind::LeftBrace:
                PrintLeftBrace(token, previous, rawNext);
                return;
            case SyntaxNodeKind::RightBrace:
                PrintRightBrace(token, next, rawNext);
                return;
            case SyntaxNodeKind::Semicolon:
                BufferToken(token);
                if (
                    !token.inSingleStatementLambdaBody &&
                    ShouldBreakAfterSemicolon() &&
                    !(rawNext != nullptr && rawNext->kind == PrintTokenKind::TrailingComment)
                ) {
                    FlushPendingTokens();
                    NewLine(ShouldContinueMacroLine(token, next));
                }
                return;
            case SyntaxNodeKind::Comma:
                if (TryPrintDeferredSplitCallComma(token)) {
                    return;
                }
                BufferToken(token);
                if (
                    InEnumBody() &&
                    parenDepth_ == 0 &&
                    bracketDepth_ == 0 &&
                    !(rawNext != nullptr && rawNext->kind == PrintTokenKind::TrailingComment)
                ) {
                    FlushPendingTokens();
                    NewLine(ShouldContinueMacroLine(token, next));
                }
                return;
            case SyntaxNodeKind::Colon:
                BufferToken(token);
                if (token.parentKind == SyntaxNodeKind::CaseStatement) {
                    FlushPendingTokens();
                    ++indentLevel_;
                    activeCaseBodySwitchDepths_.push_back(switchDepth_);
                    if (
                        rawNext != nullptr &&
                        rawNext->kind == PrintTokenKind::Known &&
                        rawNext->syntaxKind == SyntaxNodeKind::LeftBrace
                    ) {
                        return;
                    }
                    NewLine(ShouldContinueMacroLine(token, next));
                    return;
                }
                if (
                    previous != nullptr &&
                    previous->kind == PrintTokenKind::Known && (
                        previous->syntaxKind == SyntaxNodeKind::KeywordDefault ||
                        SyntaxNodeKindHasClass(previous->syntaxKind, TokenClass::AccessKeyword)
                    )
                ) {
                    FlushPendingTokens();
                    NewLine(ShouldContinueMacroLine(token, next));
                }
                return;
            default:
                if (IsCaseLabelKeyword(token) && atLineStart_) {
                    CloseCaseBodyIndentIfNeeded();
                }
                if (IsAccessLabel(token, next)) {
                    FlushPendingTokens();
                    WriteWithIndentOffset(SyntaxNodeKindTokenText(token.syntaxKind), -1);
                    return;
                }
                if (
                    token.syntaxKind == SyntaxNodeKind::KeywordRequires &&
                    token.inTemplateDeclaration &&
                    token.splitRequiresClause &&
                    HasBufferedLineText()
                ) {
                    FlushPendingTokens();
                    NewLineWithIndent(indentLevel_ + 1);
                }
                BufferToken(token);
                if (
                    token.inRequiresClause &&
                    token.inTemplateDeclaration &&
                    !(next != nullptr && next->inRequiresClause)
                ) {
                    FlushPendingTokens();
                    NewLine(ShouldContinueMacroLine(token, next));
                }
                return;
        }
    }

    void PrintLeftBrace(const PrintToken& token, const PrintToken* previous, const PrintToken* rawNext) {
        const bool isEmptyBracePair = rawNext != nullptr &&
            rawNext->kind == PrintTokenKind::Known &&
            rawNext->syntaxKind == SyntaxNodeKind::RightBrace;
        const bool isCaseBlock = previous != nullptr &&
            previous->kind == PrintTokenKind::Known &&
            previous->syntaxKind == SyntaxNodeKind::Colon &&
            previous->parentKind == SyntaxNodeKind::CaseStatement;
        const BraceRole role = isCaseBlock ? BraceRole::CaseBlock : RoleForBrace(token);
        const bool followsConstructorInitializer = previous != nullptr && (
            previous->parentKind == SyntaxNodeKind::FieldInitializerList ||
            previous->grandParentKind == SyntaxNodeKind::FieldInitializer
        );
        if (!isEmptyBracePair && role == BraceRole::Block && followsConstructorInitializer && HasBufferedLineText()) {
            FlushPendingTokens();
            NewLine(ShouldContinueMacroLine(token, rawNext));
        }
        if (role == BraceRole::CaseBlock && lineHasText_) {
            Space();
        }
        if (isEmptyBracePair) {
            PrintToken compact = token;
            compact.kind = PrintTokenKind::Free;
            compact.syntaxKind = SyntaxNodeKind::Unknown;
            compact.text = "{}";
            BufferToken(compact);
            ++compactRightBraceSkips_;
            if (ShouldBreakAfterCompactEmptyBlock(token)) {
                const PrintToken* afterClose = RawTokenAfterCurrent(2);
                FlushPendingTokens();
                NewLine(ShouldContinueMacroLine(token, afterClose));
            }
            return;
        }
        const std::optional<LambdaSplitCallPlan> splitCallPlan = BuildLambdaSplitCallPlan(token);
        BufferToken(token);
        if (role == BraceRole::Compact) {
            return;
        }
        const bool splitCall =
            FlushPendingTokens(splitCallPlan ? splitCallPlan->breakContext : FormatBreakModelContext{});
        if (splitCallPlan && splitCall) {
            deferredSplitCallContexts_.push_back(splitCallPlan->deferredContext);
        }
        const bool functionBlock = token.parentKind == SyntaxNodeKind::CompoundStatement &&
            token.grandParentKind == SyntaxNodeKind::FunctionDefinition;
        const int openLineIndent = splitCallPlan && splitCall ? splitCallPlan->deferredContext.argumentIndent : (
            token.inMacroValue || functionBlock ?
                indentLevel_ : (lineHasText_ ? CurrentLineIndentLevel() : indentLevel_)
        );
        braceStack_.push_back({
            .role = role,
            .parenDepth = parenDepth_,
            .indentRestore = indentLevel_,
            .closeIndent = role == BraceRole::Block || role == BraceRole::Enum ? openLineIndent : indentLevel_
        });
        if (
            token.parentKind == SyntaxNodeKind::CompoundStatement &&
            token.grandParentKind == SyntaxNodeKind::SwitchStatement
        ) {
            ++switchDepth_;
        }
        if (role == BraceRole::Block || role == BraceRole::Enum) {
            indentLevel_ = std::max(indentLevel_, openLineIndent) + 1;
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
        const BraceRole tokenRole = RoleForBrace(token);
        if (tokenRole == BraceRole::Compact) {
            BufferToken(token);
            return;
        }
        const BraceRole role = braceStack_.empty() ? tokenRole : braceStack_.back().role;
        FlushPendingTokens();
        std::optional<int> restoreIndent;
        std::optional<int> closeIndent;
        if (!braceStack_.empty()) {
            restoreIndent = braceStack_.back().indentRestore;
            closeIndent = braceStack_.back().closeIndent;
            braceStack_.pop_back();
        }
        if (role == BraceRole::Namespace) {
            if (lineHasText_) {
                NewLine(token.inMacroValue);
            }
            BlankLine();
            BufferToken(token);
            FlushPendingTokens();
            if (rawNext != nullptr && rawNext->kind == PrintTokenKind::TrailingComment) {
                return;
            }
            NewLine(ShouldContinueMacroLine(token, next));
            return;
        }
        if (role == BraceRole::CaseBlock) {
            if (lineHasText_) {
                NewLine(token.inMacroValue);
            }
            WriteWithIndentOffset("}", -1);
            NewLine(ShouldContinueMacroLine(token, next));
            return;
        }
        if (role != BraceRole::Compact) {
            if (lineHasText_) {
                NewLine(token.inMacroValue);
            }
            const bool isSwitchBody = token.parentKind == SyntaxNodeKind::CompoundStatement &&
                token.grandParentKind == SyntaxNodeKind::SwitchStatement;
            if (isSwitchBody) {
                CloseCaseBodyIndentIfNeeded();
            }
            indentLevel_ = closeIndent.value_or(restoreIndent.value_or(std::max(0, indentLevel_ - 1)));
            if (restoreIndent && *restoreIndent != indentLevel_) {
                pendingIndentRestoreAfterFlush_ = *restoreIndent;
            }
            BufferToken(token);
            MarkDeferredSplitLambdaClosed(token);
            if (isSwitchBody) {
                switchDepth_ = std::max(0, switchDepth_ - 1);
            }
            if (next != nullptr && next->kind == PrintTokenKind::Known) {
                const bool closesLambdaArgument = token.parentKind == SyntaxNodeKind::CompoundStatement &&
                    token.grandParentKind == SyntaxNodeKind::LambdaExpression &&
                    next->syntaxKind == SyntaxNodeKind::RightParen;
                const bool attachesToFollowingKeyword =
                    SyntaxNodeKindHasClass(next->syntaxKind, TokenClass::AttachAfterBlockKeyword) &&
                        next->syntaxKind != SyntaxNodeKind::KeywordWhile;
                const bool closesDoWhile =
                    next->syntaxKind == SyntaxNodeKind::KeywordWhile && next->parentKind == SyntaxNodeKind::DoStatement;
                if (
                    next->syntaxKind == SyntaxNodeKind::Semicolon ||
                    next->syntaxKind == SyntaxNodeKind::Comma ||
                    closesLambdaArgument ||
                    attachesToFollowingKeyword ||
                    closesDoWhile
                ) {
                    return;
                }
            }
            if (
                next != nullptr &&
                token.parentKind == SyntaxNodeKind::FieldDeclarationList && (
                    token.grandParentKind == SyntaxNodeKind::StructSpecifier ||
                    token.grandParentKind == SyntaxNodeKind::ClassSpecifier
                )
            ) {
                return;
            }
            FlushPendingTokens();
            NewLine(ShouldContinueMacroLine(token, next));
            return;
        }
    }
};

}  // namespace

std::string FormatModelText(const FormatterConfig& config, const FormatModel& model, std::string_view sourcePath) {
    FormatModelTextStats stats;
    return FormatModelText(config, model, sourcePath, stats);
}

std::string FormatModelText(
    const FormatterConfig& config,
    const FormatModel& model,
    std::string_view sourcePath,
    FormatModelTextStats& stats
) {
    if (!model.root) {
        return {};
    }
    const auto tokenizeStart = std::chrono::steady_clock::now();
    std::vector<PrintToken> tokens;
    const size_t sourceSize = model.sourceText != nullptr ? model.sourceText->size() : 0;
    tokens.reserve(std::max<size_t>(256, sourceSize / 4));
    AppendTokens(
        *model.root,
        SyntaxNodeKind::Unknown,
        SyntaxNodeKind::Unknown,
        false,
        false,
        false,
        false,
        false,
        nullptr,
        nullptr,
        false,
        false,
        tokens
    );
    stats.tokenize += std::chrono::steady_clock::now() - tokenizeStart;
    const auto annotateStart = std::chrono::steady_clock::now();
    AnnotateMacroValueWidths(tokens);
    stats.annotate += std::chrono::steady_clock::now() - annotateStart;
    const auto printStart = std::chrono::steady_clock::now();
    std::string result = Printer(config, sourcePath, &stats).Print(tokens);
    stats.print += std::chrono::steady_clock::now() - printStart;
    return result;
}
