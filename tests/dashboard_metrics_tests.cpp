#include <gtest/gtest.h>

#include "dashboard_metrics.h"

namespace {

void RebuildRetainedHistoryIndex(SystemSnapshot& snapshot) {
    snapshot.retainedHistoryIndexByRef.clear();
    snapshot.retainedHistoryIndexByRef.reserve(snapshot.retainedHistories.size());
    for (size_t i = 0; i < snapshot.retainedHistories.size(); ++i) {
        snapshot.retainedHistoryIndexByRef[snapshot.retainedHistories[i].seriesRef] = i;
    }
}

MetricsSectionConfig BuildMetricsConfig() {
    MetricsSectionConfig metrics;
    metrics.definitions.push_back(
        MetricDefinitionConfig{"gpu.load", MetricDisplayStyle::Percent, true, 0.0, "%", "Load"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"gpu.temp", MetricDisplayStyle::Scalar, false, 100.0, "C", "Temp"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"gpu.clock", MetricDisplayStyle::Scalar, false, 3000.0, "MHz", "Clock"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"gpu.fan", MetricDisplayStyle::Scalar, false, 3000.0, "RPM", "Fan"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"cpu.load", MetricDisplayStyle::Percent, true, 0.0, "%", "Load"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"cpu.clock", MetricDisplayStyle::Scalar, false, 5.0, "GHz", "Clock"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"cpu.ram", MetricDisplayStyle::Memory, true, 0.0, "GB", "RAM"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"gpu.vram", MetricDisplayStyle::Memory, true, 0.0, "GB", "VRAM"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"board.temp.cpu", MetricDisplayStyle::Scalar, false, 100.0, "C", "Temp"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"board.fan.system", MetricDisplayStyle::Scalar, false, 3000.0, "RPM", "System Fan"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"network.upload", MetricDisplayStyle::Throughput, true, 0.0, "MB/s", "Up"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"network.download", MetricDisplayStyle::Throughput, true, 0.0, "MB/s", "Down"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"storage.read", MetricDisplayStyle::Throughput, true, 0.0, "MB/s", "Read"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"storage.write", MetricDisplayStyle::Throughput, true, 0.0, "MB/s", "Write"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"drive.usage", MetricDisplayStyle::Percent, false, 100.0, "%", "Usage"});
    metrics.definitions.push_back(
        MetricDefinitionConfig{"drive.free", MetricDisplayStyle::SizeAuto, true, 0.0, "GB|TB", "Free"});
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

TEST(DashboardMetrics, ResolvesTextMetricsAndStaticTextTraitsFromBindingRegistry) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.cpu.name = "Ryzen 7";
    snapshot.gpu.name = "Radeon";

    DashboardMetricSource source(snapshot, metrics);

    EXPECT_EQ(source.ResolveText("cpu.name"), "Ryzen 7");
    EXPECT_EQ(source.ResolveText("gpu.name"), "Radeon");
    EXPECT_EQ(source.ResolveText("unknown.metric"), "N/A");

    EXPECT_TRUE(IsStaticDashboardTextMetric("cpu.name"));
    EXPECT_TRUE(IsStaticDashboardTextMetric("gpu.name"));
    EXPECT_FALSE(IsStaticDashboardTextMetric("cpu.load"));
    EXPECT_FALSE(IsStaticDashboardTextMetric("board.temp.cpu"));
    EXPECT_FALSE(IsStaticDashboardTextMetric("unknown.metric"));
}

TEST(DashboardMetrics, KeepsDisplayOnlyDriveBindingsMetadataOnly) {
    MetricsSectionConfig metrics = BuildMetricsConfig();
    metrics.definitions.push_back(
        MetricDefinitionConfig{"drive.activity.read", MetricDisplayStyle::LabelOnly, true, 0.0, "", "R"});
    SystemSnapshot snapshot;

    DashboardMetricSource source(snapshot, metrics);

    ASSERT_EQ(FindDashboardMetricDisplayStyle("drive.activity.read"), MetricDisplayStyle::LabelOnly);
    ASSERT_EQ(FindDashboardMetricDisplayStyle("drive.usage"), MetricDisplayStyle::Percent);
    EXPECT_FALSE(IsStaticDashboardTextMetric("drive.activity.read"));
    EXPECT_EQ(ResolveMetricSampleValueText(metrics, "drive.activity.read"), "");
    EXPECT_EQ(ResolveMetricSampleValueText(metrics, "drive.usage"), "");

    const DashboardMetricValue& metric = source.ResolveMetric("drive.activity.read");
    EXPECT_TRUE(metric.label.empty());
    EXPECT_TRUE(metric.valueText.empty());
    EXPECT_DOUBLE_EQ(metric.ratio, 0.0);
    EXPECT_DOUBLE_EQ(metric.peakRatio, 0.0);
}

TEST(DashboardMetrics, MarksDriveMetricsAsSpecialAndNotGenerallyAvailable) {
    EXPECT_TRUE(IsGenerallyAvailableDashboardMetric("cpu.load"));
    EXPECT_TRUE(IsGenerallyAvailableDashboardMetric("gpu.vram"));
    EXPECT_TRUE(IsGenerallyAvailableDashboardMetric("board.temp.cpu"));
    EXPECT_FALSE(IsGenerallyAvailableDashboardMetric("drive.activity.read"));
    EXPECT_FALSE(IsGenerallyAvailableDashboardMetric("drive.usage"));
    EXPECT_FALSE(IsGenerallyAvailableDashboardMetric("drive.free"));
}

TEST(DashboardMetrics, ResolvesUnifiedMetricsForGaugeAndMetricList) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.cpu.loadPercent = 63.0;
    snapshot.cpu.clock = ScalarMetric{4.25, ScalarMetricUnit::Gigahertz};
    snapshot.cpu.memory = MemoryMetric{18.5, 32.0};
    snapshot.gpu.vram = MemoryMetric{8.4, 16.0};

    AddHistorySeries(snapshot, "cpu.load", {20.0, 91.0, 63.0});
    AddHistorySeries(snapshot, "cpu.ram", {3.2, 9.6, 18.5});
    AddHistorySeries(snapshot, "gpu.vram", {3.2, 8.32, 6.4});

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
    snapshot.boardTemperatures.push_back({"cpu", ScalarMetric{55.0, ScalarMetricUnit::Celsius}});
    AddHistorySeries(snapshot, "board.temp.cpu", {10.0, 55.0, 40.0});

    DashboardMetricSource source(snapshot, metrics);

    const DashboardMetricValue& metric = source.ResolveMetric("board.temp.cpu");
    EXPECT_EQ(metric.label, "Temp");
    EXPECT_EQ(metric.valueText, "55 C");
    EXPECT_DOUBLE_EQ(metric.ratio, 0.55);
    EXPECT_DOUBLE_EQ(metric.peakRatio, 0.55);
}

TEST(DashboardMetrics, ResolvesBoardFanMetricsThroughPrefixBinding) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.boardFans.push_back({"system", ScalarMetric{1200.0, ScalarMetricUnit::Rpm}});
    AddHistorySeries(snapshot, "board.fan.system", {500.0, 1200.0, 1100.0});

    DashboardMetricSource source(snapshot, metrics);

    const DashboardMetricValue& metric = source.ResolveMetric("board.fan.system");
    EXPECT_EQ(metric.label, "System Fan");
    EXPECT_EQ(metric.valueText, "1200 RPM");
    EXPECT_DOUBLE_EQ(metric.ratio, 0.4);
    EXPECT_DOUBLE_EQ(metric.peakRatio, 0.4);
}

TEST(DashboardMetrics, RenormalizesPeakGhostWhenMetricScaleChanges) {
    MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.boardTemperatures.push_back({"cpu", ScalarMetric{55.0, ScalarMetricUnit::Celsius}});
    AddHistorySeries(snapshot, "board.temp.cpu", {10.0, 55.0, 40.0});

    DashboardMetricSource defaultScaleSource(snapshot, metrics);
    EXPECT_DOUBLE_EQ(defaultScaleSource.ResolveMetric("board.temp.cpu").peakRatio, 0.55);

    MetricDefinitionConfig* definition = FindMetricDefinition(metrics, "board.temp.cpu");
    ASSERT_NE(definition, nullptr);
    definition->scale = 200.0;

    DashboardMetricSource updatedScaleSource(snapshot, metrics);
    EXPECT_DOUBLE_EQ(updatedScaleSource.ResolveMetric("board.temp.cpu").ratio, 0.275);
    EXPECT_DOUBLE_EQ(updatedScaleSource.ResolveMetric("board.temp.cpu").peakRatio, 0.275);
}

TEST(DashboardMetrics, ResolvesThroughputAndDriveTextFromConfiguredStyles) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.network.uploadMbps = 78.4;
    snapshot.now.wSecond = 12;
    snapshot.now.wMilliseconds = 0;
    DriveInfo drive;
    drive.label = "C:";
    drive.usedPercent = 42.0;
    drive.freeGb = 1500.0;
    drive.readMbps = 15.0;
    drive.writeMbps = 5.0;
    snapshot.drives.push_back(drive);

    AddHistorySeries(snapshot, "network.upload", {55.0, 70.0, 78.4});

    DashboardMetricSource source(snapshot, metrics);

    const DashboardThroughputMetric& throughput = source.ResolveThroughput("network.upload");
    EXPECT_EQ(throughput.label, "Up");
    EXPECT_EQ(throughput.valueText, "74.2 MB/s");

    const std::vector<DashboardDriveRow>& rows = source.ResolveDriveRows();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].usedText, "42%");
    EXPECT_EQ(rows[0].freeText, "1.5 TB");
}

TEST(DashboardMetrics, KeepsUnknownMetricFallbacksUnchanged) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;

    DashboardMetricSource source(snapshot, metrics);

    const DashboardMetricValue& metric = source.ResolveMetric("unknown.metric");
    EXPECT_TRUE(metric.label.empty());
    EXPECT_TRUE(metric.valueText.empty());
    EXPECT_DOUBLE_EQ(metric.ratio, 0.0);
    EXPECT_DOUBLE_EQ(metric.peakRatio, 0.0);

    const DashboardThroughputMetric& throughput = source.ResolveThroughput("unknown.metric");
    EXPECT_TRUE(throughput.label.empty());
    EXPECT_TRUE(throughput.valueText.empty());
    EXPECT_DOUBLE_EQ(throughput.valueMbps, 0.0);
    EXPECT_TRUE(throughput.history.empty());
    EXPECT_DOUBLE_EQ(throughput.maxGraph, 10.0);
    EXPECT_DOUBLE_EQ(throughput.guideStepMbps, 5.0);

    EXPECT_EQ(ResolveMetricSampleValueText(metrics, "cpu.name"), "");
    EXPECT_EQ(ResolveMetricSampleValueText(metrics, "unknown.metric"), "");
}
