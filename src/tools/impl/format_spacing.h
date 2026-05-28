#pragma once

#include <iterator>
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

struct PrintTokenSyntaxPath {
    using const_iterator = std::vector<const SyntaxNode*>::const_iterator;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    const std::vector<const SyntaxNode*>* storage = nullptr;
    size_t offset = 0;
    size_t length = 0;

    bool empty() const {
        return length == 0;
    }

    size_t size() const {
        return length;
    }

    const SyntaxNode* operator[](size_t index) const {
        return (*storage)[offset + index];
    }

    const_iterator begin() const {
        return storage == nullptr ? Empty().begin() : storage->begin() + static_cast<std::ptrdiff_t>(offset);
    }

    const_iterator end() const {
        return storage == nullptr ? Empty().end() : begin() + static_cast<std::ptrdiff_t>(length);
    }

    const_reverse_iterator rbegin() const {
        return const_reverse_iterator(end());
    }

    const_reverse_iterator rend() const {
        return const_reverse_iterator(begin());
    }

private:
    static const std::vector<const SyntaxNode*>& Empty() {
        static const std::vector<const SyntaxNode*> kEmpty;
        return kEmpty;
    }
};

struct PrintToken {
    PrintTokenKind kind = PrintTokenKind::Free;
    SyntaxNodeKind syntaxKind = SyntaxNodeKind::Unknown;
    std::string_view text;
    SyntaxNodeKind parentKind = SyntaxNodeKind::Unknown;
    SyntaxNodeKind grandParentKind = SyntaxNodeKind::Unknown;
    bool inTemplateDeclaration = false;
    bool inRequiresClause = false;
    bool splitRequiresClause = false;
    bool inSingleStatementLambdaBody = false;
    bool inMacroValue = false;
    bool breakBeforeMacroValue = false;
    int macroValueRemainingWidth = 0;
    const SyntaxNode* node = nullptr;
    PrintTokenSyntaxPath syntaxPath;
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
