#include <gtest/gtest.h>

#include <limits>

#include "telemetry_retained_history.h"

TEST(TelemetryRetainedHistory, PushSampleCreatesSeriesAndKeepsRollingWindow) {
    RetainedHistoryStore store;
    SystemSnapshot snapshot;

    store.PushSample(snapshot, "cpu.load", 0.25);
    store.PushSample(snapshot, "cpu.load", 0.5);

    ASSERT_EQ(snapshot.retainedHistories.size(), 1u);
    ASSERT_EQ(snapshot.retainedHistories[0].seriesRef, "cpu.load");
    ASSERT_EQ(snapshot.retainedHistories[0].samples.size(), 60u);
    EXPECT_EQ(snapshot.retainedHistories[0].samples.back(), 0.5);
}

TEST(TelemetryRetainedHistory, PushBoardMetricSamplesUsesConfiguredScales) {
    RetainedHistoryStore store;
    SystemSnapshot snapshot;
    MetricScaleConfig scales;
    scales.boardTemperatureC = 100.0;
    scales.boardFanRpm = 2000.0;

    snapshot.boardTemperatures.push_back({"cpu", ScalarMetric{55.0, "C"}});
    snapshot.boardFans.push_back({"system", ScalarMetric{1200.0, "RPM"}});

    store.PushBoardMetricSamples(snapshot, scales);

    ASSERT_EQ(snapshot.retainedHistories.size(), 2u);
    EXPECT_EQ(snapshot.retainedHistories[0].seriesRef, "board.temp.cpu");
    EXPECT_EQ(snapshot.retainedHistories[0].samples.back(), 0.55);
    EXPECT_EQ(snapshot.retainedHistories[1].seriesRef, "board.fan.system");
    EXPECT_EQ(snapshot.retainedHistories[1].samples.back(), 0.6);
}

TEST(TelemetryRetainedHistory, PushSampleSanitizesNonFiniteValuesToZero) {
    RetainedHistoryStore store;
    SystemSnapshot snapshot;

    store.PushSample(snapshot, "network.upload", std::numeric_limits<double>::quiet_NaN());
    store.PushSample(snapshot, "network.upload", std::numeric_limits<double>::infinity());

    ASSERT_EQ(snapshot.retainedHistories.size(), 1u);
    EXPECT_DOUBLE_EQ(snapshot.retainedHistories[0].samples[snapshot.retainedHistories[0].samples.size() - 2], 0.0);
    EXPECT_DOUBLE_EQ(snapshot.retainedHistories[0].samples.back(), 0.0);
}
