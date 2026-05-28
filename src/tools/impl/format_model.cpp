#include "tools/impl/format_model.h"

#include <array>
#include <unordered_map>

namespace {

struct SyntaxKindMapping {
    SyntaxNodeKind kind = SyntaxNodeKind::Unknown;
    std::string_view treeType;
    std::string_view tokenText;
    std::uint64_t classes = 0;
};

struct SyntaxKindInfo {
    std::string_view tokenText;
    std::uint64_t classes = 0;
};

constexpr std::uint64_t Bit(TokenClass tokenClass) {
    return static_cast<std::uint64_t>(tokenClass);
}

constexpr SyntaxKindMapping Kind(SyntaxNodeKind kind, std::uint64_t classes = 0) {
    return {kind, {}, {}, classes};
}

constexpr SyntaxKindMapping Tree(SyntaxNodeKind kind, std::string_view treeType, std::uint64_t classes = 0) {
    return {kind, treeType, {}, Bit(TokenClass::Tree) | classes};
}

constexpr SyntaxKindMapping Token(SyntaxNodeKind kind, std::string_view tokenText, std::uint64_t classes = 0) {
    return {kind, {}, tokenText, Bit(TokenClass::Known) | classes};
}

constexpr SyntaxKindMapping Keyword(SyntaxNodeKind kind, std::string_view tokenText, std::uint64_t classes = 0) {
    return Token(kind, tokenText, Bit(TokenClass::Keyword) | classes);
}

constexpr std::uint64_t kStringLikeClasses =
    Bit(TokenClass::Literal) | Bit(TokenClass::StringLike) | Bit(TokenClass::WholeNodeAsFreeToken);
constexpr std::uint64_t kNumberLiteralClasses = Bit(TokenClass::Literal) | Bit(TokenClass::WholeNodeAsFreeToken);
constexpr std::uint64_t kAtomicPreprocessorClasses =
    Bit(TokenClass::AtomicPreprocessor) | Bit(TokenClass::WholeNodeAsFreeToken);

constexpr auto kSyntaxKindMappings =
    std::to_array<SyntaxKindMapping>({Kind(SyntaxNodeKind::Tree, Bit(TokenClass::Tree)),

Tree(SyntaxNodeKind::TranslationUnit, "translation_unit"), Tree(SyntaxNodeKind::IncludeRun, "include_run"), Tree(
    SyntaxNodeKind::MacroReplacementList,
    "macro_replacement_list"
), Tree(SyntaxNodeKind::Declaration, "declaration", Bit(TokenClass::MacroDeclarationFragment)), Tree(
    SyntaxNodeKind::FieldDeclaration,
    "field_declaration",
    Bit(TokenClass::MacroDeclarationFragment)
), Tree(SyntaxNodeKind::AliasDeclaration, "alias_declaration", Bit(TokenClass::MacroDeclarationFragment)), Tree(
    SyntaxNodeKind::FunctionDefinition,
    "function_definition",
    Bit(TokenClass::MacroDeclarationFragment)
), Tree(SyntaxNodeKind::CompoundStatement, "compound_statement", Bit(TokenClass::CompoundBlock)), Tree(
    SyntaxNodeKind::FieldDeclarationList,
    "field_declaration_list",
    Bit(TokenClass::CompoundBlock)
), Tree(SyntaxNodeKind::EnumeratorList, "enumerator_list", Bit(TokenClass::CompoundBlock)), Tree(
    SyntaxNodeKind::InitializerList,
    "initializer_list"
), Tree(SyntaxNodeKind::FieldInitializerList, "field_initializer_list"), Tree(
    SyntaxNodeKind::FieldInitializer,
    "field_initializer"
), Tree(SyntaxNodeKind::DeclarationList, "declaration_list", Bit(TokenClass::CompoundBlock)), Tree(
    SyntaxNodeKind::NamespaceDefinition,
    "namespace_definition"
), Tree(SyntaxNodeKind::EnumSpecifier, "enum_specifier", Bit(TokenClass::MacroDeclarationFragment)), Tree(
    SyntaxNodeKind::ClassSpecifier,
    "class_specifier",
    Bit(TokenClass::MacroDeclarationFragment)
), Tree(SyntaxNodeKind::StructSpecifier, "struct_specifier", Bit(TokenClass::MacroDeclarationFragment)), Tree(
    SyntaxNodeKind::BaseClassClause,
    "base_class_clause"
), Tree(SyntaxNodeKind::AccessSpecifier, "access_specifier"), Tree(
    SyntaxNodeKind::IfStatement,
    "if_statement",
    Bit(TokenClass::ControlHeader) | Bit(TokenClass::FlatLogicalHeader)
), Tree(
    SyntaxNodeKind::ElseClause,
    "else_clause"
), Tree(SyntaxNodeKind::ForStatement, "for_statement", Bit(TokenClass::ControlHeader)), Tree(
    SyntaxNodeKind::WhileStatement,
    "while_statement",
    Bit(TokenClass::ControlHeader) | Bit(TokenClass::FlatLogicalHeader)
), Tree(SyntaxNodeKind::DoStatement, "do_statement"), Tree(
    SyntaxNodeKind::SwitchStatement,
    "switch_statement",
    Bit(TokenClass::ControlHeader) | Bit(TokenClass::FlatLogicalHeader)
), Tree(SyntaxNodeKind::CaseStatement, "case_statement"), Tree(
    SyntaxNodeKind::ConditionClause,
    "condition_clause",
    Bit(TokenClass::ControlHeader) | Bit(TokenClass::FlatLogicalHeader)
), Tree(SyntaxNodeKind::InitStatement, "init_statement"), Tree(
    SyntaxNodeKind::PreprocCall,
    "preproc_call",
    kAtomicPreprocessorClasses
), Tree(SyntaxNodeKind::PreprocDef, "preproc_def", Bit(TokenClass::MacroDefinition)), Tree(
    SyntaxNodeKind::PreprocFunctionDef,
    "preproc_function_def",
    Bit(TokenClass::MacroDefinition)
), Tree(SyntaxNodeKind::PreprocInclude, "preproc_include", kAtomicPreprocessorClasses), Tree(
    SyntaxNodeKind::PreprocIf,
    "preproc_if"
), Tree(SyntaxNodeKind::PreprocIfdef, "preproc_ifdef"), Tree(SyntaxNodeKind::PreprocElse, "preproc_else"), Tree(
    SyntaxNodeKind::PreprocElif,
    "preproc_elif"
), Tree(SyntaxNodeKind::PreprocUsing, "preproc_using", kAtomicPreprocessorClasses), Tree(
    SyntaxNodeKind::PreprocParams,
    "preproc_params"
), Tree(SyntaxNodeKind::PreprocArg, "preproc_arg", Bit(TokenClass::WholeNodeAsFreeToken)), Tree(
    SyntaxNodeKind::PreprocIf,
    "preproc_if_in_field_declaration_list"
), Tree(SyntaxNodeKind::PreprocIfdef, "preproc_ifdef_in_field_declaration_list"), Tree(
    SyntaxNodeKind::PreprocElse,
    "preproc_else_in_field_declaration_list"
), Tree(SyntaxNodeKind::PreprocElif, "preproc_elif_in_field_declaration_list"), Tree(
    SyntaxNodeKind::PreprocIf,
    "preproc_if_in_enumerator_list"
), Tree(SyntaxNodeKind::PreprocIfdef, "preproc_ifdef_in_enumerator_list"), Tree(
    SyntaxNodeKind::PreprocElse,
    "preproc_else_in_enumerator_list"
), Tree(SyntaxNodeKind::PreprocElif, "preproc_elif_in_enumerator_list"), Tree(
    SyntaxNodeKind::PreprocElif,
    "preproc_elifdef"
), Tree(SyntaxNodeKind::BinaryExpression, "binary_expression"), Tree(
    SyntaxNodeKind::UnaryExpression,
    "unary_expression"
), Tree(SyntaxNodeKind::ConditionalExpression, "conditional_expression"), Tree(
    SyntaxNodeKind::CommaExpression,
    "comma_expression"
), Tree(SyntaxNodeKind::AssignmentExpression, "assignment_expression"), Tree(
    SyntaxNodeKind::InitDeclarator,
    "init_declarator"
), Tree(
    SyntaxNodeKind::CastExpression,
    "cast_expression"
), Tree(SyntaxNodeKind::PointerDeclarator, "pointer_declarator", Bit(TokenClass::DeclaratorReferenceParent)), Tree(
    SyntaxNodeKind::AbstractPointerDeclarator,
    "abstract_pointer_declarator",
    Bit(TokenClass::DeclaratorReferenceParent)
), Tree(SyntaxNodeKind::ReferenceDeclarator, "reference_declarator", Bit(TokenClass::DeclaratorReferenceParent)), Tree(
    SyntaxNodeKind::AbstractReferenceDeclarator,
    "abstract_reference_declarator",
    Bit(TokenClass::DeclaratorReferenceParent)
), Tree(SyntaxNodeKind::HandleDeclarator, "handle_declarator", Bit(TokenClass::DeclaratorReferenceParent)), Tree(
    SyntaxNodeKind::AbstractHandleDeclarator,
    "abstract_handle_declarator",
    Bit(TokenClass::DeclaratorReferenceParent)
), Tree(
    SyntaxNodeKind::MemberPointerDeclarator,
    "member_pointer_declarator",
    Bit(TokenClass::DeclaratorReferenceParent)
), Tree(SyntaxNodeKind::FunctionDeclarator, "function_declarator"), Tree(
    SyntaxNodeKind::AbstractFunctionDeclarator,
    "abstract_function_declarator"
), Tree(
    SyntaxNodeKind::ParenthesizedDeclarator,
    "parenthesized_declarator",
    Bit(TokenClass::ParenthesizedDeclarator)
), Tree(
    SyntaxNodeKind::AbstractParenthesizedDeclarator,
    "abstract_parenthesized_declarator",
    Bit(TokenClass::ParenthesizedDeclarator)
), Tree(SyntaxNodeKind::ParameterList, "parameter_list"), Tree(SyntaxNodeKind::ArgumentList, "argument_list"), Tree(
    SyntaxNodeKind::TemplateParameterList,
    "template_parameter_list"
), Tree(
    SyntaxNodeKind::TemplateArgumentList,
    "template_argument_list"
), Tree(SyntaxNodeKind::TemplateDeclaration, "template_declaration", Bit(TokenClass::MacroDeclarationFragment)), Tree(
    SyntaxNodeKind::RequiresClause,
    "requires_clause"
), Tree(SyntaxNodeKind::LambdaExpression, "lambda_expression"), Tree(
    SyntaxNodeKind::LambdaCaptureSpecifier,
    "lambda_capture_specifier"
), Tree(SyntaxNodeKind::StructuredBindingDeclarator, "structured_binding_declarator"), Tree(
    SyntaxNodeKind::FieldDesignator,
    "field_designator"
), Tree(SyntaxNodeKind::FieldExpression, "field_expression"), Tree(
    SyntaxNodeKind::TrailingReturnType,
    "trailing_return_type"
), Tree(SyntaxNodeKind::OperatorName, "operator_name"), Tree(SyntaxNodeKind::OperatorCast, "operator_cast"), Tree(
    SyntaxNodeKind::MsCallModifier,
    "ms_call_modifier",
    Bit(TokenClass::WholeNodeAsFreeToken)
), Tree(SyntaxNodeKind::MsDeclspecModifier, "ms_declspec_modifier", Bit(TokenClass::WholeNodeAsFreeToken)), Tree(
    SyntaxNodeKind::ConcatenatedString,
    "concatenated_string",
    kStringLikeClasses
), Tree(SyntaxNodeKind::RawStringLiteral, "raw_string_literal", kStringLikeClasses), Tree(
    SyntaxNodeKind::StringLiteral,
    "string_literal",
    kStringLikeClasses
), Tree(SyntaxNodeKind::SystemLibString, "system_lib_string", Bit(TokenClass::WholeNodeAsFreeToken)), Tree(
    SyntaxNodeKind::CharacterLiteral,
    "char_literal",
    kStringLikeClasses
), Tree(SyntaxNodeKind::NumberLiteral, "number_literal", kNumberLiteralClasses), Tree(
    SyntaxNodeKind::Identifier,
    "identifier"
), Tree(SyntaxNodeKind::Identifier, "field_identifier"), Tree(SyntaxNodeKind::Identifier, "namespace_identifier"), Tree(
    SyntaxNodeKind::Identifier,
    "type_identifier"
), Tree(SyntaxNodeKind::Identifier, "qualified_identifier"),

Token(SyntaxNodeKind::Hash, "#"), Token(SyntaxNodeKind::LeftParen, "("), Token(SyntaxNodeKind::RightParen, ")"), Token(
    SyntaxNodeKind::LeftBracket,
    "["
), Token(SyntaxNodeKind::RightBracket, "]"), Token(SyntaxNodeKind::LeftBrace, "{"), Token(
    SyntaxNodeKind::RightBrace,
    "}"
), Token(SyntaxNodeKind::Less, "<", Bit(TokenClass::BinaryOperator)), Token(
    SyntaxNodeKind::Greater,
    ">",
    Bit(TokenClass::BinaryOperator)
), Token(SyntaxNodeKind::LessEqual, "<=", Bit(TokenClass::BinaryOperator)), Token(
    SyntaxNodeKind::GreaterEqual,
    ">=",
    Bit(TokenClass::BinaryOperator)
), Token(SyntaxNodeKind::EqualEqual, "==", Bit(TokenClass::BinaryOperator)), Token(
    SyntaxNodeKind::BangEqual,
    "!=",
    Bit(TokenClass::BinaryOperator)
), Token(SyntaxNodeKind::Spaceship, "<=>", Bit(TokenClass::BinaryOperator)), Token(
    SyntaxNodeKind::Plus,
    "+",
    Bit(TokenClass::BinaryOperator) | Bit(TokenClass::UnaryOperator)
), Token(SyntaxNodeKind::Minus, "-", Bit(TokenClass::BinaryOperator) | Bit(TokenClass::UnaryOperator)), Token(
    SyntaxNodeKind::Star,
    "*",
    Bit(TokenClass::BinaryOperator) | Bit(TokenClass::UnaryOperator) | Bit(TokenClass::DeclaratorReferenceToken)
), Token(SyntaxNodeKind::Slash, "/", Bit(TokenClass::BinaryOperator)), Token(
    SyntaxNodeKind::Percent,
    "%",
    Bit(TokenClass::BinaryOperator) | Bit(TokenClass::DeclaratorReferenceToken)
), Token(
    SyntaxNodeKind::Caret,
    "^",
    Bit(TokenClass::BinaryOperator) | Bit(TokenClass::DeclaratorReferenceToken)
), Token(
    SyntaxNodeKind::Ampersand,
    "&",
    Bit(TokenClass::BinaryOperator) | Bit(TokenClass::UnaryOperator) | Bit(TokenClass::DeclaratorReferenceToken)
), Token(SyntaxNodeKind::Pipe, "|", Bit(TokenClass::BinaryOperator)), Token(
    SyntaxNodeKind::Bang,
    "!",
    Bit(TokenClass::UnaryOperator)
), Token(SyntaxNodeKind::Tilde, "~", Bit(TokenClass::UnaryOperator)), Token(
    SyntaxNodeKind::Equal,
    "=",
    Bit(TokenClass::AssignmentOperator)
), Token(SyntaxNodeKind::PlusEqual, "+=", Bit(TokenClass::AssignmentOperator)), Token(
    SyntaxNodeKind::MinusEqual,
    "-=",
    Bit(TokenClass::AssignmentOperator)
), Token(SyntaxNodeKind::StarEqual, "*=", Bit(TokenClass::AssignmentOperator)), Token(
    SyntaxNodeKind::SlashEqual,
    "/=",
    Bit(TokenClass::AssignmentOperator)
), Token(SyntaxNodeKind::PercentEqual, "%=", Bit(TokenClass::AssignmentOperator)), Token(
    SyntaxNodeKind::CaretEqual,
    "^=",
    Bit(TokenClass::AssignmentOperator)
), Token(SyntaxNodeKind::AmpersandEqual, "&=", Bit(TokenClass::AssignmentOperator)), Token(
    SyntaxNodeKind::PipeEqual,
    "|=",
    Bit(TokenClass::AssignmentOperator)
), Token(SyntaxNodeKind::LessLess, "<<", Bit(TokenClass::BinaryOperator)), Token(
    SyntaxNodeKind::GreaterGreater,
    ">>",
    Bit(TokenClass::BinaryOperator)
), Token(SyntaxNodeKind::LessLessEqual, "<<=", Bit(TokenClass::AssignmentOperator)), Token(
    SyntaxNodeKind::GreaterGreaterEqual,
    ">>=",
    Bit(TokenClass::AssignmentOperator)
), Token(
    SyntaxNodeKind::AmpersandAmpersand,
    "&&",
    Bit(TokenClass::BinaryOperator) | Bit(TokenClass::DeclaratorReferenceToken)
), Token(SyntaxNodeKind::PipePipe, "||", Bit(TokenClass::BinaryOperator)), Token(
    SyntaxNodeKind::PlusPlus,
    "++",
    Bit(TokenClass::UnaryOperator)
), Token(SyntaxNodeKind::MinusMinus, "--", Bit(TokenClass::UnaryOperator)), Token(
    SyntaxNodeKind::Arrow,
    "->",
    Bit(TokenClass::MemberOperator)
), Token(SyntaxNodeKind::Dot, ".", Bit(TokenClass::MemberOperator)), Token(
    SyntaxNodeKind::ArrowStar,
    "->*",
    Bit(TokenClass::MemberOperator)
), Token(SyntaxNodeKind::DotStar, ".*", Bit(TokenClass::MemberOperator)), Token(
    SyntaxNodeKind::ColonColon,
    "::",
    Bit(TokenClass::MemberOperator)
), Token(SyntaxNodeKind::Question, "?"), Token(SyntaxNodeKind::Colon, ":"), Token(
    SyntaxNodeKind::Semicolon,
    ";"
), Token(SyntaxNodeKind::Comma, ","), Token(SyntaxNodeKind::Ellipsis, "..."), Keyword(
    SyntaxNodeKind::KeywordAlignas,
    "alignas"
), Keyword(SyntaxNodeKind::KeywordAlignof, "alignof"), Keyword(SyntaxNodeKind::KeywordAsm, "asm"), Keyword(
    SyntaxNodeKind::KeywordAuto,
    "auto"
), Keyword(SyntaxNodeKind::KeywordBool, "bool"), Keyword(SyntaxNodeKind::KeywordBreak, "break"), Keyword(
    SyntaxNodeKind::KeywordCase,
    "case"
), Keyword(
    SyntaxNodeKind::KeywordCatch,
    "catch",
    Bit(TokenClass::ControlKeyword) | Bit(TokenClass::AttachAfterBlockKeyword)
), Keyword(SyntaxNodeKind::KeywordChar, "char"), Keyword(SyntaxNodeKind::KeywordChar16T, "char16_t"), Keyword(
    SyntaxNodeKind::KeywordChar32T,
    "char32_t"
), Keyword(SyntaxNodeKind::KeywordClass, "class"), Keyword(SyntaxNodeKind::KeywordConcept, "concept"), Keyword(
    SyntaxNodeKind::KeywordConst,
    "const"
), Keyword(SyntaxNodeKind::KeywordConsteval, "consteval"), Keyword(
    SyntaxNodeKind::KeywordConstexpr,
    "constexpr"
), Keyword(SyntaxNodeKind::KeywordConstinit, "constinit"), Keyword(
    SyntaxNodeKind::KeywordConstCast,
    "const_cast"
), Keyword(SyntaxNodeKind::KeywordContinue, "continue"), Keyword(SyntaxNodeKind::KeywordDecltype, "decltype"), Keyword(
    SyntaxNodeKind::KeywordDefault,
    "default"
), Keyword(SyntaxNodeKind::KeywordDelete, "delete"), Keyword(SyntaxNodeKind::KeywordDo, "do"), Keyword(
    SyntaxNodeKind::KeywordDouble,
    "double"
), Keyword(SyntaxNodeKind::KeywordDynamicCast, "dynamic_cast"), Keyword(
    SyntaxNodeKind::KeywordElse,
    "else",
    Bit(TokenClass::AttachAfterBlockKeyword)
), Keyword(SyntaxNodeKind::KeywordEnum, "enum"), Keyword(SyntaxNodeKind::KeywordExplicit, "explicit"), Keyword(
    SyntaxNodeKind::KeywordExport,
    "export"
), Keyword(SyntaxNodeKind::KeywordExtern, "extern"), Keyword(SyntaxNodeKind::KeywordFalse, "false"), Keyword(
    SyntaxNodeKind::KeywordFinal,
    "final"
), Keyword(SyntaxNodeKind::KeywordFinally, "finally", Bit(TokenClass::AttachAfterBlockKeyword)), Keyword(
    SyntaxNodeKind::KeywordFloat,
    "float"
), Keyword(SyntaxNodeKind::KeywordFor, "for", Bit(TokenClass::ControlKeyword)), Keyword(
    SyntaxNodeKind::KeywordFriend,
    "friend"
), Keyword(
    SyntaxNodeKind::KeywordGoto,
    "goto"
), Keyword(SyntaxNodeKind::KeywordIf, "if", Bit(TokenClass::ControlKeyword)), Keyword(
    SyntaxNodeKind::KeywordInline,
    "inline"
), Keyword(SyntaxNodeKind::KeywordInt, "int"), Keyword(SyntaxNodeKind::KeywordLong, "long"), Keyword(
    SyntaxNodeKind::KeywordMutable,
    "mutable"
), Keyword(SyntaxNodeKind::KeywordNamespace, "namespace"), Keyword(SyntaxNodeKind::KeywordNew, "new"), Keyword(
    SyntaxNodeKind::KeywordNoexcept,
    "noexcept"
), Keyword(SyntaxNodeKind::KeywordNullptr, "nullptr"), Keyword(SyntaxNodeKind::KeywordOperator, "operator"), Keyword(
    SyntaxNodeKind::KeywordOverride,
    "override"
), Keyword(SyntaxNodeKind::KeywordPrivate, "private", Bit(TokenClass::AccessKeyword)), Keyword(
    SyntaxNodeKind::KeywordProtected,
    "protected",
    Bit(TokenClass::AccessKeyword)
), Keyword(SyntaxNodeKind::KeywordPublic, "public", Bit(TokenClass::AccessKeyword)), Keyword(
    SyntaxNodeKind::KeywordRegister,
    "register"
), Keyword(SyntaxNodeKind::KeywordReinterpretCast, "reinterpret_cast"), Keyword(
    SyntaxNodeKind::KeywordRequires,
    "requires"
), Keyword(SyntaxNodeKind::KeywordReturn, "return"), Keyword(SyntaxNodeKind::KeywordShort, "short"), Keyword(
    SyntaxNodeKind::KeywordSigned,
    "signed"
), Keyword(SyntaxNodeKind::KeywordSizeof, "sizeof"), Keyword(SyntaxNodeKind::KeywordStatic, "static"), Keyword(
    SyntaxNodeKind::KeywordStaticAssert,
    "static_assert"
), Keyword(SyntaxNodeKind::KeywordStaticCast, "static_cast"), Keyword(SyntaxNodeKind::KeywordStruct, "struct"), Keyword(
    SyntaxNodeKind::KeywordSwitch,
    "switch",
    Bit(TokenClass::ControlKeyword)
), Keyword(SyntaxNodeKind::KeywordTemplate, "template"), Keyword(SyntaxNodeKind::KeywordThis, "this"), Keyword(
    SyntaxNodeKind::KeywordThreadLocal,
    "thread_local"
), Keyword(SyntaxNodeKind::KeywordThrow, "throw"), Keyword(SyntaxNodeKind::KeywordTrue, "true"), Keyword(
    SyntaxNodeKind::KeywordTry,
    "try"
), Keyword(SyntaxNodeKind::KeywordTypedef, "typedef"), Keyword(SyntaxNodeKind::KeywordTypeid, "typeid"), Keyword(
    SyntaxNodeKind::KeywordTypename,
    "typename"
), Keyword(SyntaxNodeKind::KeywordUnion, "union"), Keyword(SyntaxNodeKind::KeywordUnsigned, "unsigned"), Keyword(
    SyntaxNodeKind::KeywordUsing,
    "using"
), Keyword(SyntaxNodeKind::KeywordVirtual, "virtual"), Keyword(SyntaxNodeKind::KeywordVoid, "void"), Keyword(
    SyntaxNodeKind::KeywordVolatile,
    "volatile"
), Keyword(SyntaxNodeKind::KeywordWcharT, "wchar_t"), Keyword(
    SyntaxNodeKind::KeywordWhile,
    "while",
    Bit(TokenClass::ControlKeyword) | Bit(TokenClass::AttachAfterBlockKeyword)
), Keyword(SyntaxNodeKind::KeywordCdecl, "__cdecl"), Keyword(SyntaxNodeKind::KeywordDeclspec, "__declspec"), Keyword(
    SyntaxNodeKind::KeywordCoAwait,
    "co_await"
), Keyword(SyntaxNodeKind::KeywordCoReturn, "co_return"), Keyword(SyntaxNodeKind::KeywordCoYield, "co_yield")});

constexpr size_t KindIndex(SyntaxNodeKind kind) {
    return static_cast<size_t>(kind);
}

constexpr size_t kSyntaxNodeKindCount = KindIndex(SyntaxNodeKind::KeywordCoYield) + 1;

constexpr auto BuildSyntaxKindInfoByKind() {
    std::array<SyntaxKindInfo, kSyntaxNodeKindCount> result{};
    for (const SyntaxKindMapping& mapping : kSyntaxKindMappings) {
        SyntaxKindInfo& info = result[KindIndex(mapping.kind)];
        info.classes |= mapping.classes;
        if (!mapping.tokenText.empty()) {
            info.tokenText = mapping.tokenText;
        }
    }
    return result;
}

constexpr size_t MaxTokenTextLength() {
    size_t result = 0;
    for (const SyntaxKindMapping& mapping : kSyntaxKindMappings) {
        if (!mapping.tokenText.empty() && mapping.tokenText.size() > result) {
            result = mapping.tokenText.size();
        }
    }
    return result;
}

constexpr auto kSyntaxKindInfoByKind = BuildSyntaxKindInfoByKind();
constexpr size_t kMaxTokenTextLength = MaxTokenTextLength();

const std::unordered_map<std::string_view, SyntaxNodeKind>& SyntaxKindByTreeType() {
    static const std::unordered_map<std::string_view, SyntaxNodeKind> kindsByTreeType = [] {
        std::unordered_map<std::string_view, SyntaxNodeKind> result;
        result.reserve(kSyntaxKindMappings.size());
        for (const SyntaxKindMapping& mapping : kSyntaxKindMappings) {
            if (!mapping.treeType.empty()) {
                result.emplace(mapping.treeType, mapping.kind);
            }
        }
        return result;
    }
    ();
    return kindsByTreeType;
}

const std::unordered_map<std::string_view, SyntaxNodeKind>& SyntaxKindByTokenText() {
    static const std::unordered_map<std::string_view, SyntaxNodeKind> tokens = [] {
        std::unordered_map<std::string_view, SyntaxNodeKind> result;
        result.reserve(kSyntaxKindMappings.size());
        for (const SyntaxKindMapping& mapping : kSyntaxKindMappings) {
            if (!mapping.tokenText.empty()) {
                result.emplace(mapping.tokenText, mapping.kind);
            }
        }
        return result;
    }
    ();
    return tokens;
}

}  // namespace

SyntaxNodeKind SyntaxNodeKindFromTreeType(std::string_view type) {
    const auto& kindsByTreeType = SyntaxKindByTreeType();
    const auto found = kindsByTreeType.find(type);
    return found == kindsByTreeType.end() ? SyntaxNodeKind::Unknown : found->second;
}

SyntaxNodeKind SyntaxNodeKindFromTokenText(std::string_view text) {
    if (text.size() > kMaxTokenTextLength) {
        return SyntaxNodeKind::Unknown;
    }
    const auto& tokens = SyntaxKindByTokenText();
    const auto found = tokens.find(text);
    return found == tokens.end() ? SyntaxNodeKind::Unknown : found->second;
}

std::string_view SyntaxNodeKindTokenText(SyntaxNodeKind kind) {
    const size_t index = KindIndex(kind);
    if (index >= kSyntaxKindInfoByKind.size()) {
        return {};
    }
    return kSyntaxKindInfoByKind[index].tokenText;
}

bool SyntaxNodeKindHasClass(SyntaxNodeKind kind, TokenClass tokenClass) {
    const size_t index = KindIndex(kind);
    if (index >= kSyntaxKindInfoByKind.size()) {
        return false;
    }
    return (kSyntaxKindInfoByKind[index].classes & Bit(tokenClass)) != 0;
}
