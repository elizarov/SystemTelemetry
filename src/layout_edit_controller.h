#pragma once

#include <optional>
#include <vector>

#include <windows.h>

#include "config.h"
#include "dashboard_renderer.h"

class LayoutEditHost {
public:
    virtual ~LayoutEditHost() = default;

    struct ValueTarget {
        enum class Field {
            MetricListLabelWidth,
            MetricListVerticalGap,
            DriveUsageActivityWidth,
            DriveUsageFreeWidth,
            DriveUsageHeaderGap,
            DriveUsageRowGap,
            DriveUsageActivitySegments,
            ThroughputAxisPadding,
            ThroughputHeaderGap,
            GaugeSweepDegrees,
            GaugeSegmentGapDegrees,
            FontTitle,
            FontBig,
            FontValue,
            FontLabel,
            FontText,
            FontSmall,
            FontFooter,
            FontClockTime,
            FontClockDate,
            MetricListBarHeight,
            DriveUsageBarHeight,
            GaugeSegmentCount,
        };

        Field field = Field::MetricListLabelWidth;

        static ValueTarget ForWidgetGuide(const DashboardRenderer::WidgetEditGuide& guide);
        static ValueTarget ForEditableText(const DashboardRenderer::EditableTextKey& key);
        static ValueTarget ForEditableAnchor(const DashboardRenderer::EditableAnchorKey& key);
    };

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
        const std::vector<int>& weights, const DashboardRenderer::LayoutWidgetIdentity& widget,
        DashboardRenderer::LayoutGuideAxis axis) = 0;
    virtual bool ApplyLayoutEditValue(const ValueTarget& target, double value) = 0;
    virtual void InvalidateLayoutEdit() = 0;
};

class LayoutEditController {
public:
    explicit LayoutEditController(LayoutEditHost& host);

    void StartSession();
    void StopSession(bool showLayoutEditGuidesAfterStop);

    bool HandleLButtonDown(HWND hwnd, POINT clientPoint);
    bool HandleMouseMove(POINT clientPoint);
    bool HandleLButtonUp(POINT clientPoint);
    bool HandleCaptureChanged(HWND hwnd, HWND newCaptureOwner);
    bool HandleSetCursor(HWND hwnd);

private:
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

    struct TextEditDragState {
        DashboardRenderer::EditableTextKey key;
        int initialValue = 0;
        int dragStartCoordinate = 0;
    };

    struct AnchorEditDragState {
        DashboardRenderer::EditableAnchorKey key;
        DashboardRenderer::LayoutGuideAxis dragAxis = DashboardRenderer::LayoutGuideAxis::Vertical;
        int initialValue = 0;
        int dragStartCoordinate = 0;
    };

    const DashboardRenderer::LayoutEditGuide* HitTestLayoutGuide(POINT clientPoint, size_t* index = nullptr) const;
    const DashboardRenderer::WidgetEditGuide* HitTestWidgetEditGuide(POINT clientPoint, size_t* index = nullptr) const;
    void RefreshHover(POINT clientPoint);
    bool UpdateLayoutDrag(POINT clientPoint);
    bool UpdateWidgetEditDrag(POINT clientPoint);
    bool UpdateTextEditDrag(POINT clientPoint);
    bool UpdateAnchorEditDrag(POINT clientPoint);
    void SyncRendererInteractionState();
    void ClearInteractionState();
    void SetCursorForPoint(POINT clientPoint);
    std::optional<std::vector<int>> FindSnappedLayoutGuideWeights(const LayoutDragState& drag,
        const std::vector<int>& freeWeights);

    LayoutEditHost& host_;
    std::optional<size_t> hoveredLayoutGuideIndex_;
    std::optional<DashboardRenderer::LayoutWidgetIdentity> hoveredEditableWidget_;
    std::optional<size_t> hoveredWidgetEditGuideIndex_;
    std::optional<DashboardRenderer::EditableTextKey> hoveredEditableText_;
    std::optional<DashboardRenderer::EditableTextKey> hoveredEditableTextAnchor_;
    std::optional<DashboardRenderer::EditableAnchorKey> hoveredEditableAnchor_;
    std::optional<LayoutDragState> activeLayoutDrag_;
    std::optional<WidgetEditDragState> activeWidgetEditDrag_;
    std::optional<TextEditDragState> activeTextEditDrag_;
    std::optional<AnchorEditDragState> activeAnchorEditDrag_;
};
