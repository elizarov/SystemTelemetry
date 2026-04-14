#include "render_types.h"

#include <algorithm>

D2D1_POINT_2F RenderPoint::ToD2DPoint2F() const {
    return D2D1::Point2F(static_cast<float>(x), static_cast<float>(y));
}

int RenderRect::Width() const {
    return (std::max)(0, right - left);
}

int RenderRect::Height() const {
    return (std::max)(0, bottom - top);
}

bool RenderRect::IsEmpty() const {
    return right <= left || bottom <= top;
}

bool RenderRect::Contains(RenderPoint point) const {
    return point.x >= left && point.x < right && point.y >= top && point.y < bottom;
}

RenderRect RenderRect::Inflate(int dx, int dy) const {
    return RenderRect{left - dx, top - dy, right + dx, bottom + dy};
}

RenderPoint RenderRect::Center() const {
    return RenderPoint{left + (Width() / 2), top + (Height() / 2)};
}

D2D1_RECT_F RenderRect::ToD2DRectF() const {
    return D2D1::RectF(
        static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom));
}

RenderColor RenderColor::WithAlpha(std::uint8_t alpha) const {
    return RenderColor{r, g, b, alpha};
}

std::uint32_t RenderColor::PackedRgba() const {
    return (static_cast<std::uint32_t>(r) << 24) | (static_cast<std::uint32_t>(g) << 16) |
           (static_cast<std::uint32_t>(b) << 8) | static_cast<std::uint32_t>(a);
}

D2D1_COLOR_F RenderColor::ToD2DColorF() const {
    constexpr float kScale = 1.0f / 255.0f;
    return D2D1::ColorF(static_cast<float>(r) * kScale,
        static_cast<float>(g) * kScale,
        static_cast<float>(b) * kScale,
        static_cast<float>(a) * kScale);
}

RenderStroke RenderStroke::Solid(RenderColor color, float width) {
    return RenderStroke{color, width, StrokePattern::Solid};
}

RenderStroke RenderStroke::Dotted(RenderColor color, float width) {
    return RenderStroke{color, width, StrokePattern::Dotted};
}

TextLayoutOptions TextLayoutOptions::SingleLine(
    TextHorizontalAlign horizontal, TextVerticalAlign vertical, bool clip, bool ellipsis) {
    return TextLayoutOptions{horizontal, vertical, false, ellipsis, clip};
}

TextLayoutOptions TextLayoutOptions::Wrapped(
    TextHorizontalAlign horizontal, TextVerticalAlign vertical, bool clip, bool ellipsis) {
    return TextLayoutOptions{horizontal, vertical, true, ellipsis, clip};
}
