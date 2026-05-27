#pragma once

#include <string>

#include "tools/format_config.h"
#include "tools/format_model.h"

namespace tools::format {

std::string FormatModelText(const FormatterConfig& config, const FormatModel& model);

}  // namespace tools::format
