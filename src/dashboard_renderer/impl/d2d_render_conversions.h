#pragma once

#include <d2d1.h>
#include <optional>
#include <wrl/client.h>

#include "widget/render_types.h"

D2D1_POINT_2F D2DPointFromRenderPoint(RenderPoint point);
D2D1_RECT_F D2DRectFromRenderRect(const RenderRect& rect);
Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateD2DRingSegmentPath(
    ID2D1Factory* factory, const RenderRingSegment& segment);
std::optional<RenderRect> RenderRectFromD2DGeometryBounds(ID2D1Geometry* geometry);
