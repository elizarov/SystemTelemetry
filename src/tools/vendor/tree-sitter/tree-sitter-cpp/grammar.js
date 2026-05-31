/**
 * @file C++ grammar for tree-sitter
 * @author Max Brunsfeld <maxbrunsfeld@gmail.com>
 * @author Amaan Qureshi <amaanq12@gmail.com>
 * @author John Drouhard <john@drouhard.dev>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const C = require('tree-sitter-c/grammar');
const MACRO_CONFIG = require('./macro_config');

function macroCategoryNames(category) {
  return MACRO_CONFIG.macro_categories[category] ?? [];
}

function macroChoice(category, fallback = []) {
  const names = [...fallback, ...macroCategoryNames(category)];
  return names.length === 1 ? names[0] : choice(...names);
}

function escapeRegex(value) {
  return value.replace(/[\\^$.*+?()[\]{}|]/g, '\\$&');
}

function regexUnion(parts) {
  const presentParts = parts.filter(Boolean);
  return presentParts.length === 0 ? '(?!)' : `(?:${presentParts.join('|')})`;
}

function macroNamePattern(category) {
  const names = macroCategoryNames(category);
  return names.length === 0 ? '' : regexUnion(names.map(escapeRegex));
}

function macroPrefixPattern(category) {
  const pattern = macroNamePattern(category);
  return pattern ? `${pattern}[A-Za-z0-9_]*` : '';
}

const PREC = Object.assign(C.PREC, {
  LAMBDA: 18,
  NEW: C.PREC.CALL + 1,
  STRUCTURED_BINDING: -1,
  THREE_WAY: C.PREC.RELATIONAL + 1,
});

const FOLD_OPERATORS = [
  '+', '-', '*', '/', '%',
  '^', '&', '|',
  '=', '<', '>',
  '<<', '>>',
  '+=', '-=', '*=', '/=', '%=', '^=', '&=', '|=',
  '>>=', '<<=',
  '==', '!=', '<=', '>=',
  '&&', '||',
  ',',
  '.*', '->*',
  'or', 'and', 'bitor', 'xor', 'bitand', 'not_eq',
];

const ASSIGNMENT_OPERATORS = [
  '=',
  '*=',
  '/=',
  '%=',
  '+=',
  '-=',
  '<<=',
  '>>=',
  '&=',
  '^=',
  '|=',
  'and_eq',
  'or_eq',
  'xor_eq',
];

const GENERIC_MACRO_FUNCTION_DEFINITION_PATTERN =
  /(?:TYPED_TEST_P|TYPED_TEST|TEST_F|TEST_P|TEST|MATCHER_P[0-9]*|MATCHER)/.source;
const CONDITIONAL_MACRO_FUNCTION_PATTERN = regexUnion([
  /(?:TYPED_TEST_P|TYPED_TEST|TEST_F|TEST_P|TEST)/.source,
  macroNamePattern('macro_function_definition'),
]);
const MACRO_FUNCTION_DEFINITION_PATTERN = regexUnion([
  GENERIC_MACRO_FUNCTION_DEFINITION_PATTERN,
  macroNamePattern('macro_function_definition'),
]);
const RAW_MACRO_FUNCTION_PREFIX_PATTERN = regexUnion([
  macroPrefixPattern('raw_macro_function_prefix'),
]);
const FUNCTION_PREFIX_MACRO_PATTERN = regexUnion([
  /[A-Z][A-Z0-9_]*_(?:FUNC|ATTR|SIMD)/.source,
  /ATTRIBUTE_[A-Z0-9_]+/.source,
  macroNamePattern('function_prefix'),
]);
const NO_THROW_MACRO_PATTERN = regexUnion([
  /EXPECT_NO_THROW/.source,
  macroNamePattern('no_throw_macro'),
]);
const NAME_MACRO_CALL_PATTERN = regexUnion([
  macroNamePattern('name_macro_call'),
]);
const NAMESPACE_ALIAS_MACRO_PATTERN = regexUnion([
  macroNamePattern('namespace_alias_macro'),
]);

module.exports = grammar(C, {
  name: 'cpp',

  externals: $ => [
    $.raw_string_delimiter,
    $.raw_string_content,
  ],

  conflicts: $ => [
    // C
    [$.type_specifier, $._declarator],
    [$.type_specifier, $._type_declarator],
    [$.type_specifier, $._declarator, $._type_declarator],
    [$.type_specifier, $.expression],
    [$.expression, $.class_specifier],
    [$.type_specifier, $._class_name],
    [$.sized_type_specifier],
    [$.attributed_statement],
    [$._declaration_modifiers, $.attributed_statement],
    [$._top_level_item, $._top_level_statement],
    [$._block_item, $.statement],
    [$.type_qualifier, $.extension_expression],

    // C++
    [$.template_function, $.template_type],
    [$._type_declarator, $.template_type],
    [$._type_declarator, $.template_type, $.template_function],
    [$.pointer_type_declarator, $.member_pointer_type_declarator],
    [$.template_function, $.template_type, $.expression],
    [$.template_function, $.template_type, $.qualified_identifier],
    [$.template_type, $.qualified_type_identifier],
    [$.qualified_type_identifier, $.qualified_identifier],
    [$.comma_expression, $.initializer_list],
    [$.expression, $._template_argument_expression],
    [$.type_specifier, $._template_argument_expression],
    [$.type_specifier, $.expression, $._template_argument_expression],
    [$.expression, $._declarator],
    [$.expression, $.structured_binding_declarator],
    [$.expression, $._declarator, $.type_specifier],
    [$._declarator, $._type_declarator],
    [$.expression, $.identifier_parameter_pack_expansion],
    [$.expression, $._lambda_capture_identifier],
    [$.expression, $._lambda_capture],
    [$.expression, $.structured_binding_declarator, $._lambda_capture_identifier],
    [$.structured_binding_declarator, $._lambda_capture_identifier],
    [$.parameter_list, $.argument_list],
    [$.type_specifier, $.call_expression],
    [$._declaration_specifiers, $._constructor_specifiers],
    [$._binary_fold_operator, $._fold_operator],
    [$._function_declarator_seq],
    [$.type_specifier, $.sized_type_specifier],
    [$.type_specifier, $.sized_type_specifier, $.expression],
    [$._type_declarator, $.sized_type_specifier],
    [$.type_specifier, $.expression, $.concatenated_string],
    [$.expression, $.concatenated_string],
    [$._declaration_specifiers, $.macro_replacement_list],
    [$._string, $.concatenated_string],
    [$.type_specifier, $.macro_template_declaration],
    [$.template_type, $.template_method],
    [$.enum_specifier, $.macro_enum_specifier],
    [$.enumerator_list, $.macro_enumerator_list],
    [$.enumerator, $.expression],
    [$.expression_statement, $.macro_expression_item],
    [$.comma_expression, $.macro_expression_item],
    [$.initializer_pair, $.comma_expression],
    [$.expression_statement, $._for_statement_body],
    [$.init_statement, $._for_statement_body],
    [$.field_expression, $.template_method, $.template_type],
    [$.qualified_field_identifier, $.template_method, $.template_type],
    [$.qualified_field_identifier, $.template_method],
    [$.template_type, $.template_method, $.dependent_field_identifier],
    [$._function_declaration_declarator, $._function_attributes_start],
    [$.argument_list, $.preproc_prefixed_expression],
  ],

  inline: ($, original) => original.concat([
    $._namespace_identifier,
  ]),

  precedences: $ => [
    [$.argument_list, $.type_qualifier],
    [$._expression_not_binary, $._class_name],
  ],

  rules: {
    _top_level_item: ($, original) => choice(
      $.conditional_macro_function_definition,
      $.raw_macro_function_definition,
      $.raw_macro_definition,
      $.deleted_operator_declaration,
      $.preproc_hashhash_function_def,
      $.preproc_nested_define_ifdef,
      $.preproc_define_elif_chain,
      $.preproc_define_namespace_if,
      ...original.members.filter((member) => member.content?.name != '_old_style_function_definition'),
      $.preproc_using,
      $.preproc_define_ifdef,
      $.conditional_extern_c_open,
      $.conditional_extern_c_close,
      $.standalone_attribute_preproc_if,
      $.namespace_definition,
      $.concept_definition,
      $.namespace_alias_definition,
      $.using_declaration,
      $.function_pointer_alias_declaration,
      $.deduction_guide_declaration,
      $.alias_declaration,
      $.macro_namespace_boundary,
      $.benchmark_macro_function_definition,
      $.top_level_macro_call_statement,
      $.top_level_operator_macro_call,
      $.name_macro_call,
      $.static_assert_declaration,
      $.macro_function_definition,
      $.template_declaration,
      $.template_instantiation,
      alias($.constructor_or_destructor_definition, $.function_definition),
      alias($.operator_cast_definition, $.function_definition),
      alias($.operator_cast_declaration, $.declaration),
    ),

    _block_item: ($, original) => choice(
      $.conditional_macro_function_definition,
      $.raw_macro_function_definition,
      $.raw_macro_definition,
      $.deleted_operator_declaration,
      $.preproc_hashhash_function_def,
      $.preproc_value_declaration,
      $.preproc_nested_define_ifdef,
      $.preproc_define_elif_chain,
      $.preproc_define_namespace_if,
      ...original.members.filter((member) => member.content?.name != '_old_style_function_definition'),
      $.preproc_using,
      $.preproc_define_ifdef,
      $.conditional_extern_c_open,
      $.conditional_extern_c_close,
      $.standalone_attribute_preproc_if,
      $.standalone_qualifier_preproc_if,
      $.namespace_definition,
      $.concept_definition,
      $.namespace_alias_definition,
      $.using_declaration,
      $.function_pointer_alias_declaration,
      $.deduction_guide_declaration,
      $.alias_declaration,
      $.macro_namespace_boundary,
      $.benchmark_macro_function_definition,
      $.top_level_macro_call_statement,
      $.top_level_operator_macro_call,
      $.name_macro_call,
      $.static_assert_declaration,
      $.macro_function_definition,
      $.template_declaration,
      $.template_instantiation,
      alias($.constructor_or_destructor_definition, $.function_definition),
      alias($.operator_cast_definition, $.function_definition),
      alias($.operator_cast_declaration, $.declaration),
    ),

    // Types

    placeholder_type_specifier: $ => prec(1, seq(
      field('constraint', optional($.type_specifier)),
      choice($.auto, alias($.decltype_auto, $.decltype)),
    )),

    auto: _ => 'auto',
    decltype_auto: $ => seq(
      'decltype',
      '(',
      $.auto,
      ')',
    ),
    decltype: $ => seq(
      'decltype',
      '(',
      choice($.expression, $.comma_expression),
      ')',
    ),

    type_specifier: $ => choice(
      $.struct_specifier,
      $.union_specifier,
      $.enum_specifier,
      $.class_specifier,
      $.sized_type_specifier,
      $.primitive_type,
      $.locked_channel_proxy_type,
      $.map_proxy_type,
      $.openssl_stack_type,
      $.template_type,
      $.dependent_type,
      $.placeholder_type_specifier,
      $.decltype,
      prec.right(choice(
        alias($.qualified_type_identifier, $.qualified_identifier),
        $._type_identifier,
      )),
    ),

    type_qualifier: (_, original) => choice(
      original,
      'mutable',
      'constinit',
      'consteval',
    ),

    preproc_def: ($, original) => withStructuredMacroReplacementList($, original),
    preproc_function_def: ($, original) => withStructuredMacroReplacementList($, original),

    raw_macro_function_definition: _ => token(prec(
      2,
      new RegExp(String.raw`#[ \t]*define[ \t]+${RAW_MACRO_FUNCTION_PREFIX_PATTERN}\([^\n]*\)(?:[^\n]*\\\r?\n)*[^\n]*`),
    )),

    raw_macro_definition: _ => token(prec(
      2,
      /#[ \t]*define[ \t]+[A-Za-z_]\w*[ \t]+__attribute__[ \t]*\(\([^\n]*\)\)[^\n]*/,
    )),

    macro_replacement_list: $ => repeat1(choice(
      alias($.macro_template_declaration, $.template_declaration),
      alias($.macro_enum_declaration, $.declaration),
      $.function_definition,
      $.declaration,
      $.static_assert_declaration,
      $.type_specifier,
      alias($.macro_do_statement, $.do_statement),
      $.try_statement,
      $.macro_arrow_chain,
      $.expression_statement,
      $.macro_expression_item,
    )),

    macro_do_statement: $ => prec(1, seq(
      'do',
      field('body', $.compound_statement),
      'while',
      field('condition', $.parenthesized_expression),
    )),

    macro_expression_item: $ => seq(
      $.expression,
      optional(','),
    ),

    macro_arrow_chain: _ => token(prec(1, /(?:->[A-Za-z_]\w*\([^()\n]*\))+/)),

    macro_namespace_boundary: _ => token(prec(1, /[A-Z][A-Z0-9_]*_NAMESPACE_(BEGIN|END)/)),

    macro_function_definition: $ => prec(1, seq(
      field('name', alias($._macro_function_definition_identifier, $.identifier)),
      field('arguments', $.argument_list),
      field('body', $.compound_statement),
    )),

    benchmark_macro_function_definition: $ => prec(1, seq(
      field('function', alias($._benchmark_definition_macro, $.identifier)),
      field('arguments', $.argument_list),
      field('declarator', $.parameter_list),
      field('body', $.compound_statement),
    )),

    _benchmark_definition_macro: _ => token(prec(1, /BENCHMARK_DEFINE(?:_TEMPLATE)?_F/)),

    top_level_macro_call_statement: _ => token(prec(1, /BENCHMARK[A-Z0-9_]*\([^()\n;]*\)(?:[ \t]+[A-Z][A-Z0-9]*(?:_[A-Z0-9]+)+)?(?:[ \t]*->[^\n;]*)*(?:\r?\n[ \t]*->[^\n;]*)*;/)),

    top_level_operator_macro_call: _ => token(prec(1, /[A-Z][A-Z0-9_]*\((?:==|!=|<=|>=|<=>|<|>)\)/)),

    name_macro_call: _ => token(prec(1, new RegExp(`${NAME_MACRO_CALL_PATTERN}\\([A-Za-z_]\\w*\\);?`))),

    conditional_macro_function_definition: $ => prec(1, seq(
      field('declarator', $.conditional_macro_function_header),
      field('body', alias($.conditional_macro_function_body, $.compound_statement)),
    )),

    conditional_macro_function_body: $ => seq(
      repeat($._block_item),
      '}',
    ),

    conditional_macro_function_header: _ => token(prec(
      1,
      new RegExp(String.raw`#[ \t]*if[^\n]*\r?\n(?:[ \t]*\/\/[^\n]*\r?\n)*[ \t]*${CONDITIONAL_MACRO_FUNCTION_PATTERN}[^\n]*\{[^\n]*\r?\n#[ \t]*else[^\n]*\r?\n(?:[ \t]*\/\/[^\n]*\r?\n)*[ \t]*${CONDITIONAL_MACRO_FUNCTION_PATTERN}[^\n]*\{[^\n]*\r?\n#[ \t]*endif`),
    )),

    _macro_function_definition_identifier: _ => token(prec(
      1,
      new RegExp(MACRO_FUNCTION_DEFINITION_PATTERN),
    )),

    macro_enum_declaration: $ => seq(
      alias($.macro_enum_specifier, $.enum_specifier),
      ';',
    ),

    macro_enum_specifier: $ => prec.right(seq(
      'enum',
      optional(choice('class', 'struct')),
      choice(
        seq(
          field('name', $._class_name),
          optional($._enum_base_clause),
          optional(field('body', alias($.macro_enumerator_list, $.enumerator_list))),
        ),
        field('body', alias($.macro_enumerator_list, $.enumerator_list)),
      ),
      optional($.attribute_specifier),
    )),

    macro_enumerator_list: $ => seq(
      '{',
      commaSep(choice($.enumerator, $.expression)),
      optional(','),
      '}',
    ),

    macro_template_declaration: $ => seq(
      'template',
      field('parameters', $.template_parameter_list),
      optional($.requires_clause),
      choice(
        $._empty_declaration,
        $.alias_declaration,
        $.declaration,
        $.template_declaration,
        $.function_definition,
        $.concept_definition,
        $.friend_declaration,
        $.class_specifier,
        $.struct_specifier,
        alias($.constructor_or_destructor_declaration, $.declaration),
        alias($.constructor_or_destructor_definition, $.function_definition),
        alias($.operator_cast_declaration, $.declaration),
        alias($.operator_cast_definition, $.function_definition),
      ),
    ),

    preproc_using: _ => token(prec(1, /#[ \t]*using[^\n]*/)),

    preproc_hashhash_function_def: _ => token(prec(
      2,
      /#[ \t]*define[^\n]*(?:\\\r?\n[^\n]*)*##[^\n]*(?:\\\r?\n[^\n]*)*/,
    )),

    preproc_define_ifdef: _ => token(prec(
      1,
      /#[ \t]*(?:ifdef|ifndef|if)[^\n]*\r?\n(?:[ \t]*\/\/[^\n]*\r?\n)*(?:#[ \t]*define[^\n]*\r?\n)+(?:#[ \t]*else[^\n]*\r?\n(?:[ \t]*\/\/[^\n]*\r?\n)*(?:#[ \t]*define[^\n]*\r?\n)+)?#[ \t]*endif/,
    )),

    preproc_nested_define_ifdef: _ => token(prec(
      1,
      /#[ \t]*ifdef[^\n]*\r?\n(?:[ \t]*\r?\n)*#[ \t]*if[^\n]*\r?\n(?:#[ \t]*define[^\n]*\r?\n)+(?:#[ \t]*elif[^\n]*\r?\n(?:[ \t]*\/\/[^\n]*\r?\n)*(?:#[ \t]*define[^\n]*\r?\n)+)+#[ \t]*endif\r?\n(?:[ \t]*\r?\n)*#[ \t]*endif/,
    )),

    preproc_define_elif_chain: _ => token(prec(
      1,
      /#[ \t]*ifdef[^\n]*\r?\n(?:#[ \t]*define[^\n]*\r?\n)+(?:#[ \t]*elif[^\n]*\r?\n(?:#[ \t]*define[^\n]*\r?\n)+)+#[ \t]*endif/,
    )),

    preproc_define_namespace_if: _ => token(prec(
      1,
      /#[ \t]*if[^\n]*\r?\n#[ \t]*define[^\n]*\r?\n(?:[^\r\n]*\r?\n)*?namespace[^\n]*\{\r?\n(?:[^\r\n]*\r?\n)*?\}[^\n]*\r?\n#[ \t]*else[^\n]*\r?\n#[ \t]*define[^\n]*\r?\n#[ \t]*endif/,
    )),

    conditional_extern_c_open: _ => token(prec(1, /#[ \t]*(?:ifdef|ifndef)[^\n]*\r?\nextern[ \t]+"C"[ \t]*\{\r?\n#[ \t]*endif/)),

    conditional_extern_c_close: _ => token(prec(1, /#[ \t]*(?:ifdef|ifndef)[^\n]*\r?\n\}\r?\n#[ \t]*endif/)),

    _preproc_expression: ($, original) => choice(
      original,
      $.string_literal,
      $.raw_string_literal,
      $.system_lib_string,
    ),

    type_descriptor: (_, original) => prec.right(original),

    locked_channel_proxy_type: _ => token(prec(1, /LockedChannelProxy<[A-Za-z_:][A-Za-z0-9_:]*>/)),

    map_proxy_type: _ => token(prec(1, /MapProxy<[A-Za-z_]\w*>/)),

    openssl_stack_type: _ => token(prec(1, /STACK_OF\([A-Za-z_]\w*\)/)),

    // When used in a trailing return type, these specifiers can now occur immediately before
    // a compound statement. This introduces a shift/reduce conflict that needs to be resolved
    // with an associativity.
    _class_declaration: $ => seq(
      repeat(choice($.attribute_specifier, $.alignas_qualifier)),
      optional($.ms_declspec_modifier),
      repeat($.attribute_declaration),
      $._class_declaration_item,
    ),
    _class_declaration_item: $ => prec.right(seq(
      choice(
        field('name', $._class_name),
        seq(
          optional(field('name', $._class_name)),
          repeat($.virtual_specifier),
          optional($.base_class_clause),
          field('body', $.field_declaration_list),
        ),
      ),
      optional($.attribute_specifier),
    )),

    class_specifier: $ => seq(
      optional('ref'),
      'class',
      $._class_declaration,
    ),

    union_specifier: $ => seq(
      'union',
      $._class_declaration,
    ),

    struct_specifier: $ => seq(
      'struct',
      $._class_declaration,
    ),

    _class_name: $ => prec.right(choice(
      $._type_identifier,
      $.template_type,
      alias($.qualified_type_identifier, $.qualified_identifier),
    )),

    function_definition: ($, original) => ({
      ...original,
      members: original.members.map(
        (e) => e.name !== 'body' ?
          e :
          field('body', choice(e.content, $.try_statement))),
    }),

    declaration: $ => seq(
      $._declaration_specifiers,
      commaSep1(field('declarator', choice(
        seq(
          // C uses _declaration_declarator here for some nice macro parsing in function declarators,
          // but this causes a world of pain for C++ so we'll just stick to the normal _declarator here.
          optional($.ms_call_modifier),
          $._declarator,
          optional($.gnu_asm_expression),
        ),
        $.init_declarator,
      ))),
      optional($.declaration_suffix_preproc_ifdef),
      ';',
    ),

    virtual_specifier: _ => choice(
      'final', // the only legal value here for classes
      'override', // legal for functions in addition to final, plus permutations.
      'sealed',
      'abstract',
    ),

    _declaration_modifiers: ($, original) => choice(
      original,
      $.function_prefix_macro,
      'virtual',
    ),

    function_prefix_macro: _ => token(prec(
      1,
      new RegExp(FUNCTION_PREFIX_MACRO_PATTERN),
    )),

    ms_call_modifier: ($, original) => choice(
      original,
      $.calling_convention_macro,
    ),

    calling_convention_macro: _ => macroChoice('calling_convention'),

    explicit_function_specifier: $ => choice(
      'explicit',
      prec(PREC.CALL, seq(
        'explicit',
        '(',
        $.expression,
        ')',
      )),
    ),

    base_class_clause: $ => seq(
      ':',
      commaSep1(seq(
        repeat($.attribute_declaration),
        optional(choice(
          $.access_specifier,
          seq($.access_specifier, optional('virtual')),
          seq('virtual', optional($.access_specifier)),
        )),
        choice($._class_name, $.decltype),
        optional('...'),
      )),
    ),

    enum_specifier: $ => prec.right(seq(
      'enum',
      optional(choice('class', 'struct')),
      repeat($.attribute_declaration),
      choice(
        seq(
          field('name', $._class_name),
          optional($._enum_base_clause),
          optional(field('body', $.enumerator_list)),
        ),
        field('body', $.enumerator_list),
      ),
      optional($.attribute_specifier),
    )),

    _enum_base_clause: $ => prec.left(seq(
      ':',
      field('base', choice(
        alias($.qualified_type_identifier, $.qualified_identifier),
        $._type_identifier,
        $.primitive_type,
        $.sized_type_specifier,
      )),
    )),

    // The `auto` storage class is removed in C++0x in order to allow for the `auto` type.
    storage_class_specifier: (_, original) => choice(
      ...original.members.filter((member) => member.value !== 'auto'),
      'thread_local',
    ),

    dependent_type: $ => prec.dynamic(-1, prec.right(seq(
      'typename',
      $.type_specifier,
    ))),

    // Declarations

    template_declaration: $ => seq(
      'template',
      field('parameters', $.template_parameter_list),
      optional($.requires_clause),
      choice(
        $._empty_declaration,
        $.alias_declaration,
        $.declaration,
        $.template_declaration,
        $.function_definition,
        $.concept_definition,
        $.friend_declaration,
        alias($.constructor_or_destructor_declaration, $.declaration),
        alias($.constructor_or_destructor_definition, $.function_definition),
        alias($.operator_cast_declaration, $.declaration),
        alias($.operator_cast_definition, $.function_definition),
      ),
    ),

    template_instantiation: $ => prec(1, seq(
      optional('extern'),
      'template',
      choice(
        choice($.class_specifier, $.struct_specifier, $.union_specifier),
        seq(
          optional($._declaration_specifiers),
          field('declarator', $._declarator),
        ),
      ),
      ';',
    )),

    declaration_suffix_preproc_ifdef: $ => prec(1, seq(
      choice(preprocessor('ifdef'), preprocessor('ifndef')),
      field('name', $.identifier),
      repeat1(choice(
        $.identifier,
        $.function_suffix_macro,
        $.attribute_specifier,
      )),
      preprocessor('endif'),
    )),

    template_parameter_list: $ => seq(
      '<',
      commaSep(choice(
        $.parameter_declaration,
        $.optional_parameter_declaration,
        $.type_parameter_declaration,
        $.variadic_parameter_declaration,
        $.variadic_type_parameter_declaration,
        $.optional_type_parameter_declaration,
        $.template_template_parameter_declaration,
      )),
      alias(token(prec(1, '>')), '>'),
    ),

    type_parameter_declaration: $ => prec(1, seq(
      choice('typename', 'class'),
      optional($._type_identifier),
    )),

    variadic_type_parameter_declaration: $ => prec(1, seq(
      choice('typename', 'class'),
      '...',
      optional($._type_identifier),
    )),

    optional_type_parameter_declaration: $ => seq(
      choice('typename', 'class'),
      optional(field('name', $._type_identifier)),
      '=',
      field('default_type', choice($.type_descriptor, $.function_pointer_type_descriptor)),
    ),

    function_pointer_type_descriptor: _ => token(prec(
      1,
      /[A-Za-z_:][A-Za-z0-9_:<>]*[ \t]+\((?:[A-Za-z_:][A-Za-z0-9_:<>]*::)?\*\)\([^>\n]*\)/,
    )),

    template_template_parameter_declaration: $ => seq(
      'template',
      field('parameters', $.template_parameter_list),
      choice(
        $.type_parameter_declaration,
        $.variadic_type_parameter_declaration,
        $.optional_type_parameter_declaration,
      ),
    ),

    parameter_list: $ => seq(
      '(',
      commaSep(choice(
        $.parameter_declaration,
        $.optional_parameter_declaration,
        $.variadic_parameter_declaration,
        '...',
      )),
      ')',
    ),

    optional_parameter_declaration: $ => seq(
      $._declaration_specifiers,
      field('declarator', optional(choice($._declarator, $.abstract_reference_declarator))),
      '=',
      field('default_value', choice($.expression, $.initializer_list)),
    ),

    variadic_parameter_declaration: $ => seq(
      $._declaration_specifiers,
      field('declarator', choice(
        $.variadic_declarator,
        alias($.variadic_reference_declarator, $.reference_declarator),
      )),
    ),

    variadic_declarator: $ => seq(
      '...',
      optional($.identifier),
    ),

    variadic_reference_declarator: $ => seq(
      choice('&&', '&'),
      $.variadic_declarator,
    ),

    init_declarator: ($, original) => choice(
      original,
      seq(
        field('declarator', $._declarator),
        repeat1($.attribute_specifier),
        '=',
        field('value', choice($.initializer_list, $.expression)),
      ),
      seq(
        field('declarator', $._declarator),
        field('value', choice(
          $.argument_list,
          $.initializer_list,
        )),
      ),
      seq(
        field('declarator', $.identifier),
        field('value', $.macro_initializer),
      ),
    ),

    preproc_value_declaration: $ => prec(1, seq(
      $._declaration_specifiers,
      field('declarator', $._declarator),
      '=',
      field('value', $.preproc_semicolon_initializer),
    )),

    preproc_semicolon_initializer: _ => token(prec(
      1,
      /#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n[ \t]*(?:[^#\r\n][^\n;]*;[ \t]*(?:\/\/[^\n]*)?\r?\n)+(?:#[ \t]*else[^\n]*\r?\n[ \t]*(?:[^#\r\n][^\n;]*;[ \t]*(?:\/\/[^\n]*)?\r?\n)+)?#[ \t]*endif/,
    )),

    macro_initializer: _ => token(prec(1, /[A-Z][A-Z0-9_]*(?:\([^\n]*\))?/)),

    operator_cast: $ => prec.right(1, seq(
      'operator',
      $._declaration_specifiers,
      field('declarator', $._abstract_declarator),
    )),

    // Avoid ambiguity between compound statement and initializer list in a construct like:
    //   A b {};
    compound_statement: (_, original) => prec(-1, original),

    field_initializer_list: $ => seq(
      ':',
      $.field_initializer,
      repeat(choice(seq(',', $.field_initializer), $.preproc_field_initializer_fragment)),
    ),

    preproc_field_initializer_fragment: _ => token(prec(
      1,
      /#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n[ \t]*,[ \t]*\r?\n[ \t]*[A-Za-z_]\w*\([^\r\n]*\)\r?\n#[ \t]*endif/,
    )),

    field_initializer: $ => prec(1, seq(
      choice(
        $._field_identifier,
        $.template_method,
        alias($.qualified_field_identifier, $.qualified_identifier),
      ),
      choice($.initializer_list, $.argument_list),
      optional('...'),
    )),

    _field_declaration_list_item: ($, original) => choice(
      $.standalone_attribute_preproc_if,
      $.standalone_qualifier_preproc_if,
      original,
      $.deleted_operator_cast_declaration,
      $.attributed_friend_operator_declaration,
      $.using_operator_pack_declaration,
      $.mock_method_declaration,
      $.macro_field_declaration,
      $.macro_initialized_field_declaration,
      $.template_declaration,
      alias($.inline_method_definition, $.function_definition),
      alias($.constructor_or_destructor_definition, $.function_definition),
      alias($.constructor_or_destructor_declaration, $.declaration),
      alias($.operator_cast_definition, $.function_definition),
      alias($.operator_cast_declaration, $.declaration),
      $.friend_declaration,
      seq($.access_specifier, ':'),
      $.function_pointer_alias_declaration,
      $.alias_declaration,
      $.using_declaration,
      $.type_definition,
      $.static_assert_declaration,
      ';',
    ),

    standalone_qualifier_preproc_if: _ => token(prec(
      1,
      /#[ \t]*if[^\n]*\r?\n(?:[ \t]*\/\/[^\n]*\r?\n)*[ \t]*(?:const|constexpr|consteval)[^\n]*\r?\n(?:#[ \t]*else\r?\n(?:[ \t]*\/\/[^\n]*\r?\n)*[ \t]*(?:const|constexpr|consteval)[^\n]*\r?\n)?#[ \t]*endif/,
    )),

    standalone_attribute_preproc_if: _ => token(prec(
      1,
      /#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n(?:[ \t]*\/\/[^\n]*\r?\n)*[ \t]*(?:\[\[[^\n]*\]\][ \t]*)+\r?\n(?:#[ \t]*else[^\n]*\r?\n(?:[ \t]*\/\/[^\n]*\r?\n)*[ \t]*(?:\[\[[^\n]*\]\][ \t]*)+\r?\n)?#[ \t]*endif/,
    )),

    deleted_operator_cast_declaration: _ => token(prec(
      1,
      /(?:(?:constexpr|consteval)[ \t]+)?(?:\/\*[^*]*\*\/[ \t]*)?operator[ \t]+[A-Za-z_:][A-Za-z0-9_:<>]*\(\)[^\n;]*=[ \t]*delete;/,
    )),

    attributed_friend_operator_declaration: _ => token(prec(
      1,
      /\[\[[^\]\n]+\]\][ \t]+friend[ \t]+bool[ \t]+operator(?:==|!=)\([^\n]*\)[ \t]*\{[^\n]*\}/,
    )),

    using_operator_pack_declaration: _ => token(prec(1, /using[ \t]+[A-Za-z_]\w*::operator\(\)\.\.\.;/)),

    mock_method_declaration: $ => seq(
      field('function', alias('MOCK_METHOD', $.identifier)),
      '(',
      field('return_type', $._declaration_specifiers),
      ',',
      field('name', $.identifier),
      ',',
      field('parameters', $.mock_method_parameter_list),
      ',',
      field('qualifiers', $.mock_method_qualifier_list),
      ')',
      ';',
    ),

    mock_method_parameter_list: _ => token(prec(1, /\((?:[^()\r\n]|\r?\n[ \t]*|\([^()\r\n]*\))*\)/)),

    mock_method_qualifier_list: $ => seq(
      '(',
      commaSep(choice(
        $.type_qualifier,
        $.virtual_specifier,
        $.identifier,
      )),
      ')',
    ),

    macro_field_declaration: _ => token(prec(1, /[A-Z][A-Z0-9_]*\([^\n]*\);/)),

    macro_initialized_field_declaration: $ => seq(
      $._declaration_specifiers,
      optional($.ms_call_modifier),
      field('declarator', $._field_identifier),
      field('default_value', $.macro_initializer),
      ';',
    ),

    field_declaration: $ => seq(
      $._declaration_specifiers,
      optional($.ms_call_modifier),
      commaSep(seq(
        field('declarator', $._field_declarator),
        optional(choice(
          $.bitfield_clause,
          field('default_value', $.initializer_list),
          seq('=', field('default_value', choice($.expression, $.initializer_list))),
        )),
      )),
      optional($.attribute_specifier),
      ';',
    ),

    inline_method_definition: $ => seq(
      $._declaration_specifiers,
      optional($.ms_call_modifier),
      field('declarator', $._field_declarator),
      choice(
        field('body', choice($.compound_statement, $.try_statement)),
        $.default_method_clause,
        $.delete_method_clause,
        $.pure_virtual_clause,
      ),
    ),

    _constructor_specifiers: $ => choice(
      $._declaration_modifiers,
      $.explicit_function_specifier,
    ),

    operator_cast_definition: $ => seq(
      repeat($._constructor_specifiers),
      field('declarator', choice(
        $.operator_cast,
        alias($.qualified_operator_cast_identifier, $.qualified_identifier),
      )),
      field('body', choice($.compound_statement, $.try_statement)),
    ),

    operator_cast_declaration: $ => prec(1, seq(
      repeat($._constructor_specifiers),
      field('declarator', choice(
        $.operator_cast,
        alias($.qualified_operator_cast_identifier, $.qualified_identifier),
      )),
      optional(seq('=', field('default_value', $.expression))),
      ';',
    )),

    constructor_try_statement: $ => seq(
      'try',
      optional($.field_initializer_list),
      field('body', $.compound_statement),
      repeat1($.catch_clause),
    ),

    constructor_or_destructor_definition: $ => seq(
      repeat($._constructor_specifiers),
      field('declarator', $.function_declarator),
      choice(
        seq(
          optional($.field_initializer_list),
          field('body', $.compound_statement),
        ),
        alias($.constructor_try_statement, $.try_statement),
        $.default_method_clause,
        $.delete_method_clause,
        $.pure_virtual_clause,
      ),
    ),

    constructor_or_destructor_declaration: $ => seq(
      repeat($._constructor_specifiers),
      field('declarator', $.function_declarator),
      ';',
    ),

    default_method_clause: _ => seq('=', 'default', ';'),
    delete_method_clause: _ => seq('=', 'delete', ';'),
    pure_virtual_clause: _ => seq('=', /0/, ';'),

    friend_declaration: $ => seq(
      optional('constexpr'),
      'friend',
      choice(
        $.declaration,
        $.function_definition,
        seq(
          optional(choice(
            'class',
            'struct',
            'union',
          )),
          $._class_name, ';',
        ),
      ),
    ),

    access_specifier: _ => choice(
      'public',
      'private',
      'protected',
    ),

    _declarator: ($, original) => choice(
      original,
      $.reference_declarator,
      $.handle_declarator,
      $.member_pointer_declarator,
      $.qualified_constructor_identifier,
      $.qualified_identifier,
      $.template_function,
      $.operator_name,
      $.destructor_name,
      $.structured_binding_declarator,
    ),

    _field_declarator: ($, original) => choice(
      original,
      alias($.reference_field_declarator, $.reference_declarator),
      alias($.handle_field_declarator, $.handle_declarator),
      alias($.member_pointer_field_declarator, $.member_pointer_declarator),
      $.template_method,
      $.operator_name,
    ),

    _type_declarator: ($, original) => choice(
      original,
      alias($.reference_type_declarator, $.reference_declarator),
      alias($.handle_type_declarator, $.handle_declarator),
      alias($.member_pointer_type_declarator, $.member_pointer_declarator),
    ),

    _abstract_declarator: ($, original) => choice(
      original,
      $.abstract_reference_declarator,
      $.abstract_handle_declarator,
      $.abstract_member_pointer_declarator,
    ),

    reference_declarator: $ => prec.dynamic(1, prec.right(seq(choice('&', '&&', '%'), $._declarator))),
    reference_field_declarator: $ => prec.dynamic(1, prec.right(seq(choice('&', '&&', '%'), $._field_declarator))),
    reference_type_declarator: $ => prec.dynamic(1, prec.right(seq(choice('&', '&&', '%'), $._type_declarator))),
    abstract_reference_declarator: $ => prec.right(seq(choice('&', '&&', '%'), optional($._abstract_declarator))),

    pointer_declarator: $ => prec.dynamic(1, prec.right(seq(
      optional($.ms_based_modifier),
      '*',
      repeat(choice($.ms_pointer_modifier, $.type_qualifier, $.ms_call_modifier)),
      field('declarator', $._declarator),
    ))),
    pointer_field_declarator: $ => prec.dynamic(1, prec.right(seq(
      optional($.ms_based_modifier),
      '*',
      repeat(choice($.ms_pointer_modifier, $.type_qualifier, $.ms_call_modifier)),
      field('declarator', $._field_declarator),
    ))),
    pointer_type_declarator: $ => prec.dynamic(1, prec.right(seq(
      optional($.ms_based_modifier),
      '*',
      repeat(choice($.ms_pointer_modifier, $.type_qualifier, $.ms_call_modifier)),
      field('declarator', $._type_declarator),
    ))),
    abstract_pointer_declarator: $ => prec.dynamic(1, prec.right(seq(
      '*',
      repeat(choice($.ms_pointer_modifier, $.type_qualifier, $.ms_call_modifier)),
      field('declarator', optional($._abstract_declarator)),
    ))),

    handle_declarator: $ => prec.dynamic(1, prec.right(seq('^', $._declarator))),
    handle_field_declarator: $ => prec.dynamic(1, prec.right(seq('^', $._field_declarator))),
    handle_type_declarator: $ => prec.dynamic(1, prec.right(seq('^', $._type_declarator))),
    abstract_handle_declarator: $ => prec.right(seq('^', optional($._abstract_declarator))),

    abstract_member_pointer_declarator: $ => prec.dynamic(1, prec.right(seq(
      field('scope', $._scope_resolution),
      '*',
      field('declarator', optional($._abstract_declarator)),
    ))),

    member_pointer_declarator: $ => prec.dynamic(1, prec.right(seq(
      field('scope', $._scope_resolution),
      '*',
      field('declarator', $._declarator),
    ))),
    member_pointer_field_declarator: $ => prec.dynamic(1, prec.right(seq(
      field('scope', $._scope_resolution),
      '*',
      field('declarator', $._field_declarator),
    ))),
    member_pointer_type_declarator: $ => prec.dynamic(1, prec.right(seq(
      field('scope', $._scope_resolution),
      '*',
      field('declarator', $._type_declarator),
    ))),

    structured_binding_declarator: $ => prec.dynamic(PREC.STRUCTURED_BINDING, seq(
      '[', commaSep1($.identifier), ']',
    )),

    ref_qualifier: _ => choice('&', '&&'),

    _function_declarator_seq: $ => seq(
      field('parameters', $.parameter_list),
      optional($._function_attributes_start),
      optional($.ref_qualifier),
      optional($._function_exception_specification),
      optional($._function_attributes_end),
      optional($.trailing_return_type),
      optional($._function_postfix),
    ),

    _function_attributes_start: $ => prec(1, choice(
      seq(repeat1($.attribute_specifier), repeat($.type_qualifier)),
      seq(repeat($.attribute_specifier), repeat1($.type_qualifier)),
    )),

    _function_exception_specification: $ => choice(
      $.noexcept,
      $.throw_specifier,
    ),

    _function_attributes_end: $ => prec.right(seq(
      optional($.gnu_asm_expression),
      choice(
        seq(repeat1($.attribute_specifier), repeat($.attribute_declaration)),
        seq(repeat($.attribute_specifier), repeat1($.attribute_declaration)),
        repeat1($.function_suffix_macro),
      ),
    )),

    function_suffix_macro: _ => token(prec(1, /[A-Z][A-Z0-9_]*(_[A-Z0-9]+)+/)),

    _function_postfix: $ => prec.right(choice(
      repeat1($.virtual_specifier),
      $.requires_clause,
    )),

    function_declarator: $ => prec.dynamic(1, seq(
      field('declarator', $._declarator),
      $._function_declarator_seq,
    )),

    function_field_declarator: $ => prec.dynamic(1, seq(
      field('declarator', $._field_declarator),
      $._function_declarator_seq,
    )),

    abstract_function_declarator: $ => seq(
      field('declarator', optional($._abstract_declarator)),
      $._function_declarator_seq,
    ),

    trailing_return_type: $ => seq('->', $.type_descriptor),

    noexcept: $ => prec.right(seq(
      'noexcept',
      optional(
        seq(
          '(',
          optional($.expression),
          ')',
        ),
      ),
    )),

    throw_specifier: $ => seq(
      'throw',
      seq(
        '(',
        commaSep($.type_descriptor),
        ')',
      ),
    ),

    template_type: $ => seq(
      field('name', $._type_identifier),
      field('arguments', $.template_argument_list),
    ),

    template_method: $ => seq(
      field('name', choice($._field_identifier, $.operator_name)),
      field('arguments', $.template_argument_list),
    ),

    template_function: $ => seq(
      field('name', $.identifier),
      field('arguments', $.template_argument_list),
    ),

    template_argument_list: $ => seq(
      '<',
      choice(
        commaSep(choice(
          prec.dynamic(3, $.type_descriptor),
          prec.dynamic(2, alias($.type_parameter_pack_expansion, $.parameter_pack_expansion)),
          prec.dynamic(1, $._template_argument_expression),
        )),
        seq($.preproc_template_argument_fragment, commaSep(choice(
          prec.dynamic(3, $.type_descriptor),
          prec.dynamic(2, alias($.type_parameter_pack_expansion, $.parameter_pack_expansion)),
          prec.dynamic(1, $._template_argument_expression),
        ))),
      ),
      alias(token(prec(1, '>')), '>'),
    ),

    preproc_template_argument_fragment: _ => token(prec(
      1,
      /#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n[ \t]*[A-Za-z_:][A-Za-z0-9_:<>]*[ \t]*,[ \t]*(?:\/\/[^\n]*)?\r?\n#[ \t]*endif(?:\r?\n#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n[ \t]*[A-Za-z_:][A-Za-z0-9_:<>]*[ \t]*,[ \t]*(?:\/\/[^\n]*)?\r?\n#[ \t]*endif)*/,
    )),

    _template_argument_expression: $ => choice(
      $._expression_not_binary,
      $._template_argument_binary_expression,
    ),

    _template_argument_binary_expression: $ => {
      const table = [
        ['+', PREC.ADD],
        ['-', PREC.ADD],
        ['*', PREC.MULTIPLY],
        ['/', PREC.MULTIPLY],
        ['%', PREC.MULTIPLY],
        ['||', PREC.LOGICAL_OR],
        ['&&', PREC.LOGICAL_AND],
        ['|', PREC.INCLUSIVE_OR],
        ['^', PREC.EXCLUSIVE_OR],
        ['&', PREC.BITWISE_AND],
        ['==', PREC.EQUAL],
        ['!=', PREC.EQUAL],
        ['or', PREC.LOGICAL_OR],
        ['and', PREC.LOGICAL_AND],
        ['bitor', PREC.INCLUSIVE_OR],
        ['xor', PREC.EXCLUSIVE_OR],
        ['bitand', PREC.BITWISE_AND],
        ['not_eq', PREC.EQUAL],
      ];

      return choice(...table.map(([operator, precedence]) => {
        return prec.left(precedence, seq(
          field('left', $._template_argument_expression),
          // @ts-ignore
          field('operator', operator),
          field('right', $._template_argument_expression),
        ));
      }));
    },

    namespace_definition: $ => prec(1, seq(
      optional('inline'),
      'namespace',
      optional($.attribute_declaration),
      field('name', optional(
        choice(
          $._namespace_identifier,
          $.nested_namespace_specifier,
        ))),
      field('body', $.declaration_list),
    )),

    namespace_alias_definition: $ => seq(
      'namespace',
      field('name', $._namespace_identifier),
      '=',
      choice(
        $._namespace_identifier,
        $.nested_namespace_specifier,
      ),
      ';',
    ),

    _namespace_specifier: $ => seq(
      optional('inline'),
      $._namespace_identifier,
    ),

    nested_namespace_specifier: $ => prec(1, seq(
      optional($._namespace_specifier),
      '::',
      choice(
        $.nested_namespace_specifier,
        $._namespace_specifier,
      ),
    )),

    using_declaration: $ => seq(
      'using',
      optional(choice('namespace', 'enum', 'typename')),
      commaSep1(choice(
        $.identifier,
        $.qualified_identifier,
      )),
      ';',
    ),

    function_pointer_alias_declaration: $ => prec(1, seq(
      'using',
      field('name', $._type_identifier),
      '=',
      field('return_type', choice($.qualified_type_identifier, $.qualified_identifier, $.template_type, $._class_name)),
      '(',
      choice(
        seq(
          field('class', choice($.qualified_type_identifier, $.qualified_identifier, $._class_name)),
          '::*',
        ),
        '*',
      ),
      ')',
      field('parameters', $.parameter_list),
      ';',
    )),

    deduction_guide_declaration: $ => prec(1, seq(
      field('name', alias($._deduction_guide_identifier, $.identifier)),
      field('parameters', $.parameter_list),
      field('return_type', $.trailing_return_type),
      ';',
    )),

    _deduction_guide_identifier: _ => token(prec(1, /[A-Z][A-Za-z0-9_]*Proxy/)),

    deleted_operator_declaration: _ => token(prec(
      1,
      /(?:template[ \t]*<[^;]*>\r?\n)?void[ \t]+operator(?:==|!=)[ \t]*\([^;]*\)[ \t]*=[ \t]*delete;/,
    )),

    alias_declaration: $ => seq(
      'using',
      field('name', $._type_identifier),
      repeat($.attribute_declaration),
      optional($.function_prefix_macro),
      '=',
      field('type', $.type_descriptor),
      ';',
    ),

    static_assert_declaration: $ => seq(
      'static_assert',
      '(',
      field('condition', $.expression),
      optional(seq(
        ',',
        field('message', $._string),
      )),
      ')',
      ';',
    ),

    concept_definition: $ => seq(
      'concept',
      field('name', $.identifier),
      '=',
      $.expression,
      ';',
    ),

    // Statements

    _top_level_statement: ($, original) => choice(
      original,
      $.co_return_statement,
      $.co_yield_statement,
      $.for_each_statement,
      $.for_range_loop,
      $.try_statement,
      $.throw_statement,
    ),

    _non_case_statement: ($, original) => choice(
      $.preproc_streaming_statement,
      $.preproc_pipeline_tail_statement,
      $.preproc_selected_braced_if_else_statement,
      $.preproc_selected_if_statement,
      original,
      $.capitalized_call_statement,
      $.co_return_statement,
      $.co_yield_statement,
      $.for_each_statement,
      $.for_range_loop,
      $.try_statement,
      $.throw_statement,
    ),

    switch_statement: $ => seq(
      'switch',
      field('condition', $.condition_clause),
      field('body', $.compound_statement),
    ),

    capitalized_call_statement: $ => prec(1, seq(
      field('function', alias('SetHttpProxy', $.identifier)),
      field('arguments', $.argument_list),
      ';',
    )),

    preproc_streaming_statement: _ => token(prec(
      1,
      /#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n(?:[ \t]*(?:(?:\/\/[^\n]*)?)\r?\n)*[ \t]*(?:GTEST_SKIP|FAIL)\(\)[ \t]*(?:\/\/[^\n]*)?\r?\n(?:#[ \t]*else[^\n]*\r?\n(?:[ \t]*(?:(?:\/\/[^\n]*)?)\r?\n)*[ \t]*(?:GTEST_SKIP|FAIL)\(\)[ \t]*(?:\/\/[^\n]*)?\r?\n)?#[ \t]*endif[ \t]*(?:\r?\n[ \t]*)?<<[^\n;]*(?:\r?\n[ \t]*<<[^\n;]*)*;/,
    )),

    preproc_endif_fragment: _ => token(prec(1, /#[ \t]*endif/)),

    preproc_pipeline_tail_statement: _ => token(prec(
      1,
      /#[ \t]*if[^\n]*\r?\n(?:[^\n]*\r?\n){1,8}#[ \t]*endif[ \t]*\r?\n[ \t]*handle[ \t]*=[^\n;]*;/,
    )),

    preproc_selected_if_statement: $ => prec.right(seq(
      field('condition', $.preproc_selected_if_header),
      field('consequence', $.statement),
      optional(field('alternative', $.else_clause)),
    )),

    preproc_selected_if_header: _ => token(prec(
      1,
      /#[ \t]*if[^\n]*\r?\n[ \t]*if[^\n]*\r?\n(?:#[ \t]*else[^\n]*\r?\n[ \t]*if[^\n]*\r?\n)?#[ \t]*endif/,
    )),

    preproc_selected_braced_if_else_statement: _ => token(prec(
      1,
      /#[ \t]*if[^\n]*\r?\n[ \t]*if[^\n]*\{\r?\n#[ \t]*else[^\n]*\r?\n[ \t]*if[^\n]*\{\r?\n#[ \t]*endif\r?\n(?:[ \t]*[^#{}\r\n][^\r\n]*\r?\n)*[ \t]*\}[ \t]*else[ \t]*\{\r?\n(?:[ \t]*[^#{}\r\n][^\r\n]*\r?\n)*[ \t]*\}/,
    )),

    while_statement: $ => seq(
      'while',
      field('condition', $.condition_clause),
      field('body', $.statement),
    ),

    if_statement: $ => prec.right(seq(
      'if',
      optional('constexpr'),
      field('condition', $.condition_clause),
      field('consequence', $.statement),
      optional(field('alternative', $.else_clause)),
    )),

    // Using prec(1) instead of prec.dynamic(1) causes issues with the
    // range loop's declaration specifiers if `int` is passed in, it'll
    // always prefer the standard for loop and give us a parse error.
    _for_statement_body: ($, original) => prec.dynamic(1, original),
    for_range_loop: $ => seq(
      'for',
      '(',
      $._for_range_loop_body,
      ')',
      field('body', $.statement),
    ),
    _for_range_loop_body: $ => seq(
      field('initializer', optional($.init_statement)),
      $._declaration_specifiers,
      field('declarator', $._declarator),
      ':',
      field('right', choice(
        $.expression,
        $.initializer_list,
      )),
    ),

    for_each_statement: $ => seq(
      'for',
      'each',
      '(',
      $._declaration_specifiers,
      field('declarator', $._declarator),
      'in',
      field('right', $.expression),
      ')',
      field('body', $.statement),
    ),

    init_statement: $ => choice(
      $.alias_declaration,
      $.type_definition,
      $.declaration,
      $.expression_statement,
    ),

    condition_clause: $ => seq(
      '(',
      field('initializer', optional($.init_statement)),
      field('value', choice(
        $.expression,
        $.comma_expression,
        $.preproc_condition_expression,
        alias($.condition_declaration, $.declaration),
      )),
      ')',
    ),

    preproc_condition_expression: _ => token(prec(
      1,
      /#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n(?:[ \t]*[^#\r\n][^\n]*\r?\n)*(?:#[ \t]*else[^\n]*\r?\n(?:[ \t]*[^#\r\n][^\n]*\r?\n)*)?#[ \t]*endif/,
    )),

    condition_declaration: $ => seq(
      $._declaration_specifiers,
      field('declarator', $._declarator),
      choice(
        seq(
          '=',
          field('value', $.expression),
        ),
        field('value', $.initializer_list),
      ),
    ),

    return_statement: ($, original) => seq(
      choice(
        original,
        seq('return', $.initializer_list, ';'),
      ),
    ),

    co_return_statement: $ => seq(
      'co_return',
      optional($.expression),
      ';',
    ),

    co_yield_statement: $ => seq(
      'co_yield',
      $.expression,
      ';',
    ),

    throw_expression: $ => prec.right(PREC.UNARY, seq(
      'throw',
      optional($.expression),
    )),

    throw_statement: $ => seq(
      'throw',
      optional(choice(
        alias($.throw_fold_expression, $.fold_expression),
        $.expression,
      )),
      ';',
    ),

    throw_fold_expression: _ => token(prec(1, /\([^;\n]*<<[ \t]*\.\.\.[ \t]*<<[^;\n]*\)/)),

    try_statement: $ => seq(
      'try',
      field('body', $.compound_statement),
      choice(
        repeat1($.catch_clause),
        seq(repeat($.catch_clause), $.finally_clause),
      ),
    ),

    finally_clause: $ => seq(
      'finally',
      field('body', $.compound_statement),
    ),

    catch_clause: $ => seq(
      'catch',
      field('parameters', $.parameter_list),
      field('body', $.compound_statement),
    ),

    // Expressions

    _expression_not_binary: ($, original) => choice(
      $.preproc_initializer_expression,
      original,
      $.macro_statement_call,
      $.macro_no_throw_call,
      $.throw_expression,
      alias('ref', $.identifier),
      $.co_await_expression,
      $.requires_expression,
      $.requires_clause,
      $.benchmark_template_expression,
      $.macro_qualified_identifier,
      $.suffixed_string_literal,
      $.template_function,
      $.qualified_identifier,
      $.typeid_expression,
      $.cpp_cast_expression,
      $.new_expression,
      $.gcnew_expression,
      $.delete_expression,
      $.lambda_expression,
      $.parameter_pack_expansion,
      $.this,
      $.user_defined_literal,
      $.fold_expression,
    ),

    preproc_initializer_expression: _ => token(prec(
      1,
      /#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n[ \t]*(?:true|false|[-+]?\d+|[A-Za-z_]\w*(?:::\w+)*(?:\([^\r\n]*\))?)[ \t]*,?[ \t]*(?:\/\/[^\n]*)?\r?\n(?:#[ \t]*else[^\n]*\r?\n[ \t]*(?:true|false|[-+]?\d+|[A-Za-z_]\w*(?:::\w+)*(?:\([^\r\n]*\))?)[ \t]*,?[ \t]*(?:\/\/[^\n]*)?\r?\n)?#[ \t]*endif(?:\r?\n#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n[ \t]*(?:true|false|[-+]?\d+|[A-Za-z_]\w*(?:::\w+)*(?:\([^\r\n]*\))?)[ \t]*,?[ \t]*(?:\/\/[^\n]*)?\r?\n(?:#[ \t]*else[^\n]*\r?\n[ \t]*(?:true|false|[-+]?\d+|[A-Za-z_]\w*(?:::\w+)*(?:\([^\r\n]*\))?)[ \t]*,?[ \t]*(?:\/\/[^\n]*)?\r?\n)?#[ \t]*endif)*/,
    )),

    macro_statement_call: $ => prec(PREC.CALL, seq(
      field('function', alias($._throw_macro_identifier, $.identifier)),
      '(',
      choice(
        seq(
          $.macro_statement_declaration_fragment,
          field('argument', $.expression),
          ',',
          field('exception', $.macro_exception_type),
          optional(seq(',', field('message', $.expression))),
        ),
        seq(
          $.macro_argument_declaration_fragment,
          field('argument', $.expression),
          ',',
          field('exception', $.macro_exception_type),
          optional(seq(',', field('message', $.expression))),
        ),
        seq(
          $.macro_plain_declaration_fragment,
          field('argument', $.expression),
          ',',
          field('exception', $.macro_exception_type),
          optional(seq(',', field('message', $.expression))),
        ),
        seq(
          $.macro_typed_declaration_fragment,
          field('argument', $.expression),
          ',',
          field('exception', $.macro_exception_type),
          optional(seq(',', field('message', $.expression))),
        ),
        seq(
          field('argument', $.compound_statement),
          ',',
          field('exception', $.macro_exception_type),
          optional(seq(',', field('message', $.expression))),
        ),
        seq(
          field('argument', $.expression),
          ',',
          field('exception', $.macro_exception_type),
          optional(seq(',', field('message', $.expression))),
        ),
      ),
      ')',
    )),

    _throw_macro_identifier: _ => token(prec(1, /(?:U?EXPECT_THROW(?:_MSG)?|ASSERT_THROW)/)),

    macro_exception_type: $ => choice(
      $.dependent_type,
      $.qualified_identifier,
      $.identifier,
    ),

    macro_no_throw_call: $ => prec(PREC.CALL, seq(
      field('function', alias($._no_throw_macro_identifier, $.identifier)),
      '(',
      choice(
        seq($.macro_argument_declaration_fragment, field('argument', $.expression)),
        seq($.macro_plain_declaration_fragment, field('argument', $.expression)),
        seq($.macro_typed_declaration_fragment, field('argument', $.expression)),
        field('argument', $.compound_statement),
        field('argument', $.expression),
      ),
      ')',
    )),

    _no_throw_macro_identifier: _ => token(prec(1, new RegExp(NO_THROW_MACRO_PATTERN))),

    macro_qualified_identifier: $ => seq(
      alias($._namespace_alias_macro, $.identifier),
      $.identifier,
    ),

    _namespace_alias_macro: _ => token(prec(1, new RegExp(NAMESPACE_ALIAS_MACRO_PATTERN))),

    benchmark_template_expression: $ => prec(PREC.CALL, seq(
      field('function', alias('BENCHMARK_TEMPLATE', $.identifier)),
      field('arguments', alias($.benchmark_template_argument_list, $.argument_list)),
    )),

    benchmark_template_argument_list: $ => seq(
      '(',
      commaSep1(choice($.expression, $.type_descriptor)),
      ')',
    ),

    typeid_expression: $ => prec(PREC.CALL, seq(
      'typeid',
      '(',
      field('value', choice($.expression, $.type_descriptor)),
      ')',
    )),

    cpp_cast_expression: $ => prec(PREC.CALL, seq(
      field('function', alias(choice('static_cast', 'reinterpret_cast', 'const_cast', 'dynamic_cast'), $.identifier)),
      '<',
      field('type', $.type_descriptor),
      '>',
      field('argument', $.argument_list),
    )),

    _string: $ => choice(
      $.string_literal,
      $.raw_string_literal,
      $.concatenated_string,
    ),

    raw_string_literal: $ => seq(
      choice('R"', 'LR"', 'uR"', 'UR"', 'u8R"'),
      choice(
        seq(
          field('delimiter', $.raw_string_delimiter),
          '(',
          $.raw_string_content,
          ')',
          $.raw_string_delimiter,
        ),
        seq('(', $.raw_string_content, ')'),
      ),
      '"',
    ),

    subscript_expression: $ => prec(PREC.SUBSCRIPT, seq(
      field('argument', $.expression),
      field('indices', $.subscript_argument_list),
    )),

    subscript_argument_list: $ => seq(
      '[',
      commaSep(choice($.expression, $.initializer_list)),
      ']',
    ),

    call_expression: ($, original) => choice(original, seq(
      field('function', choice($.primitive_type, $.dependent_type)),
      field('arguments', $.argument_list),
    )),

    co_await_expression: $ => prec.left(PREC.UNARY, seq(
      field('operator', 'co_await'),
      field('argument', $.expression),
    )),

    new_expression: $ => prec.right(PREC.NEW, seq(
      optional('::'),
      'new',
      field('placement', optional($.argument_list)),
      field('type', $.type_specifier),
      field('declarator', optional($.new_declarator)),
      field('arguments', optional(choice(
        $.argument_list,
        $.initializer_list,
      ))),
    )),

    gcnew_expression: $ => prec.right(PREC.NEW, seq(
      'gcnew',
      field('type', $.type_specifier),
      field('declarator', optional($.new_declarator)),
      field('arguments', optional(choice(
        $.argument_list,
        $.initializer_list,
      ))),
    )),

    new_declarator: $ => prec.right(seq(
      '[',
      field('length', $.expression),
      ']',
      optional($.new_declarator),
    )),

    delete_expression: $ => seq(
      optional('::'),
      'delete',
      optional(seq('[', ']')),
      $.expression,
    ),

    field_expression: $ => seq(
      prec(PREC.FIELD, seq(
        field('argument', $.expression),
        field('operator', choice('.', '.*', '->', '->*')),
      )),
      field('field', choice(
        prec.dynamic(1, $._field_identifier),
        alias($.qualified_field_identifier, $.qualified_identifier),
        $.destructor_name,
        alias($.operator_cast_field_identifier, $.operator_cast),
        $.template_method,
        alias($.dependent_field_identifier, $.dependent_name),
      )),
    ),

    operator_cast_field_identifier: _ => token(prec(
      1,
      /operator[ \t]+(?:unsigned[ \t]+)?(?:int|long|short|char|bool|float|double)/,
    )),

    type_requirement: $ => seq('typename', $._class_name),

    nested_requirement: _ => token(prec(1, /requires[^\n;]*(?:\r?\n[ \t]*\.[^\n;]*)?;/)),

    compound_requirement: $ => seq(
      '{', $.expression, '}',
      optional('noexcept'),
      optional($.trailing_return_type),
      ';',
    ),

    _requirement: $ => choice(
      alias($.expression_statement, $.simple_requirement),
      $.type_requirement,
      $.nested_requirement,
      $.compound_requirement,
    ),

    requirement_seq: $ => seq('{', repeat($._requirement), '}'),

    constraint_conjunction: $ => prec.left(PREC.LOGICAL_AND, seq(
      field('left', $._requirement_clause_constraint),
      field('operator', choice('&&', 'and')),
      field('right', $._requirement_clause_constraint)),
    ),

    constraint_disjunction: $ => prec.left(PREC.LOGICAL_OR, seq(
      field('left', $._requirement_clause_constraint),
      field('operator', choice('||', 'or')),
      field('right', $._requirement_clause_constraint)),
    ),

    _requirement_clause_constraint: $ => choice(
      // Primary expressions"
      $.true,
      $.false,
      $._class_name,
      $.fold_expression,
      $.lambda_expression,
      $.requires_expression,

      // Parenthesized expressions
      seq('(', $.expression, ')'),

      // conjunction or disjunction of the above
      $.constraint_conjunction,
      $.constraint_disjunction,
    ),

    requires_clause: $ => seq(
      'requires',
      field('constraint', $._requirement_clause_constraint),
    ),

    requires_parameter_list: $ => seq(
      '(',
      commaSep(choice(
        $.parameter_declaration,
        $.optional_parameter_declaration,
        $.variadic_parameter_declaration,
      )),
      ')',
    ),

    requires_expression: $ => seq(
      'requires',
      field('parameters', optional(alias($.requires_parameter_list, $.parameter_list))),
      field('requirements', $.requirement_seq),
    ),

    lambda_expression: $ => seq(
      field('captures', $.lambda_capture_specifier),
      optional(seq(
        field('template_parameters', $.template_parameter_list),
        optional(field('constraint', $.requires_clause)),
      )),
      optional(field('declarator', $.abstract_function_declarator)),
      field('body', $.compound_statement),
    ),

    lambda_capture_specifier: $ => prec(PREC.LAMBDA, seq(
      '[',
      choice(
        $.lambda_default_capture,
        commaSep($._lambda_capture),
        seq(
          $.lambda_default_capture,
          ',', commaSep1($._lambda_capture),
        ),
      ),
      ']',
    )),

    lambda_default_capture: _ => choice('=', '&'),

    _lambda_capture_identifier: $ => seq(
      optional('&'),
      choice(
        $.identifier,
        $.qualified_identifier,
        alias($.identifier_parameter_pack_expansion, $.parameter_pack_expansion),
      ),
    ),

    lambda_capture_initializer: $ => seq(
      optional('&'),
      optional('...'),
      field('left', $.identifier),
      choice(
        seq('=', field('right', $.expression)),
        field('right', $.initializer_list),
      ),
    ),

    _lambda_capture: $ => choice(
      seq(optional('*'), $.this),
      $._lambda_capture_identifier,
      $.lambda_capture_initializer,
    ),

    _fold_operator: _ => choice(...FOLD_OPERATORS),
    _binary_fold_operator: _ => choice(...FOLD_OPERATORS.map((operator) => seq(field('operator', operator), '...', operator))),

    _unary_left_fold: $ => seq(
      field('left', '...'),
      field('operator', $._fold_operator),
      field('right', $.expression),
    ),
    _unary_right_fold: $ => seq(
      field('left', $.expression),
      field('operator', $._fold_operator),
      field('right', '...'),
    ),
    _binary_fold: $ => seq(
      field('left', $.expression),
      $._binary_fold_operator,
      field('right', $.expression),
    ),

    fold_expression: $ => seq(
      '(',
      choice(
        $._unary_right_fold,
        $._unary_left_fold,
        $._binary_fold,
      ),
      ')',
    ),

    parameter_pack_expansion: $ => prec(-1, seq(
      field('pattern', $.expression),
      '...',
    )),

    type_parameter_pack_expansion: $ => seq(
      field('pattern', $.type_descriptor),
      '...',
    ),

    identifier_parameter_pack_expansion: $ => seq(
      field('pattern', $.identifier),
      '...',
    ),

    sizeof_expression: ($, original) => prec.right(PREC.SIZEOF, choice(
      original,
      seq(
        'sizeof', '...',
        '(',
        field('value', $.identifier),
        ')',
      ),
    )),

    unary_expression: ($, original) => choice(
      original,
      prec.left(PREC.UNARY, seq(
        field('operator', choice('not', 'compl')),
        field('argument', $.expression),
      )),
    ),

    binary_expression: ($, original) => {
      const table = [
        ['<=>', PREC.THREE_WAY],
        ['or', PREC.LOGICAL_OR],
        ['and', PREC.LOGICAL_AND],
        ['bitor', PREC.INCLUSIVE_OR],
        ['xor', PREC.EXCLUSIVE_OR],
        ['bitand', PREC.BITWISE_AND],
        ['not_eq', PREC.EQUAL],
      ];

      return choice(
        original,
        prec.left(PREC.INCLUSIVE_OR, seq(
          field('left', $.expression),
          field('operator', '|'),
          $.preproc_binary_expression_fragment,
          field('right', $.expression),
        )),
        prec.left(PREC.LOGICAL_OR, seq(
          field('left', $.expression),
          $.preproc_logical_expression_fragment,
        )),
        prec.left(PREC.INCLUSIVE_OR, seq(
          field('left', $.expression),
          $.preproc_bitwise_expression_fragment,
        )),
        prec.left(PREC.LOGICAL_AND, seq(
          field('left', $.expression),
          field('operator', '&&'),
          $.preproc_logical_tail_expression_fragment,
        )),
        ...table.map(([operator, precedence]) => {
          return prec.left(precedence, seq(
            field('left', $.expression),
            // @ts-ignore
            field('operator', operator),
            field('right', $.expression),
          ));
        }));
    },

    preproc_binary_expression_fragment: _ => token(prec(1, /#[ \t]*if[^\n]*\r?\n[^\n]*\r?\n#[ \t]*endif/)),

    preproc_logical_expression_fragment: _ => token(prec(
      1,
      /#[ \t]*if[^\n]*\r?\n[ \t]*(?:\|\||&&)[^\r\n]*(?:\r?\n[ \t]*[^#\r\n][^\r\n]*){0,4}\r?\n#[ \t]*endif/,
    )),

    preproc_bitwise_expression_fragment: _ => token(prec(
      1,
      /#[ \t]*if[^\n]*\r?\n[ \t]*\|[^\r\n]*(?:\r?\n[ \t]*[^#\r\n][^\r\n]*){0,4}\r?\n#[ \t]*endif/,
    )),

    preproc_logical_tail_expression_fragment: _ => token(prec(
      1,
      /#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n(?:[ \t]*\/\/[^\r\n]*\r?\n)*[ \t]*(?:true|false|!?[A-Za-z_][^\r\n]*)(?:\r?\n[ \t]*[^#\r\n][^\r\n]*){0,4}\r?\n(?:#[ \t]*else[^\n]*\r?\n(?:[ \t]*\/\/[^\r\n]*\r?\n)*[ \t]*(?:true|false|!?[A-Za-z_][^\r\n]*)(?:\r?\n[ \t]*[^#\r\n][^\r\n]*){0,4}\r?\n)?#[ \t]*endif/,
    )),

    // The compound_statement is added to parse macros taking statements as arguments, e.g. MYFORLOOP(1, 10, i, { foo(i); bar(i); })
    argument_list: $ => seq(
      '(',
      choice(
        seq(commaSep($._argument_list_item), optional(',')),
        seq(',', commaSep1($._argument_list_item)),
        seq($.preproc_argument_fragment, commaSep($._argument_list_item)),
      ),
      ')',
    ),

    _argument_list_item: $ => choice(
      $.expression,
      $.initializer_list,
      $.compound_statement,
      $.preproc_prefixed_expression,
    ),

    macro_statement_declaration_fragment: _ => token(prec(1, /crypto::[A-Za-z_:<>]+[ \t]+[A-Za-z_]\w*(?:[^\n;]|\r?\n[ \t]*)*;\r?\n/)),

    macro_argument_declaration_fragment: _ => token(prec(1, /\[\[maybe_unused\]\][ \t]+(?:const[ \t]+)?auto(?:[^\n=]|\r?\n[ \t]*)*=[ \t]*/)),

    macro_plain_declaration_fragment: _ => token(prec(1, /(?:const[ \t]+)?auto(?:[^\n=]|\r?\n[ \t]*)*=[ \t]*/)),

    macro_typed_declaration_fragment: _ => token(prec(1, /(?:const[ \t]+)?[A-Za-z_:][A-Za-z0-9_:<>]*[ \t]+[A-Za-z_]\w*[ \t]*=[ \t]*/)),

    preproc_prefixed_expression: $ => seq(
      repeat1($.preproc_argument_fragment),
      $.expression,
    ),

    preproc_argument_fragment: _ => token(prec(
      1,
      /#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n(?:[ \t]*[^#\r\n][^\n]*\r?\n)*(?:#[ \t]*else[^\n]*\r?\n(?:[ \t]*[^#\r\n][^\n]*\r?\n)*)?#[ \t]*endif/,
    )),

    destructor_name: $ => prec(1, seq('~', $.identifier)),

    compound_literal_expression: ($, original) => choice(
      original,
      prec(PREC.CALL, seq(
        field('type', choice($._class_name, $.primitive_type, $.decltype, $.dependent_type, $.template_type, $.qualified_type_identifier)),
        field('value', $.initializer_list),
      )),
      prec(PREC.CALL, seq(
        field('type', $.decltype),
        field('value', $.argument_list),
      )),
    ),

    dependent_identifier: $ => seq('template', $.template_function),
    dependent_field_identifier: $ => seq('template', choice($.template_method, $._field_identifier)),
    dependent_type_identifier: $ => seq('template', $.template_type),

    qualified_constructor_identifier: _ => token(prec(
      1,
      /(?:PostgresChaosProxy::(?:~?PostgresChaosProxy)|CDriverPoolImpl::(?:~?CDriverPoolImpl))/,
    )),

    _scope_resolution: $ => prec(1, seq(
      field('scope', optional(choice(
        $._namespace_identifier,
        $.template_type,
        $.decltype,
        alias($.dependent_type_identifier, $.dependent_name),
      ))),
      '::',
    )),

    qualified_field_identifier: $ => seq(
      $._scope_resolution,
      field('name', choice(
        alias($.dependent_field_identifier, $.dependent_name),
        alias($.qualified_field_identifier, $.qualified_identifier),
        $.template_method,
        $.operator_name,
        prec.dynamic(1, $._field_identifier),
      )),
    ),

    qualified_identifier: $ => seq(
      $._scope_resolution,
      field('name', choice(
        alias($.dependent_identifier, $.dependent_name),
        $.qualified_identifier,
        $.template_function,
        seq(optional('template'), $.identifier),
        $.operator_name,
        $.destructor_name,
        $.pointer_type_declarator,
      )),
    ),

    qualified_type_identifier: $ => seq(
      $._scope_resolution,
      field('name', choice(
        alias($.dependent_type_identifier, $.dependent_name),
        alias($.qualified_type_identifier, $.qualified_identifier),
        $.template_type,
        $._type_identifier,
      )),
    ),

    qualified_operator_cast_identifier: $ => seq(
      $._scope_resolution,
      field('name', choice(
        alias($.qualified_operator_cast_identifier, $.qualified_identifier),
        $.operator_cast,
      )),
    ),

    _assignment_left_expression: ($, original) => choice(
      original,
      $.template_function,
      $.qualified_identifier,
      $.compound_literal_expression,
      $.cpp_cast_expression,
      alias('ref', $.identifier),
      $.user_defined_literal,
    ),

    assignment_expression: $ => prec.right(PREC.ASSIGNMENT, seq(
      field('left', $._assignment_left_expression),
      field('operator', choice(...ASSIGNMENT_OPERATORS)),
      field('right', choice($.expression, $.initializer_list)),
    )),

    _assignment_expression_lhs: $ => seq(
      field('left', $.expression),
      field('operator', choice(...ASSIGNMENT_OPERATORS)),
      field('right', choice($.expression, $.initializer_list)),
    ),

    // This prevents an ambiguity between fold expressions
    // and assignment expressions within parentheses.
    parenthesized_expression: ($, original) => choice(
      prec(1, seq(
        '(',
        alias($._assignment_expression_lhs, $.assignment_expression),
        ',',
        choice($.expression, $.comma_expression),
        ')',
      )),
      seq('(', $.comma_expression, ')'),
      original,
      seq('(', alias($._assignment_expression_lhs, $.assignment_expression), ')'),
    ),

    operator_name: $ => prec(1, seq(
      'operator',
      choice(
        'co_await',
        '+', '-', '*', '/', '%',
        '^', '&', '|', '~',
        '!', '=', '<', '>',
        '+=', '-=', '*=', '/=', '%=', '^=', '&=', '|=',
        '<<', '>>', '>>=', '<<=',
        '==', '!=', '<=', '>=',
        '<=>',
        '&&', '||',
        '++', '--',
        ',',
        '->*',
        '->',
        '()', '[]',
        'xor', 'bitand', 'bitor', 'compl',
        'not', 'xor_eq', 'and_eq', 'or_eq', 'not_eq',
        'and', 'or',
        seq(choice('new', 'delete'), optional('[]')),
        seq('""', $.identifier),
      ),
    )),

    this: _ => 'this',

    concatenated_string: $ => prec.right(seq(
      choice(
        seq($.identifier, choice($.string_literal, $.raw_string_literal, $.suffixed_string_literal)),
        seq(choice($.string_literal, $.raw_string_literal, $.suffixed_string_literal), choice($.identifier, $.string_literal, $.raw_string_literal, $.suffixed_string_literal, $.preproc_string_literal_fragment)),
        seq($.preproc_string_literal_fragment, choice($.identifier, $.string_literal, $.raw_string_literal, $.suffixed_string_literal)),
      ),
      repeat(choice($.identifier, $.string_literal, $.raw_string_literal, $.suffixed_string_literal, $.preproc_string_literal_fragment)),
    )),

    suffixed_string_literal: $ => prec(1, seq(
      choice($.string_literal, $.raw_string_literal),
      $.literal_suffix,
    )),

    preproc_string_literal_fragment: _ => token(prec(
      1,
      /#[ \t]*(?:if|ifdef|ifndef)[^\n]*\r?\n[ \t]*"(?:[^"\\]|\\.)*"[ \t]*\r?\n(?:#[ \t]*else[^\n]*\r?\n[ \t]*"(?:[^"\\]|\\.)*"[ \t]*\r?\n)?#[ \t]*endif/,
    )),

    number_literal: $ => {
      const sign = /[-\+]/;
      const separator = '\'';
      const binary = /[01]/;
      const binaryDigits = seq(repeat1(binary), repeat(seq(separator, repeat1(binary))));
      const decimal = /[0-9]/;
      const firstDecimal = /[1-9]/;
      const intDecimalDigits = seq(firstDecimal, repeat(decimal), repeat(seq(separator, repeat1(decimal))));
      const floatDecimalDigits = seq(repeat1(decimal), repeat(seq(separator, repeat1(decimal))));
      const hex = /[0-9a-fA-F]/;
      const hexDigits = seq(repeat1(hex), repeat(seq(separator, repeat1(hex))));
      const octal = /[0-7]/;
      const octalDigits = seq('0', repeat(octal), repeat(seq(separator, repeat1(octal))));
      const hexExponent = seq(/[pP]/, optional(sign), floatDecimalDigits);
      const decimalExponent = seq(/[eE]/, optional(sign), floatDecimalDigits);
      const intSuffix = /(ll|LL)[uU]?|[uU](ll|LL)?|[uU][lL]?|[uU][zZ]?|[lL][uU]?|[zZ][uU]?/;
      const floatSuffix = /([fF](16|32|64|128)?)|[lL]|(bf16|BF16)/;

      return token(seq(
        optional(sign),
        choice(
          seq(
            choice(
              seq(choice('0b', '0B'), binaryDigits),
              intDecimalDigits,
              seq(choice('0x', '0X'), hexDigits),
              octalDigits,
            ),
            optional(intSuffix),
          ),
          seq(
            choice(
              seq(floatDecimalDigits, decimalExponent),
              seq(floatDecimalDigits, '.', optional(floatDecimalDigits), optional(decimalExponent)),
              seq('.', floatDecimalDigits, optional(decimalExponent)),
              seq(
                choice('0x', '0X'),
                choice(
                  hexDigits,
                  seq(hexDigits, '.', optional(hexDigits)),
                  seq('.', hexDigits)),
                hexExponent,
              ),
            ),
            optional(floatSuffix),
          ),
        ),
      ));
    },

    literal_suffix: _ => token.immediate(/[a-zA-Z_]\w*/),

    user_defined_literal: $ => seq(
      choice(
        $.number_literal,
        $.char_literal,
        $._string,
      ),
      $.literal_suffix,
    ),

    _namespace_identifier: $ => alias($.identifier, $.namespace_identifier),
  },
});

/**
 * Creates a rule to optionally match one or more of the rules separated by a comma
 *
 * @param {Rule} rule
 *
 * @returns {ChoiceRule}
 */
function commaSep(rule) {
  return optional(commaSep1(rule));
}

/**
 * Creates a rule to match one or more of the rules separated by a comma
 *
 * @param {Rule} rule
 *
 * @returns {SeqRule}
 */
function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

function preprocessor(command) {
  const pattern = command === 'if' ? '#[ \\t]*if[ \\t]+' : '#[ \\t]*' + command;
  return alias(token(prec(1, new RegExp(pattern))), '#' + command);
}

function withStructuredMacroReplacementList($, original) {
  return {
    ...original,
    members: original.members.map((member) => {
      if (member.type === 'FIELD' && member.name === 'value') {
        return field('value', optional(choice($.macro_replacement_list, $.preproc_arg)));
      }
      return member;
    }),
  };
}
