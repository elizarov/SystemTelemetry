#include "dashboard_renderer/impl/d2d_render_conversions.h"

#include <cmath>

D2D1_POINT_2F D2DPointFromRenderPoint(RenderPoint point) {
    return D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));
}

D2D1_RECT_F D2DRectFromRenderRect(const RenderRect& rect) {
    return D2D1::RectF(static_cast<float>(rect.left),
        static_cast<float>(rect.top),
        static_cast<float>(rect.right),
        static_cast<float>(rect.bottom));
}

namespace {

D2D1_POINT_2F ArcPoint(const RenderPathArc& arc, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return D2D1::Point2F(
        static_cast<float>(arc.center.x) + static_cast<float>(std::cos(radians) * static_cast<double>(arc.radiusX)),
        static_cast<float>(arc.center.y) + static_cast<float>(std::sin(radians) * static_cast<double>(arc.radiusY)));
}

D2D1_SWEEP_DIRECTION ArcSweepDirection(double sweepAngleDegrees) {
    return sweepAngleDegrees >= 0.0 ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
}

void AddArcSegments(ID2D1GeometrySink* sink, const RenderPathArc& arc) {
    if (sink == nullptr || arc.radiusX <= 0 || arc.radiusY <= 0 || arc.sweepAngleDegrees == 0.0) {
        return;
    }

    double start = arc.startAngleDegrees;
    double remaining = arc.sweepAngleDegrees;
    while (std::abs(remaining) > 180.0) {
        const double chunk = remaining > 0.0 ? 180.0 : -180.0;
        const double end = start + chunk;
        sink->AddArc(D2D1::ArcSegment(ArcPoint(arc, end),
            D2D1::SizeF(static_cast<float>(arc.radiusX), static_cast<float>(arc.radiusY)),
            0.0f,
            ArcSweepDirection(chunk),
            D2D1_ARC_SIZE_SMALL));
        start = end;
        remaining -= chunk;
    }

    if (remaining != 0.0) {
        const double end = start + remaining;
        sink->AddArc(D2D1::ArcSegment(ArcPoint(arc, end),
            D2D1::SizeF(static_cast<float>(arc.radiusX), static_cast<float>(arc.radiusY)),
            0.0f,
            ArcSweepDirection(remaining),
            std::abs(remaining) > 180.0 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    }
}

}  // namespace

Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateD2DRenderPathGeometry(ID2D1Factory* factory, const RenderPath& path) {
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
    if (factory == nullptr) {
        return geometry;
    }

    if (FAILED(factory->CreatePathGeometry(geometry.GetAddressOf())) || geometry == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(sink.GetAddressOf())) || sink == nullptr) {
        return {};
    }
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);

    bool figureOpen = false;
    for (const RenderPathCommand& command : path.commands) {
        switch (command.type) {
            case RenderPathCommandType::MoveTo:
                if (figureOpen) {
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                }
                sink->BeginFigure(D2DPointFromRenderPoint(command.point), D2D1_FIGURE_BEGIN_FILLED);
                figureOpen = true;
                break;
            case RenderPathCommandType::LineTo:
                if (figureOpen) {
                    sink->AddLine(D2DPointFromRenderPoint(command.point));
                }
                break;
            case RenderPathCommandType::ArcTo:
                if (figureOpen) {
                    AddArcSegments(sink.Get(), command.arc);
                }
                break;
            case RenderPathCommandType::Close:
                if (figureOpen) {
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    figureOpen = false;
                }
                break;
        }
    }
    if (figureOpen) {
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    if (FAILED(sink->Close())) {
        return {};
    }
    return geometry;
}
