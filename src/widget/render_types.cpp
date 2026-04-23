#include "widget/render_types.h"

#include <algorithm>

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

RenderStroke RenderStroke::Solid(RenderColorId color, float width) {
    return RenderStroke{color, width, StrokePattern::Solid};
}

RenderStroke RenderStroke::Dotted(RenderColorId color, float width) {
    return RenderStroke{color, width, StrokePattern::Dotted};
}

bool RenderPath::IsEmpty() const {
    return commands.empty();
}

void RenderPath::MoveTo(RenderPoint point) {
    commands.push_back(RenderPathCommand{RenderPathCommandType::MoveTo, point, {}});
}

void RenderPath::LineTo(RenderPoint point) {
    commands.push_back(RenderPathCommand{RenderPathCommandType::LineTo, point, {}});
}

void RenderPath::ArcTo(
    RenderPoint center, int radiusX, int radiusY, double startAngleDegrees, double sweepAngleDegrees) {
    RenderPathCommand command;
    command.type = RenderPathCommandType::ArcTo;
    command.arc = RenderPathArc{center, radiusX, radiusY, startAngleDegrees, sweepAngleDegrees};
    commands.push_back(command);
}

void RenderPath::Close() {
    commands.push_back(RenderPathCommand{RenderPathCommandType::Close, {}, {}});
}

TextLayoutOptions TextLayoutOptions::SingleLine(
    TextHorizontalAlign horizontal, TextVerticalAlign vertical, bool clip, bool ellipsis) {
    return TextLayoutOptions{horizontal, vertical, false, ellipsis, clip};
}

TextLayoutOptions TextLayoutOptions::Wrapped(
    TextHorizontalAlign horizontal, TextVerticalAlign vertical, bool clip, bool ellipsis) {
    return TextLayoutOptions{horizontal, vertical, true, ellipsis, clip};
}
