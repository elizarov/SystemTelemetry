#include "tools/impl/format_model_parse.h"

#include <cstdint>
#include <memory>
#include <string>
#include <tree_sitter/api.h>
#include <tree_sitter_cpp.h>

#include "tools/impl/format_model_builder.h"

FormatModel ParseFormatModel(std::string_view text) {
    auto sourceText = std::make_unique<std::string>(text);
    TSParser* parser = ts_parser_new();
    if (parser == nullptr) {
        FormatModel model;
        model.sourceText = std::move(sourceText);
        model.parse.error = "tree-sitter parser setup failed";
        return model;
    }
    if (!ts_parser_set_language(parser, tree_sitter_cpp())) {
        ts_parser_delete(parser);
        FormatModel model;
        model.sourceText = std::move(sourceText);
        model.parse.error = "tree-sitter parser setup failed";
        return model;
    }
    TSTree* tree =
        ts_parser_parse_string(parser, nullptr, sourceText->data(), static_cast<uint32_t>(sourceText->size()));
    if (tree == nullptr) {
        ts_parser_delete(parser);
        FormatModel model;
        model.sourceText = std::move(sourceText);
        model.parse.error = "tree-sitter parse setup failed";
        return model;
    }
    const TSNode root = ts_tree_root_node(tree);
    FormatModel model = BuildFormatModel(root, std::move(sourceText));
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return model;
}
