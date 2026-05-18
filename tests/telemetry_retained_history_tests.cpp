#include <gtest/gtest.h>
#include <limits>
#include <vector>

#include "telemetry/impl/retained_history.h"
#include "telemetry/timing.h"

TEST(TelemetryRetainedHistory, PushSampleCreatesSeriesAndKeepsRollingWindow) {
    RetainedHistoryStore store;
    SystemSnapshot snapshot;

    store.PushSample(snapshot, RetainedHistoryKey::CpuLoad, 25.0);
    store.PushSample(snapshot, RetainedHistoryKey::CpuLoad, 50.0);

    ASSERT_EQ(snapshot.retainedHistories.size(), 1u);
    ASSERT_EQ(snapshot.retainedHistories[0].seriesRef, "cpu.load");
    ASSERT_EQ(snapshot.retainedHistories[0].samples.size(), kRetainedScalarHistorySamples);
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

    store.PushSample(snapshot, RetainedHistoryKey::NetworkUpload, std::numeric_limits<double>::quiet_NaN());
    store.PushSample(snapshot, RetainedHistoryKey::NetworkUpload, std::numeric_limits<double>::infinity());

    ASSERT_EQ(snapshot.retainedHistories.size(), 1u);
    EXPECT_EQ(snapshot.retainedHistories[0].samples.size(), kRetainedThroughputHistorySamples);
    EXPECT_DOUBLE_EQ(snapshot.retainedHistories[0].samples[snapshot.retainedHistories[0].samples.size() - 2], 0.0);
    EXPECT_DOUBLE_EQ(snapshot.retainedHistories[0].samples.back(), 0.0);
    EXPECT_EQ(snapshot.retainedHistories[0].throughputLiveSamples.size(), kThroughputHistorySmoothingSamples);
    EXPECT_DOUBLE_EQ(snapshot.retainedHistories[0].throughputLiveSamples[2], 0.0);
    EXPECT_DOUBLE_EQ(snapshot.retainedHistories[0].throughputLiveSamples[3], 0.0);
}

TEST(TelemetryRetainedHistory, ThroughputSamplesCompactIntoOneHertzAveragesAndLiveWindow) {
    RetainedHistoryStore store;
    SystemSnapshot snapshot;

    store.PushSample(snapshot, RetainedHistoryKey::NetworkUpload, 4.0);
    store.PushSample(snapshot, RetainedHistoryKey::NetworkUpload, 8.0);
    store.PushSample(snapshot, RetainedHistoryKey::NetworkUpload, 12.0);

    ASSERT_EQ(snapshot.retainedHistories.size(), 1u);
    RetainedHistorySeries& history = snapshot.retainedHistories[0];
    EXPECT_EQ(history.samples.size(), kRetainedThroughputHistorySamples);
    EXPECT_DOUBLE_EQ(history.samples.back(), 0.0);
    EXPECT_EQ(history.throughputBucketSampleCount, 3u);
    EXPECT_DOUBLE_EQ(history.throughputBucketTotal, 24.0);
    EXPECT_EQ(history.throughputLiveSamples, (std::vector<double>{0.0, 4.0, 8.0, 12.0}));

    store.PushSample(snapshot, RetainedHistoryKey::NetworkUpload, 16.0);

    EXPECT_DOUBLE_EQ(history.samples.back(), 10.0);
    EXPECT_EQ(history.throughputBucketSampleCount, 0u);
    EXPECT_DOUBLE_EQ(history.throughputBucketTotal, 0.0);
    EXPECT_EQ(history.throughputLiveSamples, (std::vector<double>{4.0, 8.0, 12.0, 16.0}));
}
