#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "dashboard_renderer/impl/layout_resolver.h"
#include "dashboard_renderer/impl/metric_lookup_cache.h"
#include "layout_model/dashboard_overlay_state.h"
#include "layout_model/layout_edit_active_region.h"
#include "renderer/renderer.h"
#include "telemetry/metrics.h"
#include "util/file_path.h"
#include "util/trace.h"
#include "widget/card_chrome_layout.h"
#include "widget/widget.h"
#include "widget/widget_host.h"

class DashboardLayoutEditOverlayRenderer;
class DashboardLayerBitmapPool;
class LayoutGuideSheetRenderer;
class DashboardRendererBenchmarkAccess;
class DashboardRenderThread;
struct DashboardPresentationAnimation;
struct DashboardPresentationFrame;

struct LayoutGuideSheetCardSummary {
    std::string id;
    std::string title;
    std::string iconName;
    RenderRect rect{};
    CardChromeLayout chromeLayout{};
    std::vector<WidgetClass> widgetClasses;
};

struct LayoutGuideSheetCardChromeArtifacts {
    CardChromeLayout chromeLayout{};
    std::vector<LayoutEditWidgetGuide> widgetGuides;
    std::vector<LayoutEditAnchorRegion> anchorRegions;
    std::vector<LayoutEditColorRegion> colorRegions;
};

class DashboardRenderer : public WidgetHost {
public:
    using RenderMode = ::RenderMode;
    using TextLayoutResult = ::TextLayoutResult;
    using TextStyleMetrics = ::TextStyleMetrics;
    using LayoutEditParameter = ::LayoutEditParameter;

    explicit DashboardRenderer(Trace& trace);
    ~DashboardRenderer();

    void SetConfig(const AppConfig& config);
    void SetRenderScale(double scale);
    void SetImmediatePresent(bool enabled);
    void SetLiveAnimationEnabled(bool enabled);
    void SetRenderMode(RenderMode mode);
    void SetLayoutGuideDragActive(bool active);
    void SetInteractiveDragTraceActive(bool active);
    void RebuildEditArtifacts();
    double RenderScale() const;
    int WindowWidth() const;
    int WindowHeight() const;
    int LayoutSimilarityThreshold() const;
    std::optional<int> FindLayoutWidgetExtent(const LayoutEditWidgetIdentity& widget, LayoutGuideAxis axis) const;
    std::optional<LayoutEditWidgetIdentity> FindFirstLayoutEditPreviewWidget(const std::string& widgetTypeName) const;
    bool ApplyLayoutGuideWeightsPreview(
        const std::string& editCardId, const std::vector<size_t>& nodePath, const std::vector<int>& weights);
    const MetricDefinitionConfig* FindConfiguredMetricDefinition(std::string_view metricRef) const override;
    const std::string& ResolveConfiguredMetricSampleValueText(std::string_view metricRef) const override;
    LayoutEditActiveRegions CollectLayoutEditActiveRegions(const DashboardOverlayState& overlayState) const;
    LayoutEditHoverResolution ResolveLayoutEditHover(
        const DashboardOverlayState& overlayState, RenderPoint clientPoint) const;

    bool Initialize(HWND hwnd = nullptr);
    void Shutdown();

    bool DrawWindow(const SystemSnapshot& snapshot);
    bool DrawWindow(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    bool DrawWindowSynchronously(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    bool SaveSnapshotPng(const FilePath& imagePath, const SystemSnapshot& snapshot);
    bool SaveSnapshotPng(
        const FilePath& imagePath, const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    std::vector<LayoutGuideSheetCardSummary> CollectLayoutGuideSheetCardSummaries() const;
    bool RenderSnapshotOffscreen(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    bool PrimeLayoutEditDynamicRegions(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    bool HasActiveDashboardAnimation() const;
    void DiscardWindowRenderTarget(std::string_view reason = {});
    const std::string& LastError() const;
    ::Renderer& Renderer() override;
    const ::Renderer& Renderer() const override;
    const AppConfig& Config() const override;
    RenderMode CurrentRenderMode() const override;
    WidgetEditArtifactRegistrar& EditArtifacts() override;
    LayoutEditAnchorBinding MakeEditableTextBinding(
        const WidgetLayout& widget, LayoutEditParameter parameter, int anchorId, int value) const override;
    LayoutEditAnchorBinding MakeMetricTextBinding(
        const WidgetLayout& widget, std::string_view metricId, int anchorId) const override;
    int ScaleLogical(int value) const;
    std::optional<MetricListReorderOverlayState> ActiveMetricListReorderDrag(
        const LayoutEditWidgetIdentity& widget) const override;
    void AddWidgetAnimation(WidgetAnimationPtr animation, WidgetAnimationStatePtr targetState) override;

private:
    friend class DashboardLayoutResolver;
    friend class DashboardLayoutEditOverlayRenderer;
    friend class DashboardRendererBenchmarkAccess;
    friend class LayoutGuideSheetRenderer;

    struct PresentationBuildOptions {
        bool useLiveLayerBitmaps = false;
        bool forceCompleteLayers = false;
    };

    void DrawMoveOverlay(const DashboardMoveOverlayState& overlayState);
    void DrawResolvedWidget(const WidgetLayout& widget, const MetricSource& metrics);
    void DrawResolvedWidgetOverlay(const WidgetLayout& widget, const MetricSource& metrics);
    const LayoutCardConfig* FindCardConfigById(const std::string& id) const;
    void AddLayoutEditGuide(
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        const std::vector<RenderRect>& childRects,
        int gap,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath,
        const std::vector<LayoutEditOverlayOwner>& overlayOwners);
    void ResolveNodeWidgetsInternal(
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<WidgetLayout>& widgets,
        std::vector<std::string>& cardReferenceStack,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath,
        bool instantiateWidgets);
    void BuildWidgetEditGuides();
    void BuildStaticEditableAnchors();
    bool BuildPresentationFrame(
        const SystemSnapshot& snapshot,
        const DashboardOverlayState& overlayState,
        DashboardPresentationFrame& frame,
        PresentationBuildOptions options);
    bool DrawWindowInternal(
        const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState, bool waitForPresentation);
    void DrawFrame(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    void DrawSnapshotLayer(const DashboardOverlayState& overlayState, const MetricSource& metrics);
    void DrawOverlayLayerStatic(const DashboardOverlayState& overlayState, const MetricSource& metrics);
    void PushWidgetAnimationTranslation(RenderPoint offset);
    void PopWidgetAnimationTranslation();
    void BeginWidgetAnimationCollection();
    void BeginWidgetAnimationLayer(WidgetAnimationLayer layer);
    void DrawAnimationTargets(WidgetAnimationLayer layer);
    void EndWidgetAnimationCollection();
    std::vector<DashboardPresentationAnimation>& WidgetAnimationsForLayer(WidgetAnimationLayer layer);
    std::uint64_t ResolveSurfaceVersion();
    std::string SnapshotOverlaySignature(const DashboardOverlayState& overlayState) const;
    bool ShouldUpdateSnapshotLayer(
        const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState, std::uint64_t surfaceVersion) const;
    void MarkSnapshotLayerUpdated(
        const SystemSnapshot& snapshot, const std::string& overlaySignature, std::uint64_t surfaceVersion);
    bool CanReuseLiveLayerBitmaps(PresentationBuildOptions options) const;
    bool DrawLayerBitmap(
        RenderBitmap& bitmap,
        int width,
        int height,
        RenderBitmapClear clear,
        Renderer::DrawCallback draw,
        PresentationBuildOptions options);
    void ClearReusableLayerBitmaps();
    RenderBitmap AcquireLiveLayerBitmap(int width, int height) const;
    void RecycleFrameLayers(DashboardPresentationFrame frame) const;
    bool ResolveLayout(bool includeWidgetState = true);
    void ResolveNodeWidgets(
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<WidgetLayout>& widgets,
        bool instantiateWidgets = true);
    bool SupportsLayoutSimilarityIndicator(const WidgetLayout& widget) const;
    std::vector<const WidgetLayout*> CollectSimilarityIndicatorWidgets(LayoutGuideAxis axis) const;
    int WidgetExtentForAxis(const WidgetLayout& widget, LayoutGuideAxis axis) const;
    bool IsWidgetAffectedByGuide(const WidgetLayout& widget, const LayoutEditGuide& guide) const;
    bool MatchesWidgetIdentity(const WidgetLayout& widget, const LayoutEditWidgetIdentity& identity) const;
    const LayoutEditAnchorRegion* FindEditableAnchorRegion(const LayoutEditAnchorKey& key) const;
    static bool IsContainerNode(const LayoutNodeConfig& node);
    RendererStyle BuildRendererStyle() const;
    const MetricSource& ResolveMetrics(const SystemSnapshot& snapshot);
    void InvalidateMetricSourceCache();
    bool ShouldWriteRendererTrace() const;
    void WriteTrace(const std::string& text) const;
    void WriteTrace(ResourceStringId text) const;
    void WriteTraceFmt(const char* format, ...) const;
    void WriteTraceFmt(ResourceStringId format, ...) const;
    bool SaveLayoutGuideSheetSurfacePng(const FilePath& imagePath, int width, int height, Renderer::DrawCallback draw);
    bool RenderLayoutGuideSheetSurfaceOffscreen(int width, int height, Renderer::DrawCallback draw);
    void BeginLayoutGuideSheetDynamicArtifacts(const DashboardOverlayState& overlayState);
    void ResolveLayoutGuideSheetDynamicArtifactCollisions();
    void EndLayoutGuideSheetDynamicArtifacts();
    void DrawLayoutGuideSheetCard(
        const std::string& cardId,
        const RenderRect& sourceRect,
        const RenderRect& destRect,
        const MetricSource& metrics);
    void DrawLayoutGuideSheetOverlay(
        const DashboardOverlayState& overlayState,
        const RenderRect& sourceRect,
        const RenderRect& destRect,
        const MetricSource& metrics);
    LayoutGuideSheetCardChromeArtifacts BuildLayoutGuideSheetCardChromeArtifacts(
        const std::string& cardId, const RenderRect& rect, const MetricSource* metrics);

    AppConfig config_;
    Trace& trace_;
    std::unique_ptr<::Renderer> renderer_;
    std::unique_ptr<DashboardLayoutResolver> layoutResolver_;
    std::unique_ptr<DashboardLayoutEditOverlayRenderer> layoutEditOverlayRenderer_;
    std::unique_ptr<MetricSource> cachedMetricSource_;
    const SystemSnapshot* cachedMetricSnapshot_ = nullptr;
    uint64_t cachedMetricSnapshotRevision_ = 0;
    mutable MetricLookupCache metricLookupCache_;
    mutable std::string lastError_;
    double renderScale_ = 1.0;
    RenderMode renderMode_ = RenderMode::Normal;
    bool layoutGuideDragActive_ = false;
    bool interactiveDragTraceActive_ = false;
    bool liveAnimationEnabled_ = false;
    bool immediatePresent_ = false;
    bool widgetAnimationCollectionActive_ = false;
    WidgetAnimationLayer currentWidgetAnimationLayer_ = WidgetAnimationLayer::Snapshot;
    const DashboardOverlayState* activeOverlayState_ = nullptr;
    std::shared_ptr<DashboardLayerBitmapPool> layerBitmapPool_;
    std::unique_ptr<DashboardRenderThread> presentation_;
    HWND presentationHwnd_ = nullptr;
    int presentedWidth_ = 0;
    int presentedHeight_ = 0;
    double presentedScale_ = 0.0;
    // Guards presentation surface identity when window size or render scale changes.
    std::uint64_t surfaceVersion_ = 1;
    // Guards snapshot layer rebuilds when any dashboard config or layout value changes.
    std::uint64_t configVersion_ = 1;
    // Guards presentation snapshot bitmap replacement when the opaque base layer is rebuilt.
    std::uint64_t snapshotVersion_ = 0;
    // Guards presentation overlay bitmap replacement when transparent edit/drag content changes.
    std::uint64_t overlayVersion_ = 0;
    // Guards presentation dirty-animation reuse when animation command geometry changes.
    std::uint64_t animationGeometryVersion_ = 0;
    bool snapshotLayerValid_ = false;
    bool overlayLayerVisible_ = false;
    // Guards snapshot layer validity against telemetry revision changes.
    std::uint64_t snapshotLayerMetricVersion_ = 0;
    // Guards snapshot layer validity against config/layout changes.
    std::uint64_t snapshotLayerConfigVersion_ = 0;
    // Guards snapshot layer validity against surface size or render scale changes.
    std::uint64_t snapshotLayerSurfaceVersion_ = 0;
    RenderMode snapshotLayerRenderMode_ = RenderMode::Normal;
    std::string snapshotLayerOverlaySignature_;
    RenderPoint currentWidgetAnimationTranslation_{};
    std::vector<RenderPoint> widgetAnimationTranslationStack_;
    std::vector<DashboardPresentationAnimation> snapshotWidgetAnimations_;
    std::vector<DashboardPresentationAnimation> overlayWidgetAnimations_;
};
