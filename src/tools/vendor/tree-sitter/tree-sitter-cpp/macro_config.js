// Generated from .cpp-format and tools/tests/format/.cpp-format-userver by
// tools/regenerate_tree_sitter_grammar.py.
module.exports = {
  macro_categories: {
    calling_convention: [
      // .cpp-format
      "CALLBACK",
      "WINAPI",
      // tools/tests/format/.cpp-format-userver
    ],
    raw_macro_function_prefix: [
      // .cpp-format
      // tools/tests/format/.cpp-format-userver
      "IMPL_UTEST",
      "TYPED_UTEST",
      "INSTANTIATE_UTEST",
      "UTEST",
      "UPROTO",
      "LOG",
      "RET",
      "UASSERT",
      "UEXPECT",
      "UINVARIANT",
      "USERVER",
      "UTILS",
    ],
    function_prefix: [
      // .cpp-format
      // tools/tests/format/.cpp-format-userver
      "USERVER_IMPL_NODEBUG",
      "USERVER_IMPL_FORCE_INLINE",
      "USERVER_IMPL_DISABLE_ASAN",
    ],
    macro_function_definition: [
      // .cpp-format
      // tools/tests/format/.cpp-format-userver
      "TYPED_UTEST_P_MT",
      "TYPED_UTEST_MT",
      "TYPED_UTEST_P",
      "UTEST_F_MT",
      "UTEST_P_MT",
      "UTEST_MT",
      "UTEST_DEATH",
      "UTEST_F",
      "UTEST_P",
      "UTEST",
    ],
    no_throw_macro: [
      // .cpp-format
      // tools/tests/format/.cpp-format-userver
      "UEXPECT_NO_THROW",
      "UASSERT_NO_THROW",
    ],
    name_macro_call: [
      // .cpp-format
      // tools/tests/format/.cpp-format-userver
      "RET_NAME",
    ],
    namespace_alias_macro: [
      // .cpp-format
      // tools/tests/format/.cpp-format-userver
      "CURL_FORMAT_USERVER_NAMESPACE",
      "CURL_8_13_NAMESPACE",
      "CURL_SSLVERSION_NAMESPACE",
    ],
  },
};
