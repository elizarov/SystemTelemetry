#include "layout_edit/layout_edit_parameter_edit.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

void* FieldAddress(AppConfig& config, const LayoutEditConfigFieldMetadata& field) {
    return reinterpret_cast<char*>(&config) + field.rootOffset;
}

const void* FieldAddress(const AppConfig& config, const LayoutEditConfigFieldMetadata& field) {
    return reinterpret_cast<const char*>(&config) + field.rootOffset;
}

int ClampIntegerLayoutEditValue(const LayoutEditConfigFieldMetadata& field, int value) {
    switch (field.policy) {
        case RuntimeConfigFieldPolicy::PositiveInt:
            return (std::max)(1, value);
        case RuntimeConfigFieldPolicy::NonNegativeInt:
            return (std::max)(0, value);
        case RuntimeConfigFieldPolicy::None:
        case RuntimeConfigFieldPolicy::FontSize:
        case RuntimeConfigFieldPolicy::Degrees:
            return value;
    }
    return value;
}

double ClampDoubleLayoutEditValue(const LayoutEditConfigFieldMetadata& field, double value) {
    return field.policy == RuntimeConfigFieldPolicy::Degrees ? std::clamp(value, 0.0, 360.0) : value;
}

void ClampFontLayoutEditValue(UiFontConfig& font) {
    font.size = (std::max)(1, font.size);
    font.weight = (std::max)(1, font.weight);
}

}  // namespace

std::optional<double> FindLayoutEditParameterNumericValue(const AppConfig& config, LayoutEditParameter parameter) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    const void* address = FieldAddress(config, field);
    switch (field.valueKind) {
        case RuntimeConfigFieldValueKind::Int:
            return static_cast<double>(*reinterpret_cast<const int*>(address));
        case RuntimeConfigFieldValueKind::Double:
            return *reinterpret_cast<const double*>(address);
        case RuntimeConfigFieldValueKind::FontSpec:
            return static_cast<double>(reinterpret_cast<const UiFontConfig*>(address)->size);
        case RuntimeConfigFieldValueKind::String:
        case RuntimeConfigFieldValueKind::StringList:
        case RuntimeConfigFieldValueKind::LogicalPoint:
        case RuntimeConfigFieldValueKind::LogicalSize:
        case RuntimeConfigFieldValueKind::HexColor:
        case RuntimeConfigFieldValueKind::LayoutExpression:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<unsigned int> FindLayoutEditParameterColorValue(const AppConfig& config, LayoutEditParameter parameter) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    if (field.valueKind != RuntimeConfigFieldValueKind::HexColor) {
        return std::nullopt;
    }
    return reinterpret_cast<const ColorConfig*>(FieldAddress(config, field))->ToRgba();
}

std::optional<const ColorConfig*> FindLayoutEditParameterColorConfigValue(
    const AppConfig& config, LayoutEditParameter parameter) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    if (field.valueKind != RuntimeConfigFieldValueKind::HexColor) {
        return std::nullopt;
    }
    return reinterpret_cast<const ColorConfig*>(FieldAddress(config, field));
}

std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, LayoutEditParameter parameter) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    if (field.valueKind != RuntimeConfigFieldValueKind::FontSpec) {
        return std::nullopt;
    }
    return reinterpret_cast<const UiFontConfig*>(FieldAddress(config, field));
}

bool ApplyLayoutEditParameterValue(AppConfig& config, LayoutEditParameter parameter, double value) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    void* address = FieldAddress(config, field);
    switch (field.valueKind) {
        case RuntimeConfigFieldValueKind::Int:
            *reinterpret_cast<int*>(address) = ClampIntegerLayoutEditValue(field, static_cast<int>(std::lround(value)));
            return true;
        case RuntimeConfigFieldValueKind::Double:
            *reinterpret_cast<double*>(address) = ClampDoubleLayoutEditValue(field, value);
            return true;
        case RuntimeConfigFieldValueKind::FontSpec: {
            UiFontConfig& font = *reinterpret_cast<UiFontConfig*>(address);
            font.size = static_cast<int>(std::lround(value));
            ClampFontLayoutEditValue(font);
            return true;
        }
        case RuntimeConfigFieldValueKind::String:
        case RuntimeConfigFieldValueKind::StringList:
        case RuntimeConfigFieldValueKind::LogicalPoint:
        case RuntimeConfigFieldValueKind::LogicalSize:
        case RuntimeConfigFieldValueKind::HexColor:
        case RuntimeConfigFieldValueKind::LayoutExpression:
            return false;
    }
    return false;
}

bool ApplyLayoutEditParameterColorValue(AppConfig& config, LayoutEditParameter parameter, unsigned int value) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    if (field.valueKind != RuntimeConfigFieldValueKind::HexColor) {
        return false;
    }
    *reinterpret_cast<ColorConfig*>(FieldAddress(config, field)) = ColorConfig::FromRgba(value);
    return true;
}

bool ApplyLayoutEditParameterFontValue(AppConfig& config, LayoutEditParameter parameter, const UiFontConfig& value) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    if (field.valueKind != RuntimeConfigFieldValueKind::FontSpec) {
        return false;
    }
    UiFontConfig font = value;
    ClampFontLayoutEditValue(font);
    *reinterpret_cast<UiFontConfig*>(FieldAddress(config, field)) = std::move(font);
    return true;
}
