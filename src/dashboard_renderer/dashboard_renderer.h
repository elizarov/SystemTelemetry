#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "config/config.h"
#include "dashboard_renderer/impl/layout_resolver.h"
#include "layout_model/dashboard_overlay_state.h"
#include "renderer/renderer.h"
#include "telemetry/metrics.h"
#include "util/trace.h"
#include "widget/widget.h"
#include "widget/widget_host.h"

class DashboardLayoutEditOverlayRenderer;

enum class DashboardActiveRegionKind {
    Card,
    CardHeader,
    WidgetHover,
    LayoutWeightGuide,
    ContainerChildReorderTarget,
    GapHandle,
    WidgetGuide,
    StaticEditAnchorHandle,
    StaticEditAnchorTarget,
    DynamicEditAnchorHandle,
    DynamicEditAnchorTarget,
    StaticColorTarget,
    DynamicColorTarget,
};

using DashboardActiveRegionPayload = std::variant<const DashboardLayoutResolver::ResolvedCardLayout*,
    const WidgetLayout*,
    const LayoutEditGuide*,
    const DashboardLayoutResolver::ContainerChildReorderTarget*,
    const LayoutEditGapAnchor*,
    const LayoutEditWidgetGuide*,
    const LayoutEditAnchorRegion*,
    const LayoutEditColorRegion*>;

struct DashboardActiveRegion {
    RenderRect box{};
    DashboardActiveRegionKind kind = DashboardActiveRegionKind::Card;
    DashboardActiveRegionPayload payload = static_cast<const DashboardLayoutResolver::ResolvedCardLayout*>(nullptr);
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
    void SetRenderMode(RenderMode mode);
    void SetLayoutGuideDragActive(bool active);
    void SetInteractiveDragTraceActive(bool active);
    void RebuildEditArtifacts();
    double RenderScale() const;
    int WindowWidth() const;
    int WindowHeight() const;
    const std::vector<LayoutEditGuide>& LayoutEditGuides() const;
    const std::vector<LayoutEditWidgetGuide>& WidgetEditGuides() const;
    const std::vector<LayoutEditGapAnchor>& GapEditAnchors() const;
    int LayoutSimilarityThreshold() const;
    std::vector<LayoutGuideSnapCandidate> CollectLayoutGuideSnapCandidates(const LayoutEditGuide& guide) const;
    std::optional<int> FindLayoutWidgetExtent(const LayoutEditWidgetIdentity& widget, LayoutGuideAxis axis) const;
    std::optional<LayoutEditWidgetIdentity> FindFirstLayoutEditPreviewWidget(const std::string& widgetTypeName) const;
    bool ApplyLayoutGuideWeightsPreview(
        const std::string& editCardId, const std::vector<size_t>& nodePath, const std::vector<int>& weights);
    const MetricDefinitionConfig* FindConfiguredMetricDefinition(std::string_view metricRef) const override;
    const std::string& ResolveConfiguredMetricSampleValueText(std::string_view metricRef) const override;
    std::optional<LayoutEditWidgetIdentity> HitTestLayoutCard(RenderPoint clientPoint) const;
    std::optional<LayoutEditWidgetIdentity> HitTestEditableCard(RenderPoint clientPoint) const;
    std::optional<LayoutEditWidgetIdentity> HitTestEditableWidget(RenderPoint clientPoint) const;
    std::optional<LayoutEditGapAnchorKey> HitTestGapEditAnchor(RenderPoint clientPoint) const;
    std::optional<LayoutEditGapAnchor> FindGapEditAnchor(const LayoutEditGapAnchorKey& key) const;
    std::optional<LayoutEditAnchorKey> HitTestEditableAnchorTarget(RenderPoint clientPoint) const;
    std::optional<LayoutEditAnchorKey> HitTestEditableAnchorHandle(RenderPoint clientPoint) const;
    std::optional<LayoutEditAnchorRegion> FindEditableAnchorRegion(const LayoutEditAnchorKey& key) const;
    std::optional<LayoutEditColorRegion> HitTestEditableColorRegion(RenderPoint clientPoint) const;
    std::vector<DashboardActiveRegion> CollectActiveRegions(const DashboardOverlayState& overlayState) const;

    bool Initialize(HWND hwnd = nullptr);
    void Shutdown();

    bool DrawWindow(const SystemSnapshot& snapshot);
    bool DrawWindow(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
    bool SaveSnapshotPng(const std::filesystem::path& imagePath,
        const SystemSnapshot& snapshot,
        const DashboardOverlayState& overlayState);
    bool RenderSnapshotOffscreen(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    bool PrimeLayoutEditDynamicRegions(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
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

private:
    friend class DashboardLayoutResolver;
    friend class DashboardLayoutEditOverlayRenderer;

    void DrawMoveOverlay(const DashboardMoveOverlayState& overlayState);
    void DrawResolvedWidget(const WidgetLayout& widget, const MetricSource& metrics);
    bool UsesFixedPreferredHeightInRows(const WidgetLayout& widget) const;
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
        std::vector<WidgetLayout>& widgets,
        std::vector<std::string>& cardReferenceStack,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath,
        bool instantiateWidgets);
    void BuildWidgetEditGuides();
    void BuildStaticEditableAnchors();
    void DrawFrame(const SystemSnapshot& snapshot, const DashboardOverlayState& overlayState);
    void WriteScreenshotActiveRegionsTrace(const DashboardOverlayState& overlayState) const;
    bool ResolveLayout(bool includeWidgetState = true);
    void ResolveNodeWidgets(const LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<WidgetLayout>& widgets,
        bool instantiateWidgets = true);
    bool SupportsLayoutSimilarityIndicator(const WidgetLayout& widget) const;
    std::vector<const WidgetLayout*> CollectSimilarityIndicatorWidgets(LayoutGuideAxis axis) const;
    int WidgetExtentForAxis(const WidgetLayout& widget, LayoutGuideAxis axis) const;
    bool IsWidgetAffectedByGuide(const WidgetLayout& widget, const LayoutEditGuide& guide) const;
    bool MatchesWidgetIdentity(const WidgetLayout& widget, const LayoutEditWidgetIdentity& identity) const;
    static bool IsContainerNode(const LayoutNodeConfig& node);
    RendererStyle BuildRendererStyle() const;
    const MetricSource& ResolveMetrics(const SystemSnapshot& snapshot);
    void InvalidateMetricSourceCache();
    void WriteTrace(const std::string& text) const;

    AppConfig config_;
    Trace& trace_;
    std::unique_ptr<::Renderer> renderer_;
    std::unique_ptr<DashboardLayoutResolver> layoutResolver_;
    std::unique_ptr<DashboardLayoutEditOverlayRenderer> layoutEditOverlayRenderer_;
    std::unique_ptr<MetricSource> cachedMetricSource_;
    const SystemSnapshot* cachedMetricSnapshot_ = nullptr;
    uint64_t cachedMetricSnapshotRevision_ = 0;
    mutable std::unordered_map<std::string, const MetricDefinitionConfig*> metricDefinitionCache_;
    mutable std::unordered_map<std::string, std::string> metricSampleValueTextCache_;
    std::string lastError_;
    double renderScale_ = 1.0;
    RenderMode renderMode_ = RenderMode::Normal;
    bool layoutGuideDragActive_ = false;
    bool interactiveDragTraceActive_ = false;
    const DashboardOverlayState* activeOverlayState_ = nullptr;
};
