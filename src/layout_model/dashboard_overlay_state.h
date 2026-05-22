#pragma once

#include <optional>
#include <string>

#include "renderer/render_types.h"
#include "widget/layout_edit_types.h"

enum class LayoutSimilarityIndicatorMode {
    ActiveGuide,
    AllHorizontal,
    AllVertical,
};

enum class DashboardPlacementOverlayMode {
    Move,
    Resize,
};

struct DashboardMoveOverlayState {
    bool visible = false;
    DashboardPlacementOverlayMode mode = DashboardPlacementOverlayMode::Move;
    bool placeOnRelease = false;
    std::string monitorName;
    RenderPoint relativePosition{};
    double displayScale = 1.0;
};

struct DashboardOverlayState {
    bool showLayoutEditGuides = false;
    bool forceLayoutEditAffordances = false;
    bool forceHoverEquivalentAffordances = false;
    bool suppressLayoutGuideContainerHighlights = false;
    bool hoverOnExposedDashboard = false;
    bool drawExposedDashboardChrome = true;
    LayoutSimilarityIndicatorMode similarityIndicatorMode = LayoutSimilarityIndicatorMode::ActiveGuide;
    std::optional<LayoutEditGuide> activeLayoutEditGuide;
    std::optional<LayoutEditGuide> hoveredLayoutEditGuide;
    std::optional<LayoutEditWidgetIdentity> hoveredLayoutCard;
    std::optional<LayoutEditWidgetIdentity> hoveredEditableCard;
    std::optional<LayoutEditWidgetIdentity> hoveredEditableWidget;
    std::optional<LayoutEditWidgetGuide> activeWidgetEditGuide;
    std::optional<LayoutEditGapAnchorKey> hoveredGapEditAnchor;
    std::optional<LayoutEditGapAnchorKey> activeGapEditAnchor;
    std::optional<LayoutEditAnchorKey> hoveredEditableAnchor;
    std::optional<LayoutEditAnchorKey> activeEditableAnchor;
    std::optional<MetricListReorderOverlayState> activeMetricListReorderDrag;
    std::optional<ContainerChildReorderOverlayState> activeContainerChildReorderDrag;
    std::optional<LayoutEditSelectionHighlight> selectedTreeHighlight;
    DashboardMoveOverlayState moveOverlay{};

    bool ShouldDrawLayoutEditAffordances() const;
    bool IsContainerGuideDragActive() const;
    bool ShouldDrawSelectedTreeHighlight() const;
    bool ShouldRegisterDynamicEditArtifacts() const;
    bool ShouldDrawOverlayLayer() const;
    void SetPreviewWidget(const LayoutEditWidgetIdentity& widget);
};
