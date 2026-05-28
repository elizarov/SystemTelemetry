#pragma once

#include <span>

#include "tools/impl/format_break_model.h"

FormatBreakModel BuildFormatBreakModel(std::span<const PrintToken> tokens);
