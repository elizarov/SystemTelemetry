#pragma once

#include <memory>
#include <string_view>
#include <tree_sitter/api.h>

#include "tools/impl/format_model.h"

FormatModel BuildFormatModel(TSNode root, std::unique_ptr<std::string> sourceText);
