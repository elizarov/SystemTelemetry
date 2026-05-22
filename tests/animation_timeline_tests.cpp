#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "dashboard_renderer/impl/animation_timeline.h"
#include "telemetry/timing.h"
#include "widget/impl/animation_primitives.h"

namespace {

using Clock = DashboardAnimationTimeline::Clock;
constexpr auto kTimelineDuration = kTelemetryRefreshInterval;
constexpr auto kTimelineHalf = kTimelineDuration / 2;
constexpr auto kTimelineFifth = kTimelineDuration / 5;
constexpr auto kTimelineTwoFifths = kTimelineFifth * 2;

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
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.load");

    timeline.BeginFrame(start);
    ScalarFillSample sample = ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.0);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
    EXPECT_TRUE(timeline.HasActiveAnimations(start + kTimelineHalf));

    timeline.BeginFrame(start + kTimelineDuration);
    sample = ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.8);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
    EXPECT_FALSE(timeline.HasActiveAnimations(start + kTimelineDuration));
}

TEST(AnimationTimeline, InterruptedScalarUsesCurrentInterpolatedValueAsNewStart) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("gpu.vram");

    timeline.BeginFrame(start);
    (void)ResolveScalar(timeline, key, ScalarTarget(1.0, 1.0));
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineHalf);
    ScalarFillSample sample = ResolveScalar(timeline, key, ScalarTarget(0.2, 0.4), 2);
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.5);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 1.0);

    timeline.BeginFrame(start + kTimelineDuration);
    sample = ResolveScalar(timeline, key, ScalarTarget(0.2, 0.4), 2);
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.35);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.7);
}

TEST(AnimationTimeline, UnchangedScalarTargetKeepsProgressAcrossLayoutOnlyFrames) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.load");

    timeline.BeginFrame(start);
    (void)ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineHalf);
    const ScalarFillSample sample = ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.4);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
}

TEST(AnimationTimeline, ScaleOnlyRepackagedTargetKeepsStoredMetricTarget) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.load");
    WidgetAnimationStatePtr firstTarget = MakeScalarFillAnimationState(ScalarTarget(0.8, 0.9));
    WidgetAnimationStatePtr scaleRepackagedTarget = MakeScalarFillAnimationState(ScalarTarget(0.0, 0.0));

    timeline.BeginFrame(start);
    (void)timeline.Resolve(key, *firstTarget, 7);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineHalf);
    (void)timeline.Resolve(key, *scaleRepackagedTarget, 7);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration);
    WidgetAnimationStatePtr sampled = timeline.Resolve(key, *scaleRepackagedTarget, 7);
    const ScalarFillSample sample = ScalarFillSampleFromState(*sampled);
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.8);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
}

TEST(AnimationTimeline, LayoutOnlyRepackagedUnavailableTargetKeepsStoredMetricTarget) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("gpu.load");
    WidgetAnimationStatePtr firstTarget = MakeScalarFillAnimationState(ScalarTarget(0.8, 0.9));
    WidgetAnimationStatePtr layoutRepackagedTarget = MakeScalarFillAnimationState(ScalarFillSample{});

    timeline.BeginFrame(start);
    (void)timeline.Resolve(key, *firstTarget, 11);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineHalf);
    (void)timeline.Resolve(key, *layoutRepackagedTarget, 11);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration);
    WidgetAnimationStatePtr sampled = timeline.Resolve(key, *layoutRepackagedTarget, 11);
    const ScalarFillSample sample = ScalarFillSampleFromState(*sampled);
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.8);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
}

TEST(AnimationTimeline, UntouchedKeyCanSurviveLayoutOnlyFrame) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.load");

    timeline.BeginFrame(start);
    (void)ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineTwoFifths);
    timeline.EndFrame(DashboardAnimationTimeline::TrackRetention::KeepUntouched);

    timeline.BeginFrame(start + kTimelineHalf);
    const ScalarFillSample sample = ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.4);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
}

TEST(AnimationTimeline, CompletedUntouchedKeyCanSurviveLayoutOnlyFrame) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("gpu.load");

    timeline.BeginFrame(start);
    (void)ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration);
    (void)ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth);
    timeline.EndFrame(DashboardAnimationTimeline::TrackRetention::KeepUntouched);

    timeline.BeginFrame(start + kTimelineDuration + kTimelineHalf);
    const ScalarFillSample sample = ResolveScalar(timeline, key, ScalarTarget(0.8, 0.9));
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.8);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.9);
}

TEST(AnimationTimeline, UnavailableScalarAnimatesToZeroThenDisappears) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("gpu.fps");

    timeline.BeginFrame(start);
    (void)ResolveScalar(timeline, key, ScalarTarget(0.6, 0.75));
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration);
    (void)ResolveScalar(timeline, key, ScalarTarget(0.6, 0.75));
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth);
    ScalarFillSample sample = ResolveScalar(timeline, key, ScalarFillSample{}, 2);
    timeline.EndFrame();

    ASSERT_TRUE(sample.valueRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.valueRatio, 0.6);
    ASSERT_TRUE(sample.peakRatio.has_value());
    EXPECT_DOUBLE_EQ(*sample.peakRatio, 0.75);

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth + kTimelineDuration);
    sample = ResolveScalar(timeline, key, ScalarFillSample{}, 2);
    timeline.EndFrame();

    EXPECT_FALSE(sample.valueRatio.has_value());
    EXPECT_FALSE(sample.peakRatio.has_value());
}

TEST(AnimationTimeline, ThroughputVectorsAlignByNewestSample) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
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

    timeline.BeginFrame(start + kTimelineDuration);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    ThroughputChartSample next;
    next.samples = {5.0, 15.0, 25.0};
    next.maxGraph = 30.0;
    next.timeMarkerOffsetSamples = 4.0;
    next.guideStepMbps = 5.0;

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth);
    (void)ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth + kTimelineHalf);
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
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ThroughputKey("network.download");

    ThroughputChartSample first;
    first.samples = {0.0, 10.0, 30.0, 70.0};
    first.maxGraph = 100.0;
    first.timeMarkerOffsetSamples = 8.0;

    timeline.BeginFrame(start);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    ThroughputChartSample next;
    next.samples = {10.0, 30.0, 70.0, 90.0};
    next.maxGraph = 100.0;
    next.timeMarkerOffsetSamples = 9.0;

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth);
    (void)ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth + kTimelineHalf);
    const ThroughputChartSample sample = ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 0.5);
    EXPECT_EQ(sample.samples, (std::vector<double>{0.0, 10.0, 30.0, 70.0, 90.0}));
}

TEST(AnimationTimeline, ThroughputScrollTargetIncludesCompactTargetPhase) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ThroughputKey("network.download");

    ThroughputChartSample first;
    first.samples = {0.0, 10.0, 30.0, 70.0};
    first.liveLeaderMbps = 70.0;
    first.maxGraph = 100.0;
    first.timeMarkerOffsetSamples = 8.0;
    first.plotShiftSamples = 0.75;

    timeline.BeginFrame(start);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    ThroughputChartSample next;
    next.samples = {10.0, 30.0, 70.0, 90.0};
    next.liveLeaderMbps = 100.0;
    next.maxGraph = 100.0;
    next.timeMarkerOffsetSamples = 9.25;
    next.plotShiftSamples = 0.25;

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth);
    (void)ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth + kTimelineHalf);
    const ThroughputChartSample sample = ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 1.0);
    EXPECT_EQ(sample.samples, (std::vector<double>{0.0, 10.0, 30.0, 70.0, 90.0}));
}

TEST(AnimationTimeline, ThroughputInterpolatesPhaseShiftWithoutHistoryScroll) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ThroughputKey("network.upload");

    ThroughputChartSample first;
    first.samples = {10.0, 20.0, 30.0};
    first.liveLeaderMbps = 30.0;
    first.maxGraph = 100.0;
    first.timeMarkerOffsetSamples = 2.0;
    first.plotShiftSamples = 0.0;

    timeline.BeginFrame(start);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    ThroughputChartSample next = first;
    next.liveLeaderMbps = 40.0;
    next.timeMarkerOffsetSamples = 2.25;
    next.plotShiftSamples = 0.25;

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth);
    (void)ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth + kTimelineHalf);
    ThroughputChartSample sample = ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 0.125);
    EXPECT_DOUBLE_EQ(sample.liveLeaderMbps, 35.0);
    EXPECT_EQ(sample.samples, first.samples);

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth + kTimelineDuration);
    sample = ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 0.25);
    EXPECT_DOUBLE_EQ(sample.liveLeaderMbps, 40.0);
}

TEST(AnimationTimeline, InterruptedThroughputScrollContinuesFromCurrentPlotShift) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ThroughputKey("network.download");

    ThroughputChartSample first;
    first.samples = {0.0, 10.0, 30.0, 70.0};
    first.maxGraph = 100.0;
    first.timeMarkerOffsetSamples = 8.0;

    timeline.BeginFrame(start);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    ThroughputChartSample second;
    second.samples = {10.0, 30.0, 70.0, 90.0};
    second.maxGraph = 100.0;
    second.timeMarkerOffsetSamples = 9.0;

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth);
    (void)ResolveThroughput(timeline, key, second, 2);
    timeline.EndFrame();

    ThroughputChartSample third;
    third.samples = {30.0, 70.0, 90.0, 50.0};
    third.maxGraph = 100.0;
    third.timeMarkerOffsetSamples = 10.0;

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth + kTimelineHalf);
    ThroughputChartSample sample = ResolveThroughput(timeline, key, third, 3);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 0.5);
    EXPECT_EQ(sample.samples, (std::vector<double>{0.0, 10.0, 30.0, 70.0, 90.0}));

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth + kTimelineDuration);
    sample = ResolveThroughput(timeline, key, third, 3);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 1.25);
    EXPECT_EQ(sample.samples, (std::vector<double>{0.0, 10.0, 30.0, 70.0, 90.0, 50.0}));
}

TEST(AnimationTimeline, InterruptedThroughputCommitKeepsNextPhaseMovingForward) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ThroughputKey("network.upload");

    ThroughputChartSample first;
    first.samples = {0.0, 10.0, 30.0, 70.0};
    first.liveLeaderMbps = 70.0;
    first.maxGraph = 100.0;
    first.timeMarkerOffsetSamples = 8.75;
    first.plotShiftSamples = 0.75;

    timeline.BeginFrame(start);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    ThroughputChartSample committed;
    committed.samples = {10.0, 30.0, 70.0, 90.0};
    committed.liveLeaderMbps = 90.0;
    committed.maxGraph = 100.0;
    committed.timeMarkerOffsetSamples = 9.0;
    committed.plotShiftSamples = 0.0;

    const Clock::time_point commitStart = start + kTimelineDuration + kTimelineFifth;
    timeline.BeginFrame(commitStart);
    (void)ResolveThroughput(timeline, key, committed, 2);
    timeline.EndFrame();

    const Clock::time_point interruptedAt = commitStart + (kTimelineFifth * 4);
    timeline.BeginFrame(interruptedAt);
    ThroughputChartSample sample = ResolveThroughput(timeline, key, committed, 2);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 0.95);
    EXPECT_EQ(sample.samples, (std::vector<double>{0.0, 10.0, 30.0, 70.0, 90.0}));

    ThroughputChartSample nextPhase = committed;
    nextPhase.liveLeaderMbps = 95.0;
    nextPhase.timeMarkerOffsetSamples = 9.25;
    nextPhase.plotShiftSamples = 0.25;

    timeline.BeginFrame(interruptedAt);
    sample = ResolveThroughput(timeline, key, nextPhase, 3);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.plotShiftSamples, 0.95);
    EXPECT_EQ(sample.samples, (std::vector<double>{0.0, 10.0, 30.0, 70.0, 90.0}));

    timeline.BeginFrame(interruptedAt + kTimelineTwoFifths);
    sample = ResolveThroughput(timeline, key, nextPhase, 3);
    timeline.EndFrame();

    EXPECT_NEAR(sample.plotShiftSamples, 1.07, 0.000001);
    EXPECT_EQ(sample.samples, (std::vector<double>{0.0, 10.0, 30.0, 70.0, 90.0}));
}

TEST(AnimationTimeline, ThroughputTimeMarkerMovesForwardAcrossWrap) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ThroughputKey("storage.read");

    ThroughputChartSample first;
    first.samples = {1.0};
    first.maxGraph = 10.0;
    first.timeMarkerOffsetSamples = kThroughputTimeMarkerIntervalSamples - 1.0;

    timeline.BeginFrame(start);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration);
    (void)ResolveThroughput(timeline, key, first);
    timeline.EndFrame();

    ThroughputChartSample next = first;
    next.timeMarkerOffsetSamples = 1.0;

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth);
    (void)ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    timeline.BeginFrame(start + kTimelineDuration + kTimelineFifth + kTimelineHalf);
    const ThroughputChartSample sample = ResolveThroughput(timeline, key, next, 2);
    timeline.EndFrame();

    EXPECT_DOUBLE_EQ(sample.timeMarkerOffsetSamples, kThroughputTimeMarkerIntervalSamples);
}

TEST(AnimationTimeline, UntouchedKeysAreDiscardedAfterAFrame) {
    DashboardAnimationTimeline timeline(kTimelineDuration);
    const Clock::time_point start = Clock::time_point{};
    const AnimationDataKey key = ScalarKey("cpu.clock");

    timeline.BeginFrame(start);
    (void)ResolveScalar(timeline, key, ScalarTarget(1.0, 1.0));
    timeline.EndFrame();
    EXPECT_TRUE(timeline.HasActiveAnimations(start + kTimelineFifth));

    timeline.BeginFrame(start + kTimelineFifth);
    timeline.EndFrame();

    EXPECT_FALSE(timeline.HasActiveAnimations(start + kTimelineFifth));
}
