#pragma once

#include <string>
#include <string_view>
#include <vector>

struct FormatterConfig;
struct SyntaxNode;

std::string FormatIncludeRunText(
    const FormatterConfig& config,
    const SyntaxNode& includeRun,
    std::string_view sourcePath
);

std::string FormatIncludeLinesText(
    const FormatterConfig& config,
    const std::vector<std::string>& includeLines,
    std::string_view sourcePath
);

std::string FormatOpeningIncludeBlocksText(
    const FormatterConfig& config,
    std::string_view text,
    std::string_view sourcePath
);
