#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "dashboard_renderer/impl/animation_timeline.h"

namespace {

using Clock = DashboardAnimationTimeline::Clock;

AnimationDataKey ScalarKey(std::string subject) {
    return AnimationDataKey{AnimationDataKind::ScalarFill, std::move(subject), {}};
}

AnimationDataKey ThroughputKey(std::string subject) {
    return AnimationDataKey{AnimationDataKind::ThroughputChart, std::move(subject), {}};
}

ScalarFillSample ScalarTarget(double value, double peak) {
    return ScalarFillSample{value, peak};
}

}  // namespace

TEST(AnimationTimeline, FirstSeenScalarStartsAtZeroAndReachesTarget) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.load");

    timeline.BeginFrame(start);
    ScalarFillSample sample = timeline.ResolveScalar(key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.0);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
    EXPECT_TRUE(timeline.HasActiveAnimations(start + std::chrono::milliseconds(250)));

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    sample = timeline.ResolveScalar(key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.8);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
    EXPECT_FALSE(timeline.HasActiveAnimations(start + std::chrono::milliseconds(500)));
}

TEST(AnimationTimeline, InterruptedScalarUsesCurrentInterpolatedValueAsNewStart) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("gpu.vram");

    timeline.BeginFrame(start);
    (void)timeline.ResolveScalar(key, ScalarTarget(1.0, 1.0));
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(250));
    ScalarFillSample sample = timeline.ResolveScalar(key, ScalarTarget(0.2, 0.4));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.5);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 1.0);

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    sample = timeline.ResolveScalar(key, ScalarTarget(0.2, 0.4));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.35);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.7);
}

TEST(AnimationTimeline, UnavailableScalarAnimatesToZeroThenDisappears) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("gpu.fps");

    timeline.BeginFrame(start);
    (void)timeline.ResolveScalar(key, ScalarTarget(0.6, 0.75));
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    (void)timeline.ResolveScalar(key, ScalarTarget(0.6, 0.75));
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(600));
    ScalarFillSample sample = timeline.ResolveScalar(key, ScalarFillSample{});
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.6);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.75);

    timeline.BeginFrame(start + std::chrono::milliseconds(1100));
    sample = timeline.ResolveScalar(key, ScalarFillSample{});
    timeline.EndFrame();

    EXPECT_FALSE(sample.valueRatio.has_value());
    EXPECT_FALSE(sample.peakRatio.has_value());
}

TEST(AnimationTimeline, ThroughputVectorsAlignByNewestSample) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ThroughputKey("network.upload");

    ThroughputChartSample first;
    first.samples = {10.0, 20.0};
    first.maxGraph = 20.0;
    first.timeMarkerOffsetSamples = 3.0;
    first.guideStepMbps = 5.0;

    timeline.BeginFrame(start);
    (void)timeline.ResolveThroughput(key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    (void)timeline.ResolveThroughput(key, first);
    timeline.EndFrame();

    ThroughputChartSample next;
    next.samples = {5.0, 15.0, 25.0};
    next.maxGraph = 30.0;
    next.timeMarkerOffsetSamples = 4.0;
    next.guideStepMbps = 5.0;

    timeline.BeginFrame(start + std::chrono::milliseconds(600));
    (void)timeline.ResolveThroughput(key, next);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(850));
    const ThroughputChartSample sample = timeline.ResolveThroughput(key, next);
    timeline.EndFrame();

    ASSERT_EQ(sample.samples.size(), 3u);
    EXPECT_DOUBLE_EQ(sample.samples[0], 2.5);
    EXPECT_DOUBLE_EQ(sample.samples[1], 12.5);
    EXPECT_DOUBLE_EQ(sample.samples[2], 22.5);
    EXPECT_DOUBLE_EQ(sample.maxGraph, 25.0);
    EXPECT_DOUBLE_EQ(sample.timeMarkerOffsetSamples, 3.5);
    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 0.0);
}

TEST(AnimationTimeline, ThroughputCarriesPlotShiftAndTargetTailWhenPhaseAdvances) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ThroughputKey("network.download");

    ThroughputChartSample first;
    first.samples = {0.0, 10.0, 30.0, 70.0};
    first.maxGraph = 100.0;
    first.timeMarkerOffsetSamples = 8.0;

    timeline.BeginFrame(start);
    (void)timeline.ResolveThroughput(key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    (void)timeline.ResolveThroughput(key, first);
    timeline.EndFrame();

    ThroughputChartSample next;
    next.samples = {10.0, 30.0, 70.0, 90.0};
    next.maxGraph = 100.0;
    next.timeMarkerOffsetSamples = 9.0;

    timeline.BeginFrame(start + std::chrono::milliseconds(600));
    (void)timeline.ResolveThroughput(key, next);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(850));
    const ThroughputChartSample sample = timeline.ResolveThroughput(key, next);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 0.5);
    EXPECT_EQ(sample.samples, (std::vector<double>{0.0, 10.0, 30.0, 70.0, 90.0}));
}

TEST(AnimationTimeline, InterruptedThroughputScrollContinuesFromCurrentPlotShift) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ThroughputKey("network.download");

    ThroughputChartSample first;
    first.samples = {0.0, 10.0, 30.0, 70.0};
    first.maxGraph = 100.0;
    first.timeMarkerOffsetSamples = 8.0;

    timeline.BeginFrame(start);
    (void)timeline.ResolveThroughput(key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    (void)timeline.ResolveThroughput(key, first);
    timeline.EndFrame();

    ThroughputChartSample second;
    second.samples = {10.0, 30.0, 70.0, 90.0};
    second.maxGraph = 100.0;
    second.timeMarkerOffsetSamples = 9.0;

    timeline.BeginFrame(start + std::chrono::milliseconds(600));
    (void)timeline.ResolveThroughput(key, second);
    timeline.EndFrame();

    ThroughputChartSample third;
    third.samples = {30.0, 70.0, 90.0, 50.0};
    third.maxGraph = 100.0;
    third.timeMarkerOffsetSamples = 10.0;

    timeline.BeginFrame(start + std::chrono::milliseconds(850));
    ThroughputChartSample sample = timeline.ResolveThroughput(key, third);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 0.5);
    EXPECT_EQ(sample.samples, (std::vector<double>{0.0, 10.0, 30.0, 70.0, 90.0}));

    timeline.BeginFrame(start + std::chrono::milliseconds(1100));
    sample = timeline.ResolveThroughput(key, third);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 1.25);
    EXPECT_EQ(sample.samples, (std::vector<double>{0.0, 10.0, 30.0, 70.0, 90.0, 50.0}));
}

TEST(AnimationTimeline, ThroughputTimeMarkerMovesForwardAcrossWrap) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ThroughputKey("storage.read");

    ThroughputChartSample first;
    first.samples = {1.0};
    first.maxGraph = 10.0;
    first.timeMarkerOffsetSamples = 19.0;

    timeline.BeginFrame(start);
    (void)timeline.ResolveThroughput(key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    (void)timeline.ResolveThroughput(key, first);
    timeline.EndFrame();

    ThroughputChartSample next = first;
    next.timeMarkerOffsetSamples = 1.0;

    timeline.BeginFrame(start + std::chrono::milliseconds(600));
    (void)timeline.ResolveThroughput(key, next);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(850));
    const ThroughputChartSample sample = timeline.ResolveThroughput(key, next);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.timeMarkerOffsetSamples, 20.0);
}

TEST(AnimationTimeline, UntouchedKeysAreDiscardedAfterAFrame) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.clock");

    timeline.BeginFrame(start);
    (void)timeline.ResolveScalar(key, ScalarTarget(1.0, 1.0));
    timeline.EndFrame();
    EXPECT_TRUE(timeline.HasActiveAnimations(start + std::chrono::milliseconds(100)));

    timeline.BeginFrame(start + std::chrono::milliseconds(100));
    timeline.EndFrame();

    EXPECT_FALSE(timeline.HasActiveAnimations(start + std::chrono::milliseconds(100)));
}
