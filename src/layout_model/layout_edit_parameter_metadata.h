#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "config/config_runtime_fields.h"
#include "config/config_schema.h"
#include "widget/layout_edit_parameter_id.h"

struct LayoutEditTooltipDescriptor {
    std::string configKey;
    std::string sectionName;
    std::string memberName;
    configschema::ValueFormat valueFormat = configschema::ValueFormat::Integer;
};

struct LayoutEditConfigFieldMetadata {
    const char* sectionName = "";
    const char* parameterName = "";
    configschema::ValueFormat valueFormat = configschema::ValueFormat::Integer;
    RuntimeConfigFieldValueKind valueKind = RuntimeConfigFieldValueKind::Int;
    RuntimeConfigFieldPolicy policy = RuntimeConfigFieldPolicy::None;
    std::uint32_t rootOffset = 0;
};

struct LayoutEditParameterInfo {
    LayoutEditParameter parameter = LayoutEditParameter::MetricListLabelWidth;
};

// Implemented by generated file build/cmake/generated/layout_model/layout_edit_parameter_metadata.generated.cpp.
std::span<const LayoutEditConfigFieldMetadata> LayoutEditConfigFieldMetadataDescriptors();

LayoutEditParameterInfo GetLayoutEditParameterInfo(LayoutEditParameter parameter);
const LayoutEditConfigFieldMetadata& GetLayoutEditConfigFieldMetadata(LayoutEditParameter parameter);
bool IsFontLayoutEditParameter(LayoutEditParameter parameter);
std::string GetLayoutEditParameterDisplayName(LayoutEditParameter parameter);
std::optional<LayoutEditParameter> FindLayoutEditParameterByConfigField(
    std::string_view sectionName, std::string_view parameterName);
std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(LayoutEditParameter parameter);
