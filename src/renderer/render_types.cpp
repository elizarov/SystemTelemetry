#include "renderer/render_types.h"

#include <algorithm>
#include <cmath>

namespace {

RenderPoint PathArcEndPoint(const RenderPathArc& arc) {
    const double radians = (arc.startAngleDegrees + arc.sweepAngleDegrees) * 3.14159265358979323846 / 180.0;
    return RenderPoint{
        arc.center.x + static_cast<int>(std::lround(std::cos(radians) * static_cast<double>(arc.radiusX))),
        arc.center.y + static_cast<int>(std::lround(std::sin(radians) * static_cast<double>(arc.radiusY)))};
}

}  // namespace

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
    return commandCount_ == 0;
}

std::span<const RenderPathCommand> RenderPath::Commands() const {
    if (!overflowCommands_.empty()) {
        return overflowCommands_;
    }
    return std::span<const RenderPathCommand>(inlineCommands_.data(), commandCount_);
}

void RenderPath::MoveTo(RenderPoint point) {
    PushCommand(RenderPathCommand{RenderPathCommandType::MoveTo, point, {}});
}

void RenderPath::LineTo(RenderPoint point) {
    PushCommand(RenderPathCommand{RenderPathCommandType::LineTo, point, {}});
}

void RenderPath::ArcTo(
    RenderPoint center, int radiusX, int radiusY, double startAngleDegrees, double sweepAngleDegrees) {
    RenderPathCommand command;
    command.type = RenderPathCommandType::ArcTo;
    command.arc = RenderPathArc{center, radiusX, radiusY, startAngleDegrees, sweepAngleDegrees};
    command.point = PathArcEndPoint(command.arc);
    PushCommand(command);
}

void RenderPath::Close() {
    PushCommand(RenderPathCommand{RenderPathCommandType::Close, {}, {}});
}

void RenderPath::PushCommand(const RenderPathCommand& command) {
    if (overflowCommands_.empty() && commandCount_ < inlineCommands_.size()) {
        inlineCommands_[commandCount_] = command;
    } else {
        if (overflowCommands_.empty()) {
            overflowCommands_.reserve(inlineCommands_.size() * 2);
            overflowCommands_.insert(overflowCommands_.end(), inlineCommands_.begin(), inlineCommands_.end());
        }
        overflowCommands_.push_back(command);
    }
    ++commandCount_;
}

TextLayoutOptions TextLayoutOptions::SingleLine(
    TextHorizontalAlign horizontal, TextVerticalAlign vertical, bool clip, bool ellipsis) {
    return TextLayoutOptions{horizontal, vertical, false, ellipsis, clip};
}

TextLayoutOptions TextLayoutOptions::Wrapped(
    TextHorizontalAlign horizontal, TextVerticalAlign vertical, bool clip, bool ellipsis) {
    return TextLayoutOptions{horizontal, vertical, true, ellipsis, clip};
}
