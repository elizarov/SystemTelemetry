#pragma once

#include <string_view>

#include "tools/impl/format_model.h"

enum class PrintTokenKind {
    Known,
    Free,
    Comment,
    TrailingComment,
    BlankLine,
    Preprocessor,
    IncludeRun,
};

struct PrintToken {
    PrintTokenKind kind = PrintTokenKind::Free;
    SyntaxNodeKind syntaxKind = SyntaxNodeKind::Unknown;
    std::string_view text;
    SyntaxNodeKind parentKind = SyntaxNodeKind::Unknown;
    SyntaxNodeKind grandParentKind = SyntaxNodeKind::Unknown;
    bool inTemplateDeclaration = false;
    bool inRequiresClause = false;
    bool inCompilerCallModifier = false;
    bool inSingleStatementLambdaBody = false;
    bool inMacroValue = false;
    bool breakBeforeMacroValue = false;
    int macroValueRemainingWidth = 0;
    const SyntaxNode* node = nullptr;
    const SyntaxNode* macroDefinition = nullptr;
    const SyntaxNode* macroValueElement = nullptr;
};

bool IsPreprocessorPrintToken(PrintTokenKind kind);
bool IsPreprocessorLikeToken(const PrintToken& token);
bool IsCommentToken(PrintTokenKind kind);
bool IsWordLike(const PrintToken& token);
bool IsStringLike(const PrintToken& token);
bool IsAccessKeyword(const PrintToken& token);
bool IsCaseLabelKeyword(const PrintToken& token);
bool FormatTokensShareMacroDefinition(const PrintToken* left, const PrintToken* right);
bool FormatTokenNeedsSpace(const PrintToken* previous, const PrintToken& current);
std::string_view FormatTokenText(const PrintToken& token);
int FormatTokenWidth(const PrintToken& token);
