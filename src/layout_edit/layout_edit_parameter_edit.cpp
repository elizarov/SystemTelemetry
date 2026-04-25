#include "layout_edit/layout_edit_parameter_edit.h"

std::optional<double> FindLayoutEditParameterNumericValue(const AppConfig& config, LayoutEditParameter parameter) {
    return GetLayoutEditConfigFieldMetadata(parameter).numericValue(config);
}

std::optional<unsigned int> FindLayoutEditParameterColorValue(const AppConfig& config, LayoutEditParameter parameter) {
    return GetLayoutEditConfigFieldMetadata(parameter).colorValue(config);
}

std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, LayoutEditParameter parameter) {
    return GetLayoutEditConfigFieldMetadata(parameter).fontValue(config);
}

bool ApplyLayoutEditParameterValue(AppConfig& config, LayoutEditParameter parameter, double value) {
    return GetLayoutEditConfigFieldMetadata(parameter).applyValue(config, value);
}

bool ApplyLayoutEditParameterColorValue(AppConfig& config, LayoutEditParameter parameter, unsigned int value) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    return field.applyColorValue != nullptr ? field.applyColorValue(config, value) : false;
}

bool ApplyLayoutEditParameterFontValue(AppConfig& config, LayoutEditParameter parameter, const UiFontConfig& value) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    return field.applyFontValue != nullptr ? field.applyFontValue(config, value) : false;
}
