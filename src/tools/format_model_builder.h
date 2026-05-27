#pragma once

#include <string_view>
#include <tree_sitter/api.h>
#include <vector>

#include "tools/format_config.h"
#include "tools/format_lexer.h"
#include "tools/format_model.h"

namespace tools::format {

bool IsNewlineByte(char ch);
size_t AdvanceNewline(std::string_view text, size_t index);
size_t AppendSourceTrivia(std::string_view text, size_t begin, size_t end, std::vector<Token>& tokens);
void AppendTreeChildTokens(TSNode node, std::string_view text, std::vector<Token>& tokens);
ParseResult ParseTreeResult(TSNode root, std::string_view text);
std::vector<Token> SortIncludeTokens(
    std::vector<Token> tokens,
    const FormatterConfig& config,
    std::string_view sourcePath
);
void AnnotateTokenIndexesAndGroups(std::vector<Token>& tokens);
SourceLayoutNode BuildSourceLayoutRoot(TSNode root, const std::vector<Token>& tokens);

}  // namespace tools::format
