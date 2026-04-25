#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "config/config.h"
#include "widget/layout_edit_parameter_id.h"

struct LayoutEditTooltipDescriptor {
    std::string configKey;
    std::string sectionName;
    std::string memberName;
    configschema::ValueFormat valueFormat = configschema::ValueFormat::Integer;
};

struct LayoutEditConfigFieldMetadata {
    std::string_view sectionName;
    std::string_view parameterName;
    configschema::ValueFormat valueFormat = configschema::ValueFormat::Integer;
    bool isFont = false;
    std::optional<double> (*numericValue)(const AppConfig& config) = nullptr;
    std::optional<unsigned int> (*colorValue)(const AppConfig& config) = nullptr;
    bool (*applyValue)(AppConfig& config, double value) = nullptr;
    bool (*applyColorValue)(AppConfig& config, unsigned int value) = nullptr;
    bool (*applyFontValue)(AppConfig& config, const UiFontConfig& value) = nullptr;
    std::optional<const UiFontConfig*> (*fontValue)(const AppConfig& config) = nullptr;
};

struct LayoutEditParameterInfo {
    LayoutEditParameter parameter = LayoutEditParameter::MetricListLabelWidth;
    const LayoutEditConfigFieldMetadata* field = nullptr;
};

const LayoutEditParameterInfo& GetLayoutEditParameterInfo(LayoutEditParameter parameter);
const LayoutEditConfigFieldMetadata& GetLayoutEditConfigFieldMetadata(LayoutEditParameter parameter);
bool IsFontLayoutEditParameter(LayoutEditParameter parameter);
std::string GetLayoutEditParameterDisplayName(LayoutEditParameter parameter);
std::optional<LayoutEditParameter> FindLayoutEditParameterByConfigField(
    std::string_view sectionName, std::string_view parameterName);
std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(LayoutEditParameter parameter);
