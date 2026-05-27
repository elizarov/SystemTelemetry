#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tools/impl/format_config.h"
#include "tools/impl/format_lexer.h"

std::vector<std::optional<std::string>> SortedIncludeRunLines(
    TokenSpan tokens,
    const FormatterConfig& config,
    std::string_view sourcePath
);
