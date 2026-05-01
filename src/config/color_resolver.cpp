#include "config/color_resolver.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

#include "config/color_expression.h"
#include "config/config_runtime_fields.h"
#include "util/function_ref.h"
#include "util/strings.h"

namespace {

std::string FormatHexColor(ColorConfig color) {
    return FormatHexColorText(color.ToRgba());
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

struct ColorBytes {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 255.0;
};

struct Oklab {
    double l = 0.0;
    double a = 0.0;
    double b = 0.0;
};

double SrgbToLinear(double value) {
    value /= 255.0;
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double LinearToSrgb(double value) {
    value = std::clamp(value, 0.0, 1.0);
    const double encoded = value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
    return std::clamp(encoded * 255.0, 0.0, 255.0);
}

Oklab ToOklab(ColorBytes color) {
    const double r = SrgbToLinear(color.r);
    const double g = SrgbToLinear(color.g);
    const double b = SrgbToLinear(color.b);

    const double l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
    const double m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
    const double s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;

    const double lRoot = std::cbrt(l);
    const double mRoot = std::cbrt(m);
    const double sRoot = std::cbrt(s);

    return Oklab{0.2104542553 * lRoot + 0.7936177850 * mRoot - 0.0040720468 * sRoot,
        1.9779984951 * lRoot - 2.4285922050 * mRoot + 0.4505937099 * sRoot,
        0.0259040371 * lRoot + 0.7827717662 * mRoot - 0.8086757660 * sRoot};
}

ColorBytes FromOklab(Oklab color, double alpha) {
    const double lRoot = color.l + 0.3963377774 * color.a + 0.2158037573 * color.b;
    const double mRoot = color.l - 0.1055613458 * color.a - 0.0638541728 * color.b;
    const double sRoot = color.l - 0.0894841775 * color.a - 1.2914855480 * color.b;

    const double l = lRoot * lRoot * lRoot;
    const double m = mRoot * mRoot * mRoot;
    const double s = sRoot * sRoot * sRoot;

    const double r = +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    const double g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    const double b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;

    return ColorBytes{LinearToSrgb(r), LinearToSrgb(g), LinearToSrgb(b), alpha};
}

ColorBytes ToColorBytes(ColorConfig color) {
    const unsigned int rgba = color.ToRgba();
    return ColorBytes{static_cast<double>((rgba >> 24) & 0xFFu),
        static_cast<double>((rgba >> 16) & 0xFFu),
        static_cast<double>((rgba >> 8) & 0xFFu),
        static_cast<double>(rgba & 0xFFu)};
}

ColorConfig FromColorBytes(ColorBytes color, const std::string& expression) {
    const auto clampByte = [](double value) -> unsigned int {
        return static_cast<unsigned int>(std::clamp(std::round(value), 0.0, 255.0));
    };
    const unsigned int rgba =
        (clampByte(color.r) << 24) | (clampByte(color.g) << 16) | (clampByte(color.b) << 8) | clampByte(color.a);
    ColorConfig result = ColorConfig::FromRgba(rgba);
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
        if (field.kind == RuntimeConfigFieldValueKind::HexColor && field.key == name) {
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
        if (field.kind == RuntimeConfigFieldValueKind::HexColor && field.key == name) {
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
        Oklab lab = ToOklab(bytes);
        const double chroma = std::hypot(lab.a, lab.b);
        constexpr double kPi = 3.14159265358979323846;
        const double hue = std::atan2(lab.b, lab.a) + (*parsed->rotateHue * kPi / 180.0);
        lab.a = chroma * std::cos(hue);
        lab.b = chroma * std::sin(hue);
        bytes = FromOklab(lab, bytes.a);
    }

    if (parsed->mix.has_value()) {
        std::optional<ColorConfig> target = lookup(parsed->mix->target);
        if (!target.has_value()) {
            return std::nullopt;
        }
        const double amount = parsed->mix->amount;
        const Oklab baseLab = ToOklab(bytes);
        const Oklab targetLab = ToOklab(ToColorBytes(*target));
        const Oklab mixedLab{baseLab.l + (targetLab.l - baseLab.l) * amount,
            baseLab.a + (targetLab.a - baseLab.a) * amount,
            baseLab.b + (targetLab.b - baseLab.b) * amount};
        const double targetAlpha = static_cast<double>(target->Alpha());
        bytes = FromOklab(mixedLab, bytes.a + (targetAlpha - bytes.a) * amount);
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
