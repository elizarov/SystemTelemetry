#pragma once

#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "tools/impl/format_spacing.h"

enum class FormatBreakNodeKind {
    Token,
    Sequence,
    Delimited,
    PrefixList,
    FunctionSignature,
    BodyHeader,
    Chain,
    AdjacentStrings,
};

enum class FormatBreakDelimiterKind {
    None,
    Paren,
    Bracket,
    Brace,
    Angle,
};

enum class FormatBreakChainKind {
    AfterOperator,
    StreamBeforeOperator,
    Ternary,
};

enum class FormatBreakChoice {
    Compact,
    Split,
    SplitAttachedOpen,
    SplitDelimiterStack,
    StreamCompactTail,
    TernaryBreakAfterQuestion,
    TernaryBreakAfterColon,
};

struct FormatBreakToken {
    const PrintToken* token = nullptr;
    bool spaceBefore = false;
    bool contextOnly = false;
};

inline const PrintToken& FormatBreakTokenValue(const FormatBreakToken& token) {
    static const PrintToken kEmptyToken;
    return token.token == nullptr ? kEmptyToken : *token.token;
}

inline PrintTokenKind FormatBreakTokenKind(const FormatBreakToken& token) {
    return token.token == nullptr ? PrintTokenKind::Free : token.token->kind;
}

inline SyntaxNodeKind FormatBreakTokenSyntaxKind(const FormatBreakToken& token) {
    return token.token == nullptr ? SyntaxNodeKind::Unknown : token.token->syntaxKind;
}

struct FormatBreakNode {
    int id = 0;
    int structuralDepth = 0;
    FormatBreakNodeKind kind = FormatBreakNodeKind::Sequence;
    FormatBreakToken token;
    FormatBreakDelimiterKind delimiterKind = FormatBreakDelimiterKind::None;
    FormatBreakChainKind chainKind = FormatBreakChainKind::AfterOperator;
    bool forceSplit = false;
    bool flatSplitIndent = false;
    bool functionSignatureHasBody = false;
    std::vector<FormatBreakNode*> children;
    std::vector<FormatBreakNode*> items;
    std::vector<FormatBreakToken> separators;
    std::vector<FormatBreakToken> trailingComments;
    std::vector<bool> blankLinesBeforeItems;
    std::vector<FormatBreakNode*> operands;
    std::vector<FormatBreakToken> operators;
};

struct FormatBreakModel {
    std::unique_ptr<std::deque<FormatBreakNode>> nodes;
    FormatBreakNode* root = nullptr;
};

struct FormatBreakModelContext {
    const SyntaxNode* virtualDelimiterOpen = nullptr;
    FormatBreakToken virtualDelimiterClose;
    bool forceSplitVirtualDelimiter = false;
};

bool FormatBreakLeadingNameMatches(const FormatBreakNode& node, std::string_view candidate);
bool IsFormatBreakStreamConfigurationOperand(
    const FormatBreakNode& node,
    const std::vector<std::string>& configurationMethods
);
