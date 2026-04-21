#include <gtest/gtest.h>

#include "layout_edit/layout_snap_solver.h"

namespace {

int SplitWeightedFirstChild(int totalExtent, int gap, int firstWeight, int secondWeight) {
    const int available = totalExtent - gap;
    return available * firstWeight / (firstWeight + secondWeight);
}

int ComputeCpuGaugeWidth(int gaugeWeight) {
    constexpr int kWindowWidth = 800;
    constexpr int kOuterMargin = 10;
    constexpr int kCardGap = 10;
    constexpr int kCardPadding = 10;
    constexpr int kWidgetColumnGap = 16;

    constexpr int kTopRowCpuWeight = 849;
    constexpr int kTopRowGpuWeight = 691;
    constexpr int kCpuInnerCombinedWeight = 777;

    const int dashboardWidth = kWindowWidth - (kOuterMargin * 2);
    const int cpuCardWidth = SplitWeightedFirstChild(dashboardWidth, kCardGap, kTopRowCpuWeight, kTopRowGpuWeight);
    const int cpuContentWidth = cpuCardWidth - (kCardPadding * 2);
    return SplitWeightedFirstChild(
        cpuContentWidth, kWidgetColumnGap, gaugeWeight, kCpuInnerCombinedWeight - gaugeWeight);
}

int ComputeGpuGaugeWidth() {
    constexpr int kWindowWidth = 800;
    constexpr int kOuterMargin = 10;
    constexpr int kCardGap = 10;
    constexpr int kCardPadding = 10;
    constexpr int kWidgetColumnGap = 16;

    constexpr int kTopRowCpuWeight = 849;
    constexpr int kTopRowGpuWeight = 691;
    constexpr int kGpuGaugeWeight = 5;
    constexpr int kGpuMetricListWeight = 7;

    const int dashboardWidth = kWindowWidth - (kOuterMargin * 2);
    const int distributable = dashboardWidth - kCardGap;
    const int cpuCardWidth = distributable * kTopRowCpuWeight / (kTopRowCpuWeight + kTopRowGpuWeight);
    const int gpuCardWidth = distributable - cpuCardWidth;
    const int gpuContentWidth = gpuCardWidth - (kCardPadding * 2);
    return SplitWeightedFirstChild(gpuContentWidth, kWidgetColumnGap, kGpuGaugeWeight, kGpuMetricListWeight);
}

}  // namespace

TEST(LayoutSnapSolver, FindsExactIntegerMatchThroughNestedWeights) {
    constexpr int kCurrentGaugeWeight = 251;
    constexpr int kCombinedWeight = 777;
    constexpr int kThreshold = 5;

    const int startExtent = ComputeCpuGaugeWidth(kCurrentGaugeWeight);
    const int targetExtent = ComputeGpuGaugeWidth();
    ASSERT_EQ(startExtent, 125);
    ASSERT_EQ(targetExtent, 129);
    ASSERT_TRUE(targetExtent - startExtent <= kThreshold);

    const std::optional<int> snappedWeight = layout_snap_solver::FindNearestSnapWeight(kCurrentGaugeWeight,
        kCombinedWeight,
        kThreshold,
        {layout_snap_solver::SnapCandidate{targetExtent, targetExtent - startExtent, 0}},
        [](int firstWeight) -> std::optional<int> { return ComputeCpuGaugeWidth(firstWeight); });

    ASSERT_TRUE(snappedWeight.has_value());
    EXPECT_EQ(ComputeCpuGaugeWidth(*snappedWeight), targetExtent);
    EXPECT_TRUE(*snappedWeight > kCurrentGaugeWeight);
}
