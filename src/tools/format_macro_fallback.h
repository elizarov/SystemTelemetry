#pragma once

#include "tools/format_lexer.h"
#include "tools/format_model.h"

namespace tools::format {

LayoutTree BuildMacroFallbackLayoutTree(TokenSpan tokens);

}  // namespace tools::format
