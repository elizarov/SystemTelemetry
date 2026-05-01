#include "config/color_resolver.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <optional>
#include <string_view>

#include "config/color_expression.h"
#include "util/strings.h"

namespace {

std::string FormatHexColor(ColorConfig color) {
    return FormatHexColorText(color.ToRgba());
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
    try {
        return ColorConfig::FromRgba(static_cast<unsigned int>(std::stoul(text, nullptr, 16)));
    } catch (...) {
        return fallback;
    }
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
    if (name == "background")
        return theme.background;
    if (name == "foreground")
        return theme.foreground;
    if (name == "accent")
        return theme.accent;
    if (name == "guide")
        return theme.guide;
    return std::nullopt;
}

using ColorLookup = std::function<std::optional<ColorConfig>(std::string_view)>;

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
    for (ThemeConfig& theme : layout.themes) {
        ResolveColorInPlace(theme.background, [](std::string_view) { return std::nullopt; });
        ResolveColorInPlace(theme.foreground, [](std::string_view) { return std::nullopt; });
        ResolveColorInPlace(theme.accent, [](std::string_view) { return std::nullopt; });
        ResolveColorInPlace(theme.guide, [](std::string_view) { return std::nullopt; });
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
    ResolveColorInPlace(config.layout.colors.backgroundColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.foregroundColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.iconColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.accentColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.peakGhostColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.layoutGuideColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.activeEditColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.panelBorderColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.mutedTextColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.trackColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.panelFillColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.graphBackgroundColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.graphAxisColor, themeLookup);
    ResolveColorInPlace(config.layout.colors.graphMarkerColor, themeLookup);

    const auto guideSheetLookup = [&config, activeTheme](std::string_view name) -> std::optional<ColorConfig> {
        if (std::optional<ColorConfig> themeColor = FindThemeToken(*activeTheme, name); themeColor.has_value()) {
            return themeColor;
        }
        const ColorsConfig& colors = config.layout.colors;
        if (name == "background_color")
            return colors.backgroundColor;
        if (name == "foreground_color")
            return colors.foregroundColor;
        if (name == "icon_color")
            return colors.iconColor;
        if (name == "accent_color")
            return colors.accentColor;
        if (name == "peak_ghost_color")
            return colors.peakGhostColor;
        if (name == "layout_guide_color")
            return colors.layoutGuideColor;
        if (name == "active_edit_color")
            return colors.activeEditColor;
        if (name == "panel_border_color")
            return colors.panelBorderColor;
        if (name == "muted_text_color")
            return colors.mutedTextColor;
        if (name == "track_color")
            return colors.trackColor;
        if (name == "panel_fill_color")
            return colors.panelFillColor;
        if (name == "graph_background_color")
            return colors.graphBackgroundColor;
        if (name == "graph_axis_color")
            return colors.graphAxisColor;
        if (name == "graph_marker_color")
            return colors.graphMarkerColor;
        return std::nullopt;
    };

    ResolveColorInPlace(config.layout.layoutGuideSheet.calloutLeaderColor, guideSheetLookup);
    ResolveColorInPlace(config.layout.layoutGuideSheet.calloutFillColor, guideSheetLookup);
    ResolveColorInPlace(config.layout.layoutGuideSheet.calloutBorderColor, guideSheetLookup);
    ResolveColorInPlace(config.layout.layoutGuideSheet.calloutParameterColor, guideSheetLookup);
    ResolveColorInPlace(config.layout.layoutGuideSheet.calloutDescriptionColor, guideSheetLookup);
}
