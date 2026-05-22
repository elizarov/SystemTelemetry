#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <vector>

#include "config/config_telemetry.h"
#include "telemetry/metrics.h"

namespace {

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
        MetricDefinitionConfig{"gpu.fps", MetricDisplayStyle::Scalar, false, 240.0, "FPS", "FPS"});
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
}

void AddThroughputHistorySeries(
    SystemSnapshot& snapshot,
    const std::string& metricRef,
    const std::vector<double>& samples,
    std::initializer_list<double> liveSamples = {},
    uint8_t bucketSampleCount = 0) {
    RetainedHistorySeries series;
    series.seriesRef = metricRef;
    series.samples = samples;
    series.throughputLiveSamples.assign(liveSamples.begin(), liveSamples.end());
    series.throughputBucketSampleCount = bucketSampleCount;
    snapshot.retainedHistories.push_back(std::move(series));
}

void AddThroughputHistorySeries(
    SystemSnapshot& snapshot,
    const std::string& metricRef,
    std::initializer_list<double> samples,
    std::initializer_list<double> liveSamples = {},
    uint8_t bucketSampleCount = 0) {
    AddThroughputHistorySeries(
        snapshot, metricRef, std::vector<double>(samples.begin(), samples.end()), liveSamples, bucketSampleCount);
}

}  // namespace

TEST(Metrics, ResolvesTextMetricsAndStaticTextTraitsFromBindingRegistry) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.cpu.name = "Ryzen 7";
    snapshot.gpu.name = "Radeon";

    MetricSource source(snapshot, metrics);

    EXPECT_EQ(source.ResolveText("cpu.name"), "Ryzen 7");
    EXPECT_EQ(source.ResolveText("gpu.name"), "Radeon");
    EXPECT_EQ(source.ResolveText("unknown.metric"), "N/A");

    EXPECT_TRUE(IsStaticTextMetric("cpu.name"));
    EXPECT_TRUE(IsStaticTextMetric("gpu.name"));
    EXPECT_FALSE(IsStaticTextMetric("cpu.load"));
    EXPECT_FALSE(IsStaticTextMetric("board.temp.cpu"));
    EXPECT_FALSE(IsStaticTextMetric("unknown.metric"));
}

TEST(Metrics, KeepsDisplayOnlyDriveBindingsMetadataOnly) {
    MetricsSectionConfig metrics = BuildMetricsConfig();
    metrics.definitions.push_back(
        MetricDefinitionConfig{"drive.activity.read", MetricDisplayStyle::LabelOnly, true, 0.0, "", "R"});
    SystemSnapshot snapshot;

    MetricSource source(snapshot, metrics);

    ASSERT_EQ(FindMetricDisplayStyle("drive.activity.read"), MetricDisplayStyle::LabelOnly);
    ASSERT_EQ(FindMetricDisplayStyle("drive.usage"), MetricDisplayStyle::Percent);
    EXPECT_FALSE(IsStaticTextMetric("drive.activity.read"));
    EXPECT_EQ(ResolveMetricSampleValueText(metrics, "drive.activity.read"), "");
    EXPECT_EQ(ResolveMetricSampleValueText(metrics, "drive.usage"), "");

    const MetricValue& metric = source.ResolveMetric("drive.activity.read");
    EXPECT_TRUE(metric.label.empty());
    EXPECT_TRUE(metric.valueText.empty());
    EXPECT_DOUBLE_EQ(metric.ratio, 0.0);
    EXPECT_DOUBLE_EQ(metric.peakRatio, 0.0);
}

TEST(Metrics, MarksDriveMetricsAsSpecialAndNotGenerallyAvailable) {
    EXPECT_TRUE(IsGenerallyAvailableMetric("nothing"));
    EXPECT_TRUE(IsGenerallyAvailableMetric("cpu.load"));
    EXPECT_TRUE(IsGenerallyAvailableMetric("gpu.vram"));
    EXPECT_TRUE(IsGenerallyAvailableMetric("board.temp.cpu"));
    EXPECT_FALSE(IsGenerallyAvailableMetric("drive.activity.read"));
    EXPECT_FALSE(IsGenerallyAvailableMetric("drive.usage"));
    EXPECT_FALSE(IsGenerallyAvailableMetric("drive.free"));
}

TEST(Metrics, ResolvesNothingPlaceholderMetricAsUnavailableValue) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;

    MetricSource source(snapshot, metrics);

    const MetricValue& metric = source.ResolveMetric("nothing");
    EXPECT_EQ(metric.label, "Nothing");
    EXPECT_EQ(metric.valueText, "N/A");
    EXPECT_EQ(metric.state, MetricValueState::Unavailable);
    EXPECT_DOUBLE_EQ(metric.ratio, 0.0);
    EXPECT_DOUBLE_EQ(metric.peakRatio, 0.0);
}

TEST(Metrics, ResolvesUnifiedMetricsForGaugeAndMetricList) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.cpu.loadPercent = 63.0;
    snapshot.cpu.clock = ScalarMetric{4.25, ScalarMetricUnit::Gigahertz};
    snapshot.cpu.memory = MemoryMetric{18.5, 32.0};
    snapshot.gpu.fps = ScalarMetric{144.0, ScalarMetricUnit::Fps};
    snapshot.gpu.fpsAppName = "dota";
    snapshot.gpu.vram = MemoryMetric{8.4, 16.0};

    AddHistorySeries(snapshot, "cpu.load", {20.0, 91.0, 63.0});
    AddHistorySeries(snapshot, "cpu.ram", {3.2, 9.6, 18.5});
    AddHistorySeries(snapshot, "gpu.fps", {72.0, 144.0, 120.0});
    AddHistorySeries(snapshot, "gpu.vram", {3.2, 8.32, 6.4});

    MetricSource source(snapshot, metrics);

    const MetricValue& load = source.ResolveMetric("cpu.load");
    EXPECT_EQ(load.label, "Load");
    EXPECT_EQ(load.valueText, "63%");
    EXPECT_EQ(load.sampleValueText, "100%");
    EXPECT_DOUBLE_EQ(load.ratio, 0.63);
    EXPECT_DOUBLE_EQ(load.peakRatio, 0.91);

    const MetricValue& ram = source.ResolveMetric("cpu.ram");
    EXPECT_EQ(ram.label, "RAM");
    EXPECT_EQ(ram.valueText, "18.5 / 32 GB");
    EXPECT_DOUBLE_EQ(ram.ratio, 18.5 / 32.0);

    const MetricValue& vram = source.ResolveMetric("gpu.vram");
    EXPECT_EQ(vram.label, "VRAM");
    EXPECT_EQ(vram.valueText, "8.4 / 16 GB");
    EXPECT_DOUBLE_EQ(vram.ratio, 8.4 / 16.0);

    const MetricValue& fps = source.ResolveMetric("gpu.fps");
    EXPECT_EQ(fps.label, "FPS");
    EXPECT_EQ(fps.valueText, "144 FPS");
    EXPECT_EQ(fps.annotationText, "dota");
    EXPECT_FALSE(fps.warningAnnotation);
    EXPECT_EQ(fps.state, MetricValueState::Available);
    EXPECT_DOUBLE_EQ(fps.ratio, 0.6);
    EXPECT_DOUBLE_EQ(fps.peakRatio, 0.6);

    const MetricValue* metricListLoad = source.FindMetric("cpu.load");
    ASSERT_NE(metricListLoad, nullptr);
    EXPECT_EQ(metricListLoad->label, "Load");
    const MetricValue* metricListVram = source.FindMetric("gpu.vram");
    ASSERT_NE(metricListVram, nullptr);
    EXPECT_EQ(metricListVram->label, "VRAM");
    const MetricValue* metricListFps = source.FindMetric("gpu.fps");
    ASSERT_NE(metricListFps, nullptr);
    EXPECT_EQ(metricListFps->label, "FPS");
}

TEST(Metrics, ResolvesGpuFpsPermissionIssueAsAdminIndicator) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.gpu.fps = ScalarMetric{std::nullopt, ScalarMetricUnit::Fps, ScalarMetricIssue::PermissionRequired};
    snapshot.gpu.fpsAppName = "ignored";
    AddHistorySeries(snapshot, "gpu.fps", {72.0, 144.0, 120.0});

    MetricSource source(snapshot, metrics);

    const MetricValue& fps = source.ResolveMetric("gpu.fps");
    EXPECT_EQ(fps.label, "FPS");
    EXPECT_EQ(fps.valueText, "!admin");
    EXPECT_TRUE(fps.annotationText.empty());
    EXPECT_FALSE(fps.warningAnnotation);
    EXPECT_EQ(fps.state, MetricValueState::PermissionRequired);
    EXPECT_DOUBLE_EQ(fps.ratio, 0.0);
}

TEST(Metrics, ResolvesNativeFpsFallbackPermissionIssueAsWarningAnnotation) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.gpu.fps = ScalarMetric{90.0, ScalarMetricUnit::Fps, ScalarMetricIssue::PermissionRequired};
    snapshot.gpu.fpsAppName = "ignored";

    MetricSource source(snapshot, metrics);

    const MetricValue& fps = source.ResolveMetric("gpu.fps");
    EXPECT_EQ(fps.label, "FPS");
    EXPECT_EQ(fps.valueText, "90 FPS");
    EXPECT_EQ(fps.annotationText, "!admin");
    EXPECT_TRUE(fps.warningAnnotation);
    EXPECT_EQ(fps.state, MetricValueState::Available);
    EXPECT_DOUBLE_EQ(fps.ratio, 0.375);
}

TEST(Metrics, ResolvesBoardMetricUsingConfiguredLabelAndUnit) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.boardTemperatures.push_back({"cpu", ScalarMetric{55.0, ScalarMetricUnit::Celsius}});
    AddHistorySeries(snapshot, "board.temp.cpu", {10.0, 55.0, 40.0});

    MetricSource source(snapshot, metrics);

    const MetricValue& metric = source.ResolveMetric("board.temp.cpu");
    EXPECT_EQ(metric.label, "Temp");
    EXPECT_EQ(metric.valueText, "55 C");
    EXPECT_DOUBLE_EQ(metric.ratio, 0.55);
    EXPECT_DOUBLE_EQ(metric.peakRatio, 0.55);
}

TEST(Metrics, ResolvesBoardFanMetricsThroughPrefixBinding) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.boardFans.push_back({"system", ScalarMetric{1200.0, ScalarMetricUnit::Rpm}});
    AddHistorySeries(snapshot, "board.fan.system", {500.0, 1200.0, 1100.0});

    MetricSource source(snapshot, metrics);

    const MetricValue& metric = source.ResolveMetric("board.fan.system");
    EXPECT_EQ(metric.label, "System Fan");
    EXPECT_EQ(metric.valueText, "1200 RPM");
    EXPECT_DOUBLE_EQ(metric.ratio, 0.4);
    EXPECT_DOUBLE_EQ(metric.peakRatio, 0.4);
}

TEST(Metrics, RenormalizesPeakGhostWhenMetricScaleChanges) {
    MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.boardTemperatures.push_back({"cpu", ScalarMetric{55.0, ScalarMetricUnit::Celsius}});
    AddHistorySeries(snapshot, "board.temp.cpu", {10.0, 55.0, 40.0});

    MetricSource defaultScaleSource(snapshot, metrics);
    EXPECT_DOUBLE_EQ(defaultScaleSource.ResolveMetric("board.temp.cpu").peakRatio, 0.55);

    MetricDefinitionConfig* definition = FindMetricDefinition(metrics, "board.temp.cpu");
    ASSERT_NE(definition, nullptr);
    definition->scale = 200.0;

    MetricSource updatedScaleSource(snapshot, metrics);
    EXPECT_DOUBLE_EQ(updatedScaleSource.ResolveMetric("board.temp.cpu").ratio, 0.275);
    EXPECT_DOUBLE_EQ(updatedScaleSource.ResolveMetric("board.temp.cpu").peakRatio, 0.275);
}

TEST(Metrics, ResolvesThroughputAndDriveTextFromConfiguredStyles) {
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

    AddThroughputHistorySeries(snapshot, "network.upload", {55.0, 70.0, 78.4, 93.4}, {55.0, 70.0, 78.4, 93.4});

    MetricSource source(snapshot, metrics);

    const ThroughputMetric& throughput = source.ResolveThroughput("network.upload");
    EXPECT_EQ(throughput.label, "Up");
    EXPECT_EQ(throughput.valueText, "74.2 MB/s");
    EXPECT_DOUBLE_EQ(throughput.liveLeaderMbps, 74.2);

    const DriveRow* row = source.FindDriveRow(0);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->usedText, "42%");
    EXPECT_EQ(row->freeText, "1.5 TB");
    EXPECT_EQ(source.FindDriveRow(1), nullptr);
}

TEST(Metrics, ScalesThroughputGraphsFromCompactHistoryInsteadOfCurrentSamples) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    snapshot.network.uploadMbps = 1000.0;
    snapshot.network.downloadMbps = 900.0;
    snapshot.storage.readMbps = 700.0;
    snapshot.storage.writeMbps = 600.0;

    AddThroughputHistorySeries(snapshot, "network.upload", {5.0, 5.0, 5.0});
    AddThroughputHistorySeries(snapshot, "network.download", {4.0, 4.0, 4.0});
    AddThroughputHistorySeries(snapshot, "storage.read", {3.0, 3.0, 3.0});
    AddThroughputHistorySeries(snapshot, "storage.write", {2.0, 2.0, 2.0});

    MetricSource source(snapshot, metrics);

    EXPECT_DOUBLE_EQ(source.ResolveThroughput("network.upload").maxGraph, 10.0);
    EXPECT_DOUBLE_EQ(source.ResolveThroughput("network.download").maxGraph, 10.0);
    EXPECT_DOUBLE_EQ(source.ResolveThroughput("storage.read").maxGraph, 10.0);
    EXPECT_DOUBLE_EQ(source.ResolveThroughput("storage.write").maxGraph, 10.0);

    SystemSnapshot spikingSnapshot;
    AddThroughputHistorySeries(spikingSnapshot, "network.upload", {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 1000.0});
    AddThroughputHistorySeries(spikingSnapshot, "network.download", {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0});

    MetricSource spikingSource(spikingSnapshot, metrics);

    EXPECT_DOUBLE_EQ(spikingSource.ResolveThroughput("network.upload").valueMbps, 250.0);
    EXPECT_DOUBLE_EQ(spikingSource.ResolveThroughput("network.upload").maxGraph, 250.0);
}

TEST(Metrics, KeepsSingleThroughputHistorySampleSoGraphsStartImmediately) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;
    AddThroughputHistorySeries(snapshot, "network.upload", {20.0});
    AddThroughputHistorySeries(snapshot, "network.download", {0.0});

    MetricSource source(snapshot, metrics);
    const ThroughputMetric& upload = source.ResolveThroughput("network.upload");

    ASSERT_EQ(upload.history.size(), 1u);
    EXPECT_DOUBLE_EQ(upload.history.front(), 20.0);
    EXPECT_DOUBLE_EQ(upload.maxGraph, 20.0);
}

TEST(Metrics, KeepsThroughputGraphScaleStableAsAlternatingSpikeHistoryScrolls) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    constexpr double kSpikeMbps = 20.0;
    constexpr double kAveragedSpikeMbps = kSpikeMbps / 2.0;
    const std::vector<double> compactSpikeHistory(kRetainedThroughputHistorySamples, kAveragedSpikeMbps);
    const std::vector<double> compactZeroHistory(kRetainedThroughputHistorySamples, 0.0);

    for (int scrollStep = 0; scrollStep < 4; ++scrollStep) {
        SystemSnapshot snapshot;
        AddThroughputHistorySeries(
            snapshot,
            "network.upload",
            compactSpikeHistory,
            {kSpikeMbps, 0.0, kSpikeMbps, 0.0},
            static_cast<uint8_t>(scrollStep));
        AddThroughputHistorySeries(snapshot, "network.download", compactZeroHistory);

        MetricSource source(snapshot, metrics);
        const ThroughputMetric& upload = source.ResolveThroughput("network.upload");

        EXPECT_DOUBLE_EQ(upload.history.front(), kAveragedSpikeMbps) << "scroll step " << scrollStep;
        EXPECT_DOUBLE_EQ(upload.maxGraph, kAveragedSpikeMbps) << "scroll step " << scrollStep;
        EXPECT_DOUBLE_EQ(
            upload.plotShiftSamples,
            static_cast<double>(scrollStep) / static_cast<double>(kThroughputHistorySmoothingSamples))
            << "scroll step " << scrollStep;
    }
}

TEST(Metrics, UsesCoarseThroughputGuideStepsForLargeNetworkAndStorageGraphs) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;

    AddThroughputHistorySeries(snapshot, "network.upload", {0.0, 200.0, 350.0, 650.0});
    AddThroughputHistorySeries(snapshot, "network.download", {0.0, 0.0, 0.0, 0.0});
    AddThroughputHistorySeries(snapshot, "storage.read", {0.0, 200.0, 350.0, 650.0});
    AddThroughputHistorySeries(snapshot, "storage.write", {0.0, 0.0, 0.0, 0.0});

    MetricSource source(snapshot, metrics);

    EXPECT_DOUBLE_EQ(source.ResolveThroughput("network.upload").maxGraph, 650.0);
    EXPECT_DOUBLE_EQ(source.ResolveThroughput("network.upload").guideStepMbps, 50.0);
    EXPECT_DOUBLE_EQ(source.ResolveThroughput("storage.read").maxGraph, 650.0);
    EXPECT_DOUBLE_EQ(source.ResolveThroughput("storage.read").guideStepMbps, 50.0);
}

TEST(Metrics, FormatsClockTimeAndDateFromConfiguredTokens) {
    SYSTEMTIME time{};
    time.wYear = 2026;
    time.wMonth = 4;
    time.wDay = 28;
    time.wDayOfWeek = 2;
    time.wHour = 13;
    time.wMinute = 5;
    time.wSecond = 9;

    EXPECT_EQ(FormatClockTime(time, "HH:MM"), "13:05");
    EXPECT_EQ(FormatClockTime(time, "H:MM:SS"), "13:05:09");
    EXPECT_EQ(FormatClockTime(time, "hh:MM AM"), "01:05 PM");
    EXPECT_EQ(FormatClockTime(time, "h:M:S am"), "1:5:9 pm");

    EXPECT_EQ(FormatClockDate(time, "YYYY-MM-DD"), "2026-04-28");
    EXPECT_EQ(FormatClockDate(time, "DD MMM YYYY"), "28 Apr 2026");
    EXPECT_EQ(FormatClockDate(time, "dddd, MMMM D"), "Tuesday, April 28");
}

TEST(Metrics, KeepsUnknownMetricFallbacksUnchanged) {
    const MetricsSectionConfig metrics = BuildMetricsConfig();
    SystemSnapshot snapshot;

    MetricSource source(snapshot, metrics);

    const MetricValue& metric = source.ResolveMetric("unknown.metric");
    EXPECT_TRUE(metric.label.empty());
    EXPECT_TRUE(metric.valueText.empty());
    EXPECT_DOUBLE_EQ(metric.ratio, 0.0);
    EXPECT_DOUBLE_EQ(metric.peakRatio, 0.0);
    EXPECT_EQ(source.FindMetric("unknown.metric"), nullptr);

    const ThroughputMetric& throughput = source.ResolveThroughput("unknown.metric");
    EXPECT_TRUE(throughput.label.empty());
    EXPECT_TRUE(throughput.valueText.empty());
    EXPECT_DOUBLE_EQ(throughput.valueMbps, 0.0);
    EXPECT_TRUE(throughput.history.empty());
    EXPECT_DOUBLE_EQ(throughput.maxGraph, 10.0);
    EXPECT_DOUBLE_EQ(throughput.guideStepMbps, 5.0);

    EXPECT_EQ(ResolveMetricSampleValueText(metrics, "cpu.name"), "");
    EXPECT_EQ(ResolveMetricSampleValueText(metrics, "unknown.metric"), "");
}
