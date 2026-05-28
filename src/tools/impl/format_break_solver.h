#pragma once

#include <map>

#include "tools/impl/format_break_model.h"
#include "tools/impl/format_config.h"

struct FormatBreakSolution {
    std::map<int, FormatBreakChoice> choices;
};

FormatBreakSolution SolveFormatBreaks(
    const FormatterConfig& config,
    const FormatBreakModel& model,
    int startColumn,
    int indentLevel,
    int indentWidth
);
