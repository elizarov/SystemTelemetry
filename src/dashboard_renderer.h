#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>
#include <array>
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
#include "layout_edit_types.h"
#include "render_types.h"
#include "widget.h"

class DashboardRenderer {
public:
    using LayoutEditParameter = ::LayoutEditParameter;

    enum class RenderMode {
        Normal,
        Blank,
    };

    enum class SimilarityIndicatorMode {
        ActiveGuide,
        AllHorizontal,
        AllVertical,
    };

    struct MoveOverlayState {
        bool visible = false;
        std::string monitorName;
        RenderPoint relativePosition{};
        double monitorScale = 1.0;
    };

    struct EditOverlayState {
        bool showLayoutEditGuides = false;
        SimilarityIndicatorMode similarityIndicatorMode = SimilarityIndicatorMode::ActiveGuide;
        std::optional<LayoutEditGuide> activeLayoutEditGuide;
        std::optional<LayoutEditWidgetIdentity> hoveredLayoutCard;
        std::optional<LayoutEditWidgetIdentity> hoveredEditableCard;
        std::optional<LayoutEditWidgetIdentity> hoveredEditableWidget;
        std::optional<LayoutEditWidgetGuide> activeWidgetEditGuide;
        std::optional<LayoutEditGapAnchorKey> hoveredGapEditAnchor;
        std::optional<LayoutEditGapAnchorKey> activeGapEditAnchor;
        std::optional<LayoutEditAnchorKey> hoveredEditableAnchor;
        std::optional<LayoutEditAnchorKey> activeEditableAnchor;
        MoveOverlayState moveOverlay{};
    };

    struct TextStyleMetrics {
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

    struct TextLayoutResult {
        RenderRect textRect{};
    };

    DashboardRenderer();
    ~DashboardRenderer();

    void SetConfig(const AppConfig& config);
    void SetRenderScale(double scale);
    void SetImmediatePresent(bool enabled);
    void SetRenderMode(RenderMode mode);
    void SetLayoutGuideDragActive(bool active);
    void SetInteractiveDragTraceActive(bool active);
    void RebuildEditArtifacts();
    bool SetLayoutEditPreviewWidgetType(EditOverlayState& overlayState, const std::string& widgetTypeName) const;
    double RenderScale() const;
    int WindowWidth() const;
    int WindowHeight() const;

    RenderColor BackgroundColor() const;
    RenderColor ForegroundColor() const;
    RenderColor AccentColor() const;
    RenderColor LayoutGuideColor() const;
    RenderColor ActiveEditColor() const;
    RenderColor MutedTextColor() const;
    RenderColor TrackColor() const;
    RenderColor GraphBackgroundColor() const;
    RenderColor GraphMarkerColor() const;
    RenderColor GraphAxisColor() const;
    void SetTraceOutput(std::ostream* traceOutput);
    const std::vector<LayoutEditGuide>& LayoutEditGuides() const;
    const std::vector<LayoutEditWidgetGuide>& WidgetEditGuides() const;
    const std::vector<LayoutEditGapAnchor>& GapEditAnchors() const;
    int LayoutSimilarityThreshold() const;
    std::vector<LayoutGuideSnapCandidate> CollectLayoutGuideSnapCandidates(const LayoutEditGuide& guide) const;
    std::optional<int> FindLayoutWidgetExtent(const LayoutEditWidgetIdentity& widget, LayoutGuideAxis axis) const;
    bool ApplyLayoutGuideWeightsPreview(
        const std::string& editCardId, const std::vector<size_t>& nodePath, const std::vector<int>& weights);
    std::optional<LayoutEditWidgetIdentity> HitTestLayoutCard(RenderPoint clientPoint) const;
    std::optional<LayoutEditWidgetIdentity> HitTestEditableCard(RenderPoint clientPoint) const;
    std::optional<LayoutEditWidgetIdentity> HitTestEditableWidget(RenderPoint clientPoint) const;
    std::optional<LayoutEditGapAnchorKey> HitTestGapEditAnchor(RenderPoint clientPoint) const;
    std::optional<LayoutEditGapAnchor> FindGapEditAnchor(const LayoutEditGapAnchorKey& key) const;
    std::optional<LayoutEditAnchorKey> HitTestEditableAnchorTarget(RenderPoint clientPoint) const;
    std::optional<LayoutEditAnchorKey> HitTestEditableAnchorHandle(RenderPoint clientPoint) const;
    std::optional<LayoutEditAnchorRegion> FindEditableAnchorRegion(const LayoutEditAnchorKey& key) const;

    bool Initialize(HWND hwnd = nullptr);
    void Shutdown();

    bool DrawWindow(const SystemSnapshot& snapshot);
    bool DrawWindow(const SystemSnapshot& snapshot, const EditOverlayState& overlayState);
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
    bool SaveSnapshotPng(
        const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const EditOverlayState& overlayState);
    const std::string& LastError() const;
    const AppConfig& Config() const;
    const TextStyleMetrics& TextMetrics() const;
    RenderMode CurrentRenderMode() const;
    TextLayoutResult MeasureTextBlock(
        const RenderRect& rect, const std::string& text, TextStyleId style, const TextLayoutOptions& options) const;
    void DrawText(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColor color,
        const TextLayoutOptions& options) const;
    TextLayoutResult DrawTextBlock(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColor color,
        const TextLayoutOptions& options);
    void DrawPillBar(const RenderRect& rect, double ratio, std::optional<double> peakRatio, bool drawFill = true);
    void PushClipRect(const RenderRect& rect);
    void PopClipRect();
    bool FillSolidRect(const RenderRect& rect, RenderColor color);
    bool FillSolidEllipse(RenderPoint center, int diameter, RenderColor color);
    bool FillSolidDiamond(const RenderRect& rect, RenderColor color);
    bool DrawSolidRect(const RenderRect& rect, const RenderStroke& stroke);
    bool DrawSolidEllipse(const RenderRect& rect, const RenderStroke& stroke);
    bool DrawSolidLine(RenderPoint start, RenderPoint end, const RenderStroke& stroke);
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateD2DPathGeometry() const;
    Microsoft::WRL::ComPtr<ID2D1GeometryGroup> CreateD2DGeometryGroup(
        std::span<const Microsoft::WRL::ComPtr<ID2D1PathGeometry>> geometries, size_t count) const;
    bool FillD2DGeometry(ID2D1Geometry* geometry, RenderColor color);
    bool DrawD2DPolyline(std::span<const RenderPoint> points, const RenderStroke& stroke);
    LayoutEditAnchorBinding MakeEditableTextBinding(
        const DashboardWidgetLayout& widget, LayoutEditParameter parameter, int anchorId, int value) const;
    void RegisterStaticEditableAnchorRegion(const LayoutEditAnchorKey& key,
        const RenderRect& targetRect,
        const RenderRect& anchorRect,
        AnchorShape shape,
        AnchorDragAxis dragAxis,
        AnchorDragMode dragMode,
        RenderPoint dragOrigin,
        double dragScale,
        bool showWhenWidgetHovered,
        bool drawTargetOutline,
        int value);
    void RegisterDynamicEditableAnchorRegion(const LayoutEditAnchorKey& key,
        const RenderRect& targetRect,
        const RenderRect& anchorRect,
        AnchorShape shape,
        AnchorDragAxis dragAxis,
        AnchorDragMode dragMode,
        RenderPoint dragOrigin,
        double dragScale,
        bool showWhenWidgetHovered,
        bool drawTargetOutline,
        int value);
    void RegisterStaticTextAnchor(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable);
    void RegisterDynamicTextAnchor(const TextLayoutResult& layoutResult, const LayoutEditAnchorBinding& editable);
    void RegisterDynamicTextAnchor(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable);
    std::vector<LayoutEditWidgetGuide>& WidgetEditGuidesMutable();
    std::vector<LayoutEditGapAnchor>& GapEditAnchorsMutable();
    int ScaleLogical(int value) const;
    int MeasureTextWidth(TextStyleId style, std::string_view text) const;

private:
    friend struct DashboardLayoutResolver;

    struct SimilarityIndicator {
        LayoutGuideAxis axis = LayoutGuideAxis::Horizontal;
        RenderRect rect{};
        int exactTypeOrdinal = 0;
    };

    struct ResolvedCardLayout {
        std::string id;
        std::string title;
        std::string iconName;
        bool hasHeader = true;
        RenderRect rect{};
        RenderRect titleRect{};
        RenderRect iconRect{};
        RenderRect contentRect{};
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
        TextStyleId style = TextStyleId::Text;
        std::string text;

        bool operator==(const TextWidthCacheKey& other) const {
            return style == other.style && text == other.text;
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
        TextStyleId style = TextStyleId::Text;
        std::string_view text;
    };

    struct TextWidthCacheKeyHash {
        using is_transparent = void;

        size_t operator()(const TextWidthCacheKey& key) const {
            return (std::hash<int>{}(static_cast<int>(key.style)) * 1315423911u) ^ TransparentStringHash {}(key.text);
        }

        size_t operator()(const TextWidthCacheLookupKey& key) const {
            return (std::hash<int>{}(static_cast<int>(key.style)) * 1315423911u) ^ TransparentStringHash {}(key.text);
        }
    };

    struct TextWidthCacheKeyEqual {
        using is_transparent = void;

        bool operator()(const TextWidthCacheKey& left, const TextWidthCacheKey& right) const {
            return left.style == right.style && left.text == right.text;
        }

        bool operator()(const TextWidthCacheKey& left, const TextWidthCacheLookupKey& right) const {
            return left.style == right.style && std::string_view(left.text) == right.text;
        }

        bool operator()(const TextWidthCacheLookupKey& left, const TextWidthCacheKey& right) const {
            return left.style == right.style && left.text == std::string_view(right.text);
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
        RenderColor color{};

        bool operator==(const D2DBrushCacheKey& other) const {
            return color == other.color;
        }
    };

    struct D2DBrushCacheKeyHash {
        size_t operator()(const D2DBrushCacheKey& key) const {
            return std::hash<std::uint32_t>{}(key.color.PackedRgba());
        }
    };

    struct Palette {
        RenderColor background{};
        RenderColor foreground{};
        RenderColor accent{};
        RenderColor mutedText{};
        RenderColor track{};
        RenderColor layoutGuide{};
        RenderColor activeEdit{};
        RenderColor panelBorder{};
        RenderColor panelFill{};
        RenderColor graphBackground{};
        RenderColor graphMarker{};
        RenderColor graphAxis{};
    };

    void ClearD2DCaches();
    void DrawHoveredWidgetHighlight(const EditOverlayState& overlayState) const;
    void DrawHoveredEditableAnchorHighlight(const EditOverlayState& overlayState) const;
    void DrawLayoutEditGuides(const EditOverlayState& overlayState) const;
    void DrawWidgetEditGuides(const EditOverlayState& overlayState) const;
    void DrawGapEditAnchors(const EditOverlayState& overlayState) const;
    void DrawLayoutSimilarityIndicators(const EditOverlayState& overlayState) const;
    void DrawMoveOverlay(const MoveOverlayState& overlayState);
    void DrawPanel(const ResolvedCardLayout& card);
    void DrawPanelIcon(const std::string& iconName, const RenderRect& iconRect);
    void DrawResolvedWidget(const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics);
    const ParsedWidgetInfo* FindParsedWidgetInfo(const LayoutNodeConfig& node) const;
    DashboardWidgetLayout ResolveWidgetLayout(
        const LayoutNodeConfig& node, const RenderRect& rect, bool instantiateWidget) const;
    bool UsesFixedPreferredHeightInRows(const DashboardWidgetLayout& widget) const;
    const LayoutCardConfig* FindCardConfigById(const std::string& id) const;
    void AddLayoutEditGuide(const LayoutNodeConfig& node,
        const RenderRect& rect,
        const std::vector<RenderRect>& childRects,
        int gap,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath);
    void ResolveNodeWidgetsInternal(const LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<DashboardWidgetLayout>& widgets,
        std::vector<std::string>& cardReferenceStack,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath,
        bool instantiateWidgets);
    void BuildWidgetEditGuides();
    void BuildStaticEditableAnchors();
    std::optional<LayoutEditWidgetIdentity> FindFirstLayoutEditPreviewWidget(const std::string& widgetTypeName) const;

    bool InitializeDirect2D();
    bool InitializeWic();
    void ShutdownDirect2D();
    void RebuildPalette();
    bool LoadPanelIcons();
    void ReleasePanelIcons();
    bool RebuildTextFormatsAndMetrics();
    bool EnsureWindowRenderTarget();
    bool BeginDirect2DDraw(ID2D1RenderTarget* target);
    void EndDirect2DDraw();
    bool BeginWindowDraw();
    void EndWindowDraw();
    void DrawDirect2DFrame(const SystemSnapshot& snapshot, const EditOverlayState& overlayState);
    bool SaveWicBitmapPng(IWICBitmap* bitmap, const std::filesystem::path& imagePath);
    ID2D1SolidColorBrush* D2DSolidBrush(RenderColor color);
    IDWriteTextFormat* DWriteTextFormat(TextStyleId style) const;
    bool CreateDWriteTextFormats();
    void ConfigureDWriteTextFormat(IDWriteTextFormat* format, const TextLayoutOptions& options) const;
    TextLayoutResult MeasureTextBlockD2D(const RenderRect& rect,
        const std::wstring& wideText,
        TextStyleId style,
        const TextLayoutOptions& options,
        Microsoft::WRL::ComPtr<IDWriteTextLayout>* layout = nullptr) const;
    bool ResolveLayout(bool includeWidgetState = true);
    void ResolveNodeWidgets(const LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<DashboardWidgetLayout>& widgets,
        bool instantiateWidgets = true);
    int PreferredNodeHeight(const LayoutNodeConfig& node, int width) const;
    bool SupportsLayoutSimilarityIndicator(const DashboardWidgetLayout& widget) const;
    std::vector<const DashboardWidgetLayout*> CollectSimilarityIndicatorWidgets(LayoutGuideAxis axis) const;
    int WidgetExtentForAxis(const DashboardWidgetLayout& widget, LayoutGuideAxis axis) const;
    bool IsWidgetAffectedByGuide(const DashboardWidgetLayout& widget, const LayoutEditGuide& guide) const;
    bool MatchesWidgetIdentity(const DashboardWidgetLayout& widget, const LayoutEditWidgetIdentity& identity) const;
    static bool IsContainerNode(const LayoutNodeConfig& node);
    void RegisterEditableAnchorRegion(std::vector<LayoutEditAnchorRegion>& regions,
        const LayoutEditAnchorKey& key,
        const RenderRect& targetRect,
        const RenderRect& anchorRect,
        AnchorShape shape,
        AnchorDragAxis dragAxis,
        AnchorDragMode dragMode,
        RenderPoint dragOrigin,
        double dragScale,
        bool showWhenWidgetHovered,
        bool drawTargetOutline,
        int value);
    void RegisterTextAnchor(std::vector<LayoutEditAnchorRegion>& regions,
        const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable);
    void RegisterTextAnchor(std::vector<LayoutEditAnchorRegion>& regions,
        const TextLayoutResult& layoutResult,
        const LayoutEditAnchorBinding& editable);
    bool IsDrawActive() const;
    const DashboardMetricSource& ResolveMetrics(const SystemSnapshot& snapshot);
    void InvalidateMetricSourceCache();
    void WriteTrace(const std::string& text) const;

    AppConfig config_;
    HWND hwnd_ = nullptr;
    std::ostream* traceOutput_ = nullptr;
    std::vector<std::pair<std::string, Microsoft::WRL::ComPtr<IWICBitmapSource>>> panelIcons_;
    std::array<Microsoft::WRL::ComPtr<IDWriteTextFormat>, 9> dwriteTextFormats_{};
    TextStyleMetrics textStyleMetrics_{};
    Palette palette_{};
    ResolvedDashboardLayout resolvedLayout_{};
    std::vector<LayoutEditGuide> layoutEditGuides_;
    std::vector<LayoutEditWidgetGuide> widgetEditGuides_;
    std::vector<LayoutEditGapAnchor> gapEditAnchors_;
    std::vector<LayoutEditAnchorRegion> staticEditableAnchorRegions_;
    std::vector<LayoutEditAnchorRegion> dynamicEditableAnchorRegions_;
    bool dynamicAnchorRegistrationEnabled_ = false;
    mutable std::unordered_map<const LayoutNodeConfig*, ParsedWidgetInfo> parsedWidgetInfoCache_;
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
    bool d2dImmediatePresent_ = false;
    bool wicComInitialized_ = false;
    bool d2dFirstDrawWarmupPending_ = false;
    int d2dClipDepth_ = 0;
};
