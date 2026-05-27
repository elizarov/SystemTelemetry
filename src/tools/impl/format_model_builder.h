#pragma once

#include <string_view>
#include <tree_sitter/api.h>

#include "tools/impl/format_model.h"

FormatModel BuildFormatModel(TSNode root, std::string_view text);
