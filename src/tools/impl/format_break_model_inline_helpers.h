#pragma once

#include "tools/impl/format_break_model.h"

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
