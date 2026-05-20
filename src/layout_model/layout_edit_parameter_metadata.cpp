#include "layout_model/layout_edit_parameter_metadata.h"

#include <cstddef>

#include "util/text_format.h"

namespace {

using Parameter = LayoutEditParameter;

std::string HumanizeSnakeCase(std::string_view value) {
    std::string text;
    text.reserve(value.size());
    for (const char ch : value) {
        if (ch == '_') {
            if (!text.empty()) {
                text.push_back(' ');
            }
            continue;
        }
        text.push_back(ch);
    }
    return text;
}

}  // namespace

LayoutEditParameterInfo GetLayoutEditParameterInfo(LayoutEditParameter parameter) {
    return LayoutEditParameterInfo{parameter};
}

const LayoutEditConfigFieldMetadata& GetLayoutEditConfigFieldMetadata(LayoutEditParameter parameter) {
    return LayoutEditConfigFieldMetadataDescriptors()[static_cast<size_t>(parameter)];
}

bool IsFontLayoutEditParameter(LayoutEditParameter parameter) {
    return GetLayoutEditConfigFieldMetadata(parameter).valueKind == RuntimeConfigFieldValueKind::FontSpec;
}

std::string GetLayoutEditParameterDisplayName(LayoutEditParameter parameter) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    std::string label = HumanizeSnakeCase(field.parameterName);
    if (IsFontLayoutEditParameter(parameter)) {
        label = FormatText("%s font", label.c_str());
    }
    return label;
}

std::optional<LayoutEditParameter> FindLayoutEditParameterByConfigField(
    std::string_view sectionName, std::string_view parameterName) {
    const auto fields = LayoutEditConfigFieldMetadataDescriptors();
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto parameter = static_cast<LayoutEditParameter>(i);
        const auto& field = fields[i];
        if (std::string_view(field.sectionName) == sectionName &&
            std::string_view(field.parameterName) == parameterName) {
            return parameter;
        }
    }
    return std::nullopt;
}

std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(LayoutEditParameter parameter) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    LayoutEditTooltipDescriptor descriptor;
    descriptor.sectionName = std::string(field.sectionName);
    descriptor.memberName = std::string(field.parameterName);
    descriptor.configKey = FormatText("config.%s.%s", descriptor.sectionName.c_str(), descriptor.memberName.c_str());
    descriptor.valueFormat = field.valueFormat;
    return descriptor;
}
