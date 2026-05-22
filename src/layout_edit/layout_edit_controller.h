#pragma once

#include <windows.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "layout_model/dashboard_overlay_state.h"
#include "layout_model/layout_edit_active_region.h"
#include "layout_model/layout_edit_layout_target.h"
#include "widget/layout_edit_parameter_id.h"
#include "widget/layout_edit_types.h"

struct AppConfig;

class LayoutEditHost {
public:
    virtual ~LayoutEditHost() = default;

    enum class TracePhase {
        Snap,
        Apply,
        PaintTotal,
        PaintDraw,
    };

    virtual const AppConfig& LayoutEditConfig() const = 0;
    virtual DashboardOverlayState& LayoutDashboardOverlayState() = 0;
    virtual LayoutEditActiveRegions CollectLayoutEditActiveRegions() const = 0;
    virtual LayoutEditHoverResolution ResolveLayoutEditHover(RenderPoint clientPoint) const;
    virtual double LayoutEditRenderScale() const = 0;
    virtual int LayoutEditSimilarityThreshold() const = 0;
    virtual void SetLayoutGuideDragActive(bool active) = 0;
    virtual void SetLayoutEditInteractiveDragTraceActive(bool active) = 0;
    virtual void RebuildLayoutEditArtifacts() = 0;
    virtual bool ApplyLayoutGuideWeights(const LayoutEditLayoutTarget& target, const std::vector<int>& weights) = 0;
    virtual bool ApplyMetricListOrder(
        const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs) = 0;
    virtual bool ApplyContainerChildOrder(const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex) = 0;
    virtual std::optional<int> EvaluateLayoutWidgetExtentForWeights(const LayoutEditLayoutTarget& target,
        const std::vector<int>& weights,
        const LayoutEditWidgetIdentity& widget,
        LayoutGuideAxis axis) = 0;
    virtual bool ApplyLayoutEditValue(LayoutEditParameter parameter, double value) = 0;
    virtual void InvalidateLayoutEdit() = 0;
    virtual void BeginLayoutEditTraceSession(const char* kind, const std::string& detail) = 0;
    virtual void RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) = 0;
    virtual void EndLayoutEditTraceSession(const char* reason) = 0;
};

class LayoutEditController {
public:
    struct TooltipTarget {
        RenderPoint clientPoint{};
        TooltipPayload payload;
    };

    explicit LayoutEditController(LayoutEditHost& host);

    void StartSession();
    void StopSession(bool showLayoutEditGuidesAfterStop);

    bool HandleLButtonDown(HWND hwnd, RenderPoint clientPoint);
    bool HandleMouseMove(RenderPoint clientPoint);
    bool HandleMouseLeave();
    bool HandleLButtonUp(RenderPoint clientPoint);
    bool HandleCaptureChanged(HWND hwnd, HWND newCaptureOwner);
    bool HandleSetCursor(HWND hwnd);
    bool HasActiveDrag() const;
    void CancelInteraction();
    bool CurrentTooltipTarget(TooltipTarget& target);

private:
    struct ExtentCacheKey {
        std::vector<int> weights;
        LayoutEditWidgetIdentity widget;

        bool operator==(const ExtentCacheKey& other) const {
            return weights == other.weights && widget.kind == other.widget.kind &&
                widget.renderCardId == other.widget.renderCardId && widget.editCardId == other.widget.editCardId &&
                widget.nodePath == other.widget.nodePath;
        }
    };

    struct ExtentCacheEntry {
        ExtentCacheKey key;
        bool hasExtent = false;
        int extent = 0;
    };

    struct LayoutDragState {
        LayoutEditGuide guide;
        std::vector<int> initialWeights;
        std::vector<LayoutGuideSnapCandidate> snapCandidates;
        int dragStartCoordinate = 0;
        // Size: drag sessions reuse only a few extent keys; flat entries beat hash-table machinery.
        std::vector<ExtentCacheEntry> extentCache;
    };

    struct WidgetEditDragState {
        LayoutEditWidgetGuide guide;
        double initialValue = 0.0;
        int dragStartCoordinate = 0;
    };

    struct AnchorEditDragState {
        LayoutEditAnchorKey key;
        AnchorDragAxis dragAxis = AnchorDragAxis::Vertical;
        AnchorDragMode dragMode = AnchorDragMode::AxisDelta;
        RenderPoint dragOrigin{};
        double dragScale = 1.0;
        int initialValue = 0;
        RenderPoint dragStartPoint{};
        double dragStartDistancePixels = 0.0;
    };

    struct GapEditDragState {
        LayoutEditGapAnchor anchor;
        double initialValue = 0.0;
        int dragStartCoordinate = 0;
    };

    struct MetricListReorderDragState {
        LayoutEditWidgetIdentity widget;
        std::vector<std::string> metricRefs;
        int rowTop = 0;
        int rowHeight = 1;
        int rowCount = 0;
        int currentIndex = 0;
        int dragOffsetY = 0;
        int mouseY = 0;
    };

    struct ContainerChildReorderDragState {
        LayoutEditWidgetIdentity widget;
        LayoutContainerChildOrderEditKey key;
        std::vector<RenderRect> childRects;
        bool horizontal = false;
        std::vector<RenderRect> stableChildRects;
        int currentIndex = 0;
        int childCount = 0;
        int dragOffset = 0;
        int mouseCoordinate = 0;
    };

    LayoutEditActiveRegions ActiveRegions() const;
    LayoutEditHoverResolution ResolveHover(RenderPoint clientPoint) const;
    void RefreshHover(RenderPoint clientPoint);
    bool UpdateLayoutDrag(RenderPoint clientPoint);
    bool UpdateWidgetEditDrag(RenderPoint clientPoint);
    bool UpdateGapEditDrag(RenderPoint clientPoint);
    bool UpdateAnchorEditDrag(RenderPoint clientPoint);
    bool UpdateMetricListReorderDrag(RenderPoint clientPoint);
    bool UpdateContainerChildReorderDrag(RenderPoint clientPoint);
    void RefreshContainerChildReorderRects(ContainerChildReorderDragState& drag);
    void SyncRendererInteractionState();
    void ClearInteractionState();
    void SetCursorForPoint(RenderPoint clientPoint);
    bool FindSnappedLayoutGuideWeights(LayoutDragState& drag, std::vector<int>& weights);

    LayoutEditHost& host_;
    std::optional<LayoutEditGuide> hoveredLayoutGuide_;
    std::optional<LayoutEditWidgetIdentity> hoveredLayoutCard_;
    std::optional<LayoutEditWidgetIdentity> hoveredEditableCard_;
    std::optional<LayoutEditWidgetIdentity> hoveredEditableWidget_;
    std::optional<LayoutEditGapAnchor> hoveredGapEditAnchorRegion_;
    std::optional<LayoutEditWidgetGuide> hoveredWidgetEditGuide_;
    std::optional<LayoutEditGapAnchorKey> hoveredGapEditAnchor_;
    std::optional<LayoutEditAnchorKey> hoveredEditableAnchor_;
    std::optional<LayoutDragState> activeLayoutDrag_;
    std::optional<WidgetEditDragState> activeWidgetEditDrag_;
    std::optional<GapEditDragState> activeGapEditDrag_;
    std::optional<AnchorEditDragState> activeAnchorEditDrag_;
    std::optional<MetricListReorderDragState> activeMetricListReorderDrag_;
    std::optional<ContainerChildReorderDragState> activeContainerChildReorderDrag_;
    RenderPoint lastClientPoint_{};
    bool hasLastClientPoint_ = false;
};
