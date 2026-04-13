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

enum class LayoutEditWidgetDragMode {
    Linear,
    GaugeSweepDegrees,
    GaugeSegmentGapDegrees,
};

struct LayoutEditParameterInfo {
    DashboardRenderer::LayoutEditParameter parameter = DashboardRenderer::LayoutEditParameter::MetricListLabelWidth;
    LayoutEditTooltipDescriptor tooltip;
    bool supportsWidgetGuide = false;
    bool supportsAnchor = false;
    bool isFont = false;
    LayoutEditWidgetDragMode widgetGuideDragMode = LayoutEditWidgetDragMode::Linear;
    bool (*applyValue)(AppConfig& config, double value) = nullptr;
    const UiFontConfig* (*fontValue)(const AppConfig& config) = nullptr;
};

const LayoutEditParameterInfo& GetLayoutEditParameterInfo(DashboardRenderer::LayoutEditParameter parameter);
int GetLayoutEditParameterHitPriority(DashboardRenderer::LayoutEditParameter parameter);
bool IsFontLayoutEditParameter(DashboardRenderer::LayoutEditParameter parameter);
std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(DashboardRenderer::LayoutEditParameter parameter);
std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, DashboardRenderer::LayoutEditParameter parameter);
bool ApplyLayoutEditParameterValue(AppConfig& config, DashboardRenderer::LayoutEditParameter parameter, double value);
