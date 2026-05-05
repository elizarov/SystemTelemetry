#include "layout_model/layout_edit_parameter_metadata.h"

#include <cstdint>
#include <tuple>
#include <type_traits>

#include "config/config.h"

namespace {

using Parameter = LayoutEditParameter;

template <typename Value> RuntimeConfigFieldValueKind RuntimeFieldValueKindFor() {
    if constexpr (std::is_same_v<Value, int>) {
        return RuntimeConfigFieldValueKind::Int;
    } else if constexpr (std::is_same_v<Value, double>) {
        return RuntimeConfigFieldValueKind::Double;
    } else if constexpr (std::is_same_v<Value, ColorConfig>) {
        return RuntimeConfigFieldValueKind::HexColor;
    } else if constexpr (std::is_same_v<Value, UiFontConfig>) {
        return RuntimeConfigFieldValueKind::FontSpec;
    } else {
        return RuntimeConfigFieldValueKind::String;
    }
}

template <typename Policy> RuntimeConfigFieldPolicy RuntimeFieldPolicyFor() {
    if constexpr (std::is_same_v<Policy, configschema::PositiveIntPolicy>) {
        return RuntimeConfigFieldPolicy::PositiveInt;
    } else if constexpr (std::is_same_v<Policy, configschema::NonNegativeIntPolicy>) {
        return RuntimeConfigFieldPolicy::NonNegativeInt;
    } else if constexpr (std::is_same_v<Policy, configschema::FontSizePolicy>) {
        return RuntimeConfigFieldPolicy::FontSize;
    } else if constexpr (std::is_same_v<Policy, configschema::DegreesPolicy>) {
        return RuntimeConfigFieldPolicy::Degrees;
    } else {
        return RuntimeConfigFieldPolicy::None;
    }
}

template <typename Owner, typename MemberType, MemberType Owner::* Member> std::uint32_t MemberOffset() {
    // Size: offsets are static facts; constructing AppConfig pulls string/vector initialization into metadata.
    constexpr std::uintptr_t kOffsetBaseAddress = 0x10000u;
    const auto* owner = reinterpret_cast<const Owner*>(kOffsetBaseAddress);
    const auto* member = &(owner->*Member);
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(member) - kOffsetBaseAddress);
}

template <typename Owner, typename Section, typename Section::owner_type Owner::* Member>
std::uint32_t BindingOffset(configschema::StructuredBindingDescriptor<Owner, Section, Member>*) {
    return MemberOffset<Owner, typename Section::owner_type, Member>();
}

template <typename Owner, typename NestedOwner, NestedOwner Owner::* Member>
std::uint32_t BindingOffset(configschema::RecursiveStructuredBindingDescriptor<Owner, NestedOwner, Member>*) {
    return MemberOffset<Owner, NestedOwner, Member>();
}

template <typename Binding> std::uint32_t BindingOffset() {
    return BindingOffset(static_cast<Binding*>(nullptr));
}

template <typename... Bindings> std::uint32_t BindingPathOffset(std::tuple<Bindings...>) {
    return (0u + ... + BindingOffset<Bindings>());
}

template <typename Field> std::uint32_t FieldOffset() {
    return MemberOffset<typename Field::owner_type, typename Field::field_type, Field::member>();
}

template <typename Root, typename Field, typename... Bindings>
std::uint32_t RootFieldOffset(configschema::RootFieldLens<Root, Field, Bindings...>*) {
    return BindingPathOffset(std::tuple<Bindings...>{}) + FieldOffset<Field>();
}

template <typename Meta> std::uint32_t RootFieldOffset() {
    return RootFieldOffset(static_cast<typename Meta::resolved_type*>(nullptr));
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

#define CASEDASH_DECLARE_LAYOUT_EDIT_PARAMETER_METADATA(name, meta)                                                    \
    {meta::section_name,                                                                                               \
        meta::parameter_name,                                                                                          \
        meta::traits_type::value_format,                                                                               \
        RuntimeFieldValueKindFor<typename meta::value_type>(),                                                         \
        RuntimeFieldPolicyFor<typename meta::traits_type::policy_tag>(),                                               \
        RootFieldOffset<meta>()},

const LayoutEditConfigFieldMetadata kParameterFields[] = {
    CASEDASH_LAYOUT_EDIT_PARAMETER_ITEMS(CASEDASH_DECLARE_LAYOUT_EDIT_PARAMETER_METADATA)};

#undef CASEDASH_DECLARE_LAYOUT_EDIT_PARAMETER_METADATA

#define CASEDASH_DECLARE_LAYOUT_EDIT_PARAMETER_INFO(name, meta) {Parameter::name},

const LayoutEditParameterInfo kParameterInfo[] = {
    CASEDASH_LAYOUT_EDIT_PARAMETER_ITEMS(CASEDASH_DECLARE_LAYOUT_EDIT_PARAMETER_INFO)};

#undef CASEDASH_DECLARE_LAYOUT_EDIT_PARAMETER_INFO

constexpr size_t kParameterInfoCount = sizeof(kParameterInfo) / sizeof(kParameterInfo[0]);
static_assert(kParameterInfoCount == sizeof(kParameterFields) / sizeof(kParameterFields[0]));
static_assert(kParameterInfoCount == static_cast<size_t>(Parameter::Count));

}  // namespace

const LayoutEditParameterInfo& GetLayoutEditParameterInfo(LayoutEditParameter parameter) {
    return kParameterInfo[static_cast<size_t>(parameter)];
}

const LayoutEditConfigFieldMetadata& GetLayoutEditConfigFieldMetadata(LayoutEditParameter parameter) {
    return kParameterFields[static_cast<size_t>(parameter)];
}

bool IsFontLayoutEditParameter(LayoutEditParameter parameter) {
    return GetLayoutEditConfigFieldMetadata(parameter).valueKind == RuntimeConfigFieldValueKind::FontSpec;
}

std::string GetLayoutEditParameterDisplayName(LayoutEditParameter parameter) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    std::string label = HumanizeSnakeCase(field.parameterName);
    if (IsFontLayoutEditParameter(parameter)) {
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
