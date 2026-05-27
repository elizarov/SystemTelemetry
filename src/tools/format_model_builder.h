#pragma once

#include <string_view>
#include <tree_sitter/api.h>
#include <vector>

#include "tools/format_lexer.h"
#include "tools/format_model.h"

namespace tools::format {

bool IsNewlineByte(char ch);
size_t AdvanceNewline(std::string_view text, size_t index);
ParseResult ParseTreeResult(TSNode root, std::string_view text);
void AnnotateTokenIndexesAndGroups(std::vector<Token>& tokens);
SourceLayoutNode BuildSourceLayoutRoot(TSNode root, std::string_view text, std::vector<Token>& tokens);

}  // namespace tools::format
