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
#include "layout_edit_parameter_id.h"
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

    using LayoutEditParameter = ::LayoutEditParameter;

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
        LayoutEditParameter parameter = LayoutEditParameter::MetricListBarHeight;
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

    struct WidgetEditGuide {
        LayoutGuideAxis axis = LayoutGuideAxis::Vertical;
        LayoutWidgetIdentity widget;
        LayoutEditParameter parameter = LayoutEditParameter::DriveUsageActivityWidth;
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
    void SetLayoutGuideDragActive(bool active);
    void SetInteractiveDragTraceActive(bool active);
    void RebuildEditArtifacts();
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
    const Fonts& WidgetFonts() const;
    RenderMode CurrentRenderMode() const;
    COLORREF TrackColor() const;
    TextLayoutResult MeasureTextBlock(
        HDC hdc, const RECT& rect, const std::string& text, HFONT font, UINT format) const;
    TextLayoutResult MeasureTextBlock(const RECT& rect, const std::string& text, HFONT font, UINT format) const;
    void DrawText(HDC hdc, const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format) const;
    TextLayoutResult DrawTextBlock(
        HDC hdc, const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format);
    void DrawPillBar(HDC hdc, const RECT& rect, double ratio, std::optional<double> peakRatio, bool drawFill = true);
    HBRUSH SolidBrush(COLORREF color);
    HPEN SolidPen(COLORREF color, int width = 1);
    EditableAnchorBinding MakeEditableTextBinding(
        const DashboardWidgetLayout& widget, LayoutEditParameter parameter, int anchorId, int value) const;
    void RegisterStaticEditableAnchorRegion(const EditableAnchorKey& key,
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
    void RegisterDynamicEditableAnchorRegion(const EditableAnchorKey& key,
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
    void RegisterStaticTextAnchor(
        const RECT& rect, const std::string& text, HFONT font, UINT format, const EditableAnchorBinding& editable);
    void RegisterDynamicTextAnchor(
        HDC hdc, const RECT& rect, const std::string& text, HFONT font, UINT format, const EditableAnchorBinding& editable);
    void RegisterDynamicTextAnchor(
        const RECT& rect, const std::string& text, HFONT font, UINT format, const EditableAnchorBinding& editable);
    std::vector<WidgetEditGuide>& WidgetEditGuidesMutable();
    int ScaleLogical(int value) const;
    int MeasureTextWidth(HFONT font, std::string_view text) const;

private:
    friend struct DashboardLayoutResolver;

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

    struct TextWidthCacheKey {
        HFONT font = nullptr;
        std::string text;

        bool operator==(const TextWidthCacheKey& other) const {
            return font == other.font && text == other.text;
        }
    };

    struct TextWidthCacheKeyHash {
        size_t operator()(const TextWidthCacheKey& key) const {
            return (std::hash<HFONT>{}(key.font) * 1315423911u) ^ std::hash<std::string>{}(key.text);
        }
    };

    struct TextMeasureCacheKey {
        HFONT font = nullptr;
        std::string text;
        UINT format = 0;
        int width = 0;
        int height = 0;

        bool operator==(const TextMeasureCacheKey& other) const {
            return font == other.font && text == other.text && format == other.format && width == other.width &&
                   height == other.height;
        }
    };

    struct TextMeasureCacheKeyHash {
        size_t operator()(const TextMeasureCacheKey& key) const {
            size_t hash = std::hash<HFONT>{}(key.font);
            hash = (hash * 1315423911u) ^ std::hash<std::string>{}(key.text);
            hash = (hash * 1315423911u) ^ std::hash<UINT>{}(key.format);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.width);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.height);
            return hash;
        }
    };

    struct PenCacheKey {
        COLORREF color = 0;
        int width = 1;

        bool operator==(const PenCacheKey& other) const {
            return color == other.color && width == other.width;
        }
    };

    struct AlphaCapsuleCacheKey {
        COLORREF color = 0;
        BYTE alpha = 255;
        int width = 0;
        int height = 0;

        bool operator==(const AlphaCapsuleCacheKey& other) const {
            return color == other.color && alpha == other.alpha && width == other.width && height == other.height;
        }
    };

    struct AlphaCapsuleCacheKeyHash {
        size_t operator()(const AlphaCapsuleCacheKey& key) const {
            size_t hash = std::hash<COLORREF>{}(key.color);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.alpha);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.width);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.height);
            return hash;
        }
    };

    struct AlphaCapsuleBitmap {
        HDC hdc = nullptr;
        HBITMAP bitmap = nullptr;
    };

    struct PanelIconCacheKey {
        std::string name;
        int width = 0;
        int height = 0;

        bool operator==(const PanelIconCacheKey& other) const {
            return name == other.name && width == other.width && height == other.height;
        }
    };

    struct PanelIconCacheKeyHash {
        size_t operator()(const PanelIconCacheKey& key) const {
            size_t hash = std::hash<std::string>{}(key.name);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.width);
            hash = (hash * 1315423911u) ^ std::hash<int>{}(key.height);
            return hash;
        }
    };

    struct PenCacheKeyHash {
        size_t operator()(const PenCacheKey& key) const {
            return (std::hash<COLORREF>{}(key.color) * 1315423911u) ^ std::hash<int>{}(key.width);
        }
    };

    const std::wstring& GetWideText(std::string_view text) const;
    void ClearGdiCaches();
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
    void BuildStaticEditableAnchors();
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
    void RegisterEditableAnchorRegion(std::vector<EditableAnchorRegion>& regions,
        const EditableAnchorKey& key,
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
    void RegisterTextAnchor(std::vector<EditableAnchorRegion>& regions,
        const RECT& rect,
        const std::string& text,
        HDC measureHdc,
        HFONT font,
        UINT format,
        const EditableAnchorBinding& editable);
    void DrawAlphaCapsule(HDC hdc, const RECT& rect, COLORREF color, BYTE alpha);
    void WriteTrace(const std::string& text) const;

    AppConfig config_;
    HWND hwnd_ = nullptr;
    std::ostream* traceOutput_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    std::vector<std::pair<std::string, std::unique_ptr<Gdiplus::Bitmap>>> panelIcons_;
    Fonts fonts_{};
    FontHeights fontHeights_{};
    ResolvedDashboardLayout resolvedLayout_{};
    std::vector<LayoutEditGuide> layoutEditGuides_;
    std::vector<WidgetEditGuide> widgetEditGuides_;
    std::vector<EditableAnchorRegion> staticEditableAnchorRegions_;
    std::vector<EditableAnchorRegion> dynamicEditableAnchorRegions_;
    bool dynamicAnchorRegistrationEnabled_ = false;
    mutable std::unordered_map<const LayoutNodeConfig*, ParsedWidgetInfo> parsedWidgetInfoCache_;
    mutable std::unordered_map<std::string, std::wstring> wideTextCache_;
    mutable std::unordered_map<TextWidthCacheKey, int, TextWidthCacheKeyHash> textWidthCache_;
    mutable std::unordered_map<TextWidthCacheKey, SIZE, TextWidthCacheKeyHash> textExtentCache_;
    mutable std::unordered_map<TextMeasureCacheKey, SIZE, TextMeasureCacheKeyHash> textMeasureCache_;
    std::unordered_map<COLORREF, HBRUSH> solidBrushCache_;
    std::unordered_map<PenCacheKey, HPEN, PenCacheKeyHash> solidPenCache_;
    std::unordered_map<AlphaCapsuleCacheKey, AlphaCapsuleBitmap, AlphaCapsuleCacheKeyHash> alphaCapsuleCache_;
    std::unordered_map<PanelIconCacheKey, AlphaCapsuleBitmap, PanelIconCacheKeyHash> scaledPanelIconCache_;
    std::string lastError_;
    double renderScale_ = 1.0;
    RenderMode renderMode_ = RenderMode::Normal;
    bool layoutGuideDragActive_ = false;
    bool interactiveDragTraceActive_ = false;
};
