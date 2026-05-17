#include <gtest/gtest.h>

#include "config/metric_board_binding.h"

namespace {

BoardMetricBindingTarget RequireMetricBoardBindingTarget(const char* metricId) {
    auto target = ResolveMetricBoardBindingTarget(metricId);
    if (!target.has_value()) {
        ADD_FAILURE() << "expected board binding target for " << metricId;
        return {};
    }
    return *target;
}

}  // namespace

TEST(BoardMetricBinding, ParsesBoardTemperatureAndFanMetrics) {
    const BoardMetricBindingTarget temperature = RequireMetricBoardBindingTarget("board.temp.cpu");
    EXPECT_EQ(temperature.kind, BoardMetricBindingKind::Temperature);
    EXPECT_EQ(temperature.logicalName, "cpu");

    const BoardMetricBindingTarget fan = RequireMetricBoardBindingTarget("board.fan.system");
    EXPECT_EQ(fan.kind, BoardMetricBindingKind::Fan);
    EXPECT_EQ(fan.logicalName, "system");
}

TEST(BoardMetricBinding, MapsGpuProviderFallbackMetricsToBoardBindings) {
    const BoardMetricBindingTarget temperature = RequireMetricBoardBindingTarget("gpu.temp");
    EXPECT_EQ(temperature.kind, BoardMetricBindingKind::Temperature);
    EXPECT_EQ(temperature.logicalName, "cpu");

    const BoardMetricBindingTarget target = RequireMetricBoardBindingTarget("gpu.fan");
    EXPECT_EQ(target.kind, BoardMetricBindingKind::Fan);
    EXPECT_EQ(target.logicalName, "gpu");
}

TEST(BoardMetricBinding, IgnoresMetricsWithoutBoardBindingEditors) {
    EXPECT_FALSE(ResolveMetricBoardBindingTarget("gpu.load").has_value());
    EXPECT_FALSE(ResolveMetricBoardBindingTarget("cpu.fan").has_value());
}

TEST(BoardMetricBinding, ExposesDirectBoardBindingsWithoutRuntimeFallbackUse) {
    EXPECT_TRUE(ShouldExposeMetricBoardBinding("board.temp.cpu", {}));
    EXPECT_TRUE(ShouldExposeMetricBoardBinding("board.fan.system", {}));
}

TEST(BoardMetricBinding, ExposesFallbackBindingsOnlyWhenRuntimeUsesThem) {
    const BoardMetricBindingTarget gpuFan = RequireMetricBoardBindingTarget("gpu.fan");

    EXPECT_FALSE(ShouldExposeMetricBoardBinding("gpu.fan", {}));
    EXPECT_FALSE(ShouldExposeMetricBoardBinding("gpu.temp", {MetricBoardBindingUse{"gpu.fan", gpuFan}}));
    EXPECT_TRUE(ShouldExposeMetricBoardBinding("gpu.fan", {MetricBoardBindingUse{"gpu.fan", gpuFan}}));
}
