#include "tools/impl/format_model_parse.h"

#include <cstdint>
#include <memory>
#include <string>
#include <tree_sitter/api.h>
#include <tree_sitter_cpp.h>

#include "tools/impl/format_model_builder.h"

namespace {

struct TSParserDeleter {
    void operator()(TSParser* parser) const {
        ts_parser_delete(parser);
    }
};

TSParser* ThreadFormatParser() {
    thread_local std::unique_ptr<TSParser, TSParserDeleter> parser(ts_parser_new());
    thread_local bool languageReady = parser != nullptr && ts_parser_set_language(parser.get(), tree_sitter_cpp());
    return languageReady ? parser.get() : nullptr;
}

}  // namespace

FormatModel ParseFormatModel(std::string_view text) {
    auto sourceText = std::make_unique<std::string>(text);
    TSParser* parser = ThreadFormatParser();
    if (parser == nullptr) {
        FormatModel model;
        model.sourceText = std::move(sourceText);
        model.parse.error = "tree-sitter parser setup failed";
        return model;
    }
    ts_parser_reset(parser);
    TSTree* tree =
        ts_parser_parse_string(parser, nullptr, sourceText->data(), static_cast<uint32_t>(sourceText->size()));
    if (tree == nullptr) {
        FormatModel model;
        model.sourceText = std::move(sourceText);
        model.parse.error = "tree-sitter parse setup failed";
        return model;
    }
    const TSNode root = ts_tree_root_node(tree);
    FormatModel model = BuildFormatModel(root, std::move(sourceText));
    ts_tree_delete(tree);
    return model;
}
