#include "config/color_math.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double SrgbToLinear(double value) {
    value /= 255.0;
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double LinearToSrgb(double value) {
    value = std::clamp(value, 0.0, 1.0);
    const double encoded = value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
    return std::clamp(encoded * 255.0, 0.0, 255.0);
}

double NormalizeHueDegrees(double value) {
    value = std::fmod(value, 360.0);
    if (value < 0.0) {
        value += 360.0;
    }
    return value;
}

unsigned int ClampByte(double value) {
    return static_cast<unsigned int>(std::clamp(std::round(value), 0.0, 255.0));
}

double Min3Double(double first, double second, double third) {
    // Size: avoid std::min/std::max initializer_list helper code in color conversions.
    return std::min(std::min(first, second), third);
}

double Max3Double(double first, double second, double third) {
    return std::max(std::max(first, second), third);
}

}  // namespace

ColorBytes ColorBytesFromRgba(std::uint32_t rgba) {
    return ColorBytes{
        static_cast<double>((rgba >> 24) & 0xFFu),
        static_cast<double>((rgba >> 16) & 0xFFu),
        static_cast<double>((rgba >> 8) & 0xFFu),
        static_cast<double>(rgba & 0xFFu)};
}

std::uint32_t RgbaFromColorBytes(ColorBytes color) {
    return (ClampByte(color.r) << 24) | (ClampByte(color.g) << 16) | (ClampByte(color.b) << 8) | ClampByte(color.a);
}

OklabColor OklabFromColorBytes(ColorBytes color) {
    const double r = SrgbToLinear(color.r);
    const double g = SrgbToLinear(color.g);
    const double b = SrgbToLinear(color.b);

    const double l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
    const double m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
    const double s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;

    const double lRoot = std::cbrt(l);
    const double mRoot = std::cbrt(m);
    const double sRoot = std::cbrt(s);

    return OklabColor{
        0.2104542553 * lRoot + 0.7936177850 * mRoot - 0.0040720468 * sRoot,
        1.9779984951 * lRoot - 2.4285922050 * mRoot + 0.4505937099 * sRoot,
        0.0259040371 * lRoot + 0.7827717662 * mRoot - 0.8086757660 * sRoot};
}

ColorBytes ColorBytesFromOklab(OklabColor color, double alpha) {
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

OklchColor OklchFromOklab(OklabColor color) {
    const double chroma = std::hypot(color.a, color.b);
    const double hue = chroma <= 0.000001 ? 0.0 : NormalizeHueDegrees(std::atan2(color.b, color.a) * 180.0 / kPi);
    return OklchColor{color.l, chroma, hue};
}

OklabColor OklabFromOklch(OklchColor color) {
    const double hue = NormalizeHueDegrees(color.h) * kPi / 180.0;
    return OklabColor{color.l, color.c * std::cos(hue), color.c * std::sin(hue)};
}

OklchColor OklchFromColorBytes(ColorBytes color) {
    return OklchFromOklab(OklabFromColorBytes(color));
}

ColorBytes ColorBytesFromOklch(OklchColor color, double alpha) {
    return ColorBytesFromOklab(OklabFromOklch(color), alpha);
}

HsvColor HsvFromColorBytes(ColorBytes color) {
    const double r = std::clamp(color.r / 255.0, 0.0, 1.0);
    const double g = std::clamp(color.g / 255.0, 0.0, 1.0);
    const double b = std::clamp(color.b / 255.0, 0.0, 1.0);
    const double maxChannel = Max3Double(r, g, b);
    const double minChannel = Min3Double(r, g, b);
    const double delta = maxChannel - minChannel;

    double hue = 0.0;
    if (delta > 0.000001) {
        if (maxChannel == r) {
            hue = 60.0 * std::fmod(((g - b) / delta), 6.0);
        } else if (maxChannel == g) {
            hue = 60.0 * (((b - r) / delta) + 2.0);
        } else {
            hue = 60.0 * (((r - g) / delta) + 4.0);
        }
    }
    const double saturation = maxChannel <= 0.0 ? 0.0 : delta / maxChannel;
    return HsvColor{NormalizeHueDegrees(hue), saturation, maxChannel};
}

ColorBytes ColorBytesFromHsv(HsvColor color, double alpha) {
    const double hue = NormalizeHueDegrees(color.h);
    const double saturation = std::clamp(color.s, 0.0, 1.0);
    const double value = std::clamp(color.v, 0.0, 1.0);
    const double chroma = value * saturation;
    const double hueSection = hue / 60.0;
    const double x = chroma * (1.0 - std::abs(std::fmod(hueSection, 2.0) - 1.0));
    const double m = value - chroma;

    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    if (hueSection < 1.0) {
        r = chroma;
        g = x;
    } else if (hueSection < 2.0) {
        r = x;
        g = chroma;
    } else if (hueSection < 3.0) {
        g = chroma;
        b = x;
    } else if (hueSection < 4.0) {
        g = x;
        b = chroma;
    } else if (hueSection < 5.0) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }
    return ColorBytes{(r + m) * 255.0, (g + m) * 255.0, (b + m) * 255.0, alpha};
}

OklabColor MixOklab(OklabColor from, OklabColor to, double amount) {
    return OklabColor{
        from.l + (to.l - from.l) * amount, from.a + (to.a - from.a) * amount, from.b + (to.b - from.b) * amount};
}

OklabColor RotateOklabHue(OklabColor color, double degrees) {
    OklchColor lch = OklchFromOklab(color);
    lch.h += degrees;
    return OklabFromOklch(lch);
}
