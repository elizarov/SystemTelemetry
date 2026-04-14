#include "dashboard_metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "numeric_safety.h"

namespace {

std::string FormatScalarValue(const ScalarMetric& metric, int precision) {
    if (!metric.value.has_value() || !IsFiniteDouble(*metric.value)) {
        return "N/A";
    }
    char buffer[64];
    sprintf_s(buffer, "%.*f %s", precision, *metric.value, metric.unit.c_str());
    return buffer;
}

std::string FormatMemory(double usedGb, double totalGb) {
    if (!IsFiniteDouble(usedGb) || !IsFiniteDouble(totalGb) || totalGb <= 0.0) {
        return "N/A";
    }
    char buffer[64];
    sprintf_s(buffer, "%.1f / %.0f GB", usedGb, totalGb);
    return buffer;
}

std::string FormatDriveFree(double freeGb) {
    if (!IsFiniteDouble(freeGb) || freeGb < 0.0) {
        return "N/A";
    }
    char buffer[64];
    if (freeGb >= 1024.0) {
        sprintf_s(buffer, "%.1f TB", freeGb / 1024.0);
    } else {
        sprintf_s(buffer, "%.0f GB", freeGb);
    }
    return buffer;
}

std::vector<double> SmoothThroughputHistory(const std::vector<double>& history) {
    if (history.empty()) {
        return {};
    }

    std::vector<double> smoothed;
    smoothed.reserve(history.size());
    smoothed.push_back(FiniteNonNegativeOr(history.front()));
    for (size_t i = 1; i < history.size(); ++i) {
        smoothed.push_back((FiniteNonNegativeOr(history[i - 1]) + FiniteNonNegativeOr(history[i])) / 2.0);
    }
    return smoothed;
}

double ResolveDisplayedThroughputValue(double fallbackValue, const std::vector<double>& smoothedHistory) {
    if (!smoothedHistory.empty()) {
        return FiniteNonNegativeOr(smoothedHistory.back());
    }
    return FiniteNonNegativeOr(fallbackValue);
}

double GetThroughputGraphMax(const std::vector<double>& firstHistory, const std::vector<double>& secondHistory) {
    double rawMax = 10.0;
    for (double value : firstHistory) {
        rawMax = std::max(rawMax, FiniteNonNegativeOr(value));
    }
    for (double value : secondHistory) {
        rawMax = std::max(rawMax, FiniteNonNegativeOr(value));
    }
    const double roundingStep = rawMax > 100.0 ? 50.0 : 5.0;
    return std::max(10.0, std::ceil(rawMax / roundingStep) * roundingStep);
}

double GetStorageGuideStep(double maxGraph) {
    return maxGraph > 50.0 ? 50.0 : 5.0;
}

double GetTimeMarkerOffsetSamples(const SYSTEMTIME& now) {
    const double secondsIntoTenSecondWindow =
        std::fmod(static_cast<double>(now.wSecond) + (static_cast<double>(now.wMilliseconds) / 1000.0), 10.0);
    return secondsIntoTenSecondWindow / 0.5;
}

std::string BuildBoardMetricLabel(const std::string& name, const char* suffix) {
    if (name.empty()) {
        return suffix;
    }
    return name + " " + suffix;
}

double ResolveScaleRatio(double value, double scale) {
    if (!IsFiniteDouble(value) || !IsFiniteDouble(scale) || scale <= 0.0) {
        return 0.0;
    }
    return value / scale;
}

const std::vector<double>* FindRetainedHistory(const SystemSnapshot& snapshot, const std::string& seriesRef) {
    const auto indexIt = snapshot.retainedHistoryIndexByRef.find(seriesRef);
    if (indexIt == snapshot.retainedHistoryIndexByRef.end() || indexIt->second >= snapshot.retainedHistories.size()) {
        return nullptr;
    }
    return &snapshot.retainedHistories[indexIt->second].samples;
}

double ResolvePeakRatio(const SystemSnapshot& snapshot, const std::string& metricRef, double fallbackRatio) {
    const auto* history = FindRetainedHistory(snapshot, metricRef);
    if (history == nullptr || history->empty()) {
        return ClampFinite(fallbackRatio, 0.0, 1.0);
    }
    double peak = 0.0;
    for (double value : *history) {
        peak = std::max(peak, FiniteNonNegativeOr(value));
    }
    return ClampFinite(peak, 0.0, 1.0);
}

std::vector<double> ResolveRetainedHistorySamples(const SystemSnapshot& snapshot, const std::string& seriesRef) {
    const auto* history = FindRetainedHistory(snapshot, seriesRef);
    return history != nullptr ? *history : std::vector<double>{};
}

std::optional<DashboardMetricRow> ResolveNamedBoardMetric(const std::vector<NamedScalarMetric>& metrics,
    const SystemSnapshot& snapshot,
    const std::string& metricHistoryRef,
    const std::string& name,
    const char* suffix,
    double scale) {
    for (const auto& metric : metrics) {
        if (metric.name != name) {
            continue;
        }
        const double ratio = ResolveScaleRatio(metric.metric.value.value_or(0.0), scale);
        return DashboardMetricRow{BuildBoardMetricLabel(metric.name, suffix),
            FormatScalarValue(metric.metric, 0),
            ratio,
            ResolvePeakRatio(snapshot, metricHistoryRef, ratio)};
    }

    ScalarMetric unavailable{std::nullopt, std::string(suffix) == "Temp" ? "°C" : "RPM"};
    return DashboardMetricRow{BuildBoardMetricLabel(name, suffix),
        FormatScalarValue(unavailable, 0),
        0.0,
        ResolvePeakRatio(snapshot, metricHistoryRef, 0.0)};
}

std::optional<DashboardMetricRow> ResolveMetricRow(
    const SystemSnapshot& snapshot, const MetricScaleConfig& metricScales, const std::string& metricRef) {
    if (metricRef == "cpu.clock") {
        const double ratio = ResolveScaleRatio(snapshot.cpu.clock.value.value_or(0.0), metricScales.cpuClockGHz);
        return DashboardMetricRow{
            "Clock", FormatScalarValue(snapshot.cpu.clock, 2), ratio, ResolvePeakRatio(snapshot, metricRef, ratio)};
    }
    if (metricRef == "cpu.ram") {
        const double rawTotal = snapshot.cpu.memory.totalGb;
        const double rawUsed = snapshot.cpu.memory.usedGb;
        const double total = FiniteNonNegativeOr(rawTotal);
        const double used = FiniteNonNegativeOr(rawUsed);
        const double ratio = total > 0.0 ? used / total : 0.0;
        return DashboardMetricRow{
            "RAM", FormatMemory(rawUsed, rawTotal), ratio, ResolvePeakRatio(snapshot, metricRef, ratio)};
    }
    if (metricRef == "gpu.temp") {
        const double ratio =
            ResolveScaleRatio(snapshot.gpu.temperature.value.value_or(0.0), metricScales.gpuTemperatureC);
        return DashboardMetricRow{"Temp",
            FormatScalarValue(snapshot.gpu.temperature, 0),
            ratio,
            ResolvePeakRatio(snapshot, metricRef, ratio)};
    }
    if (metricRef == "gpu.clock") {
        const double ratio = ResolveScaleRatio(snapshot.gpu.clock.value.value_or(0.0), metricScales.gpuClockMHz);
        return DashboardMetricRow{
            "Clock", FormatScalarValue(snapshot.gpu.clock, 0), ratio, ResolvePeakRatio(snapshot, metricRef, ratio)};
    }
    if (metricRef == "gpu.fan") {
        const double ratio = ResolveScaleRatio(snapshot.gpu.fan.value.value_or(0.0), metricScales.gpuFanRpm);
        return DashboardMetricRow{
            "Fan", FormatScalarValue(snapshot.gpu.fan, 0), ratio, ResolvePeakRatio(snapshot, metricRef, ratio)};
    }
    if (metricRef == "gpu.vram") {
        const double rawTotalCapacity = snapshot.gpu.vram.totalGb;
        const double rawUsed = snapshot.gpu.vram.usedGb;
        const double totalCapacity = FiniteNonNegativeOr(rawTotalCapacity);
        const double used = FiniteNonNegativeOr(rawUsed);
        const double ratio = totalCapacity > 0.0 ? used / totalCapacity : 0.0;
        return DashboardMetricRow{
            "VRAM", FormatMemory(rawUsed, rawTotalCapacity), ratio, ResolvePeakRatio(snapshot, metricRef, ratio)};
    }
    if (metricRef.rfind("board.temp.", 0) == 0) {
        return ResolveNamedBoardMetric(snapshot.boardTemperatures,
            snapshot,
            metricRef,
            metricRef.substr(std::string("board.temp.").size()),
            "Temp",
            metricScales.boardTemperatureC);
    }
    if (metricRef.rfind("board.fan.", 0) == 0) {
        return ResolveNamedBoardMetric(snapshot.boardFans,
            snapshot,
            metricRef,
            metricRef.substr(std::string("board.fan.").size()),
            "Fan",
            metricScales.boardFanRpm);
    }
    return std::nullopt;
}

void ApplyMetricLabelOverride(DashboardMetricRow& row, const std::string& labelOverride) {
    if (!labelOverride.empty()) {
        row.label = labelOverride;
    }
}

}  // namespace

DashboardMetricSource::DashboardMetricSource(const SystemSnapshot& snapshot, const MetricScaleConfig& metricScales)
    : snapshot_(snapshot), metricScales_(metricScales) {}

const std::string& DashboardMetricSource::ResolveText(const std::string& metricRef) const {
    const auto cached = textCache_.find(metricRef);
    if (cached != textCache_.end()) {
        return cached->second;
    }

    std::string resolved = "N/A";
    if (metricRef == "cpu.name") {
        resolved = snapshot_.cpu.name;
    } else if (metricRef == "gpu.name") {
        resolved = snapshot_.gpu.name;
    }
    return textCache_.emplace(metricRef, std::move(resolved)).first->second;
}

const DashboardGaugeMetric& DashboardMetricSource::ResolveGauge(const std::string& metricRef) const {
    const auto cached = gaugeCache_.find(metricRef);
    if (cached != gaugeCache_.end()) {
        return cached->second;
    }

    DashboardGaugeMetric metric;
    if (metricRef == "cpu.load") {
        const double percent = ClampFinite(snapshot_.cpu.loadPercent, 0.0, 100.0);
        metric = DashboardGaugeMetric{percent, ResolvePeakRatio(snapshot_, "cpu.load", percent / 100.0)};
    } else if (metricRef == "gpu.load") {
        const double percent = ClampFinite(snapshot_.gpu.loadPercent, 0.0, 100.0);
        metric = DashboardGaugeMetric{percent, ResolvePeakRatio(snapshot_, "gpu.load", percent / 100.0)};
    }
    return gaugeCache_.emplace(metricRef, metric).first->second;
}

const std::vector<DashboardMetricRow>& DashboardMetricSource::ResolveMetricList(
    const std::vector<DashboardMetricListEntry>& metricRefs) const {
    std::ostringstream cacheKey;
    for (const auto& metricRef : metricRefs) {
        cacheKey << metricRef.metricRef << "=" << metricRef.labelOverride << '\n';
    }

    const std::string key = cacheKey.str();
    const auto cached = metricListCache_.find(key);
    if (cached != metricListCache_.end()) {
        return cached->second;
    }

    std::vector<DashboardMetricRow> rows;
    rows.reserve(metricRefs.size());
    for (const auto& metricRef : metricRefs) {
        if (auto row = ResolveMetricRow(snapshot_, metricScales_, metricRef.metricRef); row.has_value()) {
            ApplyMetricLabelOverride(*row, metricRef.labelOverride);
            rows.push_back(*row);
        }
    }
    return metricListCache_.emplace(key, std::move(rows)).first->second;
}

const DashboardThroughputMetric& DashboardMetricSource::ResolveThroughput(const std::string& metricRef) const {
    const auto cached = throughputCache_.find(metricRef);
    if (cached != throughputCache_.end()) {
        return cached->second.metric;
    }

    if (!throughputSharedState_.has_value()) {
        throughputSharedState_ = ThroughputSharedState{};
        throughputSharedState_->networkUploadHistory =
            SmoothThroughputHistory(ResolveRetainedHistorySamples(snapshot_, "network.upload"));
        throughputSharedState_->networkDownloadHistory =
            SmoothThroughputHistory(ResolveRetainedHistorySamples(snapshot_, "network.download"));
        throughputSharedState_->storageReadHistory =
            SmoothThroughputHistory(ResolveRetainedHistorySamples(snapshot_, "storage.read"));
        throughputSharedState_->storageWriteHistory =
            SmoothThroughputHistory(ResolveRetainedHistorySamples(snapshot_, "storage.write"));
        throughputSharedState_->networkMaxGraph = GetThroughputGraphMax(
            throughputSharedState_->networkUploadHistory, throughputSharedState_->networkDownloadHistory);
        throughputSharedState_->storageMaxGraph = GetThroughputGraphMax(
            throughputSharedState_->storageReadHistory, throughputSharedState_->storageWriteHistory);
        throughputSharedState_->timeMarkerOffsetSamples = GetTimeMarkerOffsetSamples(snapshot_.now);
    }

    DashboardThroughputMetric metric;
    if (metricRef == "network.upload") {
        metric = DashboardThroughputMetric{"Up",
            ResolveDisplayedThroughputValue(snapshot_.network.uploadMbps, throughputSharedState_->networkUploadHistory),
            throughputSharedState_->networkUploadHistory,
            throughputSharedState_->networkMaxGraph,
            5.0,
            throughputSharedState_->timeMarkerOffsetSamples,
            20.0};
    } else if (metricRef == "network.download") {
        metric = DashboardThroughputMetric{"Down",
            ResolveDisplayedThroughputValue(
                snapshot_.network.downloadMbps, throughputSharedState_->networkDownloadHistory),
            throughputSharedState_->networkDownloadHistory,
            throughputSharedState_->networkMaxGraph,
            5.0,
            throughputSharedState_->timeMarkerOffsetSamples,
            20.0};
    } else if (metricRef == "storage.read") {
        metric = DashboardThroughputMetric{"Read",
            ResolveDisplayedThroughputValue(snapshot_.storage.readMbps, throughputSharedState_->storageReadHistory),
            throughputSharedState_->storageReadHistory,
            throughputSharedState_->storageMaxGraph,
            GetStorageGuideStep(throughputSharedState_->storageMaxGraph),
            throughputSharedState_->timeMarkerOffsetSamples,
            20.0};
    } else if (metricRef == "storage.write") {
        metric = DashboardThroughputMetric{"Write",
            ResolveDisplayedThroughputValue(snapshot_.storage.writeMbps, throughputSharedState_->storageWriteHistory),
            throughputSharedState_->storageWriteHistory,
            throughputSharedState_->storageMaxGraph,
            GetStorageGuideStep(throughputSharedState_->storageMaxGraph),
            throughputSharedState_->timeMarkerOffsetSamples,
            20.0};
    }
    return throughputCache_.emplace(metricRef, ThroughputCacheEntry{metric}).first->second.metric;
}

const std::string& DashboardMetricSource::ResolveNetworkFooter() const {
    if (!networkFooterCache_.has_value()) {
        if (snapshot_.network.adapterName.empty()) {
            networkFooterCache_ = snapshot_.network.ipAddress;
        } else {
            networkFooterCache_ = snapshot_.network.adapterName + " | " + snapshot_.network.ipAddress;
        }
    }
    return *networkFooterCache_;
}

const std::vector<DashboardDriveRow>& DashboardMetricSource::ResolveDriveRows() const {
    if (!driveRowsCache_.has_value()) {
        driveRowsCache_ = std::vector<DashboardDriveRow>{};
        driveRowsCache_->reserve(snapshot_.drives.size());
        double totalReadMbps = 0.0;
        double totalWriteMbps = 0.0;
        for (const auto& drive : snapshot_.drives) {
            totalReadMbps += FiniteNonNegativeOr(drive.readMbps);
            totalWriteMbps += FiniteNonNegativeOr(drive.writeMbps);
        }
        for (const auto& drive : snapshot_.drives) {
            const double readActivity =
                totalReadMbps > 0.0 ? ClampFinite(FiniteNonNegativeOr(drive.readMbps) / totalReadMbps, 0.0, 1.0) : 0.0;
            const double writeActivity =
                totalWriteMbps > 0.0 ? ClampFinite(FiniteNonNegativeOr(drive.writeMbps) / totalWriteMbps, 0.0, 1.0)
                                     : 0.0;
            driveRowsCache_->push_back(DashboardDriveRow{drive.label,
                readActivity,
                writeActivity,
                ClampFinite(drive.usedPercent, 0.0, 100.0),
                FormatDriveFree(drive.freeGb)});
        }
    }
    return *driveRowsCache_;
}

const std::string& DashboardMetricSource::ResolveClockTime() const {
    if (!clockTimeCache_.has_value()) {
        char buffer[32];
        sprintf_s(buffer, "%02d:%02d", snapshot_.now.wHour, snapshot_.now.wMinute);
        clockTimeCache_ = buffer;
    }
    return *clockTimeCache_;
}

const std::string& DashboardMetricSource::ResolveClockDate() const {
    if (!clockDateCache_.has_value()) {
        char buffer[32];
        sprintf_s(buffer, "%04d-%02d-%02d", snapshot_.now.wYear, snapshot_.now.wMonth, snapshot_.now.wDay);
        clockDateCache_ = buffer;
    }
    return *clockDateCache_;
}
