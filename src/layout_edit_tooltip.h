#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "layout_edit_types.h"
#include "layout_edit_parameter.h"

std::string FormatLayoutEditTooltipValue(double value, configschema::ValueFormat format);
std::string FormatLayoutEditTooltipValue(unsigned int value);
std::string FormatLayoutEditTooltipValue(const UiFontConfig& value);
std::string FormatLayoutEditTooltipValue(std::string_view value);
std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, double value);
std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, unsigned int value);
std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, const UiFontConfig& value);
std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, std::string_view value);
std::optional<std::string> BuildMetricListOrderTooltipLine(
    const AppConfig& config, const LayoutMetricListOrderEditKey& key, int rowIndex);
std::optional<std::string> BuildMetricListAddRowTooltipLine(
    const AppConfig& config, const LayoutMetricListOrderEditKey& key);
