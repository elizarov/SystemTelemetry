#pragma once

#include <optional>
#include <vector>

#include "renderer/render_types.h"
#include "widget/layout_edit_types.h"

class DashboardLayoutResolver;
class DashboardRenderer;
class MetricSource;
struct DashboardOverlayState;

std::vector<LayoutEditAnchorRegion> CollectRelatedEditableAnchorHighlights(
    const std::vector<LayoutEditAnchorRegion>& staticRegions,
    const std::vector<LayoutEditAnchorRegion>& dynamicRegions,
    const LayoutEditAnchorRegion& source);

class DashboardLayoutEditOverlayRenderer {
public:
    DashboardLayoutEditOverlayRenderer(DashboardRenderer& renderer, DashboardLayoutResolver& layoutResolver);

    bool ShouldSkipBaseWidget(const DashboardOverlayState& overlayState, const WidgetLayout& widget) const;
    void Draw(const DashboardOverlayState& overlayState, const MetricSource& metrics);
    void DrawBackgroundAffordances(const DashboardOverlayState& overlayState) const;
    void DrawDraggedContent(const MetricSource& metrics);
    void DrawForegroundAffordances(const DashboardOverlayState& overlayState) const;

private:
    struct OverlayAffordanceRect {
        RenderRect rect{};
        const std::vector<LayoutEditOverlayOwner>* owners = nullptr;
        LayoutEditOverlayAffordanceLayer layer = LayoutEditOverlayAffordanceLayer::Background;
    };

    void DrawAffordances(const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const;
    void DrawHoveredWidgetHighlight(
        const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const;
    void DrawHoveredEditableAnchorHighlight(
        const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const;
    void DrawSelectedColorEditHighlights(
        const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const;
    void DrawSelectedTreeNodeHighlight(
        const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const;
    void DrawLayoutEditGuides(const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const;
    void DrawWidgetEditGuides(const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const;
    void DrawGapEditAnchors(const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const;
    std::optional<OverlayAffordanceRect> FindHoveredWidgetOutlineRect(const DashboardOverlayState& overlayState) const;
    void DrawDottedHighlightRect(const RenderRect& rect, RenderColorId color, bool active, bool outside = true) const;
    void DrawDottedAffordanceRect(
        const DashboardOverlayState& overlayState,
        LayoutEditOverlayAffordanceLayer layer,
        const RenderRect& rect,
        const std::vector<LayoutEditOverlayOwner>& owners,
        LayoutEditOverlayAffordanceLayer artifactLayer,
        RenderColorId color,
        bool active,
        bool outside = true) const;
    void DrawLayoutSimilarityIndicators(
        const DashboardOverlayState& overlayState, LayoutEditOverlayAffordanceLayer layer) const;
    std::optional<RenderPoint> OverlayDragOffsetForOwners(
        const DashboardOverlayState& overlayState, const std::vector<LayoutEditOverlayOwner>& owners) const;
    std::optional<RenderPoint> OverlayDragOffsetForAnchor(
        const DashboardOverlayState& overlayState, const LayoutEditAnchorRegion& region) const;
    bool ShouldDrawAffordanceLayer(
        LayoutEditOverlayAffordanceLayer artifactLayer, LayoutEditOverlayAffordanceLayer drawLayer) const;
    bool HasForegroundAffordanceLayer(const DashboardOverlayState& overlayState) const;
    RenderRect ApplyOverlayDragOffset(
        const DashboardOverlayState& overlayState,
        const RenderRect& rect,
        const std::vector<LayoutEditOverlayOwner>& owners) const;
    RenderPoint ApplyOverlayDragOffset(
        const DashboardOverlayState& overlayState,
        RenderPoint point,
        const std::vector<LayoutEditOverlayOwner>& owners) const;
    void ApplyOverlayDragOffset(const DashboardOverlayState& overlayState, LayoutEditAnchorRegion& region) const;
    bool ShouldSkipForContainerChildReorder(const WidgetLayout& widget) const;
    void DrawContainerChildReorderOverlay(const MetricSource& metrics);

    DashboardRenderer& renderer_;
    DashboardLayoutResolver& layoutResolver_;
};
