#pragma once

#include <d2d1.h>
#include <wrl/client.h>

#include "widget/render_types.h"

D2D1_POINT_2F D2DPointFromRenderPoint(RenderPoint point);
D2D1_RECT_F D2DRectFromRenderRect(const RenderRect& rect);
Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateD2DRenderPathGeometry(ID2D1Factory* factory, const RenderPath& path);
