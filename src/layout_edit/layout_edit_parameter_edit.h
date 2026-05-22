#pragma once

#include <optional>

#include "config/config.h"
#include "widget/layout_edit_parameter_id.h"

std::optional<double> FindLayoutEditParameterNumericValue(const AppConfig& config, LayoutEditParameter parameter);
std::optional<unsigned int> FindLayoutEditParameterColorValue(const AppConfig& config, LayoutEditParameter parameter);
std::optional<const ColorConfig*> FindLayoutEditParameterColorConfigValue(
    const AppConfig& config, LayoutEditParameter parameter);
std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, LayoutEditParameter parameter);
bool ApplyLayoutEditParameterValue(AppConfig& config, LayoutEditParameter parameter, double value);
bool ApplyLayoutEditParameterColorValue(AppConfig& config, LayoutEditParameter parameter, unsigned int value);
bool ApplyLayoutEditParameterFontValue(AppConfig& config, LayoutEditParameter parameter, const UiFontConfig& value);
