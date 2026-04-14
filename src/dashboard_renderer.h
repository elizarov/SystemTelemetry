#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

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
    bool ApplyLayoutGuideWeightsPreview(
        const std::string& editCardId, const std::vector<size_t>& nodePath, const std::vector<int>& weights);
    std::optional<LayoutWidgetIdentity> HitTestEditableWidget(POINT clientPoint) const;
    std::optional<EditableAnchorKey> HitTestEditableAnchorTarget(POINT clientPoint) const;
    std::optional<EditableAnchorKey> HitTestEditableAnchorHandle(POINT clientPoint) const;
    std::optional<EditableAnchorRegion> FindEditableAnchorRegion(const EditableAnchorKey& key) const;

    bool Initialize(HWND hwnd = nullptr);
    void Shutdown();

    bool DrawWindow(const SystemSnapshot& snapshot);
    bool DrawWindow(const SystemSnapshot& snapshot, const EditOverlayState& overlayState);
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
    bool SaveSnapshotPng(
        const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const EditOverlayState& overlayState);
    const std::string& LastError() const;
    const AppConfig& Config() const;
    const FontHeights& FontMetrics() const;
    const Fonts& WidgetFonts() const;
    RenderMode CurrentRenderMode() const;
    COLORREF TrackColor() const;
    TextLayoutResult MeasureTextBlock(const RECT& rect, const std::string& text, HFONT font, UINT format) const;
    void DrawText(const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format) const;
    TextLayoutResult DrawTextBlock(const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format);
    void DrawPillBar(const RECT& rect, double ratio, std::optional<double> peakRatio, bool drawFill = true);
    void PushClipRect(const RECT& rect);
    void PopClipRect();
    bool FillSolidRect(const RECT& rect, COLORREF color, BYTE alpha = 255);
    bool FillSolidEllipse(int centerX, int centerY, int diameter, COLORREF color, BYTE alpha = 255);
    bool FillSolidDiamond(const RECT& rect, COLORREF color, BYTE alpha = 255);
    bool DrawSolidRect(const RECT& rect, COLORREF color, int strokeWidth = 1, bool dashed = false);
    bool DrawSolidEllipse(const RECT& rect, COLORREF color, int strokeWidth = 1);
    bool DrawSolidLine(POINT start, POINT end, COLORREF color, int strokeWidth = 1);
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateD2DPathGeometry() const;
    Microsoft::WRL::ComPtr<ID2D1GeometryGroup> CreateD2DGeometryGroup(
        std::span<const Microsoft::WRL::ComPtr<ID2D1PathGeometry>> geometries, size_t count) const;
    bool FillD2DGeometry(ID2D1Geometry* geometry, COLORREF color, BYTE alpha = 255);
    bool DrawD2DPolyline(std::span<const POINT> points, COLORREF color, int strokeWidth);
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
    void RegisterDynamicTextAnchor(const TextLayoutResult& layoutResult, const EditableAnchorBinding& editable);
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

    struct TransparentStringHash {
        using is_transparent = void;

        size_t operator()(std::string_view value) const {
            return std::hash<std::string_view>{}(value);
        }

        size_t operator()(const std::string& value) const {
            return (*this)(std::string_view(value));
        }
    };

    struct TransparentStringEqual {
        using is_transparent = void;

        bool operator()(std::string_view left, std::string_view right) const {
            return left == right;
        }

        bool operator()(const std::string& left, const std::string& right) const {
            return left == right;
        }

        bool operator()(const std::string& left, std::string_view right) const {
            return std::string_view(left) == right;
        }

        bool operator()(std::string_view left, const std::string& right) const {
            return left == std::string_view(right);
        }
    };

    struct TextWidthCacheLookupKey {
        HFONT font = nullptr;
        std::string_view text;
    };

    struct TextWidthCacheKeyHash {
        using is_transparent = void;

        size_t operator()(const TextWidthCacheKey& key) const {
            return (std::hash<HFONT>{}(key.font) * 1315423911u) ^ TransparentStringHash {}(key.text);
        }

        size_t operator()(const TextWidthCacheLookupKey& key) const {
            return (std::hash<HFONT>{}(key.font) * 1315423911u) ^ TransparentStringHash {}(key.text);
        }
    };

    struct TextWidthCacheKeyEqual {
        using is_transparent = void;

        bool operator()(const TextWidthCacheKey& left, const TextWidthCacheKey& right) const {
            return left.font == right.font && left.text == right.text;
        }

        bool operator()(const TextWidthCacheKey& left, const TextWidthCacheLookupKey& right) const {
            return left.font == right.font && std::string_view(left.text) == right.text;
        }

        bool operator()(const TextWidthCacheLookupKey& left, const TextWidthCacheKey& right) const {
            return left.font == right.font && left.text == std::string_view(right.text);
        }
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

    struct D2DBrushCacheKey {
        COLORREF color = 0;
        BYTE alpha = 255;

        bool operator==(const D2DBrushCacheKey& other) const {
            return color == other.color && alpha == other.alpha;
        }
    };

    struct D2DBrushCacheKeyHash {
        size_t operator()(const D2DBrushCacheKey& key) const {
            return (std::hash<COLORREF>{}(key.color) * 1315423911u) ^ std::hash<int>{}(key.alpha);
        }
    };

    struct D2DTextFormats {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> title;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> big;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> value;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> label;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> text;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> smallFont;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> footer;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> clockTime;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> clockDate;
    };

    const std::wstring& GetWideText(std::string_view text) const;
    void ClearD2DCaches();
    void DrawHoveredWidgetHighlight(const EditOverlayState& overlayState) const;
    void DrawHoveredEditableAnchorHighlight(const EditOverlayState& overlayState) const;
    void DrawLayoutEditGuides(const EditOverlayState& overlayState) const;
    void DrawWidgetEditGuides(const EditOverlayState& overlayState) const;
    void DrawLayoutSimilarityIndicators(const EditOverlayState& overlayState) const;
    void DrawPanel(const ResolvedCardLayout& card);
    void DrawPanelIcon(const std::string& iconName, const RECT& iconRect);
    void DrawResolvedWidget(const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics);
    const ParsedWidgetInfo* FindParsedWidgetInfo(const LayoutNodeConfig& node) const;
    DashboardWidgetLayout ResolveWidgetLayout(
        const LayoutNodeConfig& node, const RECT& rect, bool instantiateWidget) const;
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
        const std::vector<size_t>& nodePath,
        bool instantiateWidgets);
    void BuildWidgetEditGuides();
    void BuildStaticEditableAnchors();
    std::optional<LayoutWidgetIdentity> FindFirstLayoutEditPreviewWidget(const std::string& widgetTypeName) const;

    bool InitializeGdiplus();
    void ShutdownGdiplus();
    bool InitializeDirect2D();
    bool InitializeWic();
    void ShutdownDirect2D();
    bool CreateFonts();
    void DestroyFonts();
    bool LoadPanelIcons();
    void ReleasePanelIcons();
    bool MeasureFonts();
    bool EnsureWindowRenderTarget();
    bool BeginDirect2DDraw(ID2D1RenderTarget* target);
    void EndDirect2DDraw();
    bool BeginWindowDraw();
    void EndWindowDraw();
    void DrawDirect2DFrame(const SystemSnapshot& snapshot, const EditOverlayState& overlayState);
    bool SaveWicBitmapPng(IWICBitmap* bitmap, const std::filesystem::path& imagePath);
    ID2D1SolidColorBrush* D2DSolidBrush(COLORREF color, BYTE alpha = 255);
    IDWriteTextFormat* DWriteTextFormatForFont(HFONT font) const;
    bool CreateDWriteTextFormats();
    void ConfigureDWriteTextFormat(IDWriteTextFormat* format, UINT drawTextFormat) const;
    TextLayoutResult MeasureTextBlockD2D(const RECT& rect,
        const std::wstring& wideText,
        HFONT font,
        UINT format,
        Microsoft::WRL::ComPtr<IDWriteTextLayout>* layout = nullptr) const;
    bool ResolveLayout(bool includeWidgetState = true);
    void ResolveNodeWidgets(const LayoutNodeConfig& node,
        const RECT& rect,
        std::vector<DashboardWidgetLayout>& widgets,
        bool instantiateWidgets = true);
    int PreferredNodeHeight(const LayoutNodeConfig& node, int width) const;
    int EffectiveHeaderHeight() const;
    bool SupportsLayoutSimilarityIndicator(const DashboardWidgetLayout& widget) const;
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
        HFONT font,
        UINT format,
        const EditableAnchorBinding& editable);
    void RegisterTextAnchor(std::vector<EditableAnchorRegion>& regions,
        const TextLayoutResult& layoutResult,
        const EditableAnchorBinding& editable);
    bool IsDirect2DActive() const;
    const DashboardMetricSource& ResolveMetrics(const SystemSnapshot& snapshot);
    void InvalidateMetricSourceCache();
    void WriteTrace(const std::string& text) const;

    AppConfig config_;
    HWND hwnd_ = nullptr;
    std::ostream* traceOutput_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    std::vector<std::pair<std::string, std::unique_ptr<Gdiplus::Bitmap>>> panelIcons_;
    Fonts fonts_{};
    D2DTextFormats d2dTextFormats_{};
    FontHeights fontHeights_{};
    ResolvedDashboardLayout resolvedLayout_{};
    std::vector<LayoutEditGuide> layoutEditGuides_;
    std::vector<WidgetEditGuide> widgetEditGuides_;
    std::vector<EditableAnchorRegion> staticEditableAnchorRegions_;
    std::vector<EditableAnchorRegion> dynamicEditableAnchorRegions_;
    bool dynamicAnchorRegistrationEnabled_ = false;
    mutable std::unordered_map<const LayoutNodeConfig*, ParsedWidgetInfo> parsedWidgetInfoCache_;
    mutable std::unordered_map<std::string, std::wstring, TransparentStringHash, TransparentStringEqual> wideTextCache_;
    mutable std::unordered_map<TextWidthCacheKey, int, TextWidthCacheKeyHash, TextWidthCacheKeyEqual> textWidthCache_;
    std::unordered_map<D2DBrushCacheKey, Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>, D2DBrushCacheKeyHash>
        d2dSolidBrushCache_;
    std::unordered_map<PanelIconCacheKey, Microsoft::WRL::ComPtr<ID2D1Bitmap>, PanelIconCacheKeyHash>
        d2dPanelIconCache_;
    std::unique_ptr<DashboardMetricSource> cachedMetricSource_;
    const SystemSnapshot* cachedMetricSnapshot_ = nullptr;
    uint64_t cachedMetricSnapshotRevision_ = 0;
    std::string lastError_;
    double renderScale_ = 1.0;
    RenderMode renderMode_ = RenderMode::Normal;
    bool layoutGuideDragActive_ = false;
    bool interactiveDragTraceActive_ = false;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> d2dWindowRenderTarget_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> d2dSolidStrokeStyle_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> d2dDashedStrokeStyle_;
    ID2D1RenderTarget* d2dActiveRenderTarget_ = nullptr;
    ID2D1RenderTarget* d2dCacheOwnerTarget_ = nullptr;
    bool wicComInitialized_ = false;
    bool d2dFirstDrawWarmupPending_ = false;
    bool d2dDrawActive_ = false;
    int d2dClipDepth_ = 0;
};
