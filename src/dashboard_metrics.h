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

struct DashboardMetricRow {
    std::string label;
    std::string valueText;
    double ratio = 0.0;
    double peakRatio = 0.0;
};

struct DashboardMetricListEntry {
    std::string metricRef;
    std::string labelOverride;
};

struct DashboardGaugeMetric {
    double percent = 0.0;
    double peakRatio = 0.0;
};

struct DashboardThroughputMetric {
    std::string label;
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
    std::string freeText;
};

class DashboardMetricSource {
public:
    DashboardMetricSource(const SystemSnapshot& snapshot, const MetricScaleConfig& metricScales);

    std::string ResolveText(const std::string& metricRef) const;
    DashboardGaugeMetric ResolveGauge(const std::string& metricRef) const;
    std::vector<DashboardMetricRow> ResolveMetricList(const std::vector<DashboardMetricListEntry>& metricRefs) const;
    DashboardThroughputMetric ResolveThroughput(const std::string& metricRef) const;
    std::string ResolveNetworkFooter() const;
    std::vector<DashboardDriveRow> ResolveDriveRows() const;
    std::string ResolveClockTime() const;
    std::string ResolveClockDate() const;

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
    const MetricScaleConfig& metricScales_;
    mutable std::unordered_map<std::string, std::string> textCache_;
    mutable std::unordered_map<std::string, DashboardGaugeMetric> gaugeCache_;
    mutable std::unordered_map<std::string, std::vector<DashboardMetricRow>> metricListCache_;
    mutable std::unordered_map<std::string, ThroughputCacheEntry> throughputCache_;
    mutable std::optional<ThroughputSharedState> throughputSharedState_;
    mutable std::optional<std::string> networkFooterCache_;
    mutable std::optional<std::vector<DashboardDriveRow>> driveRowsCache_;
    mutable std::optional<std::string> clockTimeCache_;
    mutable std::optional<std::string> clockDateCache_;
};
