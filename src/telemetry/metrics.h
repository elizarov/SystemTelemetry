#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "config/config.h"
#include "config/metric_catalog.h"
#include "telemetry/telemetry.h"

struct MetricValue {
    std::string label;
    std::string valueText;
    std::string sampleValueText;
    std::string unit;
    double ratio = 0.0;
    double peakRatio = 0.0;
};

struct ThroughputMetric {
    std::string label;
    std::string valueText;
    double valueMbps = 0.0;
    std::vector<double> history;
    double maxGraph = 10.0;
    double guideStepMbps = 5.0;
    double timeMarkerOffsetSamples = 0.0;
    double timeMarkerIntervalSamples = 20.0;
};

struct DriveRow {
    std::string label;
    double readActivity = 0.0;
    double writeActivity = 0.0;
    double usedPercent = 0.0;
    std::string usedText;
    std::string freeText;
};

bool IsStaticTextMetric(std::string_view metricRef);
std::optional<MetricDisplayStyle> FindMetricDisplayStyle(std::string_view metricRef);
ConfigMetricCatalog TelemetryMetricCatalog();
bool IsGenerallyAvailableMetric(std::string_view metricRef);
std::string ResolveMetricSampleValueText(const MetricsSectionConfig& metrics, const std::string& metricRef);

class MetricSource {
public:
    struct ThroughputCacheEntry {
        ThroughputMetric metric;
    };

    struct ThroughputSharedState {
        std::unordered_map<std::string, std::vector<double>> historyByMetricRef;
        double networkMaxGraph = 10.0;
        double storageMaxGraph = 10.0;
        double timeMarkerOffsetSamples = 0.0;
    };

    MetricSource(const SystemSnapshot& snapshot, const MetricsSectionConfig& metrics);

    const std::string& ResolveText(const std::string& metricRef) const;
    const MetricValue& ResolveMetric(const std::string& metricRef) const;
    const std::vector<MetricValue>& ResolveMetricList(const std::vector<std::string>& metricRefs) const;
    const ThroughputMetric& ResolveThroughput(const std::string& metricRef) const;
    const std::string& ResolveNetworkFooter() const;
    const std::vector<DriveRow>& ResolveDriveRows() const;
    const std::string& ResolveClockTime() const;
    const std::string& ResolveClockDate() const;

private:
    const SystemSnapshot& snapshot_;
    const MetricsSectionConfig& metrics_;
    mutable std::unordered_map<std::string, std::string> textCache_;
    mutable std::unordered_map<std::string, MetricValue> metricCache_;
    mutable std::unordered_map<std::string, std::vector<MetricValue>> metricListCache_;
    mutable std::unordered_map<std::string, ThroughputCacheEntry> throughputCache_;
    mutable std::optional<ThroughputSharedState> throughputSharedState_;
    mutable std::optional<std::string> networkFooterCache_;
    mutable std::optional<std::vector<DriveRow>> driveRowsCache_;
    mutable std::optional<std::string> clockTimeCache_;
    mutable std::optional<std::string> clockDateCache_;
};
