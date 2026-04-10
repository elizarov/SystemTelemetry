#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>

#include "config.h"
#include "dashboard_metrics.h"
#include "widget.h"

namespace Gdiplus {
class Bitmap;
}

class DashboardRenderer {
public:
    enum class LayoutGuideAxis {
        Horizontal,
        Vertical,
    };

    struct LayoutEditGuide {
        LayoutGuideAxis axis = LayoutGuideAxis::Horizontal;
        std::string renderCardId;
        std::string editCardId;
        std::vector<size_t> nodePath;
        size_t separatorIndex = 0;
        RECT containerRect{};
        RECT lineRect{};
        RECT hitRect{};
        int gap = 0;
        std::vector<int> childExtents;
        std::vector<bool> childFixedExtents;
        std::vector<RECT> childRects;
    };

    struct LayoutWidgetIdentity {
        std::string renderCardId;
        std::string editCardId;
        std::vector<size_t> nodePath;
    };

    enum class AnchorEditParameter {
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
        SegmentCount,
        DriveUsageActivitySegments,
        ThroughputGuideStrokeWidth,
        ThroughputPlotStrokeWidth,
        ThroughputLeaderDiameter,
        GaugeOuterPadding,
        GaugeRingThickness,
    };

    enum class AnchorShape {
        Circle,
        Diamond,
    };

    enum class AnchorDragAxis {
        Horizontal,
        Vertical,
        Both,
    };

    enum class AnchorDragMode {
        AxisDelta,
        RadialDistance,
    };

    struct EditableAnchorKey {
        LayoutWidgetIdentity widget;
        AnchorEditParameter parameter = AnchorEditParameter::MetricListBarHeight;
        int anchorId = 0;
    };

    struct EditableAnchorRegion {
        EditableAnchorKey key;
        RECT targetRect{};
        RECT anchorRect{};
        RECT anchorHitRect{};
        int anchorHitPadding = 0;
        AnchorShape shape = AnchorShape::Circle;
        AnchorDragAxis dragAxis = AnchorDragAxis::Vertical;
        AnchorDragMode dragMode = AnchorDragMode::AxisDelta;
        POINT dragOrigin{};
        double dragScale = 1.0;
        bool showWhenWidgetHovered = false;
        bool drawTargetOutline = true;
        int value = 0;
    };

    enum class WidgetEditParameter {
        MetricListLabelWidth,
        MetricListVerticalGap,
        DriveUsageLabelGap,
        DriveUsageBarGap,
        DriveUsageRwGap,
        DriveUsagePercentGap,
        DriveUsageActivityWidth,
        DriveUsageFreeWidth,
        DriveUsageActivitySegmentGap,
        DriveUsageHeaderGap,
        DriveUsageRowGap,
        ThroughputAxisPadding,
        ThroughputHeaderGap,
        ThroughputGuideStrokeWidth,
        ThroughputPlotStrokeWidth,
        ThroughputLeaderDiameter,
        GaugeOuterPadding,
        GaugeRingThickness,
        GaugeSweepDegrees,
        GaugeSegmentGapDegrees,
    };

    struct WidgetEditGuide {
        LayoutGuideAxis axis = LayoutGuideAxis::Vertical;
        LayoutWidgetIdentity widget;
        WidgetEditParameter parameter = WidgetEditParameter::DriveUsageActivityWidth;
        int guideId = 0;
        RECT widgetRect{};
        POINT drawStart{};
        POINT drawEnd{};
        RECT hitRect{};
        POINT dragOrigin{};
        double value = 0.0;
        bool angularDrag = false;
        double angularMin = 0.0;
        double angularMax = 0.0;
        int dragDirection = 1;
    };

    struct LayoutGuideSnapCandidate {
        LayoutWidgetIdentity widget;
        int targetExtent = 0;
        int startExtent = 0;
        int startDistance = 0;
        size_t groupOrder = 0;
    };

    enum class RenderMode {
        Normal,
        Blank,
    };

    enum class SimilarityIndicatorMode {
        ActiveGuide,
        AllHorizontal,
        AllVertical,
    };

    struct EditOverlayState {
        bool showLayoutEditGuides = false;
        SimilarityIndicatorMode similarityIndicatorMode = SimilarityIndicatorMode::ActiveGuide;
        std::optional<LayoutEditGuide> activeLayoutEditGuide;
        std::optional<LayoutWidgetIdentity> hoveredEditableWidget;
        std::optional<WidgetEditGuide> activeWidgetEditGuide;
        std::optional<EditableAnchorKey> hoveredEditableAnchor;
        std::optional<EditableAnchorKey> activeEditableAnchor;
    };

    struct FontHeights {
        int title = 0;
        int big = 0;
        int value = 0;
        int label = 0;
        int text = 0;
        int smallText = 0;
        int footer = 0;
        int clockTime = 0;
        int clockDate = 0;
    };

    struct MeasuredWidths {
        int throughputAxis = 0;
        int driveLabel = 0;
        int drivePercent = 0;
    };

    struct Fonts {
        HFONT title = nullptr;
        HFONT big = nullptr;
        HFONT value = nullptr;
        HFONT label = nullptr;
        HFONT text = nullptr;
        HFONT smallFont = nullptr;
        HFONT footer = nullptr;
        HFONT clockTime = nullptr;
        HFONT clockDate = nullptr;
    };

    struct EditableAnchorBinding {
        EditableAnchorKey key;
        int value = 0;
        AnchorShape shape = AnchorShape::Circle;
        AnchorDragAxis dragAxis = AnchorDragAxis::Vertical;
        AnchorDragMode dragMode = AnchorDragMode::AxisDelta;
    };

    struct TextLayoutResult {
        RECT textRect{};
    };

    DashboardRenderer();
    ~DashboardRenderer();

    void SetConfig(const AppConfig& config);
    void SetRenderScale(double scale);
    void SetRenderMode(RenderMode mode);
    bool SetLayoutEditPreviewWidgetType(EditOverlayState& overlayState, const std::string& widgetTypeName) const;
    double RenderScale() const;
    int WindowWidth() const;
    int WindowHeight() const;

    COLORREF BackgroundColor() const;
    COLORREF ForegroundColor() const;
    COLORREF AccentColor() const;
    COLORREF LayoutGuideColor() const;
    COLORREF ActiveEditColor() const;
    COLORREF MutedTextColor() const;
    HFONT LabelFont() const;
    HFONT SmallFont() const;
    void SetTraceOutput(std::ostream* traceOutput);
    const std::vector<LayoutEditGuide>& LayoutEditGuides() const;
    const std::vector<WidgetEditGuide>& WidgetEditGuides() const;
    int LayoutSimilarityThreshold() const;
    std::vector<LayoutGuideSnapCandidate> CollectLayoutGuideSnapCandidates(const LayoutEditGuide& guide) const;
    std::optional<int> FindLayoutWidgetExtent(const LayoutWidgetIdentity& widget, LayoutGuideAxis axis) const;
    std::optional<LayoutWidgetIdentity> HitTestEditableWidget(POINT clientPoint) const;
    std::optional<EditableAnchorKey> HitTestEditableAnchorTarget(POINT clientPoint) const;
    std::optional<EditableAnchorKey> HitTestEditableAnchorHandle(POINT clientPoint) const;
    std::optional<EditableAnchorRegion> FindEditableAnchorRegion(const EditableAnchorKey& key) const;

    bool Initialize(HWND hwnd = nullptr);
    void Shutdown();

    void Draw(HDC hdc, const SystemSnapshot& snapshot);
    void Draw(HDC hdc, const SystemSnapshot& snapshot, const EditOverlayState& overlayState);
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
    bool SaveSnapshotPng(
        const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const EditOverlayState& overlayState);
    const std::string& LastError() const;
    const AppConfig& Config() const;
    const FontHeights& FontMetrics() const;
    const MeasuredWidths& MeasuredTextWidths() const;
    const Fonts& WidgetFonts() const;
    RenderMode CurrentRenderMode() const;
    COLORREF TrackColor() const;
    TextLayoutResult MeasureTextBlock(
        HDC hdc, const RECT& rect, const std::string& text, HFONT font, UINT format) const;
    TextLayoutResult DrawTextBlock(HDC hdc,
        const RECT& rect,
        const std::string& text,
        HFONT font,
        COLORREF color,
        UINT format,
        const std::optional<EditableAnchorBinding>& editable = std::nullopt);
    void DrawPillBar(HDC hdc, const RECT& rect, double ratio, std::optional<double> peakRatio, bool drawFill = true);
    EditableAnchorBinding MakeEditableTextBinding(
        const DashboardWidgetLayout& widget, AnchorEditParameter parameter, int anchorId, int value) const;
    void RegisterEditableAnchorRegion(const EditableAnchorKey& key,
        const RECT& targetRect,
        const RECT& anchorRect,
        AnchorShape shape,
        AnchorDragAxis dragAxis,
        AnchorDragMode dragMode,
        POINT dragOrigin,
        double dragScale,
        bool showWhenWidgetHovered,
        bool drawTargetOutline,
        int value);
    std::vector<WidgetEditGuide>& WidgetEditGuidesMutable();
    int ScaleLogical(int value) const;

private:
    friend struct DashboardRendererLayoutEngine;

    struct SimilarityIndicator {
        LayoutGuideAxis axis = LayoutGuideAxis::Horizontal;
        RECT rect{};
        int exactTypeOrdinal = 0;
    };

    struct ResolvedCardLayout {
        std::string id;
        std::string title;
        std::string iconName;
        bool hasHeader = true;
        RECT rect{};
        RECT titleRect{};
        RECT iconRect{};
        RECT contentRect{};
        std::vector<DashboardWidgetLayout> widgets;
    };

    struct ResolvedDashboardLayout {
        int windowWidth = 800;
        int windowHeight = 480;
        std::vector<ResolvedCardLayout> cards;
    };

    struct ParsedWidgetInfo {
        std::unique_ptr<DashboardWidget> widgetPrototype;
        int preferredHeight = 0;
        bool fixedPreferredHeightInRows = false;
        bool verticalSpring = false;
    };

    void DrawHoveredWidgetHighlight(HDC hdc, const EditOverlayState& overlayState) const;
    void DrawHoveredEditableAnchorHighlight(HDC hdc, const EditOverlayState& overlayState) const;
    void DrawLayoutEditGuides(HDC hdc, const EditOverlayState& overlayState) const;
    void DrawWidgetEditGuides(HDC hdc, const EditOverlayState& overlayState) const;
    void DrawLayoutSimilarityIndicators(HDC hdc, const EditOverlayState& overlayState) const;
    void DrawPanel(HDC hdc, const ResolvedCardLayout& card);
    void DrawPanelIcon(HDC hdc, const std::string& iconName, const RECT& iconRect);
    void DrawResolvedWidget(HDC hdc, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics);
    const ParsedWidgetInfo* FindParsedWidgetInfo(const LayoutNodeConfig& node) const;
    DashboardWidgetLayout ResolveWidgetLayout(const LayoutNodeConfig& node, const RECT& rect) const;
    bool UsesFixedPreferredHeightInRows(const DashboardWidgetLayout& widget) const;
    const LayoutCardConfig* FindCardConfigById(const std::string& id) const;
    void AddLayoutEditGuide(const LayoutNodeConfig& node,
        const RECT& rect,
        const std::vector<RECT>& childRects,
        int gap,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath);
    void ResolveNodeWidgetsInternal(const LayoutNodeConfig& node,
        const RECT& rect,
        std::vector<DashboardWidgetLayout>& widgets,
        std::vector<std::string>& cardReferenceStack,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath);
    void BuildWidgetEditGuides();
    std::optional<LayoutWidgetIdentity> FindFirstLayoutEditPreviewWidget(const std::string& widgetTypeName) const;

    bool InitializeGdiplus();
    void ShutdownGdiplus();
    bool CreateFonts();
    void DestroyFonts();
    bool LoadPanelIcons();
    void ReleasePanelIcons();
    bool MeasureFonts();
    bool ResolveLayout();
    void ResolveNodeWidgets(
        const LayoutNodeConfig& node, const RECT& rect, std::vector<DashboardWidgetLayout>& widgets);
    int PreferredNodeHeight(const LayoutNodeConfig& node, int width) const;
    int EffectiveHeaderHeight() const;
    bool SupportsLayoutSimilarityIndicator(const DashboardWidgetLayout& widget) const;
    bool IsFirstWidgetForSimilarityIndicator(const DashboardWidgetLayout& widget, LayoutGuideAxis axis) const;
    std::vector<const DashboardWidgetLayout*> CollectSimilarityIndicatorWidgets(LayoutGuideAxis axis) const;
    int WidgetExtentForAxis(const DashboardWidgetLayout& widget, LayoutGuideAxis axis) const;
    bool IsWidgetAffectedByGuide(const DashboardWidgetLayout& widget, const LayoutEditGuide& guide) const;
    bool MatchesWidgetIdentity(const DashboardWidgetLayout& widget, const LayoutWidgetIdentity& identity) const;
    bool MatchesEditableAnchorKey(const EditableAnchorKey& left, const EditableAnchorKey& right) const;
    bool MatchesLayoutEditGuide(const LayoutEditGuide& left, const LayoutEditGuide& right) const;
    bool MatchesWidgetEditGuide(const WidgetEditGuide& left, const WidgetEditGuide& right) const;
    static bool IsContainerNode(const LayoutNodeConfig& node);
    void WriteTrace(const std::string& text) const;

    AppConfig config_;
    HWND hwnd_ = nullptr;
    std::ostream* traceOutput_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    std::vector<std::pair<std::string, std::unique_ptr<Gdiplus::Bitmap>>> panelIcons_;
    Fonts fonts_{};
    FontHeights fontHeights_{};
    MeasuredWidths measuredWidths_{};
    ResolvedDashboardLayout resolvedLayout_{};
    std::vector<LayoutEditGuide> layoutEditGuides_;
    std::vector<WidgetEditGuide> widgetEditGuides_;
    std::vector<EditableAnchorRegion> editableAnchorRegions_;
    mutable std::unordered_map<const LayoutNodeConfig*, ParsedWidgetInfo> parsedWidgetInfoCache_;
    std::string lastError_;
    double renderScale_ = 1.0;
    RenderMode renderMode_ = RenderMode::Normal;
};
