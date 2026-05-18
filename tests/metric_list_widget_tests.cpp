#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/file_path.h"
#include "widget/impl/metric_list.h"
#include "widget/widget_host.h"

namespace {

const void* MetricListTestRenderBitmapResourceTypeToken() {
    static const int token = 0;
    return &token;
}

class MetricListTestRenderBitmapResource final : public RenderBitmapResource {
public:
    const void* TypeToken() const override {
        return MetricListTestRenderBitmapResourceTypeToken();
    }
};

struct DrawnText {
    RenderRect rect{};
    std::string text;
    TextStyleId style = TextStyleId::Label;
    RenderColorId color = RenderColorId::Foreground;
};

class MetricListTestEditArtifacts final : public WidgetEditArtifactRegistrar {
public:
    void RegisterStaticEditAnchor(LayoutEditAnchorRegistration registration) override {
        staticAnchors.push_back(LayoutEditAnchorRegion{registration.key,
            registration.targetRect,
            registration.anchorRect,
            registration.anchorRect,
            0,
            registration.shape});
    }

    void RegisterDynamicEditAnchor(LayoutEditAnchorRegistration) override {}

    void RegisterStaticCornerEditAnchor(const LayoutEditAnchorKey& key, const RenderRect& targetRect) override {
        staticAnchors.push_back(LayoutEditAnchorRegion{key, targetRect, targetRect, targetRect, 0, AnchorShape::Wedge});
    }

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

    void RegisterWidgetEditGuide(LayoutEditWidgetGuide guide) override {
        guides.push_back(std::move(guide));
    }

    std::vector<LayoutEditAnchorRegion> staticAnchors;
    std::vector<LayoutEditWidgetGuide> guides;
};

class MetricListTestRenderer final : public WidgetHost, public Renderer {
public:
    MetricListTestRenderer() {
        config_.layout.metricList.barHeight = 8;
        config_.layout.metricList.rowGap = 5;
        config_.layout.metricList.labelWidth = 82;
        textMetrics_.value = 16;
        definitions_.push_back(MetricDefinitionConfig{"cpu.ram", MetricDisplayStyle::Memory, true, 0.0, "GB", "RAM"});
        definitions_.push_back(
            MetricDefinitionConfig{"cpu.clock", MetricDisplayStyle::Scalar, false, 5.0, "GHz", "Clock"});
        definitions_.push_back(
            MetricDefinitionConfig{"gpu.fps", MetricDisplayStyle::Scalar, false, 240.0, "FPS", "FPS"});
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
        bitmap.resource = std::make_shared<MetricListTestRenderBitmapResource>();
        draw();
        return true;
    }

    bool DrawToLiveLayerBitmap(
        RenderBitmap& bitmap, int width, int height, RenderBitmapClear, const DrawCallback& draw) override {
        bitmap.width = width;
        bitmap.height = height;
        bitmap.storage = RenderBitmapStorage::LiveLayer;
        bitmap.resource = std::make_shared<MetricListTestRenderBitmapResource>();
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

    RenderMode CurrentRenderMode() const override {
        return RenderMode::Normal;
    }

    WidgetEditArtifactRegistrar& EditArtifacts() override {
        return editArtifacts;
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

    void DrawText(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColorId color,
        const TextLayoutOptions&) const override {
        drawnTexts.push_back(DrawnText{rect, text, style, color});
    }

    TextLayoutResult DrawTextBlock(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColorId color,
        const TextLayoutOptions&) override {
        drawnTexts.push_back(DrawnText{rect, text, style, color});
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
        for (const auto& definition : definitions_) {
            if (definition.id == metricRef) {
                return &definition;
            }
        }
        return nullptr;
    }

    const std::string& ResolveConfiguredMetricSampleValueText(std::string_view) const override {
        return empty_;
    }

    std::optional<MetricListReorderOverlayState> ActiveMetricListReorderDrag(
        const LayoutEditWidgetIdentity&) const override {
        return std::nullopt;
    }

    MetricListTestEditArtifacts editArtifacts;
    mutable std::vector<DrawnText> drawnTexts;

private:
    AppConfig config_{};
    TextStyleMetrics textMetrics_{};
    std::vector<MetricDefinitionConfig> definitions_;
    std::string empty_;
};

MetricListWidget BuildMetricListWidget() {
    MetricListWidget widget;
    LayoutNodeConfig node;
    node.parameter = "cpu.ram,cpu.clock";
    widget.Initialize(node);
    return widget;
}

MetricListWidget BuildGpuFpsMetricListWidget() {
    MetricListWidget widget;
    LayoutNodeConfig node;
    node.parameter = "gpu.fps";
    widget.Initialize(node);
    return widget;
}

int CountPlusAnchors(const MetricListTestRenderer& renderer) {
    return static_cast<int>(std::count_if(renderer.editArtifacts.staticAnchors.begin(),
        renderer.editArtifacts.staticAnchors.end(),
        [](const LayoutEditAnchorRegion& region) { return region.shape == AnchorShape::Plus; }));
}

MetricsSectionConfig BuildMetricsConfig() {
    MetricsSectionConfig metrics;
    metrics.definitions.push_back(
        MetricDefinitionConfig{"gpu.fps", MetricDisplayStyle::Scalar, false, 240.0, "FPS", "FPS"});
    return metrics;
}

}  // namespace

TEST(MetricListWidget, AddsNewRowAnchorOnlyWhenFullRowFits) {
    MetricListTestRenderer renderer;
    MetricListWidget widget = BuildMetricListWidget();
    WidgetLayout layout;
    layout.rect = RenderRect{0, 0, 240, 87};
    layout.cardId = "cpu";
    layout.editCardId = "cpu";

    widget.ResolveLayoutState(renderer, layout.rect);
    widget.BuildStaticAnchors(renderer, layout);

    EXPECT_EQ(CountPlusAnchors(renderer), 1);
}

TEST(MetricListWidget, OmitsNewRowAnchorWhenOnlyPartialRowFits) {
    MetricListTestRenderer renderer;
    MetricListWidget widget = BuildMetricListWidget();
    WidgetLayout layout;
    layout.rect = RenderRect{0, 0, 240, 73};
    layout.cardId = "cpu";
    layout.editCardId = "cpu";

    widget.ResolveLayoutState(renderer, layout.rect);
    widget.BuildStaticAnchors(renderer, layout);

    EXPECT_EQ(CountPlusAnchors(renderer), 0);
}

TEST(MetricListWidget, MiddleEllipsizesLongGpuFpsAnnotationBeforeItOverlapsValueText) {
    MetricListTestRenderer renderer;
    MetricListWidget widget = BuildGpuFpsMetricListWidget();
    WidgetLayout layout;
    layout.rect = RenderRect{0, 0, 200, 29};
    layout.cardId = "gpu";
    layout.editCardId = "gpu";

    SystemSnapshot snapshot;
    snapshot.gpu.fps = ScalarMetric{144.0, ScalarMetricUnit::Fps};
    snapshot.gpu.fpsAppName = "VeryLongApplicationName";
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    MetricSource source(snapshot, metrics);

    widget.ResolveLayoutState(renderer, layout.rect);
    widget.Draw(renderer, layout, source);

    auto annotationIt = std::find_if(renderer.drawnTexts.begin(), renderer.drawnTexts.end(), [](const DrawnText& text) {
        return text.text == "Ver...e";
    });
    ASSERT_NE(annotationIt, renderer.drawnTexts.end());
    EXPECT_EQ(annotationIt->style, TextStyleId::Label);

    auto valueIt = std::find_if(renderer.drawnTexts.begin(), renderer.drawnTexts.end(), [](const DrawnText& text) {
        return text.text == "144 FPS";
    });
    ASSERT_NE(valueIt, renderer.drawnTexts.end());
    EXPECT_LE(valueIt->rect.Width(), 56);
}

TEST(MetricListWidget, UsesWarningColorForAdminIndicatorInValueAndAnnotationSlots) {
    MetricListTestRenderer missingFpsRenderer;
    MetricListWidget widget = BuildGpuFpsMetricListWidget();
    WidgetLayout layout;
    layout.rect = RenderRect{0, 0, 200, 29};
    layout.cardId = "gpu";
    layout.editCardId = "gpu";
    const MetricsSectionConfig metrics = BuildMetricsConfig();

    SystemSnapshot missingFpsSnapshot;
    missingFpsSnapshot.gpu.fps =
        ScalarMetric{std::nullopt, ScalarMetricUnit::Fps, ScalarMetricIssue::PermissionRequired};
    MetricSource missingFpsSource(missingFpsSnapshot, metrics);

    widget.ResolveLayoutState(missingFpsRenderer, layout.rect);
    widget.Draw(missingFpsRenderer, layout, missingFpsSource);

    auto missingFpsIt = std::find_if(missingFpsRenderer.drawnTexts.begin(),
        missingFpsRenderer.drawnTexts.end(),
        [](const DrawnText& text) { return text.text == "!admin" && text.style == TextStyleId::Value; });
    ASSERT_NE(missingFpsIt, missingFpsRenderer.drawnTexts.end());
    EXPECT_EQ(missingFpsIt->color, RenderColorId::Warning);

    MetricListTestRenderer missingNameRenderer;
    SystemSnapshot missingNameSnapshot;
    missingNameSnapshot.gpu.fps = ScalarMetric{90.0, ScalarMetricUnit::Fps, ScalarMetricIssue::PermissionRequired};
    MetricSource missingNameSource(missingNameSnapshot, metrics);

    widget.ResolveLayoutState(missingNameRenderer, layout.rect);
    widget.Draw(missingNameRenderer, layout, missingNameSource);

    auto missingNameIt = std::find_if(missingNameRenderer.drawnTexts.begin(),
        missingNameRenderer.drawnTexts.end(),
        [](const DrawnText& text) { return text.text == "!admin" && text.style == TextStyleId::Label; });
    ASSERT_NE(missingNameIt, missingNameRenderer.drawnTexts.end());
    EXPECT_EQ(missingNameIt->color, RenderColorId::Warning);
}
