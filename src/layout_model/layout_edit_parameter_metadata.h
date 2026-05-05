#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "config/config_runtime_fields.h"
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
    RuntimeConfigFieldValueKind valueKind = RuntimeConfigFieldValueKind::Int;
    RuntimeConfigFieldPolicy policy = RuntimeConfigFieldPolicy::None;
    std::uint32_t rootOffset = 0;
};

struct LayoutEditParameterInfo {
    LayoutEditParameter parameter = LayoutEditParameter::MetricListLabelWidth;
};

const LayoutEditParameterInfo& GetLayoutEditParameterInfo(LayoutEditParameter parameter);
const LayoutEditConfigFieldMetadata& GetLayoutEditConfigFieldMetadata(LayoutEditParameter parameter);
bool IsFontLayoutEditParameter(LayoutEditParameter parameter);
std::string GetLayoutEditParameterDisplayName(LayoutEditParameter parameter);
std::optional<LayoutEditParameter> FindLayoutEditParameterByConfigField(
    std::string_view sectionName, std::string_view parameterName);
std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(LayoutEditParameter parameter);
