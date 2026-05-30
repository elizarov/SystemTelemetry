#include "tools/impl/format_spacing.h"

namespace {

bool IsDeclaratorReferenceParent(SyntaxNodeKind kind) {
    return SyntaxNodeKindHasClass(kind, TokenClass::DeclaratorReferenceParent);
}

bool IsReferenceToken(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known &&
        SyntaxNodeKindHasClass(token.syntaxKind, TokenClass::DeclaratorReferenceToken);
}

bool IsDeclaratorBindingToken(const PrintToken& token) {
    return IsDeclaratorReferenceParent(token.parentKind) && IsReferenceToken(token);
}

bool IsUnaryContext(const PrintToken& token) {
    return token.parentKind == SyntaxNodeKind::UnaryExpression;
}

bool IsBinaryContext(const PrintToken& token) {
    return token.parentKind == SyntaxNodeKind::BinaryExpression ||
        token.parentKind == SyntaxNodeKind::AssignmentExpression ||
        token.parentKind == SyntaxNodeKind::ConditionalExpression;
}

bool IsParenthesizedDeclarator(SyntaxNodeKind kind) {
    return SyntaxNodeKindHasClass(kind, TokenClass::ParenthesizedDeclarator);
}

bool IsWordBoundaryChar(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
}

bool LooksLikeStringLiteral(std::string_view text) {
    return text.find('"') != std::string_view::npos;
}

const SyntaxNode* ParentNode(const PrintToken& token) {
    return token.node != nullptr ? token.node->parent : nullptr;
}

const SyntaxNode* GrandParentNode(const PrintToken& token) {
    const SyntaxNode* parent = ParentNode(token);
    return parent != nullptr ? parent->parent : nullptr;
}

bool IsCompactEmptyBraceToken(const PrintToken& token) {
    return token.kind == PrintTokenKind::Free && token.text == "{}";
}

bool IsAttributeCloseToken(const PrintToken& token) {
    return token.kind == PrintTokenKind::Free && token.text == "]]" && (
        token.parentKind == SyntaxNodeKind::AttributeSpecifier ||
        token.parentKind == SyntaxNodeKind::AttributeDeclaration
    );
}

bool HasCallModifierBeforeDeclaratorBinding(const PrintToken& token) {
    const SyntaxNode* declarator = ParentNode(token);
    const SyntaxNode* parenthesized = GrandParentNode(token);
    if (
        declarator == nullptr ||
        parenthesized == nullptr ||
        !IsDeclaratorReferenceParent(declarator->kind) ||
        !IsParenthesizedDeclarator(parenthesized->kind)
    ) {
        return false;
    }
    for (const SyntaxNode* child : parenthesized->children) {
        if (child == declarator) {
            return false;
        }
        if (child != nullptr && child->kind == SyntaxNodeKind::MsCallModifier) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool IsPreprocessorPrintToken(PrintTokenKind kind) {
    return kind == PrintTokenKind::Preprocessor || kind == PrintTokenKind::IncludeRun;
}

bool IsPreprocessorLikeToken(const PrintToken& token) {
    return IsPreprocessorPrintToken(token.kind) || token.macroDefinition != nullptr;
}

bool IsCommentToken(PrintTokenKind kind) {
    return kind == PrintTokenKind::Comment || kind == PrintTokenKind::TrailingComment;
}

bool IsWordLike(const PrintToken& token) {
    if (token.kind == PrintTokenKind::Free) {
        return !token.text.empty() && (IsWordBoundaryChar(token.text.front()) || IsWordBoundaryChar(token.text.back()));
    }
    return token.kind == PrintTokenKind::Known && SyntaxNodeKindHasClass(token.syntaxKind, TokenClass::Keyword);
}

bool IsStringLike(const PrintToken& token) {
    return SyntaxNodeKindHasClass(token.syntaxKind, TokenClass::StringLike) ||
        (token.kind == PrintTokenKind::Free && LooksLikeStringLiteral(token.text));
}

bool IsAccessKeyword(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known && SyntaxNodeKindHasClass(token.syntaxKind, TokenClass::AccessKeyword);
}

bool IsCaseLabelKeyword(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known &&
        token.parentKind == SyntaxNodeKind::CaseStatement &&
        (token.syntaxKind == SyntaxNodeKind::KeywordCase || token.syntaxKind == SyntaxNodeKind::KeywordDefault);
}

bool FormatTokensShareMacroDefinition(const PrintToken* left, const PrintToken* right) {
    return left != nullptr &&
        right != nullptr &&
        left->macroDefinition != nullptr &&
        left->macroDefinition == right->macroDefinition;
}

bool FormatTokenNeedsSpace(const PrintToken* previous, const PrintToken& current) {
    if (previous == nullptr || (IsPreprocessorLikeToken(*previous) && previous->macroDefinition == nullptr) || (
        IsPreprocessorLikeToken(current) && current.macroDefinition == nullptr
    )) {
        return false;
    }
    if (current.inMacroValue && !previous->inMacroValue && FormatTokensShareMacroDefinition(previous, &current)) {
        return true;
    }
    if (current.kind == PrintTokenKind::Free && current.text == "{}") {
        if (
            current.parentKind == SyntaxNodeKind::CompoundStatement ||
            current.parentKind == SyntaxNodeKind::FieldDeclarationList ||
            current.parentKind == SyntaxNodeKind::DeclarationList ||
            current.parentKind == SyntaxNodeKind::EnumeratorList
        ) {
            return true;
        }
        return previous->kind == PrintTokenKind::Known && (
            SyntaxNodeKindHasClass(previous->syntaxKind, TokenClass::AssignmentOperator) ||
            previous->syntaxKind == SyntaxNodeKind::Comma ||
            previous->syntaxKind == SyntaxNodeKind::KeywordReturn ||
            previous->syntaxKind == SyntaxNodeKind::Colon ||
            previous->syntaxKind == SyntaxNodeKind::Question
        );
    }
    if (IsStringLike(*previous) && IsStringLike(current)) {
        return true;
    }
    if (IsAttributeCloseToken(*previous)) {
        return true;
    }
    if (IsCompactEmptyBraceToken(*previous) && current.kind == PrintTokenKind::Known) {
        return SyntaxNodeKindHasClass(current.syntaxKind, TokenClass::AttachAfterBlockKeyword);
    }
    if (previous->kind != PrintTokenKind::Known && current.kind != PrintTokenKind::Known) {
        return IsWordLike(*previous) && IsWordLike(current);
    }
    const SyntaxNodeKind prev =
        previous->kind == PrintTokenKind::Known ? previous->syntaxKind : SyntaxNodeKind::Unknown;
    const SyntaxNodeKind cur = current.kind == PrintTokenKind::Known ? current.syntaxKind : SyntaxNodeKind::Unknown;

    if ((cur == SyntaxNodeKind::Arrow && current.parentKind == SyntaxNodeKind::TrailingReturnType) || (
        prev == SyntaxNodeKind::Arrow && previous->parentKind == SyntaxNodeKind::TrailingReturnType
    )) {
        return true;
    }
    if (
        cur == SyntaxNodeKind::Dot &&
        current.parentKind == SyntaxNodeKind::FieldDesignator &&
        prev == SyntaxNodeKind::Comma
    ) {
        return true;
    }
    if (cur == SyntaxNodeKind::RightBrace && current.inSingleStatementLambdaBody) {
        return true;
    }
    if (
        cur == SyntaxNodeKind::RightParen ||
        cur == SyntaxNodeKind::RightBracket ||
        cur == SyntaxNodeKind::Comma ||
        cur == SyntaxNodeKind::Semicolon ||
        cur == SyntaxNodeKind::ColonColon ||
        cur == SyntaxNodeKind::Dot ||
        cur == SyntaxNodeKind::Arrow ||
        cur == SyntaxNodeKind::DotStar ||
        cur == SyntaxNodeKind::ArrowStar
    ) {
        if (cur == SyntaxNodeKind::ColonColon && (
            SyntaxNodeKindHasClass(prev, TokenClass::Keyword) ||
            SyntaxNodeKindHasClass(prev, TokenClass::AssignmentOperator) ||
            prev == SyntaxNodeKind::KeywordReturn
        )) {
            return true;
        }
        return false;
    }
    if (prev == SyntaxNodeKind::LeftBrace && previous->inSingleStatementLambdaBody) {
        return true;
    }
    if (
        prev == SyntaxNodeKind::LeftParen ||
        prev == SyntaxNodeKind::LeftBracket ||
        prev == SyntaxNodeKind::ColonColon ||
        prev == SyntaxNodeKind::Dot ||
        prev == SyntaxNodeKind::Arrow ||
        prev == SyntaxNodeKind::DotStar ||
        prev == SyntaxNodeKind::ArrowStar ||
        prev == SyntaxNodeKind::Tilde
    ) {
        return false;
    }
    if (prev == SyntaxNodeKind::KeywordOperator && cur != SyntaxNodeKind::LeftParen) {
        return false;
    }
    if (prev == SyntaxNodeKind::KeywordVirtual && cur == SyntaxNodeKind::Tilde) {
        return true;
    }
    if (prev == SyntaxNodeKind::KeywordCase && current.parentKind == SyntaxNodeKind::CaseStatement) {
        return true;
    }
    if (IsAccessKeyword(*previous) && IsWordLike(current)) {
        return true;
    }
    if (cur == SyntaxNodeKind::LeftParen) {
        if (
            current.parentKind == SyntaxNodeKind::MsCallModifier ||
            current.grandParentKind == SyntaxNodeKind::MsCallModifier
        ) {
            return false;
        }
        if (IsParenthesizedDeclarator(current.parentKind)) {
            return true;
        }
        if (
            previous->parentKind == SyntaxNodeKind::OperatorName || previous->parentKind == SyntaxNodeKind::OperatorCast
        ) {
            return false;
        }
        if (previous->kind == PrintTokenKind::Known && (
            SyntaxNodeKindHasClass(prev, TokenClass::AssignmentOperator) ||
            (SyntaxNodeKindHasClass(prev, TokenClass::BinaryOperator) && IsBinaryContext(*previous)) ||
            prev == SyntaxNodeKind::Comma ||
            prev == SyntaxNodeKind::KeywordReturn ||
            (prev == SyntaxNodeKind::Colon && previous->parentKind == SyntaxNodeKind::ConditionalExpression) ||
            prev == SyntaxNodeKind::Question
        )) {
            return true;
        }
        return previous->kind == PrintTokenKind::Known && SyntaxNodeKindHasClass(prev, TokenClass::ControlKeyword);
    }
    if (cur == SyntaxNodeKind::LeftBracket) {
        if (current.parentKind == SyntaxNodeKind::StructuredBindingDeclarator) {
            return true;
        }
        if (current.parentKind == SyntaxNodeKind::LambdaCaptureSpecifier) {
            return previous->kind == PrintTokenKind::Known && (
                SyntaxNodeKindHasClass(prev, TokenClass::AssignmentOperator) ||
                prev == SyntaxNodeKind::Comma ||
                prev == SyntaxNodeKind::KeywordReturn ||
                prev == SyntaxNodeKind::Question ||
                (prev == SyntaxNodeKind::Colon && previous->parentKind == SyntaxNodeKind::ConditionalExpression)
            );
        }
        return false;
    }
    if (cur == SyntaxNodeKind::LeftBrace) {
        if (current.parentKind == SyntaxNodeKind::InitializerList) {
            return previous->kind == PrintTokenKind::Known && (
                SyntaxNodeKindHasClass(prev, TokenClass::AssignmentOperator) ||
                prev == SyntaxNodeKind::Comma ||
                prev == SyntaxNodeKind::KeywordReturn ||
                prev == SyntaxNodeKind::Question ||
                prev == SyntaxNodeKind::Colon
            );
        }
        if (
            current.parentKind == SyntaxNodeKind::CompoundStatement ||
            current.parentKind == SyntaxNodeKind::FieldDeclarationList ||
            current.parentKind == SyntaxNodeKind::DeclarationList ||
            current.parentKind == SyntaxNodeKind::EnumeratorList
        ) {
            return true;
        }
        return prev == SyntaxNodeKind::KeywordReturn;
    }
    if (
        prev == SyntaxNodeKind::Semicolon &&
        cur == SyntaxNodeKind::Semicolon &&
        previous->parentKind == SyntaxNodeKind::ForStatement &&
        current.parentKind == SyntaxNodeKind::ForStatement
    ) {
        return false;
    }
    if (prev == SyntaxNodeKind::Comma || prev == SyntaxNodeKind::Semicolon || prev == SyntaxNodeKind::Question) {
        return true;
    }
    if (prev == SyntaxNodeKind::KeywordReturn) {
        return true;
    }
    if (prev == SyntaxNodeKind::Ellipsis && IsWordLike(current)) {
        return true;
    }
    if (cur == SyntaxNodeKind::Question) {
        return true;
    }
    if (prev == SyntaxNodeKind::Colon) {
        return current.parentKind != SyntaxNodeKind::CaseStatement &&
            !SyntaxNodeKindHasClass(previous->syntaxKind, TokenClass::AccessKeyword);
    }
    if (cur == SyntaxNodeKind::Less && prev == SyntaxNodeKind::KeywordTemplate) {
        return true;
    }
    if (cur == SyntaxNodeKind::Colon) {
        if (
            previous->syntaxKind == SyntaxNodeKind::KeywordDefault ||
            SyntaxNodeKindHasClass(previous->syntaxKind, TokenClass::AccessKeyword)
        ) {
            return false;
        }
        return current.parentKind != SyntaxNodeKind::CaseStatement;
    }
    if (IsDeclaratorBindingToken(current)) {
        return false;
    }
    if (IsDeclaratorBindingToken(*previous)) {
        if (cur == SyntaxNodeKind::Greater) {
            return false;
        }
        if (IsParenthesizedDeclarator(previous->grandParentKind)) {
            return HasCallModifierBeforeDeclaratorBinding(*previous) &&
                cur != SyntaxNodeKind::RightParen &&
                !IsDeclaratorBindingToken(current);
        }
        return !IsDeclaratorBindingToken(current);
    }
    if (prev == SyntaxNodeKind::KeywordReturn && current.kind == PrintTokenKind::Known && SyntaxNodeKindHasClass(
        cur,
        TokenClass::UnaryOperator
    )) {
        return true;
    }
    if (current.kind == PrintTokenKind::Known && (SyntaxNodeKindHasClass(cur, TokenClass::AssignmentOperator) || (
        SyntaxNodeKindHasClass(cur, TokenClass::BinaryOperator) && IsBinaryContext(current)
    ))) {
        return true;
    }
    if (previous->kind == PrintTokenKind::Known && (SyntaxNodeKindHasClass(prev, TokenClass::AssignmentOperator) || (
        SyntaxNodeKindHasClass(prev, TokenClass::BinaryOperator) && IsBinaryContext(*previous)
    ))) {
        return true;
    }
    if (
        current.kind == PrintTokenKind::Known &&
        SyntaxNodeKindHasClass(cur, TokenClass::UnaryOperator) &&
        IsUnaryContext(current)
    ) {
        return false;
    }
    if (
        previous->kind == PrintTokenKind::Known &&
        SyntaxNodeKindHasClass(prev, TokenClass::UnaryOperator) &&
        IsUnaryContext(*previous)
    ) {
        return false;
    }
    if (
        SyntaxNodeKindHasClass(prev, TokenClass::MemberOperator) ||
        SyntaxNodeKindHasClass(cur, TokenClass::MemberOperator)
    ) {
        return false;
    }
    if (IsWordLike(*previous) && IsWordLike(current)) {
        return true;
    }
    if (
        previous->kind == PrintTokenKind::Known && (
            prev == SyntaxNodeKind::RightParen ||
            prev == SyntaxNodeKind::RightBracket ||
            prev == SyntaxNodeKind::RightBrace ||
            prev == SyntaxNodeKind::Greater
        ) &&
        IsWordLike(current)
    ) {
        if (prev == SyntaxNodeKind::RightParen && previous->parentKind == SyntaxNodeKind::CastExpression) {
            return false;
        }
        return true;
    }
    return false;
}

std::string_view FormatTokenText(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known && token.text.empty() ? SyntaxNodeKindTokenText(token.syntaxKind) :
        token.text;
}

int FormatTokenWidth(const PrintToken& token) {
    return static_cast<int>(FormatTokenText(token).size());
}
