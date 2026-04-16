#include <gtest/gtest.h>

#include <sstream>

#include "snapshot_dump.h"
#include "telemetry_retained_history.h"

TEST(SnapshotDump, RoundTripsScalarMetricUnitsThroughDumpText) {
    TelemetryDump dump;
    dump.snapshot.cpu.clock = ScalarMetric{4.75, ScalarMetricUnit::Gigahertz};
    dump.snapshot.gpu.temperature = ScalarMetric{68.0, ScalarMetricUnit::Celsius};
    dump.snapshot.gpu.clock = ScalarMetric{2450.0, ScalarMetricUnit::Megahertz};
    dump.snapshot.gpu.fan = ScalarMetric{1500.0, ScalarMetricUnit::Rpm};
    dump.snapshot.boardTemperatures.push_back({"cpu", ScalarMetric{55.0, ScalarMetricUnit::Celsius}});
    dump.snapshot.boardFans.push_back({"system", ScalarMetric{900.0, ScalarMetricUnit::Rpm}});

    std::ostringstream output;
    ASSERT_TRUE(WriteTelemetryDump(output, dump));

    const std::string text = output.str();
    EXPECT_NE(text.find("cpu.clock.unit=\"GHz\""), std::string::npos);
    EXPECT_NE(text.find("gpu.temperature.unit=\"C\""), std::string::npos);
    EXPECT_NE(text.find("gpu.clock.unit=\"MHz\""), std::string::npos);
    EXPECT_NE(text.find("gpu.fan.unit=\"RPM\""), std::string::npos);

    std::istringstream input(text);
    TelemetryDump loaded;
    std::string error;
    ASSERT_TRUE(LoadTelemetryDump(input, loaded, &error)) << error;
    EXPECT_EQ(loaded.snapshot.cpu.clock.unit, ScalarMetricUnit::Gigahertz);
    EXPECT_EQ(loaded.snapshot.gpu.temperature.unit, ScalarMetricUnit::Celsius);
    EXPECT_EQ(loaded.snapshot.gpu.clock.unit, ScalarMetricUnit::Megahertz);
    EXPECT_EQ(loaded.snapshot.gpu.fan.unit, ScalarMetricUnit::Rpm);
    ASSERT_EQ(loaded.snapshot.boardTemperatures.size(), 1u);
    ASSERT_EQ(loaded.snapshot.boardFans.size(), 1u);
    EXPECT_EQ(loaded.snapshot.boardTemperatures[0].metric.unit, ScalarMetricUnit::Celsius);
    EXPECT_EQ(loaded.snapshot.boardFans[0].metric.unit, ScalarMetricUnit::Rpm);
}

TEST(SnapshotDump, RejectsNonCanonicalScalarMetricUnitTokensOnLoad) {
    std::istringstream input("format=system_telemetry_snapshot_v8\n"
                             "cpu.name=\"CPU\"\n"
                             "cpu.load_percent=0\n"
                             "cpu.clock.value=null\n"
                             "cpu.clock.unit=\"GHz\"\n"
                             "cpu.memory.used_gb=0\n"
                             "cpu.memory.total_gb=0\n"
                             "board.temperatures.count=1\n"
                             "board.temperatures.0.name=\"cpu\"\n"
                             "board.temperatures.0.value=55\n"
                             "board.temperatures.0.unit=\"°C\"\n"
                             "board.fans.count=0\n"
                             "retained_histories.count=0\n"
                             "gpu.name=\"GPU\"\n"
                             "gpu.load_percent=0\n"
                             "gpu.temperature.value=60\n"
                             "gpu.temperature.unit=\"°C\"\n"
                             "gpu.clock.value=null\n"
                             "gpu.clock.unit=\"MHz\"\n"
                             "gpu.fan.value=null\n"
                             "gpu.fan.unit=\"RPM\"\n"
                             "gpu.vram.used_gb=0\n"
                             "gpu.vram.total_gb=0\n"
                             "network.adapter_name=\"Auto\"\n"
                             "network.upload_mbps=0\n"
                             "network.download_mbps=0\n"
                             "network.ip_address=\"N/A\"\n"
                             "storage.read_mbps=0\n"
                             "storage.write_mbps=0\n"
                             "drives.count=0\n"
                             "time.year=2026\n"
                             "time.month=1\n"
                             "time.day=2\n"
                             "time.hour=3\n"
                             "time.minute=4\n"
                             "time.second=5\n"
                             "time.milliseconds=6\n");

    TelemetryDump loaded;
    std::string error;
    EXPECT_FALSE(LoadTelemetryDump(input, loaded, &error));
    EXPECT_EQ(error, "Invalid scalar unit for key: board.temperatures.0.unit");
}

TEST(SnapshotDump, RoundTripsRawRetainedHistorySamples) {
    TelemetryDump dump;
    dump.snapshot.retainedHistories.push_back({"cpu.load", std::vector<double>{20.0, 91.0, 63.0}});
    dump.snapshot.retainedHistories.push_back({"board.temp.cpu", std::vector<double>{10.0, 55.0, 40.0}});
    RebuildRetainedHistoryIndex(dump.snapshot);

    std::ostringstream output;
    ASSERT_TRUE(WriteTelemetryDump(output, dump));

    std::istringstream input(output.str());
    TelemetryDump loaded;
    std::string error;
    ASSERT_TRUE(LoadTelemetryDump(input, loaded, &error)) << error;
    ASSERT_EQ(loaded.snapshot.retainedHistories.size(), 2u);
    EXPECT_EQ(loaded.snapshot.retainedHistories[0].samples, (std::vector<double>{20.0, 91.0, 63.0}));
    EXPECT_EQ(loaded.snapshot.retainedHistories[1].samples, (std::vector<double>{10.0, 55.0, 40.0}));
}

TEST(SnapshotDump, RejectsPreviousNormalizedHistoryFormatVersion) {
    std::istringstream input("format=system_telemetry_snapshot_v7\n");

    TelemetryDump loaded;
    std::string error;
    EXPECT_FALSE(LoadTelemetryDump(input, loaded, &error));
    EXPECT_EQ(error, "Unsupported or missing dump format.");
}
