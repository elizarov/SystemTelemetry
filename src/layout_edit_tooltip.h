#pragma once

#include <optional>
#include <string>

#include "config.h"
#include "dashboard_renderer.h"

enum class LayoutEditTooltipValueFormat {
    Integer,
    FloatingPoint,
    FontSpec,
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
std::string FormatLayoutEditTooltipValue(const UiFontConfig& value);
std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, double value);
std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, const UiFontConfig& value);
std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, DashboardRenderer::AnchorEditParameter parameter);
