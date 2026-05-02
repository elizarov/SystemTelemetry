#include "config/config_runtime_fields.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <type_traits>

#include "config/config_parser.h"
#include "util/strings.h"

namespace {

int ParseIntOrDefault(const std::string& value, int fallback) {
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

double ParseDoubleOrDefault(const std::string& value, double fallback) {
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (errno != 0 || end == value.c_str() || *end != '\0') {
        return fallback;
    }
    return parsed;
}

ColorConfig ParseHexColorOrDefault(const std::string& value, ColorConfig fallback) {
    const std::string expression = Trim(value);
    std::string text = expression;
    if (!text.empty() && text.front() == '#') {
        text.erase(text.begin());
    }
    if (text.size() != 8) {
        if (!expression.empty()) {
            fallback.expression = expression;
        }
        return fallback;
    }
    for (unsigned char ch : text) {
        if (!std::isxdigit(ch)) {
            if (!expression.empty()) {
                fallback.expression = expression;
            }
            return fallback;
        }
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 16);
    if (errno != 0 || end == text.c_str() || *end != '\0' || parsed > 0xFFFFFFFFul) {
        return fallback;
    }
    ColorConfig color = ColorConfig::FromRgba(static_cast<unsigned int>(parsed));
    color.expression = FormatHexColorText(color.ToRgba());
    return color;
}

bool ParseIntPair(const std::string& value, int& first, int& second) {
    const std::vector<std::string> parts = SplitTrimmed(value, ',');
    if (parts.size() != 2) {
        return false;
    }
    first = ParseIntOrDefault(parts[0], first);
    second = ParseIntOrDefault(parts[1], second);
    return true;
}

void ParseFontSpec(UiFontConfig& font, const std::string& value) {
    const std::vector<std::string> parts = SplitTrimmed(value, ',');
    if (parts.size() != 3) {
        return;
    }
    font.face = parts[0];
    font.size = ParseIntOrDefault(parts[1], font.size);
    font.weight = ParseIntOrDefault(parts[2], font.weight);
}

std::string FormatHexColor(ColorConfig color) {
    return FormatHexColorText(color.ToRgba());
}

std::string FormatColorConfigValue(const ColorConfig& color) {
    return !color.expression.empty() ? color.expression : FormatHexColor(color);
}

bool ColorConfigPersistedValueEquals(const ColorConfig& color, const ColorConfig& compareColor) {
    return FormatColorConfigValue(color) == FormatColorConfigValue(compareColor);
}

std::string FormatLogicalSize(const LogicalSizeConfig& size) {
    return std::to_string(size.width) + "," + std::to_string(size.height);
}

std::string FormatFontSpec(const UiFontConfig& font) {
    return font.face + "," + std::to_string(font.size) + "," + std::to_string(font.weight);
}

template <typename Value> RuntimeConfigFieldValueKind RuntimeFieldValueKindFor() {
    if constexpr (std::is_same_v<Value, int>) {
        return RuntimeConfigFieldValueKind::Int;
    } else if constexpr (std::is_same_v<Value, double>) {
        return RuntimeConfigFieldValueKind::Double;
    } else if constexpr (std::is_same_v<Value, std::string>) {
        return RuntimeConfigFieldValueKind::String;
    } else if constexpr (std::is_same_v<Value, std::vector<std::string>>) {
        return RuntimeConfigFieldValueKind::StringList;
    } else if constexpr (std::is_same_v<Value, LogicalPointConfig>) {
        return RuntimeConfigFieldValueKind::LogicalPoint;
    } else if constexpr (std::is_same_v<Value, LogicalSizeConfig>) {
        return RuntimeConfigFieldValueKind::LogicalSize;
    } else if constexpr (std::is_same_v<Value, ColorConfig>) {
        return RuntimeConfigFieldValueKind::HexColor;
    } else if constexpr (std::is_same_v<Value, UiFontConfig>) {
        return RuntimeConfigFieldValueKind::FontSpec;
    } else if constexpr (std::is_same_v<Value, LayoutNodeConfig>) {
        return RuntimeConfigFieldValueKind::LayoutExpression;
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

template <typename Field> std::uint32_t RuntimeFieldOffset() {
    typename Field::owner_type owner{};
    const auto* ownerBytes = reinterpret_cast<const char*>(&owner);
    const auto* fieldBytes = reinterpret_cast<const char*>(&Field::RawGet(owner));
    return static_cast<std::uint32_t>(fieldBytes - ownerBytes);
}

template <typename Field> RuntimeConfigFieldDescriptor MakeRuntimeFieldDescriptor() {
    using Policy = typename Field::layout_edit_traits_type::policy_tag;
    return RuntimeConfigFieldDescriptor{Field::key.view(),
        RuntimeFieldOffset<Field>(),
        RuntimeFieldValueKindFor<typename Field::field_type>(),
        RuntimeFieldPolicyFor<Policy>()};
}

template <typename... Field> auto MakeRuntimeFieldDescriptors(std::tuple<Field...>) {
    return std::array<RuntimeConfigFieldDescriptor, sizeof...(Field)>{MakeRuntimeFieldDescriptor<Field>()...};
}

char* FieldAddress(void* owner, const RuntimeConfigFieldDescriptor& field) {
    return static_cast<char*>(owner) + field.offset;
}

const char* FieldAddress(const void* owner, const RuntimeConfigFieldDescriptor& field) {
    return static_cast<const char*>(owner) + field.offset;
}

void ClampDecodedValue(const RuntimeConfigFieldDescriptor& field, void* address) {
    switch (field.policy) {
        case RuntimeConfigFieldPolicy::PositiveInt:
            *reinterpret_cast<int*>(address) = (std::max)(1, *reinterpret_cast<int*>(address));
            break;
        case RuntimeConfigFieldPolicy::NonNegativeInt:
            *reinterpret_cast<int*>(address) = (std::max)(0, *reinterpret_cast<int*>(address));
            break;
        case RuntimeConfigFieldPolicy::FontSize: {
            UiFontConfig& font = *reinterpret_cast<UiFontConfig*>(address);
            font.size = (std::max)(1, font.size);
            font.weight = (std::max)(1, font.weight);
            break;
        }
        case RuntimeConfigFieldPolicy::Degrees:
            *reinterpret_cast<double*>(address) = std::clamp(*reinterpret_cast<double*>(address), 0.0, 360.0);
            break;
        case RuntimeConfigFieldPolicy::None:
            break;
    }
}

}  // namespace

void DecodeRuntimeConfigField(const RuntimeConfigFieldDescriptor& field, void* owner, const std::string& value) {
    void* address = FieldAddress(owner, field);
    switch (field.kind) {
        case RuntimeConfigFieldValueKind::Int:
            *reinterpret_cast<int*>(address) = ParseIntOrDefault(value, *reinterpret_cast<int*>(address));
            break;
        case RuntimeConfigFieldValueKind::Double:
            *reinterpret_cast<double*>(address) = ParseDoubleOrDefault(value, *reinterpret_cast<double*>(address));
            break;
        case RuntimeConfigFieldValueKind::String:
            *reinterpret_cast<std::string*>(address) = value;
            break;
        case RuntimeConfigFieldValueKind::StringList:
            *reinterpret_cast<std::vector<std::string>*>(address) = SplitTrimmed(value, ',');
            break;
        case RuntimeConfigFieldValueKind::LogicalPoint: {
            LogicalPointConfig& point = *reinterpret_cast<LogicalPointConfig*>(address);
            ParseIntPair(value, point.x, point.y);
            break;
        }
        case RuntimeConfigFieldValueKind::LogicalSize: {
            LogicalSizeConfig& size = *reinterpret_cast<LogicalSizeConfig*>(address);
            ParseIntPair(value, size.width, size.height);
            break;
        }
        case RuntimeConfigFieldValueKind::HexColor:
            *reinterpret_cast<ColorConfig*>(address) =
                ParseHexColorOrDefault(value, *reinterpret_cast<ColorConfig*>(address));
            break;
        case RuntimeConfigFieldValueKind::FontSpec:
            ParseFontSpec(*reinterpret_cast<UiFontConfig*>(address), value);
            break;
        case RuntimeConfigFieldValueKind::LayoutExpression: {
            LayoutNodeConfig parsed;
            if (ParseLayoutExpression(value, parsed)) {
                *reinterpret_cast<LayoutNodeConfig*>(address) = std::move(parsed);
            }
            break;
        }
    }
    ClampDecodedValue(field, address);
}

std::string EncodeRuntimeConfigField(const RuntimeConfigFieldDescriptor& field, const void* owner) {
    const void* address = FieldAddress(owner, field);
    switch (field.kind) {
        case RuntimeConfigFieldValueKind::Int:
            return std::to_string(*reinterpret_cast<const int*>(address));
        case RuntimeConfigFieldValueKind::Double:
            return FormatDoubleGeneral(*reinterpret_cast<const double*>(address));
        case RuntimeConfigFieldValueKind::String:
            return *reinterpret_cast<const std::string*>(address);
        case RuntimeConfigFieldValueKind::StringList: {
            const auto& values = *reinterpret_cast<const std::vector<std::string>*>(address);
            std::string encoded;
            for (size_t i = 0; i < values.size(); ++i) {
                if (i > 0) {
                    encoded += ",";
                }
                encoded += values[i];
            }
            return encoded;
        }
        case RuntimeConfigFieldValueKind::LogicalPoint: {
            const LogicalPointConfig& point = *reinterpret_cast<const LogicalPointConfig*>(address);
            return std::to_string(point.x) + "," + std::to_string(point.y);
        }
        case RuntimeConfigFieldValueKind::LogicalSize:
            return FormatLogicalSize(*reinterpret_cast<const LogicalSizeConfig*>(address));
        case RuntimeConfigFieldValueKind::HexColor:
            return FormatColorConfigValue(*reinterpret_cast<const ColorConfig*>(address));
        case RuntimeConfigFieldValueKind::FontSpec:
            return FormatFontSpec(*reinterpret_cast<const UiFontConfig*>(address));
        case RuntimeConfigFieldValueKind::LayoutExpression:
            return FormatLayoutExpression(*reinterpret_cast<const LayoutNodeConfig*>(address));
    }
    return {};
}

bool RuntimeConfigFieldEquals(const RuntimeConfigFieldDescriptor& field, const void* owner, const void* compareOwner) {
    const void* address = FieldAddress(owner, field);
    const void* compareAddress = FieldAddress(compareOwner, field);
    switch (field.kind) {
        case RuntimeConfigFieldValueKind::Int:
            return *reinterpret_cast<const int*>(address) == *reinterpret_cast<const int*>(compareAddress);
        case RuntimeConfigFieldValueKind::Double:
            return *reinterpret_cast<const double*>(address) == *reinterpret_cast<const double*>(compareAddress);
        case RuntimeConfigFieldValueKind::String:
            return *reinterpret_cast<const std::string*>(address) ==
                   *reinterpret_cast<const std::string*>(compareAddress);
        case RuntimeConfigFieldValueKind::StringList:
            return *reinterpret_cast<const std::vector<std::string>*>(address) ==
                   *reinterpret_cast<const std::vector<std::string>*>(compareAddress);
        case RuntimeConfigFieldValueKind::LogicalPoint:
            return *reinterpret_cast<const LogicalPointConfig*>(address) ==
                   *reinterpret_cast<const LogicalPointConfig*>(compareAddress);
        case RuntimeConfigFieldValueKind::LogicalSize:
            return *reinterpret_cast<const LogicalSizeConfig*>(address) ==
                   *reinterpret_cast<const LogicalSizeConfig*>(compareAddress);
        case RuntimeConfigFieldValueKind::HexColor:
            return ColorConfigPersistedValueEquals(
                *reinterpret_cast<const ColorConfig*>(address), *reinterpret_cast<const ColorConfig*>(compareAddress));
        case RuntimeConfigFieldValueKind::FontSpec:
            return *reinterpret_cast<const UiFontConfig*>(address) ==
                   *reinterpret_cast<const UiFontConfig*>(compareAddress);
        case RuntimeConfigFieldValueKind::LayoutExpression:
            return *reinterpret_cast<const LayoutNodeConfig*>(address) ==
                   *reinterpret_cast<const LayoutNodeConfig*>(compareAddress);
    }
    return false;
}

std::string FormatLayoutExpression(const LayoutNodeConfig& node) {
    std::string text = node.name;
    if (node.weight != 1) {
        text += ":" + std::to_string(node.weight);
    }
    if (!node.children.empty()) {
        text += "(";
        for (size_t i = 0; i < node.children.size(); ++i) {
            if (i > 0) {
                text += ",";
            }
            text += FormatLayoutExpression(node.children[i]);
        }
        text += ")";
    } else if (!node.parameter.empty()) {
        text += "(" + node.parameter + ")";
    }
    return text;
}

#define CASEDASH_DEFINE_RUNTIME_FIELDS(section_type)                                                                   \
    template <> std::span<const RuntimeConfigFieldDescriptor> RuntimeConfigFieldDescriptors<section_type>() {          \
        static const auto fields = MakeRuntimeFieldDescriptors(typename section_type::fields_type{});                  \
        return fields;                                                                                                 \
    }

CASEDASH_CONFIG_FIELD_SECTIONS(CASEDASH_DEFINE_RUNTIME_FIELDS)

#undef CASEDASH_DEFINE_RUNTIME_FIELDS
