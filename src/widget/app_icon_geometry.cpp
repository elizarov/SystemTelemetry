#include "widget/app_icon_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "config/config.h"

namespace {

struct IconColor {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 0.0;
};

struct IconPalette {
    IconColor appBackground;
    IconColor cardBackground;
    IconColor foreground;
    IconColor accent;
    IconColor muted;
};

IconColor ColorFromConfig(ColorConfig color) {
    const unsigned int rgba = color.ToRgba();
    return IconColor{
        static_cast<double>((rgba >> 24) & 0xFFu) / 255.0,
        static_cast<double>((rgba >> 16) & 0xFFu) / 255.0,
        static_cast<double>((rgba >> 8) & 0xFFu) / 255.0,
        static_cast<double>(rgba & 0xFFu) / 255.0,
    };
}

IconColor WithAlpha(IconColor color, double alpha) {
    color.a = std::clamp(alpha, 0.0, 1.0);
    return color;
}

IconColor Mix(IconColor left, IconColor right, double amount) {
    const double t = std::clamp(amount, 0.0, 1.0);
    return IconColor{
        left.r + (right.r - left.r) * t,
        left.g + (right.g - left.g) * t,
        left.b + (right.b - left.b) * t,
        left.a + (right.a - left.a) * t,
    };
}

IconPalette PaletteFromConfig(const AppConfig& config) {
    const ColorsConfig& colors = config.layout.colors;
    const IconColor background = WithAlpha(ColorFromConfig(colors.backgroundColor), 1.0);
    const IconColor foreground = ColorFromConfig(colors.foregroundColor);
    const IconColor accent = ColorFromConfig(colors.accentColor);
    const IconColor panel = ColorFromConfig(colors.panelFillColor);
    const IconColor muted = ColorFromConfig(colors.mutedTextColor);
    return IconPalette{
        background,
        panel,
        foreground,
        accent,
        Mix(muted, foreground, 0.15),
    };
}

double RoundedRectDistance(double x, double y, double left, double top, double right, double bottom, double radius) {
    const double centerX = (left + right) * 0.5;
    const double centerY = (top + bottom) * 0.5;
    const double halfWidth = (right - left) * 0.5;
    const double halfHeight = (bottom - top) * 0.5;
    const double qx = std::abs(x - centerX) - (halfWidth - radius);
    const double qy = std::abs(y - centerY) - (halfHeight - radius);
    const double ox = std::max(qx, 0.0);
    const double oy = std::max(qy, 0.0);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - radius;
}

double DistanceToSegment(double x, double y, double x1, double y1, double x2, double y2) {
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0) {
        const double px = x - x1;
        const double py = y - y1;
        return std::sqrt(px * px + py * py);
    }
    const double t = std::clamp(((x - x1) * dx + (y - y1) * dy) / lengthSquared, 0.0, 1.0);
    const double nearestX = x1 + dx * t;
    const double nearestY = y1 + dy * t;
    const double px = x - nearestX;
    const double py = y - nearestY;
    return std::sqrt(px * px + py * py);
}

double NormalizeAngle(double degrees) {
    while (degrees < 0.0) {
        degrees += 360.0;
    }
    while (degrees >= 360.0) {
        degrees -= 360.0;
    }
    return degrees;
}

bool AngleInSweep(double angle, double start, double sweep) {
    const double normalizedAngle = NormalizeAngle(angle);
    const double normalizedStart = NormalizeAngle(start);
    const double normalizedEnd = NormalizeAngle(start + sweep);
    if (sweep >= 360.0) {
        return true;
    }
    if (normalizedStart <= normalizedEnd) {
        return normalizedAngle >= normalizedStart && normalizedAngle <= normalizedEnd;
    }
    return normalizedAngle >= normalizedStart || normalizedAngle <= normalizedEnd;
}

bool InArcStroke(
    double x, double y, double centerX, double centerY, double radius, double width, double start, double sweep) {
    const double dx = x - centerX;
    const double dy = y - centerY;
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double angle = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
    return std::abs(distance - radius) <= width * 0.5 && AngleInSweep(angle, start, sweep);
}

bool InRect(double x, double y, double left, double top, double right, double bottom) {
    return x >= left && x <= right && y >= top && y <= bottom;
}

bool InLineStroke(double x, double y, double x1, double y1, double x2, double y2, double width) {
    return DistanceToSegment(x, y, x1, y1, x2, y2) <= width * 0.5;
}

void Blend(IconColor& destination, IconColor source) {
    const double outA = source.a + destination.a * (1.0 - source.a);
    if (outA <= 0.0) {
        destination = {};
        return;
    }
    destination.r = (source.r * source.a + destination.r * destination.a * (1.0 - source.a)) / outA;
    destination.g = (source.g * source.a + destination.g * destination.a * (1.0 - source.a)) / outA;
    destination.b = (source.b * source.a + destination.b * destination.a * (1.0 - source.a)) / outA;
    destination.a = outA;
}

IconColor RenderSample(double x, double y, const IconPalette& palette) {
    IconColor color;
    const double outerDistance = RoundedRectDistance(x, y, 4.0, 4.0, 252.0, 252.0, 58.0);
    if (outerDistance > 0.0) {
        return color;
    }

    Blend(color, palette.appBackground);
    Blend(color, palette.cardBackground);
    const double innerDistance = RoundedRectDistance(x, y, 10.0, 10.0, 246.0, 246.0, 52.0);
    if (innerDistance > 0.0) {
        Blend(color, palette.foreground);
    }

    if (InArcStroke(x, y, 128.0, 127.0, 80.0, 18.0, 88.0, 260.0)) {
        Blend(color, WithAlpha(palette.muted, 0.42));
    }
    if (InArcStroke(x, y, 128.0, 127.0, 80.0, 18.0, 158.0, 88.0)) {
        Blend(color, palette.accent);
    }

    if (InRect(x, y, 92.0, 148.0, 110.0, 204.0)) {
        Blend(color, palette.accent);
    }
    if (InRect(x, y, 120.0, 132.0, 138.0, 204.0)) {
        Blend(color, palette.accent);
    }
    if (InRect(x, y, 148.0, 116.0, 166.0, 204.0)) {
        Blend(color, WithAlpha(palette.muted, 0.62));
    }

    constexpr double pulseWidth = 7.0;
    if (InLineStroke(x, y, 62.0, 206.0, 194.0, 206.0, pulseWidth) ||
        InLineStroke(x, y, 94.0, 206.0, 109.0, 182.0, pulseWidth) ||
        InLineStroke(x, y, 109.0, 182.0, 126.0, 224.0, pulseWidth) ||
        InLineStroke(x, y, 126.0, 224.0, 146.0, 164.0, pulseWidth) ||
        InLineStroke(x, y, 146.0, 164.0, 164.0, 206.0, pulseWidth)) {
        Blend(color, palette.foreground);
    }

    return color;
}

std::uint8_t ByteFromUnit(double value) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0), 0l, 255l));
}

void StoreBgra(std::uint8_t* target, IconColor color) {
    target[0] = ByteFromUnit(color.b);
    target[1] = ByteFromUnit(color.g);
    target[2] = ByteFromUnit(color.r);
    target[3] = ByteFromUnit(color.a);
}

}  // namespace

bool IsValidAppIconSize(int size) {
    return size >= kMinAppIconSize && size <= kMaxAppIconSize;
}

AppIconBitmap RenderAppIconBitmap(const AppConfig& config, int size) {
    const int iconSize = IsValidAppIconSize(size) ? size : kDefaultAppIconSize;
    AppIconBitmap bitmap;
    bitmap.size = iconSize;
    bitmap.bgra.resize(static_cast<size_t>(iconSize) * static_cast<size_t>(iconSize) * 4u);

    const IconPalette palette = PaletteFromConfig(config);
    constexpr int kSupersample = 4;
    constexpr double kSampleWeight = 1.0 / static_cast<double>(kSupersample * kSupersample);
    for (int y = 0; y < iconSize; ++y) {
        for (int x = 0; x < iconSize; ++x) {
            double premultipliedR = 0.0;
            double premultipliedG = 0.0;
            double premultipliedB = 0.0;
            double alpha = 0.0;
            for (int sy = 0; sy < kSupersample; ++sy) {
                for (int sx = 0; sx < kSupersample; ++sx) {
                    const double sampleX = (static_cast<double>(x) + (static_cast<double>(sx) + 0.5) / kSupersample) *
                        256.0 / static_cast<double>(iconSize);
                    const double sampleY = (static_cast<double>(y) + (static_cast<double>(sy) + 0.5) / kSupersample) *
                        256.0 / static_cast<double>(iconSize);
                    const IconColor sample = RenderSample(sampleX, sampleY, palette);
                    premultipliedR += sample.r * sample.a * kSampleWeight;
                    premultipliedG += sample.g * sample.a * kSampleWeight;
                    premultipliedB += sample.b * sample.a * kSampleWeight;
                    alpha += sample.a * kSampleWeight;
                }
            }
            IconColor pixel;
            if (alpha > 0.0) {
                pixel = IconColor{
                    premultipliedR / alpha,
                    premultipliedG / alpha,
                    premultipliedB / alpha,
                    alpha,
                };
            }
            StoreBgra(
                &bitmap.bgra[(static_cast<size_t>(y) * static_cast<size_t>(iconSize) + static_cast<size_t>(x)) * 4u],
                pixel);
        }
    }
    return bitmap;
}
