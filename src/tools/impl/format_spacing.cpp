#include "tools/impl/format_spacing.h"

namespace {

bool IsDeclaratorReferenceParent(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::PointerDeclarator ||
        kind == SyntaxTreeKind::AbstractPointerDeclarator ||
        kind == SyntaxTreeKind::ReferenceDeclarator ||
        kind == SyntaxTreeKind::AbstractReferenceDeclarator ||
        kind == SyntaxTreeKind::HandleDeclarator ||
        kind == SyntaxTreeKind::AbstractHandleDeclarator ||
        kind == SyntaxTreeKind::MemberPointerDeclarator;
}

bool IsReferenceToken(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known && KnownTokenHasClass(token.known, TokenClass::DeclaratorReferenceToken);
}

bool IsDeclaratorBindingToken(const PrintToken& token) {
    return IsDeclaratorReferenceParent(token.parentKind) && IsReferenceToken(token);
}

bool IsUnaryContext(const PrintToken& token) {
    return token.parentKind == SyntaxTreeKind::UnaryExpression;
}

bool IsBinaryContext(const PrintToken& token) {
    return token.parentKind == SyntaxTreeKind::BinaryExpression ||
        token.parentKind == SyntaxTreeKind::AssignmentExpression ||
        token.parentKind == SyntaxTreeKind::ConditionalExpression;
}

bool IsParenthesizedDeclarator(SyntaxTreeKind kind) {
    return kind == SyntaxTreeKind::ParenthesizedDeclarator || kind == SyntaxTreeKind::AbstractParenthesizedDeclarator;
}

bool IsWordBoundaryChar(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
}

bool LooksLikeStringLiteral(std::string_view text) {
    return text.find('"') != std::string_view::npos;
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
    return token.kind == PrintTokenKind::Known && KnownTokenHasClass(token.known, TokenClass::Keyword);
}

bool IsStringLike(const PrintToken& token) {
    return token.treeKind == SyntaxTreeKind::ConcatenatedString ||
        token.treeKind == SyntaxTreeKind::StringLiteral ||
        token.treeKind == SyntaxTreeKind::RawStringLiteral ||
        token.treeKind == SyntaxTreeKind::CharacterLiteral ||
        (token.kind == PrintTokenKind::Free && LooksLikeStringLiteral(token.text));
}

bool IsAccessKeyword(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known && KnownTokenHasClass(token.known, TokenClass::AccessKeyword);
}

bool IsCaseLabelKeyword(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known &&
        token.parentKind == SyntaxTreeKind::CaseStatement &&
        (token.known == KnownToken::KeywordCase || token.known == KnownToken::KeywordDefault);
}

bool IsCompilerCallModifierStart(const PrintToken* token) {
    return token != nullptr &&
        token->kind == PrintTokenKind::Known &&
        (token->known == KnownToken::KeywordCdecl || token->known == KnownToken::KeywordDeclspec);
}

bool FormatTokensShareMacroDefinition(const PrintToken* left, const PrintToken* right) {
    return left != nullptr &&
        right != nullptr &&
        left->macroDefinition != nullptr &&
        left->macroDefinition == right->macroDefinition;
}

bool FormatTokenNeedsSpace(const PrintToken* previous, const PrintToken& current) {
    if (
        previous == nullptr ||
        (IsPreprocessorLikeToken(*previous) && previous->macroDefinition == nullptr) ||
        (IsPreprocessorLikeToken(current) && current.macroDefinition == nullptr)
    ) {
        return false;
    }
    if (current.inMacroValue && !previous->inMacroValue && FormatTokensShareMacroDefinition(previous, &current)) {
        return true;
    }
    if (current.kind == PrintTokenKind::Free && current.text == "{}") {
        if (
            current.parentKind == SyntaxTreeKind::CompoundStatement ||
            current.parentKind == SyntaxTreeKind::FieldDeclarationList ||
            current.parentKind == SyntaxTreeKind::DeclarationList ||
            current.parentKind == SyntaxTreeKind::EnumeratorList
        ) {
            return true;
        }
        return previous->kind == PrintTokenKind::Known && (
            KnownTokenHasClass(previous->known, TokenClass::AssignmentOperator) ||
            previous->known == KnownToken::Comma ||
            previous->known == KnownToken::KeywordReturn ||
            previous->known == KnownToken::Colon ||
            previous->known == KnownToken::Question
        );
    }
    if (IsStringLike(*previous) && IsStringLike(current)) {
        return true;
    }
    if (previous->kind != PrintTokenKind::Known && current.kind != PrintTokenKind::Known) {
        return IsWordLike(*previous) && IsWordLike(current);
    }
    const KnownToken prev = previous->kind == PrintTokenKind::Known ? previous->known : KnownToken::Unknown;
    const KnownToken cur = current.kind == PrintTokenKind::Known ? current.known : KnownToken::Unknown;

    if (
        (cur == KnownToken::Arrow && current.parentKind == SyntaxTreeKind::TrailingReturnType) ||
        (prev == KnownToken::Arrow && previous->parentKind == SyntaxTreeKind::TrailingReturnType)
    ) {
        return true;
    }
    if (cur == KnownToken::Dot && current.parentKind == SyntaxTreeKind::FieldDesignator && prev == KnownToken::Comma) {
        return true;
    }
    if (cur == KnownToken::RightBrace && current.inSingleStatementLambdaBody) {
        return true;
    }
    if (
        cur == KnownToken::RightParen ||
        cur == KnownToken::RightBracket ||
        cur == KnownToken::Comma ||
        cur == KnownToken::Semicolon ||
        cur == KnownToken::ColonColon ||
        cur == KnownToken::Dot ||
        cur == KnownToken::Arrow ||
        cur == KnownToken::DotStar ||
        cur == KnownToken::ArrowStar
    ) {
        if (cur == KnownToken::ColonColon && (
            KnownTokenHasClass(prev, TokenClass::Keyword) ||
            KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
            prev == KnownToken::KeywordReturn
        )) {
            return true;
        }
        return false;
    }
    if (prev == KnownToken::LeftBrace && previous->inSingleStatementLambdaBody) {
        return true;
    }
    if (
        prev == KnownToken::LeftParen ||
        prev == KnownToken::LeftBracket ||
        prev == KnownToken::ColonColon ||
        prev == KnownToken::Dot ||
        prev == KnownToken::Arrow ||
        prev == KnownToken::DotStar ||
        prev == KnownToken::ArrowStar ||
        prev == KnownToken::Tilde
    ) {
        return false;
    }
    if (prev == KnownToken::KeywordOperator && cur != KnownToken::LeftParen) {
        return false;
    }
    if (prev == KnownToken::KeywordVirtual && cur == KnownToken::Tilde) {
        return true;
    }
    if (cur == KnownToken::LeftParen) {
        if (
            current.parentKind == SyntaxTreeKind::MsCallModifier ||
            current.grandParentKind == SyntaxTreeKind::MsCallModifier
        ) {
            return false;
        }
        if (IsParenthesizedDeclarator(current.parentKind)) {
            return true;
        }
        if (
            previous->parentKind == SyntaxTreeKind::OperatorName || previous->parentKind == SyntaxTreeKind::OperatorCast
        ) {
            return false;
        }
        if (previous->kind == PrintTokenKind::Known && (
            KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
            (KnownTokenHasClass(prev, TokenClass::BinaryOperator) && IsBinaryContext(*previous)) ||
            prev == KnownToken::Comma ||
            prev == KnownToken::KeywordReturn ||
            (prev == KnownToken::Colon && previous->parentKind == SyntaxTreeKind::ConditionalExpression) ||
            prev == KnownToken::Question
        )) {
            return true;
        }
        return previous->kind == PrintTokenKind::Known && KnownTokenHasClass(prev, TokenClass::ControlKeyword);
    }
    if (cur == KnownToken::LeftBracket) {
        if (current.parentKind == SyntaxTreeKind::StructuredBindingDeclarator) {
            return true;
        }
        if (current.parentKind == SyntaxTreeKind::LambdaCaptureSpecifier) {
            return previous->kind == PrintTokenKind::Known && (
                KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
                prev == KnownToken::Comma ||
                prev == KnownToken::KeywordReturn ||
                prev == KnownToken::Question ||
                (prev == KnownToken::Colon && previous->parentKind == SyntaxTreeKind::ConditionalExpression)
            );
        }
        return false;
    }
    if (cur == KnownToken::LeftBrace) {
        if (current.parentKind == SyntaxTreeKind::InitializerList) {
            return previous->kind == PrintTokenKind::Known && (
                KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
                prev == KnownToken::Comma ||
                prev == KnownToken::KeywordReturn ||
                prev == KnownToken::Question ||
                prev == KnownToken::Colon
            );
        }
        if (
            current.parentKind == SyntaxTreeKind::CompoundStatement ||
            current.parentKind == SyntaxTreeKind::FieldDeclarationList ||
            current.parentKind == SyntaxTreeKind::DeclarationList ||
            current.parentKind == SyntaxTreeKind::EnumeratorList
        ) {
            return true;
        }
        return prev == KnownToken::KeywordReturn;
    }
    if (
        prev == KnownToken::Semicolon &&
        cur == KnownToken::Semicolon &&
        previous->parentKind == SyntaxTreeKind::ForStatement &&
        current.parentKind == SyntaxTreeKind::ForStatement
    ) {
        return false;
    }
    if (prev == KnownToken::Comma || prev == KnownToken::Semicolon || prev == KnownToken::Question) {
        return true;
    }
    if (prev == KnownToken::Ellipsis && IsWordLike(current)) {
        return true;
    }
    if (cur == KnownToken::Question) {
        return true;
    }
    if (prev == KnownToken::Colon) {
        return current.parentKind != SyntaxTreeKind::CaseStatement &&
            !KnownTokenHasClass(previous->known, TokenClass::AccessKeyword);
    }
    if (cur == KnownToken::Less && prev == KnownToken::KeywordTemplate) {
        return true;
    }
    if (cur == KnownToken::Colon) {
        if (
            previous->known == KnownToken::KeywordDefault ||
            KnownTokenHasClass(previous->known, TokenClass::AccessKeyword)
        ) {
            return false;
        }
        return current.parentKind != SyntaxTreeKind::CaseStatement;
    }
    if (IsDeclaratorBindingToken(current)) {
        return false;
    }
    if (IsDeclaratorBindingToken(*previous)) {
        if (cur == KnownToken::Greater) {
            return false;
        }
        if (IsParenthesizedDeclarator(previous->grandParentKind)) {
            return false;
        }
        return !IsDeclaratorBindingToken(current);
    }
    if (
        prev == KnownToken::KeywordReturn &&
        current.kind == PrintTokenKind::Known &&
        KnownTokenHasClass(cur, TokenClass::UnaryOperator)
    ) {
        return true;
    }
    if (current.kind == PrintTokenKind::Known && (
        KnownTokenHasClass(cur, TokenClass::AssignmentOperator) ||
        (KnownTokenHasClass(cur, TokenClass::BinaryOperator) && IsBinaryContext(current))
    )) {
        return true;
    }
    if (previous->kind == PrintTokenKind::Known && (
        KnownTokenHasClass(prev, TokenClass::AssignmentOperator) ||
        (KnownTokenHasClass(prev, TokenClass::BinaryOperator) && IsBinaryContext(*previous))
    )) {
        return true;
    }
    if (
        current.kind == PrintTokenKind::Known &&
        KnownTokenHasClass(cur, TokenClass::UnaryOperator) &&
        IsUnaryContext(current)
    ) {
        return false;
    }
    if (
        previous->kind == PrintTokenKind::Known &&
        KnownTokenHasClass(prev, TokenClass::UnaryOperator) &&
        IsUnaryContext(*previous)
    ) {
        return false;
    }
    if (KnownTokenHasClass(prev, TokenClass::MemberOperator) || KnownTokenHasClass(cur, TokenClass::MemberOperator)) {
        return false;
    }
    if (IsWordLike(*previous) && IsWordLike(current)) {
        return true;
    }
    if (
        previous->kind == PrintTokenKind::Known &&
        (
            prev == KnownToken::RightParen ||
            prev == KnownToken::RightBracket ||
            prev == KnownToken::RightBrace ||
            prev == KnownToken::Greater
        ) &&
        IsWordLike(current)
    ) {
        if (prev == KnownToken::RightParen && previous->parentKind == SyntaxTreeKind::CastExpression) {
            return false;
        }
        return true;
    }
    return false;
}

std::string_view FormatTokenText(const PrintToken& token) {
    return token.kind == PrintTokenKind::Known ? KnownTokenText(token.known) : token.text;
}

int FormatTokenWidth(const PrintToken& token) {
    return static_cast<int>(FormatTokenText(token).size());
}
