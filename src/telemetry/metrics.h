#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "config/metric_catalog.h"
#include "telemetry/telemetry.h"

enum class MetricValueState {
    Unavailable,
    Available,
    PermissionRequired,
};

struct MetricValue {
    std::string label;
    std::string valueText;
    std::string annotationText;
    std::string sampleValueText;
    std::string unit;
    double ratio = 0.0;
    double peakRatio = 0.0;
    MetricValueState state = MetricValueState::Unavailable;
    bool warningAnnotation = false;
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
std::string FormatClockTime(const SYSTEMTIME& time, std::string_view format);
std::string FormatClockDate(const SYSTEMTIME& time, std::string_view format);

class MetricSource {
public:
    struct ThroughputSharedState {
        struct HistoryEntry {
            const char* metricRef = nullptr;
            std::vector<double> samples;
        };

        HistoryEntry histories[4];
        size_t historyCount = 0;
        double networkMaxGraph = 10.0;
        double storageMaxGraph = 10.0;
        double timeMarkerOffsetSamples = 0.0;
    };

    MetricSource(const SystemSnapshot& snapshot, const MetricsSectionConfig& metrics);

    const std::string& ResolveText(const std::string& metricRef) const;
    // Size: list widgets consume rows immediately, so these borrow fixed cache slots instead of row vectors.
    // Pointers can be invalidated by later lookups after the fixed slots are reused.
    const MetricValue* FindMetric(const std::string& metricRef) const;
    const MetricValue& ResolveMetric(const std::string& metricRef) const;
    const ThroughputMetric& ResolveThroughput(const std::string& metricRef) const;
    const std::string& ResolveNetworkFooter() const;
    const DriveRow* FindDriveRow(size_t rowIndex) const;
    const std::string& ResolveClockTime(std::string_view format) const;
    const std::string& ResolveClockDate(std::string_view format) const;

private:
    struct MetricCacheEntry {
        std::string key;
        MetricValue metric;
        bool resolved = false;
    };

    struct DriveRowCacheEntry {
        DriveRow row;
        size_t rowIndex = 0;
    };

    static constexpr size_t kMetricCacheCapacity = 16;
    static constexpr size_t kDriveRowCacheCapacity = 8;

    const MetricCacheEntry& CacheMetric(const std::string& metricRef) const;
    const DriveRowCacheEntry& CacheDriveRow(size_t rowIndex) const;
    void InitializeDriveRows() const;

    const SystemSnapshot& snapshot_;
    const MetricsSectionConfig& metrics_;
    mutable ThroughputSharedState throughputSharedState_;
    mutable std::string textCacheKey_;
    mutable std::string textCache_;
    mutable MetricCacheEntry metricCache_[kMetricCacheCapacity];
    mutable std::string throughputCacheKey_;
    mutable ThroughputMetric throughputCache_;
    mutable std::string networkFooterCache_;
    mutable DriveRowCacheEntry driveRowCache_[kDriveRowCacheCapacity];
    mutable std::string clockTimeCacheKey_;
    mutable std::string clockTimeCache_;
    mutable std::string clockDateCacheKey_;
    mutable std::string clockDateCache_;
    mutable const MetricDefinitionConfig* driveUsageDefinition_ = nullptr;
    mutable const MetricDefinitionConfig* driveFreeDefinition_ = nullptr;
    mutable double driveRowsTotalReadMbps_ = 0.0;
    mutable double driveRowsTotalWriteMbps_ = 0.0;
    mutable size_t metricCacheCount_ = 0;
    mutable size_t driveRowCacheCount_ = 0;
    mutable bool throughputSharedStateReady_ = false;
    mutable bool textCached_ = false;
    mutable bool throughputCached_ = false;
    mutable bool networkFooterCached_ = false;
    mutable bool driveRowsCached_ = false;
    mutable bool clockTimeCached_ = false;
    mutable bool clockDateCached_ = false;
};
