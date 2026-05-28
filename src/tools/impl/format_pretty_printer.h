#pragma once

#include <chrono>
#include <string>
#include <string_view>

#include "tools/impl/format_config.h"
#include "tools/impl/format_model.h"

struct FormatModelTextStats {
    std::chrono::nanoseconds tokenize{};
    std::chrono::nanoseconds annotate{};
    std::chrono::nanoseconds print{};
    std::chrono::nanoseconds breakModel{};
    std::chrono::nanoseconds solve{};
    std::chrono::nanoseconds emit{};
};

std::string FormatModelText(const FormatterConfig& config, const FormatModel& model, std::string_view sourcePath);
std::string FormatModelText(
    const FormatterConfig& config,
    const FormatModel& model,
    std::string_view sourcePath,
    FormatModelTextStats& stats
);
