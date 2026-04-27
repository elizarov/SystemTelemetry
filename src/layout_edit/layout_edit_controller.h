#pragma once

#include <windows.h>

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/config.h"
#include "layout_edit/layout_edit_hit_test.h"
#include "layout_model/dashboard_overlay_state.h"
#include "layout_model/layout_edit_active_region.h"
#include "layout_model/layout_edit_layout_target.h"
#include "widget/layout_edit_parameter_id.h"
#include "widget/layout_edit_types.h"

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
    virtual void BeginLayoutEditTraceSession(const std::string& kind, const std::string& detail) = 0;
    virtual void RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) = 0;
    virtual void EndLayoutEditTraceSession(const std::string& reason) = 0;
};

class LayoutEditController {
public:
    struct TooltipTarget {
        std::optional<RenderPoint> clientPoint;
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
    std::optional<TooltipTarget> CurrentTooltipTarget();

private:
    struct WeightVectorHash {
        size_t operator()(const std::vector<int>& weights) const {
            size_t hash = 0;
            for (int weight : weights) {
                hash = (hash * 1315423911u) ^ std::hash<int>{}(weight);
            }
            return hash;
        }
    };

    struct ExtentCacheKey {
        std::vector<int> weights;
        LayoutEditWidgetIdentity widget;

        bool operator==(const ExtentCacheKey& other) const {
            return weights == other.weights && widget.kind == other.widget.kind &&
                   widget.renderCardId == other.widget.renderCardId && widget.editCardId == other.widget.editCardId &&
                   widget.nodePath == other.widget.nodePath;
        }
    };

    struct ExtentCacheKeyHash {
        size_t operator()(const ExtentCacheKey& key) const {
            size_t hash = WeightVectorHash{}(key.weights);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(static_cast<int>(key.widget.kind));
            hash = (hash * 1315423911u) ^ std::hash<std::string>{}(key.widget.renderCardId);
            hash = (hash * 1315423911u) ^ std::hash<std::string>{}(key.widget.editCardId);
            for (size_t index : key.widget.nodePath) {
                hash = (hash * 1315423911u) ^ std::hash<size_t>{}(index);
            }
            return hash;
        }
    };

    struct LayoutDragState {
        LayoutEditGuide guide;
        std::vector<int> initialWeights;
        std::vector<LayoutGuideSnapCandidate> snapCandidates;
        int dragStartCoordinate = 0;
        std::unordered_map<ExtentCacheKey, std::optional<int>, ExtentCacheKeyHash> extentCache;
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
        std::vector<int> stableSnapCenters;
        int originalIndex = 0;
        int currentIndex = 0;
        int childCount = 0;
        int containerStart = 0;
        int draggedExtent = 1;
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
    std::optional<std::vector<int>> FindSnappedLayoutGuideWeights(
        LayoutDragState& drag, const std::vector<int>& freeWeights);

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
    std::optional<RenderPoint> lastClientPoint_;
};
