#pragma once

#include <optional>
#include <string>

#include "config.h"
#include "layout_edit_parameter_id.h"

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
    LayoutEditParameter parameter = LayoutEditParameter::MetricListLabelWidth;
    LayoutEditTooltipDescriptor tooltip;
    bool supportsWidgetGuide = false;
    bool supportsAnchor = false;
    bool isFont = false;
    LayoutEditWidgetDragMode widgetGuideDragMode = LayoutEditWidgetDragMode::Linear;
    bool (*applyValue)(AppConfig& config, double value) = nullptr;
    const UiFontConfig* (*fontValue)(const AppConfig& config) = nullptr;
};

const LayoutEditParameterInfo& GetLayoutEditParameterInfo(LayoutEditParameter parameter);
int GetLayoutEditParameterHitPriority(LayoutEditParameter parameter);
bool IsFontLayoutEditParameter(LayoutEditParameter parameter);
std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(LayoutEditParameter parameter);
std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, LayoutEditParameter parameter);
bool ApplyLayoutEditParameterValue(AppConfig& config, LayoutEditParameter parameter, double value);
