#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "config/config.h"
#include "widget/layout_edit_parameter_id.h"
#include "widget/layout_edit_types.h"

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
int GetLayoutEditParameterHitPriority(LayoutEditParameter parameter);
int LayoutEditAnchorHitPriority(const LayoutEditAnchorKey& key);
bool IsFontLayoutEditParameter(LayoutEditParameter parameter);
std::string GetLayoutEditParameterDisplayName(LayoutEditParameter parameter);
std::optional<LayoutEditParameter> FindLayoutEditParameterByConfigField(
    std::string_view sectionName, std::string_view parameterName);
std::optional<double> FindLayoutEditParameterNumericValue(const AppConfig& config, LayoutEditParameter parameter);
std::optional<unsigned int> FindLayoutEditParameterColorValue(const AppConfig& config, LayoutEditParameter parameter);
std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(LayoutEditParameter parameter);
std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, LayoutEditParameter parameter);
bool ApplyLayoutEditParameterValue(AppConfig& config, LayoutEditParameter parameter, double value);
bool ApplyLayoutEditParameterColorValue(AppConfig& config, LayoutEditParameter parameter, unsigned int value);
bool ApplyLayoutEditParameterFontValue(AppConfig& config, LayoutEditParameter parameter, const UiFontConfig& value);
