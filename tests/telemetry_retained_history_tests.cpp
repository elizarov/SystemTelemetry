#include <gtest/gtest.h>

#include <limits>

#include "telemetry/impl/retained_history.h"

TEST(TelemetryRetainedHistory, PushSampleCreatesSeriesAndKeepsRollingWindow) {
    RetainedHistoryStore store;
    SystemSnapshot snapshot;

    store.PushSample(snapshot, "cpu.load", 25.0);
    store.PushSample(snapshot, "cpu.load", 50.0);

    ASSERT_EQ(snapshot.retainedHistories.size(), 1u);
    ASSERT_EQ(snapshot.retainedHistories[0].seriesRef, "cpu.load");
    ASSERT_EQ(snapshot.retainedHistories[0].samples.size(), 60u);
    EXPECT_EQ(snapshot.retainedHistories[0].samples.back(), 50.0);
}

TEST(TelemetryRetainedHistory, PushBoardMetricSamplesStoreRawValues) {
    RetainedHistoryStore store;
    SystemSnapshot snapshot;

    snapshot.boardTemperatures.push_back({"cpu", ScalarMetric{55.0, ScalarMetricUnit::Celsius}});
    snapshot.boardFans.push_back({"system", ScalarMetric{1200.0, ScalarMetricUnit::Rpm}});

    store.PushBoardMetricSamples(snapshot);

    ASSERT_EQ(snapshot.retainedHistories.size(), 2u);
    EXPECT_EQ(snapshot.retainedHistories[0].seriesRef, "board.temp.cpu");
    EXPECT_EQ(snapshot.retainedHistories[0].samples.back(), 55.0);
    EXPECT_EQ(snapshot.retainedHistories[1].seriesRef, "board.fan.system");
    EXPECT_EQ(snapshot.retainedHistories[1].samples.back(), 1200.0);
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
