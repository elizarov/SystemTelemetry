#pragma once

#include <string>
#include <string_view>

#include "tools/impl/format_config.h"
#include "tools/impl/format_model.h"

std::string FormatModelText(const FormatterConfig& config, SyntaxNode* root, std::string_view sourcePath);
