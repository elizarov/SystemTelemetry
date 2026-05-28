#pragma once

#include <string_view>
#include <vector>

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
    KnownToken known = KnownToken::Unknown;
    std::string_view text;
    SyntaxTreeKind treeKind = SyntaxTreeKind::Unknown;
    SyntaxTreeKind parentKind = SyntaxTreeKind::Unknown;
    SyntaxTreeKind grandParentKind = SyntaxTreeKind::Unknown;
    bool inTemplateDeclaration = false;
    bool inRequiresClause = false;
    bool splitRequiresClause = false;
    bool inSingleStatementLambdaBody = false;
    bool inMacroValue = false;
    bool breakBeforeMacroValue = false;
    int macroValueRemainingWidth = 0;
    const SyntaxNode* node = nullptr;
    std::vector<const SyntaxNode*> syntaxPath;
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
bool IsCompilerCallModifierStart(const PrintToken* token);
bool FormatTokensShareMacroDefinition(const PrintToken* left, const PrintToken* right);
bool FormatTokenNeedsSpace(const PrintToken* previous, const PrintToken& current);
std::string_view FormatTokenText(const PrintToken& token);
int FormatTokenWidth(const PrintToken& token);
