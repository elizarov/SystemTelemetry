#pragma once

#include <optional>

#include "renderer/render_types.h"
#include "widget/layout_edit_types.h"

class DashboardLayoutResolver;
class DashboardRenderer;
class MetricSource;
struct DashboardOverlayState;

class DashboardLayoutEditOverlayRenderer {
public:
    DashboardLayoutEditOverlayRenderer(DashboardRenderer& renderer, DashboardLayoutResolver& layoutResolver);

    bool ShouldSkipBaseWidget(const DashboardOverlayState& overlayState, const RenderRect& rect) const;
    void Draw(const DashboardOverlayState& overlayState, const MetricSource& metrics);

private:
    struct SimilarityIndicator {
        LayoutGuideAxis axis = LayoutGuideAxis::Horizontal;
        RenderRect rect{};
        int exactTypeOrdinal = 0;
    };

    void DrawHoveredWidgetHighlight(const DashboardOverlayState& overlayState) const;
    void DrawHoveredEditableAnchorHighlight(const DashboardOverlayState& overlayState) const;
    void DrawSelectedColorEditHighlights(const DashboardOverlayState& overlayState) const;
    void DrawSelectedTreeNodeHighlight(const DashboardOverlayState& overlayState) const;
    void DrawLayoutEditGuides(const DashboardOverlayState& overlayState) const;
    void DrawWidgetEditGuides(const DashboardOverlayState& overlayState) const;
    void DrawGapEditAnchors(const DashboardOverlayState& overlayState) const;
    std::optional<RenderRect> FindHoveredWidgetOutlineRect(const DashboardOverlayState& overlayState) const;
    void DrawDottedHighlightRect(const RenderRect& rect, RenderColorId color, bool active, bool outside = true) const;
    void DrawLayoutSimilarityIndicators(const DashboardOverlayState& overlayState) const;
    std::optional<RenderPoint> ContainerChildReorderOffsetForRect(
        const DashboardOverlayState& overlayState, const RenderRect& rect) const;
    RenderRect ApplyContainerChildReorderOffset(const RenderRect& rect) const;
    RenderPoint ApplyContainerChildReorderOffset(RenderPoint point, const RenderRect& sourceRect) const;
    bool ShouldSkipForContainerChildReorder(const RenderRect& rect) const;
    void DrawContainerChildReorderOverlay(const MetricSource& metrics);

    DashboardRenderer& renderer_;
    DashboardLayoutResolver& layoutResolver_;
};
