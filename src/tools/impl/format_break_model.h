#pragma once

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
    PrintToken token;
    bool spaceBefore = false;
    bool contextOnly = false;
};

struct FormatBreakNode {
    int id = 0;
    int structuralDepth = 0;
    FormatBreakNodeKind kind = FormatBreakNodeKind::Sequence;
    FormatBreakToken token;
    FormatBreakDelimiterKind delimiterKind = FormatBreakDelimiterKind::None;
    FormatBreakChainKind chainKind = FormatBreakChainKind::AfterOperator;
    bool forceSplit = false;
    bool flatSplitIndent = false;
    std::vector<std::unique_ptr<FormatBreakNode>> children;
    std::vector<std::unique_ptr<FormatBreakNode>> items;
    std::vector<FormatBreakToken> separators;
    std::vector<FormatBreakToken> trailingComments;
    std::vector<bool> blankLinesBeforeItems;
    std::vector<std::unique_ptr<FormatBreakNode>> operands;
    std::vector<FormatBreakToken> operators;
};

struct FormatBreakModel {
    std::unique_ptr<FormatBreakNode> root;
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
