#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dashboard_renderer/impl/animation_timeline.h"
#include "telemetry/impl/retained_history.h"
#include "telemetry/metrics.h"
#include "telemetry/timing.h"
#include "util/file_path.h"
#include "widget/impl/animation_primitives.h"
#include "widget/impl/throughput.h"
#include "widget/widget_host.h"

namespace {

const void* ThroughputChartTestRenderBitmapResourceTypeToken() {
    static const int token = 0;
    return &token;
}

class ThroughputChartTestRenderBitmapResource final : public RenderBitmapResource {
public:
    const void* TypeToken() const override {
        return ThroughputChartTestRenderBitmapResourceTypeToken();
    }
};

class ThroughputChartTestEditArtifacts final : public WidgetEditArtifactRegistrar {
public:
    void RegisterStaticEditAnchor(LayoutEditAnchorRegistration) override {}

    void RegisterDynamicEditAnchor(LayoutEditAnchorRegistration) override {}

    void RegisterStaticCornerEditAnchor(const LayoutEditAnchorKey&, const RenderRect&) override {}

    void RegisterDynamicCornerEditAnchor(const LayoutEditAnchorKey&, const RenderRect&) override {}

    void RegisterStaticTextAnchor(const RenderRect&,
        const std::string&,
        TextStyleId,
        const TextLayoutOptions&,
        const LayoutEditAnchorBinding&,
        std::optional<LayoutEditParameter>,
        LayoutEditTargetOutline) override {}

    void RegisterDynamicTextAnchor(const TextLayoutResult&,
        const LayoutEditAnchorBinding&,
        std::optional<LayoutEditParameter>,
        LayoutEditTargetOutline) override {}

    void RegisterDynamicTextAnchor(const RenderRect&,
        const std::string&,
        TextStyleId,
        const TextLayoutOptions&,
        const LayoutEditAnchorBinding&,
        std::optional<LayoutEditParameter>,
        LayoutEditTargetOutline) override {}

    void RegisterStaticColorEditRegion(LayoutEditParameter, const RenderRect&) override {}

    void RegisterDynamicColorEditRegion(LayoutEditParameter, const RenderRect&) override {}

    void RegisterWidgetEditGuide(LayoutEditWidgetGuide) override {}
};

struct CapturedWidgetAnimation {
    WidgetAnimationPtr animation;
    WidgetAnimationStatePtr targetState;
};

class ThroughputChartTestRenderer final : public WidgetHost, public Renderer {
public:
    ThroughputChartTestRenderer() {
        config_.layout.throughput.axisPadding = 0;
        config_.layout.throughput.headerGap = 0;
        config_.layout.throughput.guideStrokeWidth = 1;
        config_.layout.throughput.plotStrokeWidth = 2;
        config_.layout.throughput.leaderDiameter = 8;
        config_.layout.metrics.definitions.push_back(
            MetricDefinitionConfig{"network.upload", MetricDisplayStyle::Throughput, true, 0.0, "MB/s", "Up"});
        config_.layout.metrics.definitions.push_back(
            MetricDefinitionConfig{"network.download", MetricDisplayStyle::Throughput, true, 0.0, "MB/s", "Down"});
        textMetrics_.smallText = 12;
    }

    ::Renderer& Renderer() override {
        return *this;
    }

    const ::Renderer& Renderer() const override {
        return *this;
    }

    const AppConfig& Config() const override {
        return config_;
    }

    bool SetStyle(const RendererStyle&) override {
        return true;
    }

    void AttachWindow(HWND) override {}

    void Shutdown() override {}

    void SetImmediatePresent(bool) override {}

    void DiscardWindowTarget(std::string_view = {}) override {}

    bool DrawWindow(int, int, const DrawCallback& draw) override {
        draw();
        return true;
    }

    bool DrawWindowRetained(int, int, const DrawCallback& draw) override {
        draw();
        return true;
    }

    bool DrawWindowDirty(int, int, std::span<const RenderRect> dirtyRects, const DirtyDrawCallback& draw) override {
        draw(dirtyRects);
        return true;
    }

    bool DrawOffscreen(int, int, const DrawCallback& draw) override {
        draw();
        return true;
    }

    bool DrawToBitmap(
        RenderBitmap& bitmap, int width, int height, RenderBitmapClear, const DrawCallback& draw) override {
        bitmap.width = width;
        bitmap.height = height;
        bitmap.storage = RenderBitmapStorage::Generic;
        bitmap.resource = std::make_shared<ThroughputChartTestRenderBitmapResource>();
        draw();
        return true;
    }

    bool DrawToLiveLayerBitmap(
        RenderBitmap& bitmap, int width, int height, RenderBitmapClear, const DrawCallback& draw) override {
        bitmap.width = width;
        bitmap.height = height;
        bitmap.storage = RenderBitmapStorage::LiveLayer;
        bitmap.resource = std::make_shared<ThroughputChartTestRenderBitmapResource>();
        draw();
        return true;
    }

    bool SavePng(const FilePath&, int, int, const DrawCallback& draw) override {
        draw();
        return true;
    }

    const std::string& LastError() const override {
        return empty_;
    }

    const TextStyleMetrics& TextMetrics() const override {
        return textMetrics_;
    }

    int ScaleLogical(int value) const override {
        return value;
    }

    int MeasureTextWidth(TextStyleId, std::string_view text) const override {
        return static_cast<int>(text.size()) * 8;
    }

    TextLayoutResult MeasureTextBlock(
        const RenderRect& rect, const std::string&, TextStyleId, const TextLayoutOptions&) const override {
        return TextLayoutResult{rect};
    }

    void DrawText(
        const RenderRect&, const std::string&, TextStyleId, RenderColorId, const TextLayoutOptions&) const override {}

    TextLayoutResult DrawTextBlock(
        const RenderRect& rect, const std::string&, TextStyleId, RenderColorId, const TextLayoutOptions&) override {
        return TextLayoutResult{rect};
    }

    void PushClipRect(const RenderRect&) override {}

    void PopClipRect() override {}

    void PushTranslation(RenderPoint) override {}

    void PopTranslation() override {}

    bool DrawBitmap(const RenderBitmap&, RenderPoint) override {
        return true;
    }

    bool DrawBitmapRegion(const RenderBitmap&, const RenderRect&, RenderPoint) override {
        return true;
    }

    bool DrawBitmapRegions(const RenderBitmap&, std::span<const RenderRect>) override {
        return true;
    }

    bool DrawIcon(std::string_view, const RenderRect&) override {
        return true;
    }

    bool FillSolidRect(const RenderRect&, RenderColorId) override {
        return true;
    }

    bool FillSolidRoundedRect(const RenderRect&, int, RenderColorId) override {
        return true;
    }

    bool FillSolidEllipse(const RenderRect& rect, RenderColorId) override {
        ellipses.push_back(rect);
        return true;
    }

    bool FillSolidDiamond(const RenderRect&, RenderColorId) override {
        return true;
    }

    bool DrawSolidRect(const RenderRect&, const RenderStroke&) override {
        return true;
    }

    bool DrawSolidRoundedRect(const RenderRect&, int, const RenderStroke&) override {
        return true;
    }

    bool DrawSolidEllipse(const RenderRect&, const RenderStroke&) override {
        return true;
    }

    bool DrawSolidLine(RenderPoint, RenderPoint, const RenderStroke&) override {
        return true;
    }

    bool DrawArc(const RenderArc&, const RenderStroke&) override {
        return true;
    }

    bool DrawArcs(std::span<const RenderArc>, const RenderStroke&) override {
        return true;
    }

    bool DrawPolyline(std::span<const RenderPoint> points, const RenderStroke&) override {
        polylines.emplace_back(points.begin(), points.end());
        return true;
    }

    bool FillPath(const RenderPath&, RenderColorId) override {
        return true;
    }

    bool FillPaths(std::span<const RenderPath>, RenderColorId) override {
        return true;
    }

    RenderMode CurrentRenderMode() const override {
        return RenderMode::Normal;
    }

    WidgetEditArtifactRegistrar& EditArtifacts() override {
        return editArtifacts_;
    }

    LayoutEditAnchorBinding MakeEditableTextBinding(
        const WidgetLayout& widget, LayoutEditParameter parameter, int anchorId, int value) const override {
        return LayoutEditAnchorBinding{
            LayoutEditAnchorKey{
                LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath}, parameter, anchorId},
            value,
            AnchorShape::Circle,
            LayoutEditAnchorDragSpec::AxisDelta(AnchorDragAxis::Vertical)};
    }

    LayoutEditAnchorBinding MakeMetricTextBinding(
        const WidgetLayout& widget, std::string_view metricId, int anchorId) const override {
        return LayoutEditAnchorBinding{
            LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                LayoutMetricEditKey{std::string(metricId)},
                anchorId},
            0,
            AnchorShape::Wedge,
            std::nullopt};
    }

    const MetricDefinitionConfig* FindConfiguredMetricDefinition(std::string_view metricRef) const override {
        return FindMetricDefinition(config_.layout.metrics, metricRef);
    }

    const std::string& ResolveConfiguredMetricSampleValueText(std::string_view) const override {
        return empty_;
    }

    std::optional<MetricListReorderOverlayState> ActiveMetricListReorderDrag(
        const LayoutEditWidgetIdentity&) const override {
        return std::nullopt;
    }

    void AddWidgetAnimation(WidgetAnimationPtr animation, WidgetAnimationStatePtr targetState) override {
        if (animation != nullptr && targetState != nullptr) {
            animations.push_back(CapturedWidgetAnimation{std::move(animation), std::move(targetState)});
        }
    }

    void ClearDrawnGeometry() {
        polylines.clear();
        ellipses.clear();
    }

    std::vector<CapturedWidgetAnimation> animations;
    std::vector<std::vector<RenderPoint>> polylines;
    std::vector<RenderRect> ellipses;

private:
    AppConfig config_{};
    TextStyleMetrics textMetrics_{};
    ThroughputChartTestEditArtifacts editArtifacts_;
    std::string empty_;
};

ThroughputWidget BuildUploadThroughputWidget() {
    ThroughputWidget widget;
    LayoutNodeConfig node;
    node.parameter = "network.upload";
    widget.Initialize(node);
    return widget;
}

WidgetLayout BuildUploadWidgetLayout() {
    WidgetLayout layout;
    layout.rect = RenderRect{0, 0, 1000, 160};
    layout.cardId = "network";
    layout.editCardId = "network";
    layout.nodePath = {0};
    layout.widgetClass = WidgetClass::Throughput;
    return layout;
}

CapturedWidgetAnimation DrawUploadTarget(ThroughputChartTestRenderer& renderer,
    ThroughputWidget& widget,
    const WidgetLayout& layout,
    const SystemSnapshot& snapshot) {
    renderer.animations.clear();
    renderer.ClearDrawnGeometry();

    MetricSource metrics(snapshot, renderer.Config().layout.metrics);
    widget.ResolveLayoutState(renderer, layout.rect);
    widget.Draw(renderer, layout, metrics);

    EXPECT_EQ(renderer.animations.size(), 1u);
    CapturedWidgetAnimation captured = std::move(renderer.animations.back());
    renderer.animations.clear();
    return captured;
}

void PushUploadSample(SystemSnapshot& snapshot, RetainedHistoryStore& store, double value, int tickIndex) {
    snapshot.network.uploadMbps = value;
    snapshot.now.wSecond = static_cast<WORD>((tickIndex / 4) % 60);
    snapshot.now.wMilliseconds = static_cast<WORD>((tickIndex % 4) * kTelemetryRefreshInterval.count());
    store.PushSample(snapshot, RetainedHistoryKey::NetworkUpload, value);
}

RenderPoint LastBodyPointBeforeLeader(const std::vector<RenderPoint>& points, int graphRight) {
    for (auto it = points.rbegin(); it != points.rend(); ++it) {
        if (it->x < graphRight) {
            return *it;
        }
    }
    ADD_FAILURE() << "polyline has no body point before the leader";
    return RenderPoint{graphRight, 0};
}

RenderPoint RightmostPointAtY(const std::vector<RenderPoint>& points, int y) {
    RenderPoint match{};
    bool found = false;
    for (const RenderPoint& point : points) {
        if (point.y == y && point.x >= match.x) {
            match = point;
            found = true;
        }
    }
    EXPECT_TRUE(found) << "polyline has no point at tracked y=" << y;
    return match;
}

RenderPoint DrawSampledTrackedPoint(ThroughputChartTestRenderer& renderer,
    DashboardAnimationTimeline& timeline,
    const CapturedWidgetAnimation& captured,
    std::uint64_t targetVersion,
    DashboardAnimationTimeline::Clock::time_point now,
    std::optional<int> trackedY = std::nullopt) {
    renderer.ClearDrawnGeometry();
    timeline.BeginFrame(now);
    WidgetAnimationStatePtr sampled = timeline.Resolve(captured.animation->Key(), *captured.targetState, targetVersion);
    captured.animation->Draw(renderer, *sampled);
    timeline.EndFrame();

    EXPECT_FALSE(renderer.polylines.empty());
    EXPECT_FALSE(renderer.ellipses.empty());
    if (renderer.polylines.empty() || renderer.ellipses.empty()) {
        return {};
    }
    const RenderRect& leaderRect = renderer.ellipses.back();
    const int graphRight = (leaderRect.left + leaderRect.right) / 2;
    if (trackedY.has_value()) {
        return RightmostPointAtY(renderer.polylines.back(), *trackedY);
    }
    return LastBodyPointBeforeLeader(renderer.polylines.back(), graphRight);
}

}  // namespace

TEST(ThroughputChartWidget, RetainedHistoryPhaseAndDrawnPolylineContinueLeftAcrossInterruptedCommit) {
    using Clock = DashboardAnimationTimeline::Clock;

    ThroughputChartTestRenderer renderer;
    ThroughputWidget widget = BuildUploadThroughputWidget();
    const WidgetLayout layout = BuildUploadWidgetLayout();
    RetainedHistoryStore retainedHistory;
    SystemSnapshot snapshot;
    int tickIndex = 0;

    snapshot.network.downloadMbps = 100.0;
    for (int sample = 0; sample < 4; ++sample) {
        retainedHistory.PushSample(snapshot, RetainedHistoryKey::NetworkDownload, 100.0);
    }

    for (int sample = 0; sample < 4; ++sample) {
        PushUploadSample(snapshot, retainedHistory, 10.0, tickIndex++);
    }
    for (int sample = 0; sample < 3; ++sample) {
        PushUploadSample(snapshot, retainedHistory, 20.0, tickIndex++);
    }

    CapturedWidgetAnimation firstTarget = DrawUploadTarget(renderer, widget, layout, snapshot);
    const ThroughputChartSample& firstSample = ThroughputChartSampleFromState(*firstTarget.targetState);
    ASSERT_EQ(firstSample.samples.size(), kRetainedThroughputHistorySamples);
    EXPECT_DOUBLE_EQ(firstSample.samples.back(), 10.0);
    EXPECT_DOUBLE_EQ(firstSample.liveLeaderMbps, 17.5);
    EXPECT_DOUBLE_EQ(firstSample.maxGraph, 100.0);
    EXPECT_DOUBLE_EQ(firstSample.plotShiftSamples, 0.75);

    PushUploadSample(snapshot, retainedHistory, 20.0, tickIndex++);

    CapturedWidgetAnimation committedTarget = DrawUploadTarget(renderer, widget, layout, snapshot);
    const ThroughputChartSample& committedSample = ThroughputChartSampleFromState(*committedTarget.targetState);
    ASSERT_EQ(committedSample.samples.size(), kRetainedThroughputHistorySamples);
    EXPECT_DOUBLE_EQ(committedSample.samples[committedSample.samples.size() - 2], 10.0);
    EXPECT_DOUBLE_EQ(committedSample.samples.back(), 20.0);
    EXPECT_DOUBLE_EQ(committedSample.liveLeaderMbps, 20.0);
    EXPECT_DOUBLE_EQ(committedSample.maxGraph, 100.0);
    EXPECT_DOUBLE_EQ(committedSample.plotShiftSamples, 0.0);

    DashboardAnimationTimeline timeline(kTelemetryRefreshInterval);
    const Clock::time_point start = Clock::time_point{};
    (void)DrawSampledTrackedPoint(renderer, timeline, firstTarget, 1, start);
    (void)DrawSampledTrackedPoint(renderer, timeline, firstTarget, 1, start + kTelemetryRefreshInterval);

    const Clock::time_point commitStart = start + kTelemetryRefreshInterval + std::chrono::milliseconds(50);
    (void)DrawSampledTrackedPoint(renderer, timeline, committedTarget, 2, commitStart);

    const Clock::time_point interruptedAt = commitStart + std::chrono::milliseconds(200);
    const RenderPoint beforeRetargetPoint =
        DrawSampledTrackedPoint(renderer, timeline, committedTarget, 2, interruptedAt);

    PushUploadSample(snapshot, retainedHistory, 30.0, tickIndex++);

    CapturedWidgetAnimation nextPhaseTarget = DrawUploadTarget(renderer, widget, layout, snapshot);
    const ThroughputChartSample& nextPhaseSample = ThroughputChartSampleFromState(*nextPhaseTarget.targetState);
    ASSERT_EQ(nextPhaseSample.samples.size(), kRetainedThroughputHistorySamples);
    EXPECT_DOUBLE_EQ(nextPhaseSample.samples[committedSample.samples.size() - 2], 10.0);
    EXPECT_DOUBLE_EQ(nextPhaseSample.samples.back(), 20.0);
    EXPECT_DOUBLE_EQ(nextPhaseSample.liveLeaderMbps, 22.5);
    EXPECT_DOUBLE_EQ(nextPhaseSample.maxGraph, 100.0);
    EXPECT_DOUBLE_EQ(nextPhaseSample.plotShiftSamples, 0.25);

    const RenderPoint retargetStartPoint =
        DrawSampledTrackedPoint(renderer, timeline, nextPhaseTarget, 3, interruptedAt, beforeRetargetPoint.y);
    const RenderPoint afterRetargetPoint = DrawSampledTrackedPoint(
        renderer, timeline, nextPhaseTarget, 3, interruptedAt + std::chrono::milliseconds(100), beforeRetargetPoint.y);

    EXPECT_EQ(retargetStartPoint.x, beforeRetargetPoint.x);
    EXPECT_LT(afterRetargetPoint.x, retargetStartPoint.x);
}
