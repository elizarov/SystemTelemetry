#pragma once

#include <string_view>

#include "tools/impl/format_model.h"

FormatModel ParseFormatModel(std::string_view text);
