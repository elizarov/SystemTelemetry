#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "config.h"
#include "layout_edit_parameter_id.h"

struct LayoutEditTooltipDescriptor {
    std::string configKey;
    std::string sectionName;
    std::string memberName;
    configschema::ValueFormat valueFormat = configschema::ValueFormat::Integer;
};

enum class LayoutEditWidgetDragMode {
    Linear,
    GaugeSweepDegrees,
    GaugeSegmentGapDegrees,
};

struct LayoutEditConfigFieldMetadata {
    std::string_view sectionName;
    std::string_view parameterName;
    configschema::ValueFormat valueFormat = configschema::ValueFormat::Integer;
    bool isFont = false;
    bool (*applyValue)(AppConfig& config, double value) = nullptr;
    std::optional<const UiFontConfig*> (*fontValue)(const AppConfig& config) = nullptr;
};

struct LayoutEditParameterInfo {
    LayoutEditParameter parameter = LayoutEditParameter::MetricListLabelWidth;
    const LayoutEditConfigFieldMetadata* field = nullptr;
    bool supportsWidgetGuide = false;
    bool supportsAnchor = false;
    LayoutEditWidgetDragMode widgetGuideDragMode = LayoutEditWidgetDragMode::Linear;
};

const LayoutEditParameterInfo& GetLayoutEditParameterInfo(LayoutEditParameter parameter);
const LayoutEditConfigFieldMetadata& GetLayoutEditConfigFieldMetadata(LayoutEditParameter parameter);
int GetLayoutEditParameterHitPriority(LayoutEditParameter parameter);
bool IsFontLayoutEditParameter(LayoutEditParameter parameter);
std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(LayoutEditParameter parameter);
std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, LayoutEditParameter parameter);
bool ApplyLayoutEditParameterValue(AppConfig& config, LayoutEditParameter parameter, double value);
