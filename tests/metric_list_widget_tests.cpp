#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "widget/impl/metric_list.h"
#include "widget/widget_host.h"

namespace {

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

    void SetTraceSuppressed(bool) override {}

    void DiscardWindowTarget(std::string_view = {}) override {}

    bool DrawWindow(int, int, const DrawCallback& draw) override {
        draw();
        return true;
    }

    bool DrawOffscreen(int, int, const DrawCallback& draw) override {
        draw();
        return true;
    }

    bool SavePng(const std::filesystem::path&, int, int, const DrawCallback& draw) override {
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
            value};
    }

    LayoutEditAnchorBinding MakeMetricTextBinding(
        const WidgetLayout& widget, std::string_view metricId, int anchorId) const override {
        return LayoutEditAnchorBinding{
            LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                LayoutMetricEditKey{std::string(metricId)},
                anchorId}};
    }

    void RegisterStaticEditableAnchorRegion(const LayoutEditAnchorKey& key,
        const RenderRect& targetRect,
        const RenderRect& anchorRect,
        AnchorShape shape,
        AnchorDragAxis,
        AnchorDragMode,
        RenderPoint,
        double,
        bool,
        bool,
        bool,
        int) override {
        staticAnchors.push_back(LayoutEditAnchorRegion{key, targetRect, anchorRect, anchorRect, 0, shape});
    }

    void RegisterDynamicEditableAnchorRegion(const LayoutEditAnchorKey&,
        const RenderRect&,
        const RenderRect&,
        AnchorShape,
        AnchorDragAxis,
        AnchorDragMode,
        RenderPoint,
        double,
        bool,
        bool,
        bool,
        int) override {}

    void RegisterStaticTextAnchor(const RenderRect&,
        const std::string&,
        TextStyleId,
        const TextLayoutOptions&,
        const LayoutEditAnchorBinding&,
        std::optional<LayoutEditParameter>,
        bool) override {}

    void RegisterDynamicTextAnchor(
        const TextLayoutResult&, const LayoutEditAnchorBinding&, std::optional<LayoutEditParameter>, bool) override {}

    void RegisterDynamicTextAnchor(const RenderRect&,
        const std::string&,
        TextStyleId,
        const TextLayoutOptions&,
        const LayoutEditAnchorBinding&,
        std::optional<LayoutEditParameter>,
        bool) override {}

    void RegisterStaticColorEditRegion(LayoutEditParameter, const RenderRect&) override {}

    void RegisterDynamicColorEditRegion(LayoutEditParameter, const RenderRect&) override {}

    std::vector<LayoutEditWidgetGuide>& WidgetEditGuidesMutable() override {
        return guides_;
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

    std::vector<LayoutEditAnchorRegion> staticAnchors;

private:
    AppConfig config_{};
    TextStyleMetrics textMetrics_{};
    std::vector<MetricDefinitionConfig> definitions_;
    std::vector<LayoutEditWidgetGuide> guides_;
    std::string empty_;
};

MetricListWidget BuildMetricListWidget() {
    MetricListWidget widget;
    LayoutNodeConfig node;
    node.parameter = "cpu.ram,cpu.clock";
    widget.Initialize(node);
    return widget;
}

int CountPlusAnchors(const MetricListTestRenderer& renderer) {
    return static_cast<int>(std::count_if(renderer.staticAnchors.begin(),
        renderer.staticAnchors.end(),
        [](const LayoutEditAnchorRegion& region) { return region.shape == AnchorShape::Plus; }));
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
