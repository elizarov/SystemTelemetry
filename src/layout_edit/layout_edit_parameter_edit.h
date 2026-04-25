#pragma once

#include <optional>

#include "layout_model/layout_edit_parameter_metadata.h"

std::optional<double> FindLayoutEditParameterNumericValue(const AppConfig& config, LayoutEditParameter parameter);
std::optional<unsigned int> FindLayoutEditParameterColorValue(const AppConfig& config, LayoutEditParameter parameter);
std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, LayoutEditParameter parameter);
bool ApplyLayoutEditParameterValue(AppConfig& config, LayoutEditParameter parameter, double value);
bool ApplyLayoutEditParameterColorValue(AppConfig& config, LayoutEditParameter parameter, unsigned int value);
bool ApplyLayoutEditParameterFontValue(AppConfig& config, LayoutEditParameter parameter, const UiFontConfig& value);
