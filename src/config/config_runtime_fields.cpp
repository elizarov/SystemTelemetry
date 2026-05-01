#include "config/config_runtime_fields.h"

#include <array>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <type_traits>

#include "config/config_parser.h"
#include "config/config_writer.h"
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
    std::string text = Trim(value);
    if (!text.empty() && text.front() == '#') {
        text.erase(text.begin());
    }
    if (text.size() != 8) {
        return fallback;
    }
    for (unsigned char ch : text) {
        if (!std::isxdigit(ch)) {
            return fallback;
        }
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 16);
    if (errno != 0 || end == text.c_str() || *end != '\0' || parsed > 0xFFFFFFFFul) {
        return fallback;
    }
    return ColorConfig::FromRgba(static_cast<unsigned int>(parsed));
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

std::string FormatLogicalSize(const LogicalSizeConfig& size) {
    return std::to_string(size.width) + "," + std::to_string(size.height);
}

std::string FormatFontSpec(const UiFontConfig& font) {
    return font.face + "," + std::to_string(font.size) + "," + std::to_string(font.weight);
}

template <typename Codec, typename Value> void DecodeConfigValue(Value& target, const std::string& value);

template <> void DecodeConfigValue<configschema::IntCodec, int>(int& target, const std::string& value) {
    target = ParseIntOrDefault(value, target);
}

template <> void DecodeConfigValue<configschema::DoubleCodec, double>(double& target, const std::string& value) {
    target = ParseDoubleOrDefault(value, target);
}

template <>
void DecodeConfigValue<configschema::StringCodec, std::string>(std::string& target, const std::string& value) {
    target = value;
}

template <>
void DecodeConfigValue<configschema::LogicalPointCodec, LogicalPointConfig>(
    LogicalPointConfig& target, const std::string& value) {
    ParseIntPair(value, target.x, target.y);
}

template <>
void DecodeConfigValue<configschema::LogicalSizeCodec, LogicalSizeConfig>(
    LogicalSizeConfig& target, const std::string& value) {
    ParseIntPair(value, target.width, target.height);
}

template <>
void DecodeConfigValue<configschema::HexColorCodec, ColorConfig>(ColorConfig& target, const std::string& value) {
    target = ParseHexColorOrDefault(value, target);
}

template <>
void DecodeConfigValue<configschema::FontSpecCodec, UiFontConfig>(UiFontConfig& target, const std::string& value) {
    ParseFontSpec(target, value);
}

template <>
void DecodeConfigValue<configschema::StringCodec, std::vector<std::string>>(
    std::vector<std::string>& target, const std::string& value) {
    target = SplitTrimmed(value, ',');
}

template <>
void DecodeConfigValue<configschema::LayoutExpressionCodec, LayoutNodeConfig>(
    LayoutNodeConfig& target, const std::string& value) {
    LayoutNodeConfig parsed;
    if (ParseLayoutExpression(value, parsed)) {
        target = std::move(parsed);
    }
}

template <typename Codec, typename Value> std::string EncodeConfigValue(const Value& value);

template <> std::string EncodeConfigValue<configschema::IntCodec, int>(const int& value) {
    return std::to_string(value);
}

template <> std::string EncodeConfigValue<configschema::DoubleCodec, double>(const double& value) {
    return FormatDoubleGeneral(value);
}

template <> std::string EncodeConfigValue<configschema::StringCodec, std::string>(const std::string& value) {
    return value;
}

template <>
std::string EncodeConfigValue<configschema::LogicalPointCodec, LogicalPointConfig>(const LogicalPointConfig& value) {
    return std::to_string(value.x) + "," + std::to_string(value.y);
}

template <>
std::string EncodeConfigValue<configschema::LogicalSizeCodec, LogicalSizeConfig>(const LogicalSizeConfig& value) {
    return FormatLogicalSize(value);
}

template <> std::string EncodeConfigValue<configschema::HexColorCodec, ColorConfig>(const ColorConfig& value) {
    return FormatHexColor(value);
}

template <> std::string EncodeConfigValue<configschema::FontSpecCodec, UiFontConfig>(const UiFontConfig& value) {
    return FormatFontSpec(value);
}

template <>
std::string EncodeConfigValue<configschema::StringCodec, std::vector<std::string>>(
    const std::vector<std::string>& value) {
    std::string encoded;
    for (size_t i = 0; i < value.size(); ++i) {
        if (i > 0) {
            encoded += ",";
        }
        encoded += value[i];
    }
    return encoded;
}

template <>
std::string EncodeConfigValue<configschema::LayoutExpressionCodec, LayoutNodeConfig>(const LayoutNodeConfig& value) {
    return FormatLayoutExpression(value);
}

template <typename Field> void DecodeRuntimeField(void* owner, const std::string& value) {
    auto& typedOwner = *static_cast<typename Field::owner_type*>(owner);
    typename Field::field_type decoded = Field::RawGet(typedOwner);
    DecodeConfigValue<typename Field::codec_type>(decoded, value);
    Field::Set(typedOwner, std::move(decoded));
}

template <typename Field> std::string EncodeRuntimeField(const void* owner) {
    const auto& typedOwner = *static_cast<const typename Field::owner_type*>(owner);
    return EncodeConfigValue<typename Field::codec_type>(Field::RawGet(typedOwner));
}

template <typename Field> bool RuntimeFieldEquals(const void* owner, const void* compareOwner) {
    const auto& typedOwner = *static_cast<const typename Field::owner_type*>(owner);
    const auto& typedCompareOwner = *static_cast<const typename Field::owner_type*>(compareOwner);
    return Field::RawGet(typedOwner) == Field::RawGet(typedCompareOwner);
}

template <typename... Field> constexpr auto MakeRuntimeFieldDescriptors(std::tuple<Field...>) {
    return std::array<RuntimeConfigFieldDescriptor, sizeof...(Field)>{RuntimeConfigFieldDescriptor{
        Field::key.view(), &DecodeRuntimeField<Field>, &EncodeRuntimeField<Field>, &RuntimeFieldEquals<Field>}...};
}

}  // namespace

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

#define SYSTEMTELEMETRY_DEFINE_RUNTIME_FIELDS(section_type)                                                            \
    template <> std::span<const RuntimeConfigFieldDescriptor> RuntimeConfigFieldDescriptors<section_type>() {          \
        static constexpr auto fields = MakeRuntimeFieldDescriptors(typename section_type::fields_type{});              \
        return fields;                                                                                                 \
    }

SYSTEMTELEMETRY_CONFIG_FIELD_SECTIONS(SYSTEMTELEMETRY_DEFINE_RUNTIME_FIELDS)

#undef SYSTEMTELEMETRY_DEFINE_RUNTIME_FIELDS
