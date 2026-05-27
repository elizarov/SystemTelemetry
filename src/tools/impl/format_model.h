#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ParseResult {
    bool ok = false;
    bool hasErrors = false;
    bool hasMissingNodes = false;
    std::string errorNodeType;
    int errorLine = 0;
    int errorColumn = 0;
    std::string errorSnippet;
    std::string error;
};

enum class SyntaxNodeKind : std::uint8_t {
    Tree,
    KnownToken,
    FreeToken,
    Comment,
    BlankLine,
};

enum class SyntaxTreeKind : std::uint16_t {
    Unknown,
    TranslationUnit,
    Declaration,
    FieldDeclaration,
    FunctionDefinition,
    CompoundStatement,
    FieldDeclarationList,
    EnumeratorList,
    InitializerList,
    DeclarationList,
    NamespaceDefinition,
    EnumSpecifier,
    ClassSpecifier,
    StructSpecifier,
    IfStatement,
    ElseClause,
    ForStatement,
    WhileStatement,
    DoStatement,
    SwitchStatement,
    CaseStatement,
    PreprocCall,
    PreprocDef,
    PreprocFunctionDef,
    PreprocInclude,
    PreprocIf,
    PreprocIfdef,
    PreprocElse,
    PreprocElif,
    PreprocUsing,
    BinaryExpression,
    UnaryExpression,
    ConditionalExpression,
    AssignmentExpression,
    PointerDeclarator,
    AbstractPointerDeclarator,
    ReferenceDeclarator,
    AbstractReferenceDeclarator,
    HandleDeclarator,
    AbstractHandleDeclarator,
    MemberPointerDeclarator,
    FunctionDeclarator,
    ParameterList,
    ArgumentList,
    TemplateParameterList,
    TemplateArgumentList,
    LambdaExpression,
    RawStringLiteral,
    StringLiteral,
    CharacterLiteral,
    NumberLiteral,
    Identifier,
};

enum class KnownToken : std::uint16_t {
    Unknown,
    Hash,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    EqualEqual,
    BangEqual,
    Spaceship,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Caret,
    Ampersand,
    Pipe,
    Bang,
    Tilde,
    Equal,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,
    CaretEqual,
    AmpersandEqual,
    PipeEqual,
    LessLess,
    GreaterGreater,
    LessLessEqual,
    GreaterGreaterEqual,
    AmpersandAmpersand,
    PipePipe,
    PlusPlus,
    MinusMinus,
    Arrow,
    Dot,
    ArrowStar,
    DotStar,
    ColonColon,
    Question,
    Colon,
    Semicolon,
    Comma,
    Ellipsis,
    KeywordAlignas,
    KeywordAlignof,
    KeywordAsm,
    KeywordAuto,
    KeywordBool,
    KeywordBreak,
    KeywordCase,
    KeywordCatch,
    KeywordChar,
    KeywordChar16T,
    KeywordChar32T,
    KeywordClass,
    KeywordConcept,
    KeywordConst,
    KeywordConsteval,
    KeywordConstexpr,
    KeywordConstinit,
    KeywordConstCast,
    KeywordContinue,
    KeywordDecltype,
    KeywordDefault,
    KeywordDelete,
    KeywordDo,
    KeywordDouble,
    KeywordDynamicCast,
    KeywordElse,
    KeywordEnum,
    KeywordExplicit,
    KeywordExport,
    KeywordExtern,
    KeywordFalse,
    KeywordFinal,
    KeywordFinally,
    KeywordFloat,
    KeywordFor,
    KeywordFriend,
    KeywordGoto,
    KeywordIf,
    KeywordInline,
    KeywordInt,
    KeywordLong,
    KeywordMutable,
    KeywordNamespace,
    KeywordNew,
    KeywordNoexcept,
    KeywordNullptr,
    KeywordOperator,
    KeywordOverride,
    KeywordPrivate,
    KeywordProtected,
    KeywordPublic,
    KeywordRegister,
    KeywordReinterpretCast,
    KeywordRequires,
    KeywordReturn,
    KeywordShort,
    KeywordSigned,
    KeywordSizeof,
    KeywordStatic,
    KeywordStaticAssert,
    KeywordStaticCast,
    KeywordStruct,
    KeywordSwitch,
    KeywordTemplate,
    KeywordThis,
    KeywordThreadLocal,
    KeywordThrow,
    KeywordTrue,
    KeywordTry,
    KeywordTypedef,
    KeywordTypeid,
    KeywordTypename,
    KeywordUnion,
    KeywordUnsigned,
    KeywordUsing,
    KeywordVirtual,
    KeywordVoid,
    KeywordVolatile,
    KeywordWcharT,
    KeywordWhile,
    KeywordCdecl,
    KeywordDeclspec,
    KeywordCoAwait,
    KeywordCoReturn,
    KeywordCoYield,
};

enum class TokenClass : std::uint32_t {
    Keyword = 1u << 0,
    ControlKeyword = 1u << 1,
    AttachAfterBlockKeyword = 1u << 2,
    AccessKeyword = 1u << 3,
    MemberOperator = 1u << 4,
    AssignmentOperator = 1u << 5,
    BinaryOperator = 1u << 6,
    UnaryOperator = 1u << 7,
    DeclaratorReferenceToken = 1u << 8,
};

KnownToken KnownTokenFromText(std::string_view text);
std::string_view KnownTokenText(KnownToken token);
bool KnownTokenHasClass(KnownToken token, TokenClass tokenClass);

struct SyntaxNode {
    SyntaxNodeKind kind = SyntaxNodeKind::Tree;
    SyntaxTreeKind treeKind = SyntaxTreeKind::Unknown;
    KnownToken known = KnownToken::Unknown;
    std::string_view text;
    std::uint32_t startByte = 0;
    std::uint32_t endByte = 0;
    std::uint32_t startRow = 0;
    std::uint32_t startColumn = 0;
    std::uint32_t endRow = 0;
    std::uint32_t endColumn = 0;
    std::vector<std::unique_ptr<SyntaxNode>> children;
};

struct FormatModel {
    FormatModel() = default;
    FormatModel(const FormatModel&) = delete;
    FormatModel& operator=(const FormatModel&) = delete;
    FormatModel(FormatModel&&) noexcept = default;
    FormatModel& operator=(FormatModel&&) noexcept = default;

    ParseResult parse;
    std::unique_ptr<std::string> sourceText;
    std::unique_ptr<SyntaxNode> root;
};
