#include "tools/impl/format_model.h"

#include <array>

namespace {

struct KnownTokenTextMapping {
    KnownToken token;
    std::string_view text;
};

constexpr auto kKnownTokenTextMappings = std::to_array<KnownTokenTextMapping>({
    {KnownToken::Hash, "#"},
    {KnownToken::LeftParen, "("},
    {KnownToken::RightParen, ")"},
    {KnownToken::LeftBracket, "["},
    {KnownToken::RightBracket, "]"},
    {KnownToken::LeftBrace, "{"},
    {KnownToken::RightBrace, "}"},
    {KnownToken::Less, "<"},
    {KnownToken::Greater, ">"},
    {KnownToken::LessEqual, "<="},
    {KnownToken::GreaterEqual, ">="},
    {KnownToken::EqualEqual, "=="},
    {KnownToken::BangEqual, "!="},
    {KnownToken::Spaceship, "<=>"},
    {KnownToken::Plus, "+"},
    {KnownToken::Minus, "-"},
    {KnownToken::Star, "*"},
    {KnownToken::Slash, "/"},
    {KnownToken::Percent, "%"},
    {KnownToken::Caret, "^"},
    {KnownToken::Ampersand, "&"},
    {KnownToken::Pipe, "|"},
    {KnownToken::Bang, "!"},
    {KnownToken::Tilde, "~"},
    {KnownToken::Equal, "="},
    {KnownToken::PlusEqual, "+="},
    {KnownToken::MinusEqual, "-="},
    {KnownToken::StarEqual, "*="},
    {KnownToken::SlashEqual, "/="},
    {KnownToken::PercentEqual, "%="},
    {KnownToken::CaretEqual, "^="},
    {KnownToken::AmpersandEqual, "&="},
    {KnownToken::PipeEqual, "|="},
    {KnownToken::LessLess, "<<"},
    {KnownToken::GreaterGreater, ">>"},
    {KnownToken::LessLessEqual, "<<="},
    {KnownToken::GreaterGreaterEqual, ">>="},
    {KnownToken::AmpersandAmpersand, "&&"},
    {KnownToken::PipePipe, "||"},
    {KnownToken::PlusPlus, "++"},
    {KnownToken::MinusMinus, "--"},
    {KnownToken::Arrow, "->"},
    {KnownToken::Dot, "."},
    {KnownToken::ArrowStar, "->*"},
    {KnownToken::DotStar, ".*"},
    {KnownToken::ColonColon, "::"},
    {KnownToken::Question, "?"},
    {KnownToken::Colon, ":"},
    {KnownToken::Semicolon, ";"},
    {KnownToken::Comma, ","},
    {KnownToken::Ellipsis, "..."},
    {KnownToken::KeywordAlignas, "alignas"},
    {KnownToken::KeywordAlignof, "alignof"},
    {KnownToken::KeywordAsm, "asm"},
    {KnownToken::KeywordAuto, "auto"},
    {KnownToken::KeywordBool, "bool"},
    {KnownToken::KeywordBreak, "break"},
    {KnownToken::KeywordCase, "case"},
    {KnownToken::KeywordCatch, "catch"},
    {KnownToken::KeywordChar, "char"},
    {KnownToken::KeywordChar16T, "char16_t"},
    {KnownToken::KeywordChar32T, "char32_t"},
    {KnownToken::KeywordClass, "class"},
    {KnownToken::KeywordConcept, "concept"},
    {KnownToken::KeywordConst, "const"},
    {KnownToken::KeywordConsteval, "consteval"},
    {KnownToken::KeywordConstexpr, "constexpr"},
    {KnownToken::KeywordConstinit, "constinit"},
    {KnownToken::KeywordConstCast, "const_cast"},
    {KnownToken::KeywordContinue, "continue"},
    {KnownToken::KeywordDecltype, "decltype"},
    {KnownToken::KeywordDefault, "default"},
    {KnownToken::KeywordDelete, "delete"},
    {KnownToken::KeywordDo, "do"},
    {KnownToken::KeywordDouble, "double"},
    {KnownToken::KeywordDynamicCast, "dynamic_cast"},
    {KnownToken::KeywordElse, "else"},
    {KnownToken::KeywordEnum, "enum"},
    {KnownToken::KeywordExplicit, "explicit"},
    {KnownToken::KeywordExport, "export"},
    {KnownToken::KeywordExtern, "extern"},
    {KnownToken::KeywordFalse, "false"},
    {KnownToken::KeywordFinal, "final"},
    {KnownToken::KeywordFinally, "finally"},
    {KnownToken::KeywordFloat, "float"},
    {KnownToken::KeywordFor, "for"},
    {KnownToken::KeywordFriend, "friend"},
    {KnownToken::KeywordGoto, "goto"},
    {KnownToken::KeywordIf, "if"},
    {KnownToken::KeywordInline, "inline"},
    {KnownToken::KeywordInt, "int"},
    {KnownToken::KeywordLong, "long"},
    {KnownToken::KeywordMutable, "mutable"},
    {KnownToken::KeywordNamespace, "namespace"},
    {KnownToken::KeywordNew, "new"},
    {KnownToken::KeywordNoexcept, "noexcept"},
    {KnownToken::KeywordNullptr, "nullptr"},
    {KnownToken::KeywordOperator, "operator"},
    {KnownToken::KeywordOverride, "override"},
    {KnownToken::KeywordPrivate, "private"},
    {KnownToken::KeywordProtected, "protected"},
    {KnownToken::KeywordPublic, "public"},
    {KnownToken::KeywordRegister, "register"},
    {KnownToken::KeywordReinterpretCast, "reinterpret_cast"},
    {KnownToken::KeywordRequires, "requires"},
    {KnownToken::KeywordReturn, "return"},
    {KnownToken::KeywordShort, "short"},
    {KnownToken::KeywordSigned, "signed"},
    {KnownToken::KeywordSizeof, "sizeof"},
    {KnownToken::KeywordStatic, "static"},
    {KnownToken::KeywordStaticAssert, "static_assert"},
    {KnownToken::KeywordStaticCast, "static_cast"},
    {KnownToken::KeywordStruct, "struct"},
    {KnownToken::KeywordSwitch, "switch"},
    {KnownToken::KeywordTemplate, "template"},
    {KnownToken::KeywordThis, "this"},
    {KnownToken::KeywordThreadLocal, "thread_local"},
    {KnownToken::KeywordThrow, "throw"},
    {KnownToken::KeywordTrue, "true"},
    {KnownToken::KeywordTry, "try"},
    {KnownToken::KeywordTypedef, "typedef"},
    {KnownToken::KeywordTypeid, "typeid"},
    {KnownToken::KeywordTypename, "typename"},
    {KnownToken::KeywordUnion, "union"},
    {KnownToken::KeywordUnsigned, "unsigned"},
    {KnownToken::KeywordUsing, "using"},
    {KnownToken::KeywordVirtual, "virtual"},
    {KnownToken::KeywordVoid, "void"},
    {KnownToken::KeywordVolatile, "volatile"},
    {KnownToken::KeywordWcharT, "wchar_t"},
    {KnownToken::KeywordWhile, "while"},
    {KnownToken::KeywordCdecl, "__cdecl"},
    {KnownToken::KeywordDeclspec, "__declspec"},
    {KnownToken::KeywordCoAwait, "co_await"},
    {KnownToken::KeywordCoReturn, "co_return"},
    {KnownToken::KeywordCoYield, "co_yield"},
});

}  // namespace

KnownToken KnownTokenFromText(std::string_view text) {
    for (const KnownTokenTextMapping& mapping : kKnownTokenTextMappings) {
        if (mapping.text == text) {
            return mapping.token;
        }
    }
    return KnownToken::Unknown;
}

std::string_view KnownTokenText(KnownToken token) {
    for (const KnownTokenTextMapping& mapping : kKnownTokenTextMappings) {
        if (mapping.token == token) {
            return mapping.text;
        }
    }
    return {};
}
