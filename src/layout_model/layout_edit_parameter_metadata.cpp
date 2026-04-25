#include "layout_model/layout_edit_parameter_metadata.h"

#include <cmath>
#include <type_traits>

namespace {

using Parameter = LayoutEditParameter;

template <typename Meta> bool ApplyFieldEdit(AppConfig& config, double value) {
    using PolicyTag = typename Meta::traits_type::policy_tag;
    if constexpr (std::is_same_v<PolicyTag, configschema::FontSizePolicy>) {
        UiFontConfig font = Meta::RawGet(config);
        font.size = static_cast<int>(std::lround(value));
        Meta::Get(config) = std::move(font);
        return true;
    } else {
        if constexpr (std::is_same_v<typename Meta::value_type, int>) {
            Meta::Get(config) = static_cast<int>(std::lround(value));
        } else {
            Meta::Get(config) = static_cast<typename Meta::value_type>(value);
        }
        return true;
    }
}

template <typename Meta> std::optional<const UiFontConfig*> FindFontFieldValue(const AppConfig& config) {
    if constexpr (std::is_same_v<typename Meta::value_type, UiFontConfig>) {
        return &Meta::RawGet(config);
    } else {
        return std::nullopt;
    }
}

template <typename Meta> std::optional<double> FindNumericFieldValue(const AppConfig& config) {
    if constexpr (std::is_same_v<typename Meta::value_type, int>) {
        return static_cast<double>(Meta::RawGet(config));
    } else if constexpr (std::is_same_v<typename Meta::value_type, double>) {
        return Meta::RawGet(config);
    } else if constexpr (std::is_same_v<typename Meta::value_type, UiFontConfig>) {
        return static_cast<double>(Meta::RawGet(config).size);
    } else {
        return std::nullopt;
    }
}

template <typename Meta> std::optional<unsigned int> FindColorFieldValue(const AppConfig& config) {
    if constexpr (std::is_same_v<typename Meta::value_type, ColorConfig>) {
        return Meta::RawGet(config).ToRgba();
    } else {
        return std::nullopt;
    }
}

template <typename Meta> constexpr auto NumericApplyFieldEditFn() {
    if constexpr (std::is_same_v<typename Meta::value_type, int> || std::is_same_v<typename Meta::value_type, double> ||
                  std::is_same_v<typename Meta::value_type, UiFontConfig>) {
        return &ApplyFieldEdit<Meta>;
    } else {
        return static_cast<bool (*)(AppConfig&, double)>(nullptr);
    }
}

template <typename Meta> bool ApplyFontFieldEdit(AppConfig& config, const UiFontConfig& value) {
    if constexpr (std::is_same_v<typename Meta::value_type, UiFontConfig>) {
        Meta::Set(config, value);
        return true;
    } else {
        (void)config;
        (void)value;
        return false;
    }
}

template <typename Meta> bool ApplyColorFieldEdit(AppConfig& config, unsigned int value) {
    if constexpr (std::is_same_v<typename Meta::value_type, ColorConfig>) {
        Meta::Set(config, ColorConfig::FromRgba(value));
        return true;
    } else {
        (void)config;
        (void)value;
        return false;
    }
}

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

template <typename Meta> const LayoutEditConfigFieldMetadata& GetFieldMetadata() {
    static const LayoutEditConfigFieldMetadata metadata{
        Meta::section_name,
        Meta::parameter_name,
        Meta::traits_type::value_format,
        std::is_same_v<typename Meta::value_type, UiFontConfig>,
        &FindNumericFieldValue<Meta>,
        &FindColorFieldValue<Meta>,
        NumericApplyFieldEditFn<Meta>(),
        &ApplyColorFieldEdit<Meta>,
        &ApplyFontFieldEdit<Meta>,
        &FindFontFieldValue<Meta>,
    };
    return metadata;
}

#define SYSTEM_TELEMETRY_DECLARE_LAYOUT_EDIT_PARAMETER_INFO(name, meta) {Parameter::name, &GetFieldMetadata<meta>()},

const LayoutEditParameterInfo kParameterInfo[] = {
    SYSTEM_TELEMETRY_LAYOUT_EDIT_PARAMETER_ITEMS(SYSTEM_TELEMETRY_DECLARE_LAYOUT_EDIT_PARAMETER_INFO)};

#undef SYSTEM_TELEMETRY_DECLARE_LAYOUT_EDIT_PARAMETER_INFO

constexpr size_t kParameterInfoCount = sizeof(kParameterInfo) / sizeof(kParameterInfo[0]);
static_assert(kParameterInfoCount == static_cast<size_t>(Parameter::Count));

}  // namespace

const LayoutEditParameterInfo& GetLayoutEditParameterInfo(LayoutEditParameter parameter) {
    return kParameterInfo[static_cast<size_t>(parameter)];
}

const LayoutEditConfigFieldMetadata& GetLayoutEditConfigFieldMetadata(LayoutEditParameter parameter) {
    return *GetLayoutEditParameterInfo(parameter).field;
}

bool IsFontLayoutEditParameter(LayoutEditParameter parameter) {
    return GetLayoutEditConfigFieldMetadata(parameter).isFont;
}

std::string GetLayoutEditParameterDisplayName(LayoutEditParameter parameter) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    std::string label = HumanizeSnakeCase(field.parameterName);
    if (field.isFont) {
        label += " font";
    }
    return label;
}

std::optional<LayoutEditParameter> FindLayoutEditParameterByConfigField(
    std::string_view sectionName, std::string_view parameterName) {
    for (size_t i = 0; i < kParameterInfoCount; ++i) {
        const auto parameter = static_cast<LayoutEditParameter>(i);
        const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
        if (field.sectionName == sectionName && field.parameterName == parameterName) {
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
    descriptor.configKey = "config." + descriptor.sectionName + "." + descriptor.memberName;
    descriptor.valueFormat = field.valueFormat;
    return descriptor;
}
