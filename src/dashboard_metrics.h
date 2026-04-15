#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "telemetry.h"

struct DashboardMetricValue {
    std::string label;
    std::string valueText;
    std::string sampleValueText;
    std::string unit;
    double ratio = 0.0;
    double peakRatio = 0.0;
};

struct DashboardThroughputMetric {
    std::string label;
    std::string valueText;
    double valueMbps = 0.0;
    std::vector<double> history;
    double maxGraph = 10.0;
    double guideStepMbps = 5.0;
    double timeMarkerOffsetSamples = 0.0;
    double timeMarkerIntervalSamples = 20.0;
};

struct DashboardDriveRow {
    std::string label;
    double readActivity = 0.0;
    double writeActivity = 0.0;
    double usedPercent = 0.0;
    std::string usedText;
    std::string freeText;
};

std::string ResolveMetricSampleValueText(const MetricsSectionConfig& metrics, const std::string& metricRef);

class DashboardMetricSource {
public:
    DashboardMetricSource(const SystemSnapshot& snapshot, const MetricsSectionConfig& metrics);

    const std::string& ResolveText(const std::string& metricRef) const;
    const DashboardMetricValue& ResolveMetric(const std::string& metricRef) const;
    const std::vector<DashboardMetricValue>& ResolveMetricList(const std::vector<std::string>& metricRefs) const;
    const DashboardThroughputMetric& ResolveThroughput(const std::string& metricRef) const;
    const std::string& ResolveNetworkFooter() const;
    const std::vector<DashboardDriveRow>& ResolveDriveRows() const;
    const std::string& ResolveClockTime() const;
    const std::string& ResolveClockDate() const;

private:
    struct ThroughputCacheEntry {
        DashboardThroughputMetric metric;
    };

    struct ThroughputSharedState {
        std::vector<double> networkUploadHistory;
        std::vector<double> networkDownloadHistory;
        std::vector<double> storageReadHistory;
        std::vector<double> storageWriteHistory;
        double networkMaxGraph = 10.0;
        double storageMaxGraph = 10.0;
        double timeMarkerOffsetSamples = 0.0;
    };

    const SystemSnapshot& snapshot_;
    const MetricsSectionConfig& metrics_;
    mutable std::unordered_map<std::string, std::string> textCache_;
    mutable std::unordered_map<std::string, DashboardMetricValue> metricCache_;
    mutable std::unordered_map<std::string, std::vector<DashboardMetricValue>> metricListCache_;
    mutable std::unordered_map<std::string, ThroughputCacheEntry> throughputCache_;
    mutable std::optional<ThroughputSharedState> throughputSharedState_;
    mutable std::optional<std::string> networkFooterCache_;
    mutable std::optional<std::vector<DashboardDriveRow>> driveRowsCache_;
    mutable std::optional<std::string> clockTimeCache_;
    mutable std::optional<std::string> clockDateCache_;
};
