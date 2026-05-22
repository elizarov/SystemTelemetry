#include "layout_model/layout_edit_anchor_shape.h"

#include <algorithm>

#include "renderer/renderer.h"

void DrawLayoutEditAnchorShape(Renderer& renderer,
    AnchorShape shape,
    const RenderRect& rect,
    RenderColorId color,
    float outlineWidth,
    int gapHalf,
    bool outlineCircle,
    bool outlinePlus) {
    if (shape == AnchorShape::Circle) {
        if (outlineCircle) {
            renderer.DrawSolidEllipse(rect, RenderStroke::Solid(color, outlineWidth));
        } else {
            renderer.FillSolidEllipse(rect, color);
        }
        return;
    }
    if (shape == AnchorShape::Diamond) {
        renderer.FillSolidDiamond(rect, color);
        return;
    }
    if (shape == AnchorShape::Wedge) {
        const RenderPoint topRight{rect.right, rect.top};
        const RenderPoint bottomLeft{rect.left, rect.bottom};
        const RenderPoint bottomRight{rect.right, rect.bottom};
        const auto stroke = RenderStroke::Solid(color, outlineWidth);
        renderer.DrawSolidLine(bottomLeft, bottomRight, stroke);
        renderer.DrawSolidLine(topRight, bottomRight, stroke);
        return;
    }
    if (shape == AnchorShape::VerticalReorder || shape == AnchorShape::HorizontalReorder) {
        const int centerX = rect.left + (std::max(0, static_cast<int>(rect.right - rect.left)) / 2);
        const int centerY = rect.top + (std::max(0, static_cast<int>(rect.bottom - rect.top)) / 2);
        const auto stroke = RenderStroke::Solid(color, outlineWidth);
        if (shape == AnchorShape::HorizontalReorder) {
            const int halfHeight = std::max(1, static_cast<int>(rect.bottom - rect.top) / 2);
            const RenderPoint leftApex{rect.left, centerY};
            const RenderPoint leftTop{centerX - gapHalf, centerY - halfHeight};
            const RenderPoint leftBottom{centerX - gapHalf, centerY + halfHeight};
            const RenderPoint rightApex{rect.right, centerY};
            const RenderPoint rightTop{centerX + gapHalf, centerY - halfHeight};
            const RenderPoint rightBottom{centerX + gapHalf, centerY + halfHeight};
            renderer.DrawSolidLine(leftApex, leftTop, stroke);
            renderer.DrawSolidLine(leftTop, leftBottom, stroke);
            renderer.DrawSolidLine(leftBottom, leftApex, stroke);
            renderer.DrawSolidLine(rightTop, rightApex, stroke);
            renderer.DrawSolidLine(rightApex, rightBottom, stroke);
            renderer.DrawSolidLine(rightBottom, rightTop, stroke);
        } else {
            const int halfWidth = std::max(1, static_cast<int>(rect.right - rect.left) / 2);
            const RenderPoint upApex{centerX, rect.top};
            const RenderPoint upLeft{centerX - halfWidth, centerY - gapHalf};
            const RenderPoint upRight{centerX + halfWidth, centerY - gapHalf};
            const RenderPoint downApex{centerX, rect.bottom};
            const RenderPoint downLeft{centerX - halfWidth, centerY + gapHalf};
            const RenderPoint downRight{centerX + halfWidth, centerY + gapHalf};
            renderer.DrawSolidLine(upApex, upLeft, stroke);
            renderer.DrawSolidLine(upLeft, upRight, stroke);
            renderer.DrawSolidLine(upRight, upApex, stroke);
            renderer.DrawSolidLine(downLeft, downApex, stroke);
            renderer.DrawSolidLine(downApex, downRight, stroke);
            renderer.DrawSolidLine(downRight, downLeft, stroke);
        }
        return;
    }
    if (shape == AnchorShape::Plus && outlinePlus) {
        const int centerX = rect.left + (std::max(0, static_cast<int>(rect.right - rect.left)) / 2);
        const int centerY = rect.top + (std::max(0, static_cast<int>(rect.bottom - rect.top)) / 2);
        const int halfWidth = std::max(2, static_cast<int>(rect.right - rect.left) / 2);
        const int halfHeight = std::max(2, static_cast<int>(rect.bottom - rect.top) / 2);
        const auto stroke = RenderStroke::Solid(color, outlineWidth);
        renderer.DrawSolidLine(
            RenderPoint{centerX - halfWidth, centerY}, RenderPoint{centerX + halfWidth, centerY}, stroke);
        renderer.DrawSolidLine(
            RenderPoint{centerX, centerY - halfHeight}, RenderPoint{centerX, centerY + halfHeight}, stroke);
        return;
    }
    renderer.FillSolidRect(rect, color);
}
