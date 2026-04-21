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

RenderStroke RenderStroke::Solid(RenderColorId color, float width) {
    return RenderStroke{color, width, StrokePattern::Solid};
}

RenderStroke RenderStroke::Dotted(RenderColorId color, float width) {
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
