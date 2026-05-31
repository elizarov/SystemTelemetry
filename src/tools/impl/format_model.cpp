#include "tools/impl/format_model.h"

#include <array>
#include <tree_sitter_cpp.h>
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
constexpr std::uint64_t kCommentClasses = Bit(TokenClass::Comment) | Bit(TokenClass::Trivia);
constexpr std::uint64_t kAtomicPreprocessorClasses =
    Bit(TokenClass::AtomicPreprocessor) | Bit(TokenClass::WholeNodeAsFreeToken);
constexpr std::uint64_t kChainBinaryClasses = Bit(TokenClass::BinaryOperator) | Bit(TokenClass::ChainOperator);
constexpr std::uint64_t kSymbolLocalClasses =
    Bit(TokenClass::WholeNodeAsFreeToken) | Bit(TokenClass::AtomicPreprocessor);

constexpr auto kSyntaxKindMappings = std::to_array<SyntaxKindMapping>({
    Kind(SyntaxNodeKind::Tree, Bit(TokenClass::Tree)),
    Kind(SyntaxNodeKind::Comment, kCommentClasses),
    Kind(SyntaxNodeKind::TrailingComment, kCommentClasses),
    Kind(SyntaxNodeKind::BlankLine, Bit(TokenClass::Trivia)),
    Tree(SyntaxNodeKind::TranslationUnit, "translation_unit"),
    Tree(SyntaxNodeKind::IncludeRun, "include_run"),
    Tree(SyntaxNodeKind::MacroReplacementList, "macro_replacement_list"),
    Tree(SyntaxNodeKind::Declaration, "declaration", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::Declaration, "preproc_value_declaration", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::FieldDeclaration, "field_declaration", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::FieldDeclaration, "mock_method_declaration", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::AliasDeclaration, "alias_declaration", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::AliasDeclaration, "function_pointer_alias_declaration", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::Declaration, "deduction_guide_declaration", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::FunctionDefinition, "function_definition", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::FunctionDefinition, "macro_function_definition", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::FunctionDefinition, "conditional_macro_function_definition", Bit(
        TokenClass::MacroDeclarationFragment
    )),
    Tree(SyntaxNodeKind::CompoundStatement, "compound_statement", Bit(TokenClass::CompoundBlock)),
    Tree(SyntaxNodeKind::FieldDeclarationList, "field_declaration_list", Bit(TokenClass::CompoundBlock)),
    Tree(SyntaxNodeKind::EnumeratorList, "enumerator_list", Bit(TokenClass::CompoundBlock)),
    Tree(SyntaxNodeKind::InitializerList, "initializer_list"),
    Tree(SyntaxNodeKind::FieldInitializerList, "field_initializer_list"),
    Tree(SyntaxNodeKind::FieldInitializer, "field_initializer"),
    Tree(SyntaxNodeKind::DeclarationList, "declaration_list", Bit(TokenClass::CompoundBlock)),
    Tree(SyntaxNodeKind::NamespaceDefinition, "namespace_definition"),
    Tree(SyntaxNodeKind::EnumSpecifier, "enum_specifier", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::ClassSpecifier, "class_specifier", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::StructSpecifier, "struct_specifier", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::BaseClassClause, "base_class_clause"),
    Tree(SyntaxNodeKind::AccessSpecifier, "access_specifier"),
    Tree(SyntaxNodeKind::IfStatement, "if_statement", Bit(TokenClass::ControlHeader) | Bit(
        TokenClass::FlatLogicalHeader
    )),
    Tree(SyntaxNodeKind::ElseClause, "else_clause"),
    Tree(SyntaxNodeKind::ForStatement, "for_statement", Bit(TokenClass::ControlHeader)),
    Tree(SyntaxNodeKind::WhileStatement, "while_statement", Bit(TokenClass::ControlHeader) | Bit(
        TokenClass::FlatLogicalHeader
    )),
    Tree(SyntaxNodeKind::DoStatement, "do_statement"),
    Tree(SyntaxNodeKind::SwitchStatement, "switch_statement", Bit(TokenClass::ControlHeader) | Bit(
        TokenClass::FlatLogicalHeader
    )),
    Tree(SyntaxNodeKind::CaseStatement, "case_statement"),
    Tree(SyntaxNodeKind::ConditionClause, "condition_clause", Bit(TokenClass::ControlHeader) | Bit(
        TokenClass::FlatLogicalHeader
    )),
    Tree(SyntaxNodeKind::InitStatement, "init_statement"),
    Tree(SyntaxNodeKind::PreprocCall, "preproc_call", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocDef, "preproc_def", Bit(TokenClass::MacroDefinition)),
    Tree(SyntaxNodeKind::PreprocFunctionDef, "preproc_function_def", Bit(TokenClass::MacroDefinition)),
    Tree(SyntaxNodeKind::PreprocFunctionDef, "raw_macro_function_definition", kAtomicPreprocessorClasses | Bit(
        TokenClass::MacroDefinition
    )),
    Tree(SyntaxNodeKind::PreprocDef, "raw_macro_definition", kAtomicPreprocessorClasses | Bit(
        TokenClass::MacroDefinition
    )),
    Tree(SyntaxNodeKind::PreprocFunctionDef, "preproc_hashhash_function_def", kAtomicPreprocessorClasses | Bit(
        TokenClass::MacroDefinition
    )),
    Tree(SyntaxNodeKind::PreprocInclude, "preproc_include", kAtomicPreprocessorClasses | Bit(
        TokenClass::IncludeDirective
    )),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_if"),
    Tree(SyntaxNodeKind::PreprocIfdef, "preproc_ifdef"),
    Tree(SyntaxNodeKind::PreprocElse, "preproc_else"),
    Tree(SyntaxNodeKind::PreprocElif, "preproc_elif"),
    Tree(SyntaxNodeKind::PreprocUsing, "preproc_using", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocParams, "preproc_params"),
    Tree(SyntaxNodeKind::PreprocArg, "preproc_arg", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::PreprocArg, "macro_arrow_chain", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FunctionDefinition, "benchmark_macro_function_definition"),
    Tree(SyntaxNodeKind::FreeToken, "top_level_macro_call_statement", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FreeToken, "top_level_operator_macro_call", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FreeToken, "name_macro_call", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FreeToken, "function_pointer_type_descriptor", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FreeToken, "locked_channel_proxy_type", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FreeToken, "map_proxy_type", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FreeToken, "openssl_stack_type", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FreeToken, "qualified_constructor_identifier", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FreeToken, "using_operator_pack_declaration", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FreeToken, "deleted_operator_declaration", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FreeToken, "attributed_friend_operator_declaration", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::Tree, "throw_expression"),
    Tree(SyntaxNodeKind::Tree, "typeid_expression"),
    Tree(SyntaxNodeKind::Tree, "cpp_cast_expression"),
    Tree(SyntaxNodeKind::Tree, "macro_exception_type"),
    Tree(SyntaxNodeKind::Tree, "capitalized_call_statement"),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_streaming_statement", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_pipeline_tail_statement", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_selected_braced_if_else_statement", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_selected_if_header", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_endif_fragment", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "standalone_qualifier_preproc_if", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "standalone_attribute_preproc_if", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "conditional_macro_function_header", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIfdef, "declaration_suffix_preproc_ifdef", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIfdef, "preproc_define_ifdef", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIfdef, "preproc_nested_define_ifdef", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIfdef, "preproc_define_elif_chain", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIfdef, "preproc_define_namespace_if", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIfdef, "conditional_extern_c_open", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIfdef, "conditional_extern_c_close", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_binary_expression_fragment", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_logical_expression_fragment", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_bitwise_expression_fragment", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_logical_tail_expression_fragment", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_condition_expression", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_semicolon_initializer", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_initializer_expression", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_template_argument_fragment", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_field_initializer_fragment", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_string_literal_fragment", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocIfdef, "preproc_argument_fragment", kAtomicPreprocessorClasses),
    Tree(SyntaxNodeKind::PreprocArg, "macro_argument_declaration_fragment", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::PreprocArg, "macro_plain_declaration_fragment", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::PreprocArg, "macro_statement_declaration_fragment", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::PreprocArg, "macro_typed_declaration_fragment", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_if_in_field_declaration_list"),
    Tree(SyntaxNodeKind::PreprocIfdef, "preproc_ifdef_in_field_declaration_list"),
    Tree(SyntaxNodeKind::PreprocElse, "preproc_else_in_field_declaration_list"),
    Tree(SyntaxNodeKind::PreprocElif, "preproc_elif_in_field_declaration_list"),
    Tree(SyntaxNodeKind::PreprocIf, "preproc_if_in_enumerator_list"),
    Tree(SyntaxNodeKind::PreprocIfdef, "preproc_ifdef_in_enumerator_list"),
    Tree(SyntaxNodeKind::PreprocElse, "preproc_else_in_enumerator_list"),
    Tree(SyntaxNodeKind::PreprocElif, "preproc_elif_in_enumerator_list"),
    Tree(SyntaxNodeKind::PreprocElif, "preproc_elifdef"),
    Tree(SyntaxNodeKind::BinaryExpression, "binary_expression"),
    Tree(SyntaxNodeKind::UnaryExpression, "unary_expression"),
    Tree(SyntaxNodeKind::ConditionalExpression, "conditional_expression"),
    Tree(SyntaxNodeKind::CommaExpression, "comma_expression"),
    Tree(SyntaxNodeKind::AssignmentExpression, "assignment_expression"),
    Tree(SyntaxNodeKind::InitDeclarator, "init_declarator"),
    Tree(SyntaxNodeKind::CastExpression, "cast_expression"),
    Tree(SyntaxNodeKind::PointerDeclarator, "pointer_declarator", Bit(TokenClass::DeclaratorReferenceParent)),
    Tree(SyntaxNodeKind::AbstractPointerDeclarator, "abstract_pointer_declarator", Bit(
        TokenClass::DeclaratorReferenceParent
    )),
    Tree(SyntaxNodeKind::ReferenceDeclarator, "reference_declarator", Bit(TokenClass::DeclaratorReferenceParent)),
    Tree(SyntaxNodeKind::AbstractReferenceDeclarator, "abstract_reference_declarator", Bit(
        TokenClass::DeclaratorReferenceParent
    )),
    Tree(SyntaxNodeKind::HandleDeclarator, "handle_declarator", Bit(TokenClass::DeclaratorReferenceParent)),
    Tree(SyntaxNodeKind::AbstractHandleDeclarator, "abstract_handle_declarator", Bit(
        TokenClass::DeclaratorReferenceParent
    )),
    Tree(SyntaxNodeKind::MemberPointerDeclarator, "member_pointer_declarator", Bit(
        TokenClass::DeclaratorReferenceParent
    )),
    Tree(SyntaxNodeKind::FunctionDeclarator, "function_declarator"),
    Tree(SyntaxNodeKind::AbstractFunctionDeclarator, "abstract_function_declarator"),
    Tree(SyntaxNodeKind::ParenthesizedDeclarator, "parenthesized_declarator", Bit(TokenClass::ParenthesizedDeclarator)),
    Tree(SyntaxNodeKind::AbstractParenthesizedDeclarator, "abstract_parenthesized_declarator", Bit(
        TokenClass::ParenthesizedDeclarator
    )),
    Tree(SyntaxNodeKind::ParameterList, "parameter_list"),
    Tree(SyntaxNodeKind::ParameterList, "mock_method_parameter_list", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::ArgumentList, "argument_list"),
    Tree(SyntaxNodeKind::SubscriptArgumentList, "subscript_argument_list"),
    Tree(SyntaxNodeKind::TemplateParameterList, "template_parameter_list"),
    Tree(SyntaxNodeKind::TemplateArgumentList, "template_argument_list"),
    Tree(SyntaxNodeKind::TemplateDeclaration, "template_declaration", Bit(TokenClass::MacroDeclarationFragment)),
    Tree(SyntaxNodeKind::RequiresClause, "requires_clause"),
    Tree(SyntaxNodeKind::RequiresClause, "nested_requirement"),
    Tree(SyntaxNodeKind::RefQualifier, "ref_qualifier"),
    Tree(SyntaxNodeKind::LambdaExpression, "lambda_expression"),
    Tree(SyntaxNodeKind::LambdaCaptureSpecifier, "lambda_capture_specifier"),
    Tree(SyntaxNodeKind::StructuredBindingDeclarator, "structured_binding_declarator"),
    Tree(SyntaxNodeKind::FieldDesignator, "field_designator"),
    Tree(SyntaxNodeKind::FieldExpression, "field_expression"),
    Tree(SyntaxNodeKind::TrailingReturnType, "trailing_return_type"),
    Tree(SyntaxNodeKind::OperatorName, "operator_name"),
    Tree(SyntaxNodeKind::OperatorCast, "operator_cast"),
    Tree(SyntaxNodeKind::FreeToken, "deleted_operator_cast_declaration", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::AttributeSpecifier, "attribute_specifier"),
    Tree(SyntaxNodeKind::AttributeDeclaration, "attribute_declaration"),
    Tree(SyntaxNodeKind::Attribute, "attribute"),
    Tree(SyntaxNodeKind::AttributedStatement, "attributed_statement"),
    Tree(SyntaxNodeKind::MsCallModifier, "ms_call_modifier", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::MsDeclspecModifier, "ms_declspec_modifier", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::FunctionSuffixMacro, "function_suffix_macro", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::Identifier, "macro_initializer", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::ConcatenatedString, "concatenated_string", Bit(TokenClass::StringLike)),
    Tree(SyntaxNodeKind::StringLiteral, "suffixed_string_literal", kStringLikeClasses),
    Tree(SyntaxNodeKind::RawStringLiteral, "raw_string_literal", kStringLikeClasses),
    Tree(SyntaxNodeKind::StringLiteral, "string_literal", kStringLikeClasses),
    Tree(SyntaxNodeKind::SystemLibString, "system_lib_string", Bit(TokenClass::WholeNodeAsFreeToken)),
    Tree(SyntaxNodeKind::CharacterLiteral, "char_literal", kStringLikeClasses),
    Tree(SyntaxNodeKind::NumberLiteral, "number_literal", kNumberLiteralClasses),
    Tree(SyntaxNodeKind::Identifier, "identifier"),
    Tree(SyntaxNodeKind::Identifier, "field_identifier"),
    Tree(SyntaxNodeKind::Identifier, "namespace_identifier"),
    Tree(SyntaxNodeKind::Identifier, "type_identifier"),
    Tree(SyntaxNodeKind::Identifier, "qualified_identifier"),
    Tree(SyntaxNodeKind::Identifier, "macro_qualified_identifier"),
    Token(SyntaxNodeKind::Hash, "#"),
    Token(SyntaxNodeKind::LeftParen, "("),
    Token(SyntaxNodeKind::RightParen, ")"),
    Token(SyntaxNodeKind::LeftBracket, "["),
    Token(SyntaxNodeKind::RightBracket, "]"),
    Token(SyntaxNodeKind::LeftBrace, "{"),
    Token(SyntaxNodeKind::RightBrace, "}"),
    Token(SyntaxNodeKind::Less, "<", Bit(TokenClass::BinaryOperator)),
    Token(SyntaxNodeKind::Greater, ">", Bit(TokenClass::BinaryOperator)),
    Token(SyntaxNodeKind::LessEqual, "<=", Bit(TokenClass::BinaryOperator)),
    Token(SyntaxNodeKind::GreaterEqual, ">=", Bit(TokenClass::BinaryOperator)),
    Token(SyntaxNodeKind::EqualEqual, "==", Bit(TokenClass::BinaryOperator)),
    Token(SyntaxNodeKind::BangEqual, "!=", Bit(TokenClass::BinaryOperator)),
    Token(SyntaxNodeKind::Spaceship, "<=>", Bit(TokenClass::BinaryOperator)),
    Token(SyntaxNodeKind::Plus, "+", kChainBinaryClasses | Bit(TokenClass::UnaryOperator)),
    Token(SyntaxNodeKind::Minus, "-", Bit(TokenClass::BinaryOperator) | Bit(TokenClass::UnaryOperator)),
    Token(SyntaxNodeKind::Star, "*", kChainBinaryClasses | Bit(TokenClass::UnaryOperator) | Bit(
        TokenClass::DeclaratorReferenceToken
    )),
    Token(SyntaxNodeKind::Slash, "/", Bit(TokenClass::BinaryOperator)),
    Token(SyntaxNodeKind::Percent, "%", Bit(TokenClass::BinaryOperator) | Bit(TokenClass::DeclaratorReferenceToken)),
    Token(SyntaxNodeKind::Caret, "^", kChainBinaryClasses | Bit(TokenClass::DeclaratorReferenceToken)),
    Token(SyntaxNodeKind::Ampersand, "&", kChainBinaryClasses | Bit(TokenClass::UnaryOperator) | Bit(
        TokenClass::DeclaratorReferenceToken
    )),
    Token(SyntaxNodeKind::Pipe, "|", kChainBinaryClasses),
    Token(SyntaxNodeKind::Bang, "!", Bit(TokenClass::UnaryOperator)),
    Token(SyntaxNodeKind::Tilde, "~", Bit(TokenClass::UnaryOperator)),
    Token(SyntaxNodeKind::Equal, "=", Bit(TokenClass::AssignmentOperator)),
    Token(SyntaxNodeKind::PlusEqual, "+=", Bit(TokenClass::AssignmentOperator)),
    Token(SyntaxNodeKind::MinusEqual, "-=", Bit(TokenClass::AssignmentOperator)),
    Token(SyntaxNodeKind::StarEqual, "*=", Bit(TokenClass::AssignmentOperator)),
    Token(SyntaxNodeKind::SlashEqual, "/=", Bit(TokenClass::AssignmentOperator)),
    Token(SyntaxNodeKind::PercentEqual, "%=", Bit(TokenClass::AssignmentOperator)),
    Token(SyntaxNodeKind::CaretEqual, "^=", Bit(TokenClass::AssignmentOperator)),
    Token(SyntaxNodeKind::AmpersandEqual, "&=", Bit(TokenClass::AssignmentOperator)),
    Token(SyntaxNodeKind::PipeEqual, "|=", Bit(TokenClass::AssignmentOperator)),
    Token(SyntaxNodeKind::LessLess, "<<", kChainBinaryClasses),
    Token(SyntaxNodeKind::GreaterGreater, ">>", kChainBinaryClasses),
    Token(SyntaxNodeKind::LessLessEqual, "<<=", Bit(TokenClass::AssignmentOperator)),
    Token(SyntaxNodeKind::GreaterGreaterEqual, ">>=", Bit(TokenClass::AssignmentOperator)),
    Token(SyntaxNodeKind::AmpersandAmpersand, "&&", kChainBinaryClasses | Bit(TokenClass::DeclaratorReferenceToken)),
    Token(SyntaxNodeKind::PipePipe, "||", kChainBinaryClasses),
    Token(SyntaxNodeKind::PlusPlus, "++", Bit(TokenClass::UnaryOperator)),
    Token(SyntaxNodeKind::MinusMinus, "--", Bit(TokenClass::UnaryOperator)),
    Token(SyntaxNodeKind::Arrow, "->", Bit(TokenClass::MemberOperator)),
    Token(SyntaxNodeKind::Dot, ".", Bit(TokenClass::MemberOperator)),
    Token(SyntaxNodeKind::ArrowStar, "->*", Bit(TokenClass::MemberOperator)),
    Token(SyntaxNodeKind::DotStar, ".*", Bit(TokenClass::MemberOperator)),
    Token(SyntaxNodeKind::ColonColon, "::", Bit(TokenClass::MemberOperator)),
    Token(SyntaxNodeKind::Question, "?"),
    Token(SyntaxNodeKind::Colon, ":"),
    Token(SyntaxNodeKind::Semicolon, ";"),
    Token(SyntaxNodeKind::Comma, ",", Bit(TokenClass::ChainOperator)),
    Token(SyntaxNodeKind::Ellipsis, "..."),
    Keyword(SyntaxNodeKind::KeywordAlignas, "alignas"),
    Keyword(SyntaxNodeKind::KeywordAlignof, "alignof"),
    Keyword(SyntaxNodeKind::KeywordAsm, "asm"),
    Keyword(SyntaxNodeKind::KeywordAuto, "auto"),
    Keyword(SyntaxNodeKind::KeywordBool, "bool"),
    Keyword(SyntaxNodeKind::KeywordBreak, "break"),
    Keyword(SyntaxNodeKind::KeywordCase, "case"),
    Keyword(SyntaxNodeKind::KeywordCatch, "catch", Bit(TokenClass::ControlKeyword) | Bit(
        TokenClass::AttachAfterBlockKeyword
    )),
    Keyword(SyntaxNodeKind::KeywordChar, "char"),
    Keyword(SyntaxNodeKind::KeywordChar16T, "char16_t"),
    Keyword(SyntaxNodeKind::KeywordChar32T, "char32_t"),
    Keyword(SyntaxNodeKind::KeywordClass, "class"),
    Keyword(SyntaxNodeKind::KeywordConcept, "concept"),
    Keyword(SyntaxNodeKind::KeywordConst, "const"),
    Keyword(SyntaxNodeKind::KeywordConsteval, "consteval"),
    Keyword(SyntaxNodeKind::KeywordConstexpr, "constexpr"),
    Keyword(SyntaxNodeKind::KeywordConstinit, "constinit"),
    Keyword(SyntaxNodeKind::KeywordConstCast, "const_cast"),
    Keyword(SyntaxNodeKind::KeywordContinue, "continue"),
    Keyword(SyntaxNodeKind::KeywordDecltype, "decltype"),
    Keyword(SyntaxNodeKind::KeywordDefault, "default"),
    Keyword(SyntaxNodeKind::KeywordDelete, "delete"),
    Keyword(SyntaxNodeKind::KeywordDo, "do"),
    Keyword(SyntaxNodeKind::KeywordDouble, "double"),
    Keyword(SyntaxNodeKind::KeywordDynamicCast, "dynamic_cast"),
    Keyword(SyntaxNodeKind::KeywordElse, "else", Bit(TokenClass::AttachAfterBlockKeyword)),
    Keyword(SyntaxNodeKind::KeywordEnum, "enum"),
    Keyword(SyntaxNodeKind::KeywordExplicit, "explicit"),
    Keyword(SyntaxNodeKind::KeywordExport, "export"),
    Keyword(SyntaxNodeKind::KeywordExtern, "extern"),
    Keyword(SyntaxNodeKind::KeywordFalse, "false"),
    Keyword(SyntaxNodeKind::KeywordFinal, "final"),
    Keyword(SyntaxNodeKind::KeywordFinally, "finally", Bit(TokenClass::AttachAfterBlockKeyword)),
    Keyword(SyntaxNodeKind::KeywordFloat, "float"),
    Keyword(SyntaxNodeKind::KeywordFor, "for", Bit(TokenClass::ControlKeyword)),
    Keyword(SyntaxNodeKind::KeywordFriend, "friend"),
    Keyword(SyntaxNodeKind::KeywordGoto, "goto"),
    Keyword(SyntaxNodeKind::KeywordIf, "if", Bit(TokenClass::ControlKeyword)),
    Keyword(SyntaxNodeKind::KeywordInline, "inline"),
    Keyword(SyntaxNodeKind::KeywordInt, "int"),
    Keyword(SyntaxNodeKind::KeywordLong, "long"),
    Keyword(SyntaxNodeKind::KeywordMutable, "mutable"),
    Keyword(SyntaxNodeKind::KeywordNamespace, "namespace"),
    Keyword(SyntaxNodeKind::KeywordNew, "new"),
    Keyword(SyntaxNodeKind::KeywordNoexcept, "noexcept"),
    Keyword(SyntaxNodeKind::KeywordNullptr, "nullptr"),
    Keyword(SyntaxNodeKind::KeywordOperator, "operator"),
    Keyword(SyntaxNodeKind::KeywordOverride, "override"),
    Keyword(SyntaxNodeKind::KeywordPrivate, "private", Bit(TokenClass::AccessKeyword)),
    Keyword(SyntaxNodeKind::KeywordProtected, "protected", Bit(TokenClass::AccessKeyword)),
    Keyword(SyntaxNodeKind::KeywordPublic, "public", Bit(TokenClass::AccessKeyword)),
    Keyword(SyntaxNodeKind::KeywordRegister, "register"),
    Keyword(SyntaxNodeKind::KeywordReinterpretCast, "reinterpret_cast"),
    Keyword(SyntaxNodeKind::KeywordRequires, "requires"),
    Keyword(SyntaxNodeKind::KeywordReturn, "return"),
    Keyword(SyntaxNodeKind::KeywordShort, "short"),
    Keyword(SyntaxNodeKind::KeywordSigned, "signed"),
    Keyword(SyntaxNodeKind::KeywordSizeof, "sizeof"),
    Keyword(SyntaxNodeKind::KeywordStatic, "static"),
    Keyword(SyntaxNodeKind::KeywordStaticAssert, "static_assert"),
    Keyword(SyntaxNodeKind::KeywordStaticCast, "static_cast"),
    Keyword(SyntaxNodeKind::KeywordStruct, "struct"),
    Keyword(SyntaxNodeKind::KeywordSwitch, "switch", Bit(TokenClass::ControlKeyword)),
    Keyword(SyntaxNodeKind::KeywordTemplate, "template"),
    Keyword(SyntaxNodeKind::KeywordThis, "this"),
    Keyword(SyntaxNodeKind::KeywordThreadLocal, "thread_local"),
    Keyword(SyntaxNodeKind::KeywordThrow, "throw"),
    Keyword(SyntaxNodeKind::KeywordTrue, "true"),
    Keyword(SyntaxNodeKind::KeywordTry, "try"),
    Keyword(SyntaxNodeKind::KeywordTypedef, "typedef"),
    Keyword(SyntaxNodeKind::KeywordTypeid, "typeid"),
    Keyword(SyntaxNodeKind::KeywordTypename, "typename"),
    Keyword(SyntaxNodeKind::KeywordUnion, "union"),
    Keyword(SyntaxNodeKind::KeywordUnsigned, "unsigned"),
    Keyword(SyntaxNodeKind::KeywordUsing, "using"),
    Keyword(SyntaxNodeKind::KeywordVirtual, "virtual"),
    Keyword(SyntaxNodeKind::KeywordVoid, "void"),
    Keyword(SyntaxNodeKind::KeywordVolatile, "volatile"),
    Keyword(SyntaxNodeKind::KeywordWcharT, "wchar_t"),
    Keyword(SyntaxNodeKind::KeywordWhile, "while", Bit(TokenClass::ControlKeyword) | Bit(
        TokenClass::AttachAfterBlockKeyword
    )),
    Keyword(SyntaxNodeKind::KeywordCdecl, "__cdecl"),
    Keyword(SyntaxNodeKind::KeywordDeclspec, "__declspec"),
    Keyword(SyntaxNodeKind::KeywordCoAwait, "co_await"),
    Keyword(SyntaxNodeKind::KeywordCoReturn, "co_return"),
    Keyword(SyntaxNodeKind::KeywordCoYield, "co_yield")
});

constexpr size_t KindIndex(SyntaxNodeKind kind) {
    return static_cast<size_t>(kind);
}

constexpr size_t kSyntaxNodeKindCount = KindIndex(SyntaxNodeKind::KeywordCoYield) + 1;

constexpr auto BuildSyntaxKindInfoByKind() {
    std::array<SyntaxKindInfo, kSyntaxNodeKindCount> result{};
    for (const SyntaxKindMapping& mapping : kSyntaxKindMappings) {
        SyntaxKindInfo& info = result[KindIndex(mapping.kind)];
        info.classes |= mapping.classes & ~kSymbolLocalClasses;
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

using SymbolInfoTable = std::vector<SyntaxSymbolInfo>;

SymbolInfoTable MakeSymbolInfoTable() {
    return SymbolInfoTable(ts_language_symbol_count(tree_sitter_cpp()));
}

void StoreTreeSymbolInfo(
    SymbolInfoTable& table,
    std::string_view name,
    SyntaxNodeKind kind,
    std::uint64_t classes = 0
) {
    const TSSymbol symbol =
        ts_language_symbol_for_name(tree_sitter_cpp(), name.data(), static_cast<uint32_t>(name.size()), true);
    if (static_cast<size_t>(symbol) < table.size()) {
        table[symbol].treeKind = kind;
        table[symbol].classes |= classes;
    }
}

void StoreTokenSymbolInfo(
    SymbolInfoTable& table,
    std::string_view name,
    bool isNamed,
    SyntaxNodeKind kind,
    std::uint64_t classes = 0
) {
    const TSSymbol symbol =
        ts_language_symbol_for_name(tree_sitter_cpp(), name.data(), static_cast<uint32_t>(name.size()), isNamed);
    if (static_cast<size_t>(symbol) < table.size()) {
        table[symbol].tokenKind = kind;
        table[symbol].classes |= classes;
    }
}

void StoreSymbolInfoRole(SymbolInfoTable& table, std::string_view name, SyntaxWrapperRole role) {
    const TSSymbol symbol =
        ts_language_symbol_for_name(tree_sitter_cpp(), name.data(), static_cast<uint32_t>(name.size()), true);
    if (static_cast<size_t>(symbol) < table.size()) {
        table[symbol].wrapperRole = role;
    }
}

const SymbolInfoTable& SyntaxInfoBySymbol() {
    static const SymbolInfoTable symbols = [] {
        SymbolInfoTable result = MakeSymbolInfoTable();
        for (const SyntaxKindMapping& mapping : kSyntaxKindMappings) {
            if (!mapping.treeType.empty()) {
                StoreTreeSymbolInfo(result, mapping.treeType, mapping.kind, mapping.classes);
            }
            if (!mapping.tokenText.empty()) {
                StoreTokenSymbolInfo(result, mapping.tokenText, false, mapping.kind, mapping.classes);
                StoreTokenSymbolInfo(result, mapping.tokenText, true, mapping.kind, mapping.classes);
            }
        }
        StoreTreeSymbolInfo(result, "comment", SyntaxNodeKind::Comment, kCommentClasses);

        constexpr std::string_view flattenNames[] = {
            "call_expression",
            "compound_literal_expression",
            "initializer_pair",
            "parameter_declaration",
            "qualified_operator_cast_identifier",
            "sized_type_specifier",
            "subscript_expression",
            "template_function",
            "template_method",
            "template_type",
            "type_descriptor"
        };
        constexpr std::string_view wholeTokenNames[] = {
            "null",
            "placeholder_type_specifier",
            "primitive_type",
            "storage_class_specifier",
            "type_qualifier",
            "virtual_specifier"
        };
        constexpr std::string_view wholeAtomNames[] = {
            "dependent_field_identifier",
            "dependent_identifier",
            "dependent_type_identifier",
            "field_designator",
            "field_identifier",
            "identifier",
            "namespace_identifier",
            "pointer_expression",
            "qualified_field_identifier",
            "qualified_identifier",
            "qualified_type_identifier",
            "type_identifier",
            "unary_expression",
            "update_expression"
        };
        constexpr std::string_view compactEmptyDelimitedNames[] = {
            "argument_list",
            "field_initializer_list",
            "initializer_list",
            "lambda_capture_specifier",
            "subscript_argument_list",
            "template_argument_list"
        };
        for (const std::string_view name : flattenNames) {
            StoreSymbolInfoRole(result, name, SyntaxWrapperRole::Flatten);
        }
        for (const std::string_view name : wholeTokenNames) {
            StoreSymbolInfoRole(result, name, SyntaxWrapperRole::WholeToken);
        }
        for (const std::string_view name : wholeAtomNames) {
            StoreSymbolInfoRole(result, name, SyntaxWrapperRole::WholeAtom);
        }
        for (const std::string_view name : compactEmptyDelimitedNames) {
            StoreSymbolInfoRole(result, name, SyntaxWrapperRole::CompactEmptyDelimited);
        }
        StoreSymbolInfoRole(result, "field_expression", SyntaxWrapperRole::WholeFieldAtom);
        return result;
    }
    ();
    return symbols;
}

bool NodeOrDescendantHasClass(const SyntaxNode& node, TokenClass tokenClass) {
    if (SyntaxNodeKindHasClass(node.kind, tokenClass)) {
        return true;
    }
    for (const SyntaxNode* child : node.children) {
        if (child != nullptr && NodeOrDescendantHasClass(*child, tokenClass)) {
            return true;
        }
    }
    return false;
}

const SyntaxNode* OnlyContentChild(const SyntaxNode& node) {
    const SyntaxNode* contentChild = nullptr;
    for (const SyntaxNode* child : node.children) {
        if (child == nullptr || SyntaxNodeKindHasClass(child->kind, TokenClass::Trivia) || SyntaxNodeKindHasClass(
            child->kind,
            TokenClass::Known
        )) {
            continue;
        }
        if (contentChild != nullptr) {
            return nullptr;
        }
        contentChild = child;
    }
    return contentChild;
}

}  // namespace

SyntaxNode::SyntaxNode(std::pmr::memory_resource* childResource) : children(childResource) {}

FormatModel::FormatModel() : childStorage(std::make_unique<std::pmr::monotonic_buffer_resource>()) {}

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

SyntaxSymbolInfo SyntaxSymbolInfoForSymbol(TSSymbol symbol) {
    const auto& symbols = SyntaxInfoBySymbol();
    return static_cast<size_t>(symbol) < symbols.size() ? symbols[symbol] : SyntaxSymbolInfo{};
}

std::string_view SyntaxNodeKindTokenText(SyntaxNodeKind kind) {
    const size_t index = KindIndex(kind);
    if (index >= kSyntaxKindInfoByKind.size()) {
        return {};
    }
    return kSyntaxKindInfoByKind[index].tokenText;
}

std::string_view SyntaxNodeKindName(SyntaxNodeKind kind) {
    switch (kind) {
        case SyntaxNodeKind::Unknown:
            return "Unknown";
        case SyntaxNodeKind::Tree:
            return "Tree";
        case SyntaxNodeKind::FreeToken:
            return "FreeToken";
        case SyntaxNodeKind::Comment:
            return "Comment";
        case SyntaxNodeKind::TrailingComment:
            return "TrailingComment";
        case SyntaxNodeKind::BlankLine:
            return "BlankLine";
        case SyntaxNodeKind::TranslationUnit:
            return "TranslationUnit";
        case SyntaxNodeKind::IncludeRun:
            return "IncludeRun";
        case SyntaxNodeKind::MacroReplacementList:
            return "MacroReplacementList";
        case SyntaxNodeKind::Declaration:
            return "Declaration";
        case SyntaxNodeKind::FieldDeclaration:
            return "FieldDeclaration";
        case SyntaxNodeKind::AliasDeclaration:
            return "AliasDeclaration";
        case SyntaxNodeKind::FunctionDefinition:
            return "FunctionDefinition";
        case SyntaxNodeKind::CompoundStatement:
            return "CompoundStatement";
        case SyntaxNodeKind::FieldDeclarationList:
            return "FieldDeclarationList";
        case SyntaxNodeKind::EnumeratorList:
            return "EnumeratorList";
        case SyntaxNodeKind::InitializerList:
            return "InitializerList";
        case SyntaxNodeKind::FieldInitializerList:
            return "FieldInitializerList";
        case SyntaxNodeKind::FieldInitializer:
            return "FieldInitializer";
        case SyntaxNodeKind::DeclarationList:
            return "DeclarationList";
        case SyntaxNodeKind::NamespaceDefinition:
            return "NamespaceDefinition";
        case SyntaxNodeKind::EnumSpecifier:
            return "EnumSpecifier";
        case SyntaxNodeKind::ClassSpecifier:
            return "ClassSpecifier";
        case SyntaxNodeKind::StructSpecifier:
            return "StructSpecifier";
        case SyntaxNodeKind::BaseClassClause:
            return "BaseClassClause";
        case SyntaxNodeKind::AccessSpecifier:
            return "AccessSpecifier";
        case SyntaxNodeKind::IfStatement:
            return "IfStatement";
        case SyntaxNodeKind::ElseClause:
            return "ElseClause";
        case SyntaxNodeKind::ForStatement:
            return "ForStatement";
        case SyntaxNodeKind::WhileStatement:
            return "WhileStatement";
        case SyntaxNodeKind::DoStatement:
            return "DoStatement";
        case SyntaxNodeKind::SwitchStatement:
            return "SwitchStatement";
        case SyntaxNodeKind::CaseStatement:
            return "CaseStatement";
        case SyntaxNodeKind::ConditionClause:
            return "ConditionClause";
        case SyntaxNodeKind::InitStatement:
            return "InitStatement";
        case SyntaxNodeKind::PreprocCall:
            return "PreprocCall";
        case SyntaxNodeKind::PreprocDef:
            return "PreprocDef";
        case SyntaxNodeKind::PreprocFunctionDef:
            return "PreprocFunctionDef";
        case SyntaxNodeKind::PreprocInclude:
            return "PreprocInclude";
        case SyntaxNodeKind::PreprocIf:
            return "PreprocIf";
        case SyntaxNodeKind::PreprocIfdef:
            return "PreprocIfdef";
        case SyntaxNodeKind::PreprocElse:
            return "PreprocElse";
        case SyntaxNodeKind::PreprocElif:
            return "PreprocElif";
        case SyntaxNodeKind::PreprocUsing:
            return "PreprocUsing";
        case SyntaxNodeKind::PreprocParams:
            return "PreprocParams";
        case SyntaxNodeKind::PreprocArg:
            return "PreprocArg";
        case SyntaxNodeKind::BinaryExpression:
            return "BinaryExpression";
        case SyntaxNodeKind::UnaryExpression:
            return "UnaryExpression";
        case SyntaxNodeKind::ConditionalExpression:
            return "ConditionalExpression";
        case SyntaxNodeKind::CommaExpression:
            return "CommaExpression";
        case SyntaxNodeKind::AssignmentExpression:
            return "AssignmentExpression";
        case SyntaxNodeKind::InitDeclarator:
            return "InitDeclarator";
        case SyntaxNodeKind::CastExpression:
            return "CastExpression";
        case SyntaxNodeKind::PointerDeclarator:
            return "PointerDeclarator";
        case SyntaxNodeKind::AbstractPointerDeclarator:
            return "AbstractPointerDeclarator";
        case SyntaxNodeKind::ReferenceDeclarator:
            return "ReferenceDeclarator";
        case SyntaxNodeKind::AbstractReferenceDeclarator:
            return "AbstractReferenceDeclarator";
        case SyntaxNodeKind::HandleDeclarator:
            return "HandleDeclarator";
        case SyntaxNodeKind::AbstractHandleDeclarator:
            return "AbstractHandleDeclarator";
        case SyntaxNodeKind::MemberPointerDeclarator:
            return "MemberPointerDeclarator";
        case SyntaxNodeKind::FunctionDeclarator:
            return "FunctionDeclarator";
        case SyntaxNodeKind::AbstractFunctionDeclarator:
            return "AbstractFunctionDeclarator";
        case SyntaxNodeKind::ParenthesizedDeclarator:
            return "ParenthesizedDeclarator";
        case SyntaxNodeKind::AbstractParenthesizedDeclarator:
            return "AbstractParenthesizedDeclarator";
        case SyntaxNodeKind::ParameterList:
            return "ParameterList";
        case SyntaxNodeKind::ArgumentList:
            return "ArgumentList";
        case SyntaxNodeKind::SubscriptArgumentList:
            return "SubscriptArgumentList";
        case SyntaxNodeKind::TemplateParameterList:
            return "TemplateParameterList";
        case SyntaxNodeKind::TemplateArgumentList:
            return "TemplateArgumentList";
        case SyntaxNodeKind::TemplateDeclaration:
            return "TemplateDeclaration";
        case SyntaxNodeKind::RequiresClause:
            return "RequiresClause";
        case SyntaxNodeKind::RefQualifier:
            return "RefQualifier";
        case SyntaxNodeKind::LambdaExpression:
            return "LambdaExpression";
        case SyntaxNodeKind::LambdaCaptureSpecifier:
            return "LambdaCaptureSpecifier";
        case SyntaxNodeKind::StructuredBindingDeclarator:
            return "StructuredBindingDeclarator";
        case SyntaxNodeKind::FieldDesignator:
            return "FieldDesignator";
        case SyntaxNodeKind::FieldExpression:
            return "FieldExpression";
        case SyntaxNodeKind::TrailingReturnType:
            return "TrailingReturnType";
        case SyntaxNodeKind::OperatorName:
            return "OperatorName";
        case SyntaxNodeKind::OperatorCast:
            return "OperatorCast";
        case SyntaxNodeKind::AttributeSpecifier:
            return "AttributeSpecifier";
        case SyntaxNodeKind::AttributeDeclaration:
            return "AttributeDeclaration";
        case SyntaxNodeKind::Attribute:
            return "Attribute";
        case SyntaxNodeKind::AttributedStatement:
            return "AttributedStatement";
        case SyntaxNodeKind::MsCallModifier:
            return "MsCallModifier";
        case SyntaxNodeKind::MsDeclspecModifier:
            return "MsDeclspecModifier";
        case SyntaxNodeKind::FunctionSuffixMacro:
            return "FunctionSuffixMacro";
        case SyntaxNodeKind::ConcatenatedString:
            return "ConcatenatedString";
        case SyntaxNodeKind::RawStringLiteral:
            return "RawStringLiteral";
        case SyntaxNodeKind::StringLiteral:
            return "StringLiteral";
        case SyntaxNodeKind::SystemLibString:
            return "SystemLibString";
        case SyntaxNodeKind::CharacterLiteral:
            return "CharacterLiteral";
        case SyntaxNodeKind::NumberLiteral:
            return "NumberLiteral";
        case SyntaxNodeKind::Identifier:
            return "Identifier";
        case SyntaxNodeKind::Hash:
            return "Hash";
        case SyntaxNodeKind::LeftParen:
            return "LeftParen";
        case SyntaxNodeKind::RightParen:
            return "RightParen";
        case SyntaxNodeKind::LeftBracket:
            return "LeftBracket";
        case SyntaxNodeKind::RightBracket:
            return "RightBracket";
        case SyntaxNodeKind::LeftBrace:
            return "LeftBrace";
        case SyntaxNodeKind::RightBrace:
            return "RightBrace";
        case SyntaxNodeKind::Less:
            return "Less";
        case SyntaxNodeKind::Greater:
            return "Greater";
        case SyntaxNodeKind::LessEqual:
            return "LessEqual";
        case SyntaxNodeKind::GreaterEqual:
            return "GreaterEqual";
        case SyntaxNodeKind::EqualEqual:
            return "EqualEqual";
        case SyntaxNodeKind::BangEqual:
            return "BangEqual";
        case SyntaxNodeKind::Spaceship:
            return "Spaceship";
        case SyntaxNodeKind::Plus:
            return "Plus";
        case SyntaxNodeKind::Minus:
            return "Minus";
        case SyntaxNodeKind::Star:
            return "Star";
        case SyntaxNodeKind::Slash:
            return "Slash";
        case SyntaxNodeKind::Percent:
            return "Percent";
        case SyntaxNodeKind::Caret:
            return "Caret";
        case SyntaxNodeKind::Ampersand:
            return "Ampersand";
        case SyntaxNodeKind::Pipe:
            return "Pipe";
        case SyntaxNodeKind::Bang:
            return "Bang";
        case SyntaxNodeKind::Tilde:
            return "Tilde";
        case SyntaxNodeKind::Equal:
            return "Equal";
        case SyntaxNodeKind::PlusEqual:
            return "PlusEqual";
        case SyntaxNodeKind::MinusEqual:
            return "MinusEqual";
        case SyntaxNodeKind::StarEqual:
            return "StarEqual";
        case SyntaxNodeKind::SlashEqual:
            return "SlashEqual";
        case SyntaxNodeKind::PercentEqual:
            return "PercentEqual";
        case SyntaxNodeKind::CaretEqual:
            return "CaretEqual";
        case SyntaxNodeKind::AmpersandEqual:
            return "AmpersandEqual";
        case SyntaxNodeKind::PipeEqual:
            return "PipeEqual";
        case SyntaxNodeKind::LessLess:
            return "LessLess";
        case SyntaxNodeKind::GreaterGreater:
            return "GreaterGreater";
        case SyntaxNodeKind::LessLessEqual:
            return "LessLessEqual";
        case SyntaxNodeKind::GreaterGreaterEqual:
            return "GreaterGreaterEqual";
        case SyntaxNodeKind::AmpersandAmpersand:
            return "AmpersandAmpersand";
        case SyntaxNodeKind::PipePipe:
            return "PipePipe";
        case SyntaxNodeKind::PlusPlus:
            return "PlusPlus";
        case SyntaxNodeKind::MinusMinus:
            return "MinusMinus";
        case SyntaxNodeKind::Arrow:
            return "Arrow";
        case SyntaxNodeKind::Dot:
            return "Dot";
        case SyntaxNodeKind::ArrowStar:
            return "ArrowStar";
        case SyntaxNodeKind::DotStar:
            return "DotStar";
        case SyntaxNodeKind::ColonColon:
            return "ColonColon";
        case SyntaxNodeKind::Question:
            return "Question";
        case SyntaxNodeKind::Colon:
            return "Colon";
        case SyntaxNodeKind::Semicolon:
            return "Semicolon";
        case SyntaxNodeKind::Comma:
            return "Comma";
        case SyntaxNodeKind::Ellipsis:
            return "Ellipsis";
        case SyntaxNodeKind::KeywordAlignas:
            return "KeywordAlignas";
        case SyntaxNodeKind::KeywordAlignof:
            return "KeywordAlignof";
        case SyntaxNodeKind::KeywordAsm:
            return "KeywordAsm";
        case SyntaxNodeKind::KeywordAuto:
            return "KeywordAuto";
        case SyntaxNodeKind::KeywordBool:
            return "KeywordBool";
        case SyntaxNodeKind::KeywordBreak:
            return "KeywordBreak";
        case SyntaxNodeKind::KeywordCase:
            return "KeywordCase";
        case SyntaxNodeKind::KeywordCatch:
            return "KeywordCatch";
        case SyntaxNodeKind::KeywordChar:
            return "KeywordChar";
        case SyntaxNodeKind::KeywordChar16T:
            return "KeywordChar16T";
        case SyntaxNodeKind::KeywordChar32T:
            return "KeywordChar32T";
        case SyntaxNodeKind::KeywordClass:
            return "KeywordClass";
        case SyntaxNodeKind::KeywordConcept:
            return "KeywordConcept";
        case SyntaxNodeKind::KeywordConst:
            return "KeywordConst";
        case SyntaxNodeKind::KeywordConsteval:
            return "KeywordConsteval";
        case SyntaxNodeKind::KeywordConstexpr:
            return "KeywordConstexpr";
        case SyntaxNodeKind::KeywordConstinit:
            return "KeywordConstinit";
        case SyntaxNodeKind::KeywordConstCast:
            return "KeywordConstCast";
        case SyntaxNodeKind::KeywordContinue:
            return "KeywordContinue";
        case SyntaxNodeKind::KeywordDecltype:
            return "KeywordDecltype";
        case SyntaxNodeKind::KeywordDefault:
            return "KeywordDefault";
        case SyntaxNodeKind::KeywordDelete:
            return "KeywordDelete";
        case SyntaxNodeKind::KeywordDo:
            return "KeywordDo";
        case SyntaxNodeKind::KeywordDouble:
            return "KeywordDouble";
        case SyntaxNodeKind::KeywordDynamicCast:
            return "KeywordDynamicCast";
        case SyntaxNodeKind::KeywordElse:
            return "KeywordElse";
        case SyntaxNodeKind::KeywordEnum:
            return "KeywordEnum";
        case SyntaxNodeKind::KeywordExplicit:
            return "KeywordExplicit";
        case SyntaxNodeKind::KeywordExport:
            return "KeywordExport";
        case SyntaxNodeKind::KeywordExtern:
            return "KeywordExtern";
        case SyntaxNodeKind::KeywordFalse:
            return "KeywordFalse";
        case SyntaxNodeKind::KeywordFinal:
            return "KeywordFinal";
        case SyntaxNodeKind::KeywordFinally:
            return "KeywordFinally";
        case SyntaxNodeKind::KeywordFloat:
            return "KeywordFloat";
        case SyntaxNodeKind::KeywordFor:
            return "KeywordFor";
        case SyntaxNodeKind::KeywordFriend:
            return "KeywordFriend";
        case SyntaxNodeKind::KeywordGoto:
            return "KeywordGoto";
        case SyntaxNodeKind::KeywordIf:
            return "KeywordIf";
        case SyntaxNodeKind::KeywordInline:
            return "KeywordInline";
        case SyntaxNodeKind::KeywordInt:
            return "KeywordInt";
        case SyntaxNodeKind::KeywordLong:
            return "KeywordLong";
        case SyntaxNodeKind::KeywordMutable:
            return "KeywordMutable";
        case SyntaxNodeKind::KeywordNamespace:
            return "KeywordNamespace";
        case SyntaxNodeKind::KeywordNew:
            return "KeywordNew";
        case SyntaxNodeKind::KeywordNoexcept:
            return "KeywordNoexcept";
        case SyntaxNodeKind::KeywordNullptr:
            return "KeywordNullptr";
        case SyntaxNodeKind::KeywordOperator:
            return "KeywordOperator";
        case SyntaxNodeKind::KeywordOverride:
            return "KeywordOverride";
        case SyntaxNodeKind::KeywordPrivate:
            return "KeywordPrivate";
        case SyntaxNodeKind::KeywordProtected:
            return "KeywordProtected";
        case SyntaxNodeKind::KeywordPublic:
            return "KeywordPublic";
        case SyntaxNodeKind::KeywordRegister:
            return "KeywordRegister";
        case SyntaxNodeKind::KeywordReinterpretCast:
            return "KeywordReinterpretCast";
        case SyntaxNodeKind::KeywordRequires:
            return "KeywordRequires";
        case SyntaxNodeKind::KeywordReturn:
            return "KeywordReturn";
        case SyntaxNodeKind::KeywordShort:
            return "KeywordShort";
        case SyntaxNodeKind::KeywordSigned:
            return "KeywordSigned";
        case SyntaxNodeKind::KeywordSizeof:
            return "KeywordSizeof";
        case SyntaxNodeKind::KeywordStatic:
            return "KeywordStatic";
        case SyntaxNodeKind::KeywordStaticAssert:
            return "KeywordStaticAssert";
        case SyntaxNodeKind::KeywordStaticCast:
            return "KeywordStaticCast";
        case SyntaxNodeKind::KeywordStruct:
            return "KeywordStruct";
        case SyntaxNodeKind::KeywordSwitch:
            return "KeywordSwitch";
        case SyntaxNodeKind::KeywordTemplate:
            return "KeywordTemplate";
        case SyntaxNodeKind::KeywordThis:
            return "KeywordThis";
        case SyntaxNodeKind::KeywordThreadLocal:
            return "KeywordThreadLocal";
        case SyntaxNodeKind::KeywordThrow:
            return "KeywordThrow";
        case SyntaxNodeKind::KeywordTrue:
            return "KeywordTrue";
        case SyntaxNodeKind::KeywordTry:
            return "KeywordTry";
        case SyntaxNodeKind::KeywordTypedef:
            return "KeywordTypedef";
        case SyntaxNodeKind::KeywordTypeid:
            return "KeywordTypeid";
        case SyntaxNodeKind::KeywordTypename:
            return "KeywordTypename";
        case SyntaxNodeKind::KeywordUnion:
            return "KeywordUnion";
        case SyntaxNodeKind::KeywordUnsigned:
            return "KeywordUnsigned";
        case SyntaxNodeKind::KeywordUsing:
            return "KeywordUsing";
        case SyntaxNodeKind::KeywordVirtual:
            return "KeywordVirtual";
        case SyntaxNodeKind::KeywordVoid:
            return "KeywordVoid";
        case SyntaxNodeKind::KeywordVolatile:
            return "KeywordVolatile";
        case SyntaxNodeKind::KeywordWcharT:
            return "KeywordWcharT";
        case SyntaxNodeKind::KeywordWhile:
            return "KeywordWhile";
        case SyntaxNodeKind::KeywordCdecl:
            return "KeywordCdecl";
        case SyntaxNodeKind::KeywordDeclspec:
            return "KeywordDeclspec";
        case SyntaxNodeKind::KeywordCoAwait:
            return "KeywordCoAwait";
        case SyntaxNodeKind::KeywordCoReturn:
            return "KeywordCoReturn";
        case SyntaxNodeKind::KeywordCoYield:
            return "KeywordCoYield";
    }
    return "Unknown";
}

bool SyntaxNodeKindHasClass(SyntaxNodeKind kind, TokenClass tokenClass) {
    const size_t index = KindIndex(kind);
    if (index >= kSyntaxKindInfoByKind.size()) {
        return false;
    }
    return (kSyntaxKindInfoByKind[index].classes & Bit(tokenClass)) != 0;
}

bool LambdaBodyAllowsCompactSingleStatementForm(const SyntaxNode& node, SyntaxNodeKind parentKind) {
    if (node.kind != SyntaxNodeKind::CompoundStatement || parentKind != SyntaxNodeKind::LambdaExpression) {
        return false;
    }
    const SyntaxNode* statement = OnlyContentChild(node);
    // Compact lambda spacing and body-header choices must agree. A lone statement that owns a
    // compound block, such as if/switch/compound, needs normal block indentation for that subtree.
    return statement != nullptr && !NodeOrDescendantHasClass(*statement, TokenClass::CompoundBlock);
}
