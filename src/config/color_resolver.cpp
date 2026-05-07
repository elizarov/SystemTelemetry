#include "config/color_resolver.h"

#include <optional>
#include <string_view>

#include "config/color_expression.h"
#include "config/color_format.h"
#include "config/color_math.h"
#include "config/config_runtime_fields.h"
#include "util/function_ref.h"
#include "util/strings.h"

namespace {

std::string FormatHexColor(ColorConfig color) {
    return FormatRgbaColorText(color.ToRgba());
}

std::optional<unsigned int> HexNibble(char ch) {
    if (ch >= '0' && ch <= '9') {
        return static_cast<unsigned int>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return static_cast<unsigned int>(ch - 'a' + 10);
    }
    if (ch >= 'A' && ch <= 'F') {
        return static_cast<unsigned int>(ch - 'A' + 10);
    }
    return std::nullopt;
}

ColorConfig ParseHexColorOrDefault(const std::string& value, ColorConfig fallback) {
    std::string text = Trim(value);
    if (!text.empty() && text.front() == '#') {
        text.erase(text.begin());
    }
    if (text.size() != 8) {
        return fallback;
    }
    unsigned int rgba = 0;
    for (char ch : text) {
        const std::optional<unsigned int> nibble = HexNibble(ch);
        if (!nibble.has_value()) {
            return fallback;
        }
        rgba = (rgba << 4) | *nibble;
    }
    return ColorConfig::FromRgba(rgba);
}

ColorBytes ToColorBytes(ColorConfig color) {
    return ColorBytesFromRgba(color.ToRgba());
}

ColorConfig FromColorBytes(ColorBytes color, const std::string& expression) {
    ColorConfig result = ColorConfig::FromRgba(RgbaFromColorBytes(color));
    result.expression = expression;
    return result;
}

const ThemeConfig* FindTheme(const LayoutConfig& layout, std::string_view name) {
    for (const ThemeConfig& theme : layout.themes) {
        if (theme.name == name) {
            return &theme;
        }
    }
    return nullptr;
}

std::optional<ColorConfig> FindThemeToken(const ThemeConfig& theme, std::string_view name) {
    for (const RuntimeConfigFieldDescriptor& field : RuntimeConfigFieldDescriptors<ThemeConfig::Section>()) {
        if (field.kind == RuntimeConfigFieldValueKind::HexColor &&
            std::string_view(field.key, field.keyLength) == name) {
            return *reinterpret_cast<const ColorConfig*>(reinterpret_cast<const char*>(&theme) + field.offset);
        }
    }
    return std::nullopt;
}

using ColorLookup = FunctionRef<std::optional<ColorConfig>(std::string_view)>;

ColorConfig& MutableColorField(void* owner, const RuntimeConfigFieldDescriptor& field) {
    return *reinterpret_cast<ColorConfig*>(static_cast<char*>(owner) + field.offset);
}

const ColorConfig& ColorField(const void* owner, const RuntimeConfigFieldDescriptor& field) {
    return *reinterpret_cast<const ColorConfig*>(static_cast<const char*>(owner) + field.offset);
}

std::optional<ColorConfig> FindColorFieldByKey(
    std::span<const RuntimeConfigFieldDescriptor> fields, const void* owner, std::string_view name) {
    for (const RuntimeConfigFieldDescriptor& field : fields) {
        if (field.kind == RuntimeConfigFieldValueKind::HexColor &&
            std::string_view(field.key, field.keyLength) == name) {
            return ColorField(owner, field);
        }
    }
    return std::nullopt;
}

std::optional<ColorConfig> ResolveColorExpression(const ColorConfig& color, const ColorLookup& lookup) {
    const std::string expression = color.expression.empty() ? FormatHexColor(color) : color.expression;
    ColorConfig literal = ParseHexColorOrDefault(expression, color);
    if (!expression.empty() && expression.front() == '#') {
        literal.expression = FormatHexColor(literal);
        return literal;
    }

    std::optional<ColorExpression> parsed = ParseColorExpression(expression);
    if (!parsed.has_value()) {
        return std::nullopt;
    }

    std::optional<ColorConfig> base = lookup(parsed->base);
    if (!base.has_value()) {
        return std::nullopt;
    }
    ColorBytes bytes = ToColorBytes(*base);

    if (parsed->rotateHue.has_value()) {
        bytes = ColorBytesFromOklab(RotateOklabHue(OklabFromColorBytes(bytes), *parsed->rotateHue), bytes.a);
    }

    if (parsed->mix.has_value()) {
        std::optional<ColorConfig> target = lookup(parsed->mix->target);
        if (!target.has_value()) {
            return std::nullopt;
        }
        const double amount = parsed->mix->amount;
        const OklabColor mixedLab =
            MixOklab(OklabFromColorBytes(bytes), OklabFromColorBytes(ToColorBytes(*target)), amount);
        const double targetAlpha = static_cast<double>(target->Alpha());
        bytes = ColorBytesFromOklab(mixedLab, bytes.a + (targetAlpha - bytes.a) * amount);
    }

    if (parsed->alpha.has_value()) {
        bytes.a = static_cast<double>(*parsed->alpha);
    }

    return FromColorBytes(bytes, expression);
}

void ResolveColorInPlace(ColorConfig& color, const ColorLookup& lookup) {
    if (std::optional<ColorConfig> resolved = ResolveColorExpression(color, lookup); resolved.has_value()) {
        color = *resolved;
    } else {
        color = ColorConfig::FromRgba(color.ToRgba());
    }
}

void ResolveThemeColors(LayoutConfig& layout) {
    const auto noLookup = [](std::string_view) -> std::optional<ColorConfig> { return std::nullopt; };
    for (ThemeConfig& theme : layout.themes) {
        for (const RuntimeConfigFieldDescriptor& field : RuntimeConfigFieldDescriptors<ThemeConfig::Section>()) {
            if (field.kind == RuntimeConfigFieldValueKind::HexColor) {
                ResolveColorInPlace(MutableColorField(&theme, field), noLookup);
            }
        }
    }
}

void ResolveColorFieldsInPlace(std::span<const RuntimeConfigFieldDescriptor> fields, void* owner, ColorLookup lookup) {
    for (const RuntimeConfigFieldDescriptor& field : fields) {
        if (field.kind == RuntimeConfigFieldValueKind::HexColor) {
            ResolveColorInPlace(MutableColorField(owner, field), lookup);
        }
    }
}

}  // namespace

void ResolveConfiguredColors(AppConfig& config) {
    ResolveThemeColors(config.layout);
    const ThemeConfig* activeTheme = FindTheme(config.layout, config.display.theme);
    if (activeTheme == nullptr && !config.layout.themes.empty()) {
        activeTheme = &config.layout.themes.front();
        config.display.theme = activeTheme->name;
    }
    if (activeTheme == nullptr) {
        return;
    }

    const auto themeLookup = [activeTheme](std::string_view name) { return FindThemeToken(*activeTheme, name); };
    ResolveColorFieldsInPlace(
        RuntimeConfigFieldDescriptors<ColorsConfig::Section>(), &config.layout.colors, themeLookup);

    const auto guideSheetLookup = [&config, activeTheme](std::string_view name) -> std::optional<ColorConfig> {
        if (std::optional<ColorConfig> themeColor = FindThemeToken(*activeTheme, name); themeColor.has_value()) {
            return themeColor;
        }
        return FindColorFieldByKey(RuntimeConfigFieldDescriptors<ColorsConfig::Section>(), &config.layout.colors, name);
    };

    ResolveColorFieldsInPlace(RuntimeConfigFieldDescriptors<LayoutGuideSheetConfig::Section>(),
        &config.layout.layoutGuideSheet,
        guideSheetLookup);
}
