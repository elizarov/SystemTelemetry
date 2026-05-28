#pragma once

#include <string>
#include <string_view>

#include "tools/impl/format_config.h"

struct SourceFormatResult {
    bool ok = true;
    bool changed = false;
    std::string formatted;
    std::string error;
};

SourceFormatResult FormatSourceText(std::string_view text, const FormatterConfig& config, std::string_view sourcePath);

int RunFormat(int argc, char** argv);
