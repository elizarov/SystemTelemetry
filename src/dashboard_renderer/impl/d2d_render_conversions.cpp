#include "dashboard_renderer/impl/d2d_render_conversions.h"

#include <algorithm>
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

D2D1_ARC_SIZE ArcSize(double sweepAngleDegrees) {
    return std::abs(sweepAngleDegrees) > 180.0 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
}

}  // namespace

Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateD2DRingSegmentPath(
    ID2D1Factory* factory, const RenderRingSegment& segment) {
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
    if (factory == nullptr || segment.outerRadius <= 0 || segment.thickness <= 0 || segment.sweepAngleDegrees <= 0.0) {
        return path;
    }

    const float cx = static_cast<float>(segment.centerX);
    const float cy = static_cast<float>(segment.centerY);
    const float outerRadius = static_cast<float>(segment.outerRadius);
    const float innerRadius = (std::max)(0.0f, outerRadius - static_cast<float>(segment.thickness));
    if (outerRadius <= innerRadius) {
        return path;
    }

    const double startRadians = segment.startAngleDegrees * 3.14159265358979323846 / 180.0;
    const double endRadians = (segment.startAngleDegrees + segment.sweepAngleDegrees) * 3.14159265358979323846 / 180.0;
    const D2D1_POINT_2F outerStart = D2D1::Point2F(cx + static_cast<float>(std::cos(startRadians) * outerRadius),
        cy + static_cast<float>(std::sin(startRadians) * outerRadius));
    const D2D1_POINT_2F outerEnd = D2D1::Point2F(cx + static_cast<float>(std::cos(endRadians) * outerRadius),
        cy + static_cast<float>(std::sin(endRadians) * outerRadius));
    const D2D1_POINT_2F innerEnd = D2D1::Point2F(cx + static_cast<float>(std::cos(endRadians) * innerRadius),
        cy + static_cast<float>(std::sin(endRadians) * innerRadius));
    const D2D1_POINT_2F innerStart = D2D1::Point2F(cx + static_cast<float>(std::cos(startRadians) * innerRadius),
        cy + static_cast<float>(std::sin(startRadians) * innerRadius));

    if (FAILED(factory->CreatePathGeometry(path.GetAddressOf())) || path == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(sink.GetAddressOf())) || sink == nullptr) {
        return {};
    }
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(outerStart, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddArc(D2D1::ArcSegment(outerEnd,
        D2D1::SizeF(outerRadius, outerRadius),
        0.0f,
        D2D1_SWEEP_DIRECTION_CLOCKWISE,
        ArcSize(segment.sweepAngleDegrees)));
    sink->AddLine(innerEnd);
    if (innerRadius > 0.0f) {
        sink->AddArc(D2D1::ArcSegment(innerStart,
            D2D1::SizeF(innerRadius, innerRadius),
            0.0f,
            D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
            ArcSize(segment.sweepAngleDegrees)));
    } else {
        sink->AddLine(D2D1::Point2F(cx, cy));
    }
    sink->AddLine(outerStart);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) {
        return {};
    }
    return path;
}

std::optional<RenderRect> RenderRectFromD2DGeometryBounds(ID2D1Geometry* geometry) {
    if (geometry == nullptr) {
        return std::nullopt;
    }
    D2D1_RECT_F bounds{};
    if (FAILED(geometry->GetBounds(nullptr, &bounds))) {
        return std::nullopt;
    }
    return RenderRect{static_cast<int>(std::floor(bounds.left)),
        static_cast<int>(std::floor(bounds.top)),
        static_cast<int>(std::ceil(bounds.right)),
        static_cast<int>(std::ceil(bounds.bottom))};
}
