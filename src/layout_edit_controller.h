#pragma once

#include <optional>
#include <vector>

#include <windows.h>

#include "config.h"
#include "dashboard_renderer.h"

class LayoutEditHost {
public:
    virtual ~LayoutEditHost() = default;

    struct LayoutTarget {
        std::string editCardId;
        std::vector<size_t> nodePath;

        static LayoutTarget ForGuide(const DashboardRenderer::LayoutEditGuide& guide);
    };

    virtual const AppConfig& LayoutEditConfig() const = 0;
    virtual DashboardRenderer& LayoutEditRenderer() = 0;
    virtual DashboardRenderer::EditOverlayState& LayoutEditOverlayState() = 0;
    virtual bool ApplyLayoutGuideWeights(const LayoutTarget& target, const std::vector<int>& weights) = 0;
    virtual std::optional<int> EvaluateLayoutWidgetExtentForWeights(const LayoutTarget& target,
        const std::vector<int>& weights,
        const DashboardRenderer::LayoutWidgetIdentity& widget,
        DashboardRenderer::LayoutGuideAxis axis) = 0;
    virtual bool ApplyLayoutEditValue(DashboardRenderer::LayoutEditParameter parameter, double value) = 0;
    virtual void InvalidateLayoutEdit() = 0;
};

class LayoutEditController {
public:
    struct TooltipTarget {
        enum class Kind {
            LayoutGuide,
            WidgetGuide,
            EditableAnchor,
        };

        Kind kind = Kind::LayoutGuide;
        POINT clientPoint{};
        DashboardRenderer::LayoutEditGuide layoutGuide{};
        DashboardRenderer::WidgetEditGuide widgetGuide{};
        DashboardRenderer::EditableAnchorRegion editableAnchor{};
    };

    explicit LayoutEditController(LayoutEditHost& host);

    void StartSession();
    void StopSession(bool showLayoutEditGuidesAfterStop);

    bool HandleLButtonDown(HWND hwnd, POINT clientPoint);
    bool HandleMouseMove(POINT clientPoint);
    bool HandleMouseLeave();
    bool HandleLButtonUp(POINT clientPoint);
    bool HandleCaptureChanged(HWND hwnd, HWND newCaptureOwner);
    bool HandleSetCursor(HWND hwnd);
    std::optional<TooltipTarget> CurrentTooltipTarget();

private:
    struct HoverResolution {
        std::optional<DashboardRenderer::LayoutWidgetIdentity> hoveredEditableWidget;
        std::optional<DashboardRenderer::EditableAnchorKey> hoveredEditableAnchor;
        std::optional<size_t> hoveredWidgetEditGuideIndex;
        std::optional<size_t> hoveredLayoutGuideIndex;
        std::optional<DashboardRenderer::EditableAnchorKey> actionableAnchorHandle;
    };

    struct LayoutDragState {
        DashboardRenderer::LayoutEditGuide guide;
        std::vector<int> initialWeights;
        std::vector<DashboardRenderer::LayoutGuideSnapCandidate> snapCandidates;
        int dragStartCoordinate = 0;
    };

    struct WidgetEditDragState {
        DashboardRenderer::WidgetEditGuide guide;
        double initialValue = 0.0;
        int dragStartCoordinate = 0;
    };

    struct AnchorEditDragState {
        DashboardRenderer::EditableAnchorKey key;
        DashboardRenderer::AnchorDragAxis dragAxis = DashboardRenderer::AnchorDragAxis::Vertical;
        DashboardRenderer::AnchorDragMode dragMode = DashboardRenderer::AnchorDragMode::AxisDelta;
        POINT dragOrigin{};
        double dragScale = 1.0;
        int initialValue = 0;
        POINT dragStartPoint{};
        double dragStartDistancePixels = 0.0;
    };

    const DashboardRenderer::LayoutEditGuide* HitTestLayoutGuide(POINT clientPoint, size_t* index = nullptr) const;
    const DashboardRenderer::WidgetEditGuide* HitTestWidgetEditGuide(POINT clientPoint, size_t* index = nullptr) const;
    HoverResolution ResolveHover(POINT clientPoint) const;
    void RefreshHover(POINT clientPoint);
    bool UpdateLayoutDrag(POINT clientPoint);
    bool UpdateWidgetEditDrag(POINT clientPoint);
    bool UpdateAnchorEditDrag(POINT clientPoint);
    void SyncRendererInteractionState();
    void ClearInteractionState();
    void SetCursorForPoint(POINT clientPoint);
    std::optional<std::vector<int>> FindSnappedLayoutGuideWeights(
        const LayoutDragState& drag, const std::vector<int>& freeWeights);

    LayoutEditHost& host_;
    std::optional<size_t> hoveredLayoutGuideIndex_;
    std::optional<DashboardRenderer::LayoutWidgetIdentity> hoveredEditableWidget_;
    std::optional<size_t> hoveredWidgetEditGuideIndex_;
    std::optional<DashboardRenderer::EditableAnchorKey> hoveredEditableAnchor_;
    std::optional<LayoutDragState> activeLayoutDrag_;
    std::optional<WidgetEditDragState> activeWidgetEditDrag_;
    std::optional<AnchorEditDragState> activeAnchorEditDrag_;
    POINT lastClientPoint_{};
};
