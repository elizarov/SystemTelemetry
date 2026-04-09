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
#include <vector>

#include <windows.h>

#include "config.h"
#include "dashboard_metrics.h"

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

    enum class FontRole {
        Title,
        Big,
        Value,
        Label,
        Text,
        Small,
        Footer,
        ClockTime,
        ClockDate,
    };

    struct EditableTextKey {
        LayoutWidgetIdentity widget;
        FontRole fontRole = FontRole::Label;
        int textId = 0;
    };

    struct EditableTextRegion {
        EditableTextKey key;
        RECT textRect{};
        RECT anchorRect{};
        RECT anchorHitRect{};
        int fontSize = 0;
    };

    enum class BarEditParameter {
        MetricListBarHeight,
        DriveUsageBarHeight,
    };

    struct EditableBarKey {
        LayoutWidgetIdentity widget;
        BarEditParameter parameter = BarEditParameter::MetricListBarHeight;
        int barId = 0;
    };

    struct EditableBarRegion {
        EditableBarKey key;
        RECT barRect{};
        RECT anchorRect{};
        RECT anchorHitRect{};
        int value = 0;
    };

    enum class GaugeAnchorParameter {
        SegmentCount,
    };

    struct EditableGaugeKey {
        LayoutWidgetIdentity widget;
        GaugeAnchorParameter parameter = GaugeAnchorParameter::SegmentCount;
        int anchorId = 0;
    };

    struct EditableGaugeRegion {
        EditableGaugeKey key;
        RECT gaugeRect{};
        RECT anchorRect{};
        RECT anchorHitRect{};
        int value = 0;
    };

    enum class WidgetEditParameter {
        MetricListLabelWidth,
        MetricListVerticalGap,
        DriveUsageActivityWidth,
        DriveUsageFreeWidth,
        DriveUsageHeaderGap,
        DriveUsageRowGap,
        ThroughputAxisPadding,
        ThroughputHeaderGap,
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

    DashboardRenderer();
    ~DashboardRenderer();

    void SetConfig(const AppConfig& config);
    void SetRenderScale(double scale);
    void SetRenderMode(RenderMode mode);
    void BeginLayoutEditSession();
    void EndLayoutEditSession(bool showLayoutEditGuides);
    bool SetLayoutEditPreviewWidgetType(const std::string& widgetTypeName);
    void SetShowLayoutEditGuides(bool show);
    void SetActiveLayoutEditGuide(const std::optional<LayoutEditGuide>& guide);
    void SetHoveredEditableWidget(const std::optional<LayoutWidgetIdentity>& widget);
    void SetActiveWidgetEditGuide(const std::optional<WidgetEditGuide>& guide);
    void SetHoveredEditableText(const std::optional<EditableTextKey>& key);
    void SetActiveEditableText(const std::optional<EditableTextKey>& key);
    void SetHoveredEditableBar(const std::optional<EditableBarKey>& key);
    void SetActiveEditableBar(const std::optional<EditableBarKey>& key);
    void SetHoveredEditableGauge(const std::optional<EditableGaugeKey>& key);
    void SetActiveEditableGauge(const std::optional<EditableGaugeKey>& key);
    void SetSimilarityIndicatorMode(SimilarityIndicatorMode mode);
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
    std::optional<EditableTextKey> HitTestEditableText(POINT clientPoint) const;
    std::optional<EditableTextKey> HitTestEditableTextAnchor(POINT clientPoint) const;
    std::optional<EditableTextRegion> FindEditableTextRegion(const EditableTextKey& key) const;
    std::optional<EditableBarKey> HitTestEditableBar(POINT clientPoint) const;
    std::optional<EditableBarKey> HitTestEditableBarAnchor(POINT clientPoint) const;
    std::optional<EditableBarRegion> FindEditableBarRegion(const EditableBarKey& key) const;
    std::optional<EditableGaugeKey> HitTestEditableGaugeAnchor(POINT clientPoint) const;
    std::optional<EditableGaugeRegion> FindEditableGaugeRegion(const EditableGaugeKey& key) const;

    bool Initialize(HWND hwnd = nullptr);
    void Shutdown();

    void Draw(HDC hdc, const SystemSnapshot& snapshot);
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
    const std::string& LastError() const;

private:
    friend struct DashboardRendererLayoutEngine;

    struct WidgetBinding {
        std::string metric;
        std::string param;
    };

    enum class WidgetKind {
        Text,
        Gauge,
        MetricList,
        Throughput,
        NetworkFooter,
        Spacer,
        VerticalSpring,
        DriveUsageList,
        ClockTime,
        ClockDate,
        Unknown,
    };

    struct ResolvedWidgetLayout {
        WidgetKind kind = WidgetKind::Unknown;
        RECT rect{};
        std::string cardId;
        std::string editCardId;
        std::vector<size_t> nodePath;
        WidgetBinding binding;
        int preferredHeight = 0;
        bool fixedPreferredHeightInRows = false;
    };

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
        std::vector<ResolvedWidgetLayout> widgets;
    };

    struct ResolvedDashboardLayout {
        int windowWidth = 800;
        int windowHeight = 480;
        int globalGaugeRadius = 0;
        std::vector<ResolvedCardLayout> cards;
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
        int throughputLabel = 0;
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

    struct EditableTextBinding {
        EditableTextKey key;
        int fontSize = 0;
    };

    struct TextLayoutResult {
        RECT textRect{};
    };

    TextLayoutResult MeasureTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font, UINT format) const;
    TextLayoutResult DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font, COLORREF color,
        UINT format, const std::optional<EditableTextBinding>& editable = std::nullopt);
    void DrawHoveredWidgetHighlight(HDC hdc) const;
    void DrawHoveredEditableTextHighlight(HDC hdc) const;
    void DrawHoveredEditableBarHighlight(HDC hdc) const;
    void DrawHoveredEditableGaugeHighlight(HDC hdc) const;
    void DrawLayoutEditGuides(HDC hdc) const;
    void DrawWidgetEditGuides(HDC hdc) const;
    void DrawLayoutSimilarityIndicators(HDC hdc) const;
    void DrawPanel(HDC hdc, const ResolvedCardLayout& card);
    void DrawPanelIcon(HDC hdc, const std::string& iconName, const RECT& iconRect);
    void DrawResolvedWidget(HDC hdc, const ResolvedWidgetLayout& widget, const DashboardMetricSource& metrics);
    void DrawPillBar(HDC hdc, const RECT& rect, double ratio, std::optional<double> peakRatio, bool drawFill = true);
    void DrawGauge(HDC hdc, const ResolvedWidgetLayout& widget, int cx, int cy, int radius,
        const DashboardGaugeMetric& metric, const std::string& label);
    void DrawMetricRow(HDC hdc, const ResolvedWidgetLayout& widget, const RECT& rect, const DashboardMetricRow& row, int rowIndex);
    void DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue, double guideStepMbps,
        double timeMarkerOffsetSamples, double timeMarkerIntervalSamples,
        const std::optional<EditableTextBinding>& maxLabelEditable = std::nullopt);
    void DrawThroughputWidget(HDC hdc, const ResolvedWidgetLayout& widget, const RECT& rect, const DashboardThroughputMetric& metric);
    void DrawDriveUsageWidget(HDC hdc, const ResolvedWidgetLayout& widget, const RECT& rect, const std::vector<DashboardDriveRow>& rows);
    ResolvedWidgetLayout ResolveWidgetLayout(const LayoutNodeConfig& node, const RECT& rect) const;
    bool UsesFixedPreferredHeightInRows(const ResolvedWidgetLayout& widget) const;
    const LayoutCardConfig* FindCardConfigById(const std::string& id) const;
    void AddLayoutEditGuide(const LayoutNodeConfig& node, const RECT& rect, const std::vector<RECT>& childRects,
        int gap, const std::string& renderCardId, const std::string& editCardId, const std::vector<size_t>& nodePath);
    void ResolveNodeWidgetsInternal(const LayoutNodeConfig& node, const RECT& rect, std::vector<ResolvedWidgetLayout>& widgets,
        std::vector<std::string>& cardReferenceStack, const std::string& renderCardId, const std::string& editCardId,
        const std::vector<size_t>& nodePath);
    void BuildWidgetEditGuides();
    void ClearLayoutEditInteractionState();
    std::optional<LayoutWidgetIdentity> FindFirstLayoutEditPreviewWidget(const std::string& widgetTypeName) const;

    bool InitializeGdiplus();
    void ShutdownGdiplus();
    bool CreateFonts();
    void DestroyFonts();
    bool LoadPanelIcons();
    void ReleasePanelIcons();
    bool MeasureFonts();
    bool ResolveLayout();
    void ResolveNodeWidgets(const LayoutNodeConfig& node, const RECT& rect, std::vector<ResolvedWidgetLayout>& widgets);
    int PreferredNodeHeight(const LayoutNodeConfig& node, int width) const;
    int GaugeRadiusForRect(const RECT& rect) const;
    int EffectiveHeaderHeight() const;
    int EffectiveMetricRowHeight() const;
    int EffectiveDriveHeaderHeight() const;
    int EffectiveDriveRowHeight() const;
    bool SupportsLayoutSimilarityIndicator(const ResolvedWidgetLayout& widget) const;
    bool IsFirstWidgetForSimilarityIndicator(const ResolvedWidgetLayout& widget, LayoutGuideAxis axis) const;
    std::vector<const ResolvedWidgetLayout*> CollectSimilarityIndicatorWidgets(LayoutGuideAxis axis) const;
    int WidgetExtentForAxis(const ResolvedWidgetLayout& widget, LayoutGuideAxis axis) const;
    bool IsWidgetAffectedByGuide(const ResolvedWidgetLayout& widget, const LayoutEditGuide& guide) const;
    bool MatchesWidgetIdentity(const ResolvedWidgetLayout& widget, const LayoutWidgetIdentity& identity) const;
    bool MatchesEditableTextKey(const EditableTextKey& left, const EditableTextKey& right) const;
    bool MatchesEditableBarKey(const EditableBarKey& left, const EditableBarKey& right) const;
    bool MatchesEditableGaugeKey(const EditableGaugeKey& left, const EditableGaugeKey& right) const;
    bool MatchesLayoutEditGuide(const LayoutEditGuide& left, const LayoutEditGuide& right) const;
    bool MatchesWidgetEditGuide(const WidgetEditGuide& left, const WidgetEditGuide& right) const;
    EditableTextBinding MakeEditableTextBinding(const ResolvedWidgetLayout& widget, FontRole fontRole, int textId,
        int fontSize) const;
    void RegisterEditableBarRegion(const EditableBarKey& key, const RECT& barRect, int value);
    void RegisterEditableGaugeRegion(const EditableGaugeKey& key, const RECT& gaugeRect, const RECT& anchorRect, int value);
    static bool IsContainerNode(const LayoutNodeConfig& node);
    int ScaleLogical(int value) const;
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
    std::optional<LayoutEditGuide> activeLayoutEditGuide_;
    std::vector<WidgetEditGuide> widgetEditGuides_;
    std::vector<EditableTextRegion> editableTextRegions_;
    std::vector<EditableBarRegion> editableBarRegions_;
    std::vector<EditableGaugeRegion> editableGaugeRegions_;
    std::optional<LayoutWidgetIdentity> hoveredEditableWidget_;
    std::optional<WidgetEditGuide> activeWidgetEditGuide_;
    std::optional<EditableTextKey> hoveredEditableText_;
    std::optional<EditableTextKey> activeEditableText_;
    std::optional<EditableBarKey> hoveredEditableBar_;
    std::optional<EditableBarKey> activeEditableBar_;
    std::optional<EditableGaugeKey> hoveredEditableGauge_;
    std::optional<EditableGaugeKey> activeEditableGauge_;
    std::string lastError_;
    double renderScale_ = 1.0;
    RenderMode renderMode_ = RenderMode::Normal;
    SimilarityIndicatorMode similarityIndicatorMode_ = SimilarityIndicatorMode::ActiveGuide;
    bool showLayoutEditGuides_ = false;
};
