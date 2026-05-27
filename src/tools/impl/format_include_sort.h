#pragma once

#include <string>
#include <string_view>

struct FormatterConfig;
struct SyntaxNode;

std::string FormatIncludeRunText(
    const FormatterConfig& config,
    const SyntaxNode& includeRun,
    std::string_view sourcePath
);
