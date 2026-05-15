#include <chrono>
#include <gtest/gtest.h>
#include <optional>
#include <utility>
#include <vector>

#include "layout_edit/layout_edit_controller.h"

namespace {

struct ContainerOrderCall {
    int fromIndex = 0;
    int toIndex = 0;
};

class TestLayoutEditHost : public LayoutEditHost {
public:
    TestLayoutEditHost() {
        config_.layout.structure.cardsLayout.name = "rows";
        config_.layout.structure.cardsLayout.children = {LayoutNodeConfig{.name = "card", .cardReference = true},
            LayoutNodeConfig{.name = "card", .cardReference = true}};
    }

    const AppConfig& LayoutEditConfig() const override {
        return config_;
    }

    DashboardOverlayState& LayoutDashboardOverlayState() override {
        return overlayState_;
    }

    LayoutEditActiveRegions CollectLayoutEditActiveRegions() const override {
        return regions_;
    }

    double LayoutEditRenderScale() const override {
        return 1.0;
    }

    int LayoutEditSimilarityThreshold() const override {
        return 0;
    }

    void SetLayoutGuideDragActive(bool) override {}

    void SetLayoutEditInteractiveDragTraceActive(bool) override {}

    void RebuildLayoutEditArtifacts() override {}

    bool ApplyLayoutGuideWeights(const LayoutEditLayoutTarget&, const std::vector<int>&) override {
        return false;
    }

    bool ApplyMetricListOrder(const LayoutEditWidgetIdentity&, const std::vector<std::string>&) override {
        return false;
    }

    bool ApplyContainerChildOrder(const LayoutContainerChildOrderEditKey&, int fromIndex, int toIndex) override {
        containerOrderCalls.push_back(ContainerOrderCall{fromIndex, toIndex});
        return true;
    }

    std::optional<int> EvaluateLayoutWidgetExtentForWeights(const LayoutEditLayoutTarget&,
        const std::vector<int>&,
        const LayoutEditWidgetIdentity&,
        LayoutGuideAxis) override {
        return std::nullopt;
    }

    bool ApplyLayoutEditValue(LayoutEditParameter, double) override {
        return false;
    }

    void InvalidateLayoutEdit() override {
        ++invalidateCount;
    }

    void BeginLayoutEditTraceSession(const char*, const std::string&) override {}

    void RecordLayoutEditTracePhase(TracePhase, std::chrono::nanoseconds) override {}

    void EndLayoutEditTraceSession(const char*) override {}

    void AddContainerChildReorderAnchor(int index, RenderRect childRect, RenderRect anchorRect) {
        LayoutEditAnchorRegion region;
        region.key =
            LayoutEditAnchorKey{LayoutEditWidgetIdentity{"", "", {}, LayoutEditWidgetIdentity::Kind::DashboardChrome},
                LayoutContainerChildOrderEditKey{"", {}},
                index};
        region.targetRect = childRect;
        region.anchorRect = anchorRect;
        region.anchorHitPadding = 4;
        region.anchorHitRect = anchorRect.Inflate(region.anchorHitPadding, region.anchorHitPadding);
        region.shape = AnchorShape::VerticalReorder;
        region.dragAxis = AnchorDragAxis::Vertical;
        region.dragOrigin = anchorRect.Center();
        region.draggable = true;
        region.showWhenWidgetHovered = true;
        region.drawTargetOutline = false;
        regions_.Add(
            LayoutEditActiveRegion{region.anchorHitRect, LayoutEditActiveRegionKind::StaticEditAnchorHandle, region});
        regions_.Add(
            LayoutEditActiveRegion{region.targetRect, LayoutEditActiveRegionKind::StaticEditAnchorTarget, region});
    }

    std::vector<ContainerOrderCall> containerOrderCalls;
    int invalidateCount = 0;

private:
    AppConfig config_;
    DashboardOverlayState overlayState_;
    LayoutEditActiveRegions regions_;
};

}  // namespace

TEST(LayoutEditController, ContainerChildReorderMovesWhenPointerEntersTargetSlot) {
    TestLayoutEditHost host;
    host.AddContainerChildReorderAnchor(0, RenderRect{0, 0, 100, 100}, RenderRect{90, 5, 98, 15});
    host.AddContainerChildReorderAnchor(1, RenderRect{0, 110, 100, 210}, RenderRect{90, 155, 98, 165});
    LayoutEditController controller(host);

    ASSERT_TRUE(controller.HandleLButtonDown(nullptr, RenderPoint{94, 10}));

    EXPECT_TRUE(controller.HandleMouseMove(RenderPoint{94, 109}));
    EXPECT_TRUE(host.containerOrderCalls.empty());

    EXPECT_TRUE(controller.HandleMouseMove(RenderPoint{94, 110}));
    ASSERT_EQ(host.containerOrderCalls.size(), 1u);
    EXPECT_EQ(host.containerOrderCalls.front().fromIndex, 0);
    EXPECT_EQ(host.containerOrderCalls.front().toIndex, 1);
}
