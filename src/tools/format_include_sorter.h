#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tools/format_config.h"
#include "tools/format_lexer.h"

namespace tools::format {

std::vector<std::optional<std::string>> SortedIncludeRunLines(
    TokenSpan tokens,
    const FormatterConfig& config,
    std::string_view sourcePath
);

}  // namespace tools::format
