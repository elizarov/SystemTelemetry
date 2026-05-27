#include "tools/impl/format_model.h"

#include <array>
#include <unordered_map>

namespace {

struct KnownTokenTextMapping {
    KnownToken token = KnownToken::Unknown;
    std::string_view text;
    std::uint32_t classes = 0;
};

constexpr std::uint32_t Bit(TokenClass tokenClass) {
    return static_cast<std::uint32_t>(tokenClass);
}

constexpr KnownTokenTextMapping Token(
    KnownToken token,
    std::string_view text,
    std::uint32_t classes = 0
) {
    return {token, text, classes};
}

constexpr KnownTokenTextMapping Keyword(
    KnownToken token,
    std::string_view text,
    std::uint32_t classes = 0
) {
    return {token, text, Bit(TokenClass::Keyword) | classes};
}

constexpr auto kKnownTokenTextMappings = std::to_array<KnownTokenTextMapping>({
    {KnownToken::Hash, "#"},
    {KnownToken::LeftParen, "("},
    {KnownToken::RightParen, ")"},
    {KnownToken::LeftBracket, "["},
    {KnownToken::RightBracket, "]"},
    {KnownToken::LeftBrace, "{"},
    {KnownToken::RightBrace, "}"},
    Token(KnownToken::Less, "<", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::Greater, ">", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::LessEqual, "<=", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::GreaterEqual, ">=", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::EqualEqual, "==", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::BangEqual, "!=", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::Spaceship, "<=>", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::Plus, "+", Bit(TokenClass::BinaryOperator) | Bit(TokenClass::UnaryOperator)),
    Token(KnownToken::Minus, "-", Bit(TokenClass::BinaryOperator) | Bit(TokenClass::UnaryOperator)),
    Token(
        KnownToken::Star,
        "*",
        Bit(TokenClass::BinaryOperator) | Bit(TokenClass::UnaryOperator) | Bit(TokenClass::DeclaratorReferenceToken)
    ),
    Token(KnownToken::Slash, "/", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::Percent, "%", Bit(TokenClass::BinaryOperator) | Bit(TokenClass::DeclaratorReferenceToken)),
    Token(KnownToken::Caret, "^", Bit(TokenClass::BinaryOperator) | Bit(TokenClass::DeclaratorReferenceToken)),
    Token(
        KnownToken::Ampersand,
        "&",
        Bit(TokenClass::BinaryOperator) | Bit(TokenClass::UnaryOperator) | Bit(TokenClass::DeclaratorReferenceToken)
    ),
    Token(KnownToken::Pipe, "|", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::Bang, "!", Bit(TokenClass::UnaryOperator)),
    Token(KnownToken::Tilde, "~", Bit(TokenClass::UnaryOperator)),
    Token(KnownToken::Equal, "=", Bit(TokenClass::AssignmentOperator)),
    Token(KnownToken::PlusEqual, "+=", Bit(TokenClass::AssignmentOperator)),
    Token(KnownToken::MinusEqual, "-=", Bit(TokenClass::AssignmentOperator)),
    Token(KnownToken::StarEqual, "*=", Bit(TokenClass::AssignmentOperator)),
    Token(KnownToken::SlashEqual, "/=", Bit(TokenClass::AssignmentOperator)),
    Token(KnownToken::PercentEqual, "%=", Bit(TokenClass::AssignmentOperator)),
    Token(KnownToken::CaretEqual, "^=", Bit(TokenClass::AssignmentOperator)),
    Token(KnownToken::AmpersandEqual, "&=", Bit(TokenClass::AssignmentOperator)),
    Token(KnownToken::PipeEqual, "|=", Bit(TokenClass::AssignmentOperator)),
    Token(KnownToken::LessLess, "<<", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::GreaterGreater, ">>", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::LessLessEqual, "<<=", Bit(TokenClass::AssignmentOperator)),
    Token(KnownToken::GreaterGreaterEqual, ">>=", Bit(TokenClass::AssignmentOperator)),
    Token(KnownToken::AmpersandAmpersand, "&&", Bit(TokenClass::BinaryOperator) | Bit(TokenClass::DeclaratorReferenceToken)),
    Token(KnownToken::PipePipe, "||", Bit(TokenClass::BinaryOperator)),
    Token(KnownToken::PlusPlus, "++", Bit(TokenClass::UnaryOperator)),
    Token(KnownToken::MinusMinus, "--", Bit(TokenClass::UnaryOperator)),
    Token(KnownToken::Arrow, "->", Bit(TokenClass::MemberOperator)),
    Token(KnownToken::Dot, ".", Bit(TokenClass::MemberOperator)),
    Token(KnownToken::ArrowStar, "->*", Bit(TokenClass::MemberOperator)),
    Token(KnownToken::DotStar, ".*", Bit(TokenClass::MemberOperator)),
    Token(KnownToken::ColonColon, "::", Bit(TokenClass::MemberOperator)),
    {KnownToken::Question, "?"},
    {KnownToken::Colon, ":"},
    {KnownToken::Semicolon, ";"},
    {KnownToken::Comma, ","},
    {KnownToken::Ellipsis, "..."},
    Keyword(KnownToken::KeywordAlignas, "alignas"),
    Keyword(KnownToken::KeywordAlignof, "alignof"),
    Keyword(KnownToken::KeywordAsm, "asm"),
    Keyword(KnownToken::KeywordAuto, "auto"),
    Keyword(KnownToken::KeywordBool, "bool"),
    Keyword(KnownToken::KeywordBreak, "break"),
    Keyword(KnownToken::KeywordCase, "case"),
    Keyword(
        KnownToken::KeywordCatch,
        "catch",
        Bit(TokenClass::ControlKeyword) | Bit(TokenClass::AttachAfterBlockKeyword)
    ),
    Keyword(KnownToken::KeywordChar, "char"),
    Keyword(KnownToken::KeywordChar16T, "char16_t"),
    Keyword(KnownToken::KeywordChar32T, "char32_t"),
    Keyword(KnownToken::KeywordClass, "class"),
    Keyword(KnownToken::KeywordConcept, "concept"),
    Keyword(KnownToken::KeywordConst, "const"),
    Keyword(KnownToken::KeywordConsteval, "consteval"),
    Keyword(KnownToken::KeywordConstexpr, "constexpr"),
    Keyword(KnownToken::KeywordConstinit, "constinit"),
    Keyword(KnownToken::KeywordConstCast, "const_cast"),
    Keyword(KnownToken::KeywordContinue, "continue"),
    Keyword(KnownToken::KeywordDecltype, "decltype"),
    Keyword(KnownToken::KeywordDefault, "default"),
    Keyword(KnownToken::KeywordDelete, "delete"),
    Keyword(KnownToken::KeywordDo, "do"),
    Keyword(KnownToken::KeywordDouble, "double"),
    Keyword(KnownToken::KeywordDynamicCast, "dynamic_cast"),
    Keyword(KnownToken::KeywordElse, "else", Bit(TokenClass::AttachAfterBlockKeyword)),
    Keyword(KnownToken::KeywordEnum, "enum"),
    Keyword(KnownToken::KeywordExplicit, "explicit"),
    Keyword(KnownToken::KeywordExport, "export"),
    Keyword(KnownToken::KeywordExtern, "extern"),
    Keyword(KnownToken::KeywordFalse, "false"),
    Keyword(KnownToken::KeywordFinal, "final"),
    Keyword(KnownToken::KeywordFinally, "finally", Bit(TokenClass::AttachAfterBlockKeyword)),
    Keyword(KnownToken::KeywordFloat, "float"),
    Keyword(KnownToken::KeywordFor, "for", Bit(TokenClass::ControlKeyword)),
    Keyword(KnownToken::KeywordFriend, "friend"),
    Keyword(KnownToken::KeywordGoto, "goto"),
    Keyword(KnownToken::KeywordIf, "if", Bit(TokenClass::ControlKeyword)),
    Keyword(KnownToken::KeywordInline, "inline"),
    Keyword(KnownToken::KeywordInt, "int"),
    Keyword(KnownToken::KeywordLong, "long"),
    Keyword(KnownToken::KeywordMutable, "mutable"),
    Keyword(KnownToken::KeywordNamespace, "namespace"),
    Keyword(KnownToken::KeywordNew, "new"),
    Keyword(KnownToken::KeywordNoexcept, "noexcept"),
    Keyword(KnownToken::KeywordNullptr, "nullptr"),
    Keyword(KnownToken::KeywordOperator, "operator"),
    Keyword(KnownToken::KeywordOverride, "override"),
    Keyword(KnownToken::KeywordPrivate, "private", Bit(TokenClass::AccessKeyword)),
    Keyword(KnownToken::KeywordProtected, "protected", Bit(TokenClass::AccessKeyword)),
    Keyword(KnownToken::KeywordPublic, "public", Bit(TokenClass::AccessKeyword)),
    Keyword(KnownToken::KeywordRegister, "register"),
    Keyword(KnownToken::KeywordReinterpretCast, "reinterpret_cast"),
    Keyword(KnownToken::KeywordRequires, "requires"),
    Keyword(KnownToken::KeywordReturn, "return"),
    Keyword(KnownToken::KeywordShort, "short"),
    Keyword(KnownToken::KeywordSigned, "signed"),
    Keyword(KnownToken::KeywordSizeof, "sizeof"),
    Keyword(KnownToken::KeywordStatic, "static"),
    Keyword(KnownToken::KeywordStaticAssert, "static_assert"),
    Keyword(KnownToken::KeywordStaticCast, "static_cast"),
    Keyword(KnownToken::KeywordStruct, "struct"),
    Keyword(KnownToken::KeywordSwitch, "switch", Bit(TokenClass::ControlKeyword)),
    Keyword(KnownToken::KeywordTemplate, "template"),
    Keyword(KnownToken::KeywordThis, "this"),
    Keyword(KnownToken::KeywordThreadLocal, "thread_local"),
    Keyword(KnownToken::KeywordThrow, "throw"),
    Keyword(KnownToken::KeywordTrue, "true"),
    Keyword(KnownToken::KeywordTry, "try"),
    Keyword(KnownToken::KeywordTypedef, "typedef"),
    Keyword(KnownToken::KeywordTypeid, "typeid"),
    Keyword(KnownToken::KeywordTypename, "typename"),
    Keyword(KnownToken::KeywordUnion, "union"),
    Keyword(KnownToken::KeywordUnsigned, "unsigned"),
    Keyword(KnownToken::KeywordUsing, "using"),
    Keyword(KnownToken::KeywordVirtual, "virtual"),
    Keyword(KnownToken::KeywordVoid, "void"),
    Keyword(KnownToken::KeywordVolatile, "volatile"),
    Keyword(KnownToken::KeywordWcharT, "wchar_t"),
    Keyword(
        KnownToken::KeywordWhile,
        "while",
        Bit(TokenClass::ControlKeyword) | Bit(TokenClass::AttachAfterBlockKeyword)
    ),
    Keyword(KnownToken::KeywordCdecl, "__cdecl"),
    Keyword(KnownToken::KeywordDeclspec, "__declspec"),
    Keyword(KnownToken::KeywordCoAwait, "co_await"),
    Keyword(KnownToken::KeywordCoReturn, "co_return"),
    Keyword(KnownToken::KeywordCoYield, "co_yield"),
});

constexpr size_t TokenIndex(KnownToken token) {
    return static_cast<size_t>(token);
}

constexpr size_t kKnownTokenCount = TokenIndex(KnownToken::KeywordCoYield) + 1;

constexpr auto BuildKnownTokenInfoByToken() {
    std::array<KnownTokenTextMapping, kKnownTokenCount> result{};
    for (const KnownTokenTextMapping& mapping : kKnownTokenTextMappings) {
        result[TokenIndex(mapping.token)] = mapping;
    }
    return result;
}

constexpr size_t MaxKnownTokenTextLength() {
    size_t result = 0;
    for (const KnownTokenTextMapping& mapping : kKnownTokenTextMappings) {
        if (mapping.text.size() > result) {
            result = mapping.text.size();
        }
    }
    return result;
}

constexpr auto kKnownTokenInfoByToken = BuildKnownTokenInfoByToken();
constexpr size_t kMaxKnownTokenTextLength = MaxKnownTokenTextLength();

const std::unordered_map<std::string_view, KnownToken>& KnownTokenByText() {
    static const std::unordered_map<std::string_view, KnownToken> tokens = [] {
        std::unordered_map<std::string_view, KnownToken> result;
        result.reserve(kKnownTokenTextMappings.size());
        for (const KnownTokenTextMapping& mapping : kKnownTokenTextMappings) {
            result.emplace(mapping.text, mapping.token);
        }
        return result;
    }();
    return tokens;
}

}  // namespace

KnownToken KnownTokenFromText(std::string_view text) {
    if (text.size() > kMaxKnownTokenTextLength) {
        return KnownToken::Unknown;
    }
    const auto& tokens = KnownTokenByText();
    const auto token = tokens.find(text);
    return token == tokens.end() ? KnownToken::Unknown : token->second;
}

std::string_view KnownTokenText(KnownToken token) {
    const size_t index = TokenIndex(token);
    if (index >= kKnownTokenInfoByToken.size()) {
        return {};
    }
    return kKnownTokenInfoByToken[index].text;
}

bool KnownTokenHasClass(KnownToken token, TokenClass tokenClass) {
    const size_t index = TokenIndex(token);
    if (index >= kKnownTokenInfoByToken.size()) {
        return false;
    }
    return (kKnownTokenInfoByToken[index].classes & Bit(tokenClass)) != 0;
}
