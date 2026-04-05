#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <string>
#include <vector>

#include "telemetry.h"

struct DashboardMetricRow {
    std::string label;
    std::string valueText;
    double ratio = 0.0;
};

struct DashboardMetricListEntry {
    std::string metricRef;
    std::string labelOverride;
};

struct DashboardThroughputMetric {
    std::string label;
    double valueMbps = 0.0;
    std::vector<double> history;
    double maxGraph = 10.0;
    double guideStepMbps = 5.0;
};

struct DashboardDriveRow {
    std::string label;
    double usedPercent = 0.0;
    std::string freeText;
};

class DashboardMetricSource {
public:
    explicit DashboardMetricSource(const SystemSnapshot& snapshot);

    std::string ResolveText(const std::string& metricRef) const;
    double ResolveGaugePercent(const std::string& metricRef) const;
    std::vector<DashboardMetricRow> ResolveMetricList(const std::vector<DashboardMetricListEntry>& metricRefs) const;
    DashboardThroughputMetric ResolveThroughput(const std::string& metricRef) const;
    std::string ResolveNetworkFooter() const;
    std::vector<DashboardDriveRow> ResolveDriveRows(const std::vector<std::string>& drives) const;
    std::string ResolveClockTime() const;
    std::string ResolveClockDate() const;

private:
    const SystemSnapshot& snapshot_;
};
