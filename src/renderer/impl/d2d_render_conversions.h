#pragma once

#include <d2d1.h>
#include <span>
#include <wrl/client.h>

#include "renderer/render_types.h"

D2D1_POINT_2F D2DPointFromRenderPoint(RenderPoint point);
D2D1_RECT_F D2DRectFromRenderRect(const RenderRect& rect);
Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateD2DRenderArcGeometry(ID2D1Factory* factory, const RenderArc& arc);
Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateD2DRenderArcsGeometry(
    ID2D1Factory* factory, std::span<const RenderArc> arcs);
Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateD2DRenderPathGeometry(ID2D1Factory* factory, const RenderPath& path);
