#pragma once

#include <optional>
#include <string>

#include "layout_edit_parameter.h"

std::string FormatLayoutEditTooltipValue(double value, configschema::ValueFormat format);
std::string FormatLayoutEditTooltipValue(const UiFontConfig& value);
std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, double value);
std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, const UiFontConfig& value);
