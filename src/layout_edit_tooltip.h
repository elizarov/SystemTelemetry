#pragma once

#include <optional>
#include <string>

#include "dashboard_renderer.h"

enum class LayoutEditTooltipValueFormat {
    Integer,
    FloatingPoint,
};

struct LayoutEditTooltipDescriptor {
    std::string configKey;
    std::string sectionName;
    std::string memberName;
    LayoutEditTooltipValueFormat valueFormat = LayoutEditTooltipValueFormat::Integer;
};

std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(
    DashboardRenderer::WidgetEditParameter parameter);
std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(
    DashboardRenderer::AnchorEditParameter parameter);
std::string FormatLayoutEditTooltipValue(double value, LayoutEditTooltipValueFormat format);
std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, double value);
