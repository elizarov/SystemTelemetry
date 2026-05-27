#pragma once

#include <string>
#include <string_view>

#include "tools/format_config.h"
#include "tools/format_model.h"

namespace tools::format {

std::string FormatModelText(const FormatterConfig& config, const FormatModel& model, std::string_view sourcePath);

}  // namespace tools::format
