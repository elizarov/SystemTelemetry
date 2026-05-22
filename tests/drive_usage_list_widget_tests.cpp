#include <gtest/gtest.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/config_telemetry.h"
#include "telemetry/metrics.h"
#include "util/file_path.h"
#include "widget/impl/drive_usage_list.h"
#include "widget/widget_host.h"

namespace {

struct CapturedWidgetAnimation {
    WidgetAnimationPtr animation;
    WidgetAnimationStatePtr targetState;
    std::optional<RenderRect> clipRect;
};

class DriveUsageListTestEditArtifacts final : public WidgetEditArtifactRegistrar {
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

class DriveUsageListTestRenderer final : public WidgetHost, public Renderer {
public:
    DriveUsageListTestRenderer() {
        config_.storage.drives = {"C:", "D:"};
        config_.layout.driveUsageList.headerGap = 2;
        config_.layout.driveUsageList.rowGap = 5;
        config_.layout.driveUsageList.barHeight = 8;
        config_.layout.driveUsageList.labelGap = 4;
        config_.layout.driveUsageList.activityWidth = 6;
        config_.layout.driveUsageList.rwGap = 2;
        config_.layout.driveUsageList.barGap = 4;
        config_.layout.driveUsageList.percentGap = 4;
        config_.layout.driveUsageList.freeWidth = 48;
        config_.layout.driveUsageList.activitySegments = 4;
        config_.layout.driveUsageList.activitySegmentGap = 1;
        config_.layout.metrics.definitions.push_back(
            MetricDefinitionConfig{"drive.activity.read", MetricDisplayStyle::LabelOnly, false, 0.0, "", "R"});
        config_.layout.metrics.definitions.push_back(
            MetricDefinitionConfig{"drive.activity.write", MetricDisplayStyle::LabelOnly, false, 0.0, "", "W"});
        config_.layout.metrics.definitions.push_back(
            MetricDefinitionConfig{"drive.usage", MetricDisplayStyle::Percent, false, 100.0, "%", "Usage"});
        config_.layout.metrics.definitions.push_back(
            MetricDefinitionConfig{"drive.free", MetricDisplayStyle::SizeAuto, true, 0.0, "GB", "Free"});
        textMetrics_.label = 12;
        textMetrics_.smallText = 10;
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
        draw();
        return true;
    }

    bool DrawToLiveLayerBitmap(
        RenderBitmap& bitmap, int width, int height, RenderBitmapClear clear, const DrawCallback& draw) override {
        return DrawToBitmap(bitmap, width, height, clear, draw);
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

    RenderMode CurrentRenderMode() const override {
        return RenderMode::Normal;
    }

    WidgetEditArtifactRegistrar& EditArtifacts() override {
        return editArtifacts_;
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

    bool FillSolidEllipse(const RenderRect&, RenderColorId) override {
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

    bool DrawPolyline(std::span<const RenderPoint>, const RenderStroke&) override {
        return true;
    }

    bool FillPath(const RenderPath&, RenderColorId) override {
        return true;
    }

    bool FillPaths(std::span<const RenderPath>, RenderColorId) override {
        return true;
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

    const std::string& ResolveConfiguredMetricSampleValueText(std::string_view metricRef) const override {
        sampleValueText_ = ResolveMetricSampleValueText(config_.layout.metrics, std::string(metricRef));
        return sampleValueText_;
    }

    std::optional<MetricListReorderOverlayState> ActiveMetricListReorderDrag(
        const LayoutEditWidgetIdentity&) const override {
        return std::nullopt;
    }

    void AddWidgetAnimation(WidgetAnimationPtr animation,
        WidgetAnimationStatePtr targetState,
        std::optional<RenderRect> clipRect = std::nullopt) override {
        if (animation != nullptr && targetState != nullptr) {
            animations.push_back(CapturedWidgetAnimation{std::move(animation), std::move(targetState), clipRect});
        }
    }

    std::vector<CapturedWidgetAnimation> animations;

private:
    AppConfig config_{};
    TextStyleMetrics textMetrics_{};
    DriveUsageListTestEditArtifacts editArtifacts_;
    mutable std::string sampleValueText_;
    std::string empty_;
};

SystemSnapshot BuildDriveSnapshot() {
    SystemSnapshot snapshot;
    snapshot.drives.push_back(DriveInfo{
        .label = "C:",
        .volumeLabel = "System",
        .totalGb = 100.0,
        .usedPercent = 60.0,
        .freeGb = 40.0,
        .readMbps = 20.0,
        .writeMbps = 10.0,
        .driveType = DRIVE_FIXED,
    });
    snapshot.drives.push_back(DriveInfo{
        .label = "D:",
        .volumeLabel = "Data",
        .totalGb = 200.0,
        .usedPercent = 75.0,
        .freeGb = 50.0,
        .readMbps = 30.0,
        .writeMbps = 15.0,
        .driveType = DRIVE_FIXED,
    });
    return snapshot;
}

void ExpectRectEq(const RenderRect& actual, const RenderRect& expected) {
    EXPECT_EQ(actual.left, expected.left);
    EXPECT_EQ(actual.top, expected.top);
    EXPECT_EQ(actual.right, expected.right);
    EXPECT_EQ(actual.bottom, expected.bottom);
}

}  // namespace

TEST(DriveUsageListWidget, ClipsPartiallyOverflowingRowAnimationsToWidgetRect) {
    DriveUsageListTestRenderer renderer;
    DriveUsageListWidget widget;
    LayoutNodeConfig node;
    widget.Initialize(node);
    WidgetLayout layout;
    layout.rect = RenderRect{0, 0, 220, 39};
    layout.cardId = "storage";
    layout.editCardId = "storage";

    const SystemSnapshot snapshot = BuildDriveSnapshot();
    MetricSource source(snapshot, renderer.Config().layout.metrics);

    widget.ResolveLayoutState(renderer, layout.rect);
    widget.Draw(renderer, layout, source);

    ASSERT_EQ(renderer.animations.size(), 6u);
    for (size_t index = 3; index < renderer.animations.size(); ++index) {
        const CapturedWidgetAnimation& captured = renderer.animations[index];
        ASSERT_TRUE(captured.clipRect.has_value());
        ExpectRectEq(*captured.clipRect, layout.rect);
        EXPECT_GT(captured.animation->DirtyBounds().bottom, layout.rect.bottom);
    }
}
