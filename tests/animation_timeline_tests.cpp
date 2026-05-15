#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "dashboard_renderer/impl/animation_timeline.h"
#include "widget/impl/animation_primitives.h"

namespace {

using Clock = DashboardAnimationTimeline::Clock;

AnimationDataKey ScalarKey(std::string subject) {
    return AnimationDataKey{std::move(subject), {}};
}

AnimationDataKey ThroughputKey(std::string subject) {
    return AnimationDataKey{std::move(subject), {}};
}

ScalarFillSample ScalarTarget(double value, double peak) {
    return ScalarFillSample{value, peak};
}

ScalarFillSample ResolveScalar(DashboardAnimationTimeline& timeline,
    const AnimationDataKey& key,
    const ScalarFillSample& target,
    std::uint64_t targetVersion = 1) {
    WidgetAnimationStatePtr targetState = MakeScalarFillAnimationState(target);
    WidgetAnimationStatePtr sampled = timeline.Resolve(key, *targetState, targetVersion);
    return ScalarFillSampleFromState(*sampled);
}

ThroughputChartSample ResolveThroughput(DashboardAnimationTimeline& timeline,
    const AnimationDataKey& key,
    const ThroughputChartSample& target,
    std::uint64_t targetVersion = 1) {
    WidgetAnimationStatePtr targetState = MakeThroughputChartAnimationState(target);
    WidgetAnimationStatePtr sampled = timeline.Resolve(key, *targetState, targetVersion);
    return ThroughputChartSampleFromState(*sampled);
}

}  // namespace

TEST(AnimationTimeline, FirstSeenScalarStartsAtZeroAndReachesTarget) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.load");

    timeline.BeginFrame(start);
    ScalarFillSample sample = ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.0);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
    EXPECT_TRUE(timeline.HasActiveAnimations(start + std::chrono::milliseconds(250)));

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    sample = ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
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
    (void)ResolveScalar(timeline, key, ScalarTarget(1.0, 1.0));
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(250));
    ScalarFillSample sample = ResolveScalar(timeline, key, ScalarTarget(0.2, 0.4), 2);
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.5);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 1.0);

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    sample = ResolveScalar(timeline, key, ScalarTarget(0.2, 0.4), 2);
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.35);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.7);
}

TEST(AnimationTimeline, UnchangedScalarTargetKeepsProgressAcrossLayoutOnlyFrames) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.load");

    timeline.BeginFrame(start);
    (void)ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(250));
    const ScalarFillSample sample = ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.4);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
}

TEST(AnimationTimeline, ScaleOnlyRepackagedTargetKeepsStoredMetricTarget) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.load");
    WidgetAnimationStatePtr firstTarget = MakeScalarFillAnimationState(ScalarTarget(0.8, 0.9));
    WidgetAnimationStatePtr scaleRepackagedTarget = MakeScalarFillAnimationState(ScalarTarget(0.0, 0.0));

    timeline.BeginFrame(start);
    (void)timeline.Resolve(key, *firstTarget, 7);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(250));
    (void)timeline.Resolve(key, *scaleRepackagedTarget, 7);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    WidgetAnimationStatePtr sampled = timeline.Resolve(key, *scaleRepackagedTarget, 7);
    const ScalarFillSample sample = ScalarFillSampleFromState(*sampled);
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.8);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
}

TEST(AnimationTimeline, LayoutOnlyRepackagedUnavailableTargetKeepsStoredMetricTarget) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("gpu.load");
    WidgetAnimationStatePtr firstTarget = MakeScalarFillAnimationState(ScalarTarget(0.8, 0.9));
    WidgetAnimationStatePtr layoutRepackagedTarget = MakeScalarFillAnimationState(ScalarFillSample{});

    timeline.BeginFrame(start);
    (void)timeline.Resolve(key, *firstTarget, 11);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(250));
    (void)timeline.Resolve(key, *layoutRepackagedTarget, 11);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    WidgetAnimationStatePtr sampled = timeline.Resolve(key, *layoutRepackagedTarget, 11);
    const ScalarFillSample sample = ScalarFillSampleFromState(*sampled);
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.8);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
}

TEST(AnimationTimeline, UntouchedKeyCanSurviveLayoutOnlyFrame) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.load");

    timeline.BeginFrame(start);
    (void)ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(200));
    timeline.EndFrame(DashboardAnimationTimeline::TrackRetention::KeepUntouched);

    timeline.BeginFrame(start + std::chrono::milliseconds(250));
    const ScalarFillSample sample = ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.4);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
}

TEST(AnimationTimeline, CompletedUntouchedKeyCanSurviveLayoutOnlyFrame) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("gpu.load");

    timeline.BeginFrame(start);
    (void)ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    (void)ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(600));
    timeline.EndFrame(DashboardAnimationTimeline::TrackRetention::KeepUntouched);

    timeline.BeginFrame(start + std::chrono::milliseconds(650));
    const ScalarFillSample sample = ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.8);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
}

TEST(AnimationTimeline, UnavailableScalarAnimatesToZeroThenDisappears) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("gpu.fps");

    timeline.BeginFrame(start);
    (void)ResolveScalar(timeline, key, ScalarTarget(0.6, 0.75));
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    (void)ResolveScalar(timeline, key, ScalarTarget(0.6, 0.75));
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(600));
    ScalarFillSample sample = ResolveScalar(timeline, key, ScalarFillSample{}, 2);
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.6);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.75);

    timeline.BeginFrame(start + std::chrono::milliseconds(1100));
    sample = ResolveScalar(timeline, key, ScalarFillSample{}, 2);
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
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    ThroughputChartSample next;
    next.samples = {5.0, 15.0, 25.0};
    next.maxGraph = 30.0;
    next.timeMarkerOffsetSamples = 4.0;
    next.guideStepMbps = 5.0;

    timeline.BeginFrame(start + std::chrono::milliseconds(600));
    (void)ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(850));
    const ThroughputChartSample sample = ResolveThroughput(timeline, key, next, 2);
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
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    ThroughputChartSample next;
    next.samples = {10.0, 30.0, 70.0, 90.0};
    next.maxGraph = 100.0;
    next.timeMarkerOffsetSamples = 9.0;

    timeline.BeginFrame(start + std::chrono::milliseconds(600));
    (void)ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(850));
    const ThroughputChartSample sample = ResolveThroughput(timeline, key, next, 2);
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
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    ThroughputChartSample second;
    second.samples = {10.0, 30.0, 70.0, 90.0};
    second.maxGraph = 100.0;
    second.timeMarkerOffsetSamples = 9.0;

    timeline.BeginFrame(start + std::chrono::milliseconds(600));
    (void)ResolveThroughput(timeline, key, second, 2);
    timeline.EndFrame();

    ThroughputChartSample third;
    third.samples = {30.0, 70.0, 90.0, 50.0};
    third.maxGraph = 100.0;
    third.timeMarkerOffsetSamples = 10.0;

    timeline.BeginFrame(start + std::chrono::milliseconds(850));
    ThroughputChartSample sample = ResolveThroughput(timeline, key, third, 3);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 0.5);
    EXPECT_EQ(sample.samples, (std::vector<double>{0.0, 10.0, 30.0, 70.0, 90.0}));

    timeline.BeginFrame(start + std::chrono::milliseconds(1100));
    sample = ResolveThroughput(timeline, key, third, 3);
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
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(500));
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    ThroughputChartSample next = first;
    next.timeMarkerOffsetSamples = 1.0;

    timeline.BeginFrame(start + std::chrono::milliseconds(600));
    (void)ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    timeline.BeginFrame(start + std::chrono::milliseconds(850));
    const ThroughputChartSample sample = ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.timeMarkerOffsetSamples, 20.0);
}

TEST(AnimationTimeline, UntouchedKeysAreDiscardedAfterAFrame) {
    DashboardAnimationTimeline timeline(std::chrono::milliseconds(500));
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.clock");

    timeline.BeginFrame(start);
    (void)ResolveScalar(timeline, key, ScalarTarget(1.0, 1.0));
    timeline.EndFrame();
    EXPECT_TRUE(timeline.HasActiveAnimations(start + std::chrono::milliseconds(100)));

    timeline.BeginFrame(start + std::chrono::milliseconds(100));
    timeline.EndFrame();

    EXPECT_FALSE(timeline.HasActiveAnimations(start + std::chrono::milliseconds(100)));
}
