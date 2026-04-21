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
#include "dashboard_overlay_state.h"
#include "layout_edit_parameter_id.h"
#include "layout_edit_types.h"
#include "render_types.h"
#include "widget.h"

class DashboardLayoutResolver;
class DashboardD2DCache;
class DashboardPalette;
class DashboardTextWidthCache;

class DashboardRenderer {
public:
    using LayoutEditParameter = ::LayoutEditParameter;

    enum class RenderMode {
        Normal,
        Blank,
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
    double RenderScale() const;
    int WindowWidth() const;
    int WindowHeight() const;
    void SetTraceOutput(std::ostream* traceOutput);
    const std::vector<LayoutEditGuide>& LayoutEditGuides() const;
    const std::vector<LayoutEditWidgetGuide>& WidgetEditGuides() const;
    const std::vector<LayoutEditGapAnchor>& GapEditAnchors() const;
    int LayoutSimilarityThreshold() const;
    std::vector<LayoutGuideSnapCandidate> CollectLayoutGuideSnapCandidates(const LayoutEditGuide& guide) const;
    std::optional<int> FindLayoutWidgetExtent(const LayoutEditWidgetIdentity& widget, LayoutGuideAxis axis) const;
    std::optional<LayoutEditWidgetIdentity> FindFirstLayoutEditPreviewWidget(const std::string& widgetTypeName) const;
    bool ApplyLayoutGuideWeightsPreview(
        const std::string& editCardId, const std::vector<size_t>& nodePath, const std::vector<int>& weights);
    const MetricDefinitionConfig* FindConfiguredMetricDefinition(std::string_view metricRef) const;
    const std::string& ResolveConfiguredMetricSampleValueText(std::string_view metricRef) const;
    std::optional<LayoutEditWidgetIdentity> HitTestLayoutCard(RenderPoint clientPoint) const;
    std::optional<LayoutEditWidgetIdentity> HitTestEditableCard(RenderPoint clientPoint) const;
    std::optional<LayoutEditWidgetIdentity> HitTestEditableWidget(RenderPoint clientPoint) const;
    std::optional<LayoutEditGapAnchorKey> HitTestGapEditAnchor(RenderPoint clientPoint) const;
    std::optional<LayoutEditGapAnchor> FindGapEditAnchor(const LayoutEditGapAnchorKey& key) const;
    std::optional<LayoutEditAnchorKey> HitTestEditableAnchorTarget(RenderPoint clientPoint) const;
    std::optional<LayoutEditAnchorKey> HitTestEditableAnchorHandle(RenderPoint clientPoint) const;
    std::optional<LayoutEditAnchorRegion> FindEditableAnchorRegion(const LayoutEditAnchorKey& key) const;
    std::optional<LayoutEditColorRegion> HitTestEditableColorRegion(RenderPoint clientPoint) const;

    bool Initialize(HWND hwnd = nullptr);
    void Shutdown();

    bool DrawWindow(const SystemSnapshot& snapshot);
    bool DrawWindow(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
    bool SaveSnapshotPng(const std::filesystem::path& imagePath,
        const SystemSnapshot& snapshot,
        const DashboardOverlayState& overlayState);
    bool PrimeLayoutEditDynamicRegions(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    void DiscardWindowRenderTarget(std::string_view reason = {});
    const std::string& LastError() const;
    const AppConfig& Config() const;
    const TextStyleMetrics& TextMetrics() const;
    RenderMode CurrentRenderMode() const;
    TextLayoutResult MeasureTextBlock(
        const RenderRect& rect, const std::string& text, TextStyleId style, const TextLayoutOptions& options) const;
    void DrawText(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColorId color,
        const TextLayoutOptions& options) const;
    TextLayoutResult DrawTextBlock(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColorId color,
        const TextLayoutOptions& options);
    std::optional<RenderRect> DrawPillBar(
        const RenderRect& rect, double ratio, std::optional<double> peakRatio, bool drawFill = true);
    void PushClipRect(const RenderRect& rect);
    void PopClipRect();
    bool FillSolidRect(const RenderRect& rect, RenderColorId color);
    bool FillSolidEllipse(RenderPoint center, int diameter, RenderColorId color);
    bool FillSolidDiamond(const RenderRect& rect, RenderColorId color);
    bool DrawSolidRect(const RenderRect& rect, const RenderStroke& stroke);
    bool DrawSolidEllipse(const RenderRect& rect, const RenderStroke& stroke);
    bool DrawSolidLine(RenderPoint start, RenderPoint end, const RenderStroke& stroke);
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateD2DPathGeometry() const;
    Microsoft::WRL::ComPtr<ID2D1GeometryGroup> CreateD2DGeometryGroup(
        std::span<const Microsoft::WRL::ComPtr<ID2D1PathGeometry>> geometries, size_t count) const;
    bool FillD2DGeometry(ID2D1Geometry* geometry, RenderColorId color);
    bool DrawD2DPolyline(std::span<const RenderPoint> points, const RenderStroke& stroke);
    LayoutEditAnchorBinding MakeEditableTextBinding(
        const DashboardWidgetLayout& widget, LayoutEditParameter parameter, int anchorId, int value) const;
    LayoutEditAnchorBinding MakeMetricTextBinding(
        const DashboardWidgetLayout& widget, std::string_view metricId, int anchorId) const;
    void RegisterStaticEditableAnchorRegion(const LayoutEditAnchorKey& key,
        const RenderRect& targetRect,
        const RenderRect& anchorRect,
        AnchorShape shape,
        AnchorDragAxis dragAxis,
        AnchorDragMode dragMode,
        RenderPoint dragOrigin,
        double dragScale,
        bool draggable,
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
        bool draggable,
        bool showWhenWidgetHovered,
        bool drawTargetOutline,
        int value);
    void RegisterStaticTextAnchor(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt);
    void RegisterDynamicTextAnchor(const TextLayoutResult& layoutResult,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt);
    void RegisterDynamicTextAnchor(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt);
    void RegisterStaticColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect);
    void RegisterDynamicColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect);
    std::vector<LayoutEditWidgetGuide>& WidgetEditGuidesMutable();
    std::vector<LayoutEditGapAnchor>& GapEditAnchorsMutable();
    int ScaleLogical(int value) const;
    int MeasureTextWidth(TextStyleId style, std::string_view text) const;

private:
    friend class DashboardLayoutResolver;

    struct SimilarityIndicator {
        LayoutGuideAxis axis = LayoutGuideAxis::Horizontal;
        RenderRect rect{};
        int exactTypeOrdinal = 0;
    };

    void DrawHoveredWidgetHighlight(const DashboardOverlayState& overlayState) const;
    void DrawHoveredEditableAnchorHighlight(const DashboardOverlayState& overlayState) const;
    void DrawSelectedColorEditHighlights(const DashboardOverlayState& overlayState) const;
    void DrawSelectedTreeNodeHighlight(const DashboardOverlayState& overlayState) const;
    void DrawLayoutEditGuides(const DashboardOverlayState& overlayState) const;
    void DrawWidgetEditGuides(const DashboardOverlayState& overlayState) const;
    void DrawGapEditAnchors(const DashboardOverlayState& overlayState) const;
    void DrawDottedHighlightRect(const RenderRect& rect, RenderColorId color, bool active, bool outside = true) const;
    void DrawLayoutSimilarityIndicators(const DashboardOverlayState& overlayState) const;
    void DrawMoveOverlay(const DashboardMoveOverlayState& overlayState);
    void DrawPanel(size_t cardIndex);
    void DrawPanelIcon(const std::string& iconName, const RenderRect& iconRect);
    void DrawResolvedWidget(const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics);
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
    bool InitializeDirect2D();
    bool InitializeWic();
    void ShutdownDirect2D();
    void RebuildPalette();
    bool LoadPanelIcons();
    void ReleasePanelIcons();
    bool RebuildTextFormatsAndMetrics();
    bool EnsureWindowRenderTarget();
    bool BeginDirect2DDraw(ID2D1RenderTarget* target, bool allowDeferredWarmup = true);
    void EndDirect2DDraw();
    bool BeginWindowDraw();
    void EndWindowDraw();
    void DrawDirect2DFrame(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    bool SaveWicBitmapPng(IWICBitmap* bitmap, const std::filesystem::path& imagePath);
    void WriteScreenshotActiveRegionsTrace(const DashboardOverlayState& overlayState) const;
    ID2D1SolidColorBrush* D2DSolidBrush(RenderColorId color);
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
        bool draggable,
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
    std::unique_ptr<DashboardPalette> palette_;
    std::unique_ptr<DashboardLayoutResolver> layoutResolver_;
    std::unique_ptr<DashboardD2DCache> d2dCache_;
    std::unique_ptr<DashboardTextWidthCache> textWidthCache_;
    std::unique_ptr<DashboardMetricSource> cachedMetricSource_;
    const SystemSnapshot* cachedMetricSnapshot_ = nullptr;
    uint64_t cachedMetricSnapshotRevision_ = 0;
    mutable std::unordered_map<std::string, const MetricDefinitionConfig*> metricDefinitionCache_;
    mutable std::unordered_map<std::string, std::string> metricSampleValueTextCache_;
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
    bool d2dImmediatePresent_ = false;
    bool wicComInitialized_ = false;
    bool d2dFirstDrawWarmupPending_ = false;
    int d2dClipDepth_ = 0;
};
