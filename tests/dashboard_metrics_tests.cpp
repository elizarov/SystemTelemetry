#include <gtest/gtest.h>

#include "dashboard_metrics.h"
#include "telemetry_retained_history.h"

namespace {

MetricsSectionConfig BuildMetricsConfig() {
    MetricsSectionConfig metrics;
    metrics.definitions.push_back(MetricDefinitionConfig{"cpu.load", true, 0.0, "%", "Load"});
    metrics.definitions.push_back(MetricDefinitionConfig{"cpu.clock", false, 5.0, "GHz", "Clock"});
    metrics.definitions.push_back(MetricDefinitionConfig{"cpu.ram", true, 0.0, "GB", "RAM"});
    metrics.definitions.push_back(MetricDefinitionConfig{"gpu.vram", true, 0.0, "GB", "VRAM"});
    metrics.definitions.push_back(MetricDefinitionConfig{"board.temp.cpu", false, 100.0, "C", "Temp"});
    return metrics;
}

void AddHistorySeries(SystemSnapshot& snapshot, const std::string& metricRef, std::initializer_list<double> samples) {
    RetainedHistorySeries series;
    series.seriesRef = metricRef;
    series.samples.assign(samples.begin(), samples.end());
    snapshot.retainedHistories.push_back(std::move(series));
    RebuildRetainedHistoryIndex(snapshot);
}

}  // namespace

TEST(DashboardMetrics, ResolvesUnifiedMetricsForGaugeAndMetricList) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.cpu.loadPercent = 63.0;
    snapshot.cpu.clock = ScalarMetric{4.25, "GHz"};
    snapshot.cpu.memory = MemoryMetric{18.5, 32.0};
    snapshot.gpu.vram = MemoryMetric{8.4, 16.0};

    AddHistorySeries(snapshot, "cpu.load", {0.20, 0.91, 0.63});
    AddHistorySeries(snapshot, "cpu.ram", {0.10, 0.30, 0.58});
    AddHistorySeries(snapshot, "gpu.vram", {0.20, 0.52, 0.40});

    DashboardMetricSource source(snapshot, metrics);

    const DashboardMetricValue& load = source.ResolveMetric("cpu.load");
    EXPECT_EQ(load.label, "Load");
    EXPECT_EQ(load.valueText, "63%");
    EXPECT_EQ(load.sampleValueText, "100%");
    EXPECT_DOUBLE_EQ(load.ratio, 0.63);
    EXPECT_DOUBLE_EQ(load.peakRatio, 0.91);

    const DashboardMetricValue& ram = source.ResolveMetric("cpu.ram");
    EXPECT_EQ(ram.label, "RAM");
    EXPECT_EQ(ram.valueText, "18.5 / 32 GB");
    EXPECT_DOUBLE_EQ(ram.ratio, 18.5 / 32.0);

    const DashboardMetricValue& vram = source.ResolveMetric("gpu.vram");
    EXPECT_EQ(vram.label, "VRAM");
    EXPECT_EQ(vram.valueText, "8.4 / 16 GB");
    EXPECT_DOUBLE_EQ(vram.ratio, 8.4 / 16.0);

    const std::vector<DashboardMetricValue>& metricList = source.ResolveMetricList({"cpu.load", "gpu.vram"});
    ASSERT_EQ(metricList.size(), 2u);
    EXPECT_EQ(metricList[0].label, "Load");
    EXPECT_EQ(metricList[1].label, "VRAM");
}

TEST(DashboardMetrics, ResolvesBoardMetricUsingConfiguredLabelAndUnit) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.boardTemperatures.push_back({"cpu", ScalarMetric{55.0, "ignored"}});
    AddHistorySeries(snapshot, "board.temp.cpu", {0.10, 0.55, 0.40});

    DashboardMetricSource source(snapshot, metrics);

    const DashboardMetricValue& metric = source.ResolveMetric("board.temp.cpu");
    EXPECT_EQ(metric.label, "Temp");
    EXPECT_EQ(metric.valueText, "55 C");
    EXPECT_DOUBLE_EQ(metric.ratio, 0.55);
    EXPECT_DOUBLE_EQ(metric.peakRatio, 0.55);
}
