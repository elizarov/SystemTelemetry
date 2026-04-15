#include "dashboard_metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string_view>
#include <utility>

#include "numeric_safety.h"

namespace {

std::string FormatScalarValue(std::optional<double> value, std::string_view unit, int precision) {
    if (!value.has_value() || !IsFiniteDouble(*value)) {
        return "N/A";
    }
    char buffer[64];
    if (unit.empty()) {
        sprintf_s(buffer, "%.*f", precision, *value);
    } else {
        sprintf_s(buffer, "%.*f %s", precision, *value, std::string(unit).c_str());
    }
    return buffer;
}

std::string FormatPercentValue(std::optional<double> value, int precision) {
    if (!value.has_value() || !IsFiniteDouble(*value)) {
        return "N/A";
    }
    char buffer[64];
    sprintf_s(buffer, "%.*f%%", precision, *value);
    return buffer;
}

std::string FormatMemoryValue(double usedGb, double totalGb, std::string_view unit) {
    if (!IsFiniteDouble(usedGb) || !IsFiniteDouble(totalGb) || totalGb <= 0.0) {
        return "N/A";
    }
    char buffer[64];
    if (unit.empty()) {
        sprintf_s(buffer, "%.1f / %.0f", usedGb, totalGb);
    } else {
        sprintf_s(buffer, "%.1f / %.0f %s", usedGb, totalGb, std::string(unit).c_str());
    }
    return buffer;
}

std::string FormatThroughputValue(double valueMbps, std::string_view unit) {
    if (!IsFiniteDouble(valueMbps) || valueMbps < 0.0) {
        return "N/A";
    }
    char buffer[64];
    if (unit.empty()) {
        sprintf_s(buffer, valueMbps >= 100.0 ? "%.0f" : "%.1f", valueMbps);
    } else {
        sprintf_s(buffer, valueMbps >= 100.0 ? "%.0f %s" : "%.1f %s", valueMbps, std::string(unit).c_str());
    }
    return buffer;
}

std::pair<std::string_view, std::string_view> SplitSizeUnits(std::string_view units) {
    const size_t separator = units.find('|');
    if (separator == std::string_view::npos) {
        return {units, units};
    }
    return {units.substr(0, separator), units.substr(separator + 1)};
}

std::string FormatSizeAutoValue(double valueGb, std::string_view units) {
    if (!IsFiniteDouble(valueGb) || valueGb < 0.0) {
        return "N/A";
    }
    const auto [smallUnit, largeUnit] = SplitSizeUnits(units);
    char buffer[64];
    if (valueGb >= 1024.0) {
        if (largeUnit.empty()) {
            sprintf_s(buffer, "%.1f", valueGb / 1024.0);
        } else {
            sprintf_s(buffer, "%.1f %s", valueGb / 1024.0, std::string(largeUnit).c_str());
        }
    } else if (smallUnit.empty()) {
        sprintf_s(buffer, "%.0f", valueGb);
    } else {
        sprintf_s(buffer, "%.0f %s", valueGb, std::string(smallUnit).c_str());
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

double ResolveMetricRatio(const MetricDefinitionConfig& definition, double value, double telemetryScale = 0.0) {
    const double scale = definition.telemetryScale ? telemetryScale : definition.scale;
    if (!IsFiniteDouble(value) || !IsFiniteDouble(scale) || scale <= 0.0) {
        return 0.0;
    }
    return ClampFinite(value / scale, 0.0, 1.0);
}

int ResolveScalarPrecision(const std::string& metricRef) {
    if (metricRef == "cpu.clock") {
        return 2;
    }
    return 0;
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

std::string FormatMetricValueText(const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    std::optional<double> primaryValue,
    std::optional<double> secondaryValue = std::nullopt) {
    switch (definition.style) {
        case MetricDisplayStyle::Percent:
            return FormatPercentValue(primaryValue, 0);
        case MetricDisplayStyle::Scalar:
            return FormatScalarValue(primaryValue, definition.unit, ResolveScalarPrecision(metricRef));
        case MetricDisplayStyle::Memory:
            return primaryValue.has_value() && secondaryValue.has_value()
                       ? FormatMemoryValue(*primaryValue, *secondaryValue, definition.unit)
                       : std::string("N/A");
        case MetricDisplayStyle::Throughput:
            return primaryValue.has_value() ? FormatThroughputValue(*primaryValue, definition.unit) : std::string("N/A");
        case MetricDisplayStyle::SizeAuto:
            return primaryValue.has_value() ? FormatSizeAutoValue(*primaryValue, definition.unit) : std::string("N/A");
        case MetricDisplayStyle::LabelOnly:
            return {};
    }
    return "N/A";
}

std::string BuildMetricSampleValueText(const MetricDefinitionConfig& definition, const std::string& metricRef) {
    switch (definition.style) {
        case MetricDisplayStyle::Percent:
            return FormatPercentValue(std::optional<double>{definition.telemetryScale ? 100.0 : definition.scale}, 0);
        case MetricDisplayStyle::Scalar:
            return FormatScalarValue(
                std::optional<double>{definition.telemetryScale ? 100.0 : definition.scale},
                definition.unit,
                ResolveScalarPrecision(metricRef));
        case MetricDisplayStyle::Memory:
            return FormatMemoryValue(999.9, 1000.0, definition.unit);
        case MetricDisplayStyle::Throughput:
            return FormatThroughputValue(999.9, definition.unit);
        case MetricDisplayStyle::SizeAuto:
            return FormatSizeAutoValue(2048.0, definition.unit);
        case MetricDisplayStyle::LabelOnly:
            return {};
    }
    return {};
}

DashboardMetricValue BuildResolvedMetric(const SystemSnapshot& snapshot,
    const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    std::string valueText,
    double ratio) {
    return DashboardMetricValue{definition.label,
        std::move(valueText),
        BuildMetricSampleValueText(definition, metricRef),
        definition.unit,
        ratio,
        ResolvePeakRatio(snapshot, metricRef, ratio)};
}

std::optional<DashboardMetricValue> ResolveBoardMetric(const std::vector<NamedScalarMetric>& metrics,
    const SystemSnapshot& snapshot,
    const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    std::string_view logicalName) {
    for (const auto& metric : metrics) {
        if (metric.name != logicalName) {
            continue;
        }
        const double numericValue = FiniteNonNegativeOr(metric.metric.value.value_or(0.0));
        const double ratio = ResolveMetricRatio(definition, numericValue);
        return BuildResolvedMetric(
            snapshot, definition, metricRef, FormatMetricValueText(definition, metricRef, metric.metric.value), ratio);
    }

    return BuildResolvedMetric(snapshot, definition, metricRef, "N/A", 0.0);
}

std::optional<DashboardMetricValue> ResolveMetricValue(
    const SystemSnapshot& snapshot, const MetricsSectionConfig& metrics, const std::string& metricRef) {
    const MetricDefinitionConfig* definition = FindMetricDefinition(metrics, metricRef);
    if (definition == nullptr) {
        return std::nullopt;
    }

    if (metricRef == "cpu.load") {
        const double percent = ClampFinite(snapshot.cpu.loadPercent, 0.0, 100.0);
        const double ratio = ResolveMetricRatio(*definition, percent, 100.0);
        return BuildResolvedMetric(
            snapshot, *definition, metricRef, FormatMetricValueText(*definition, metricRef, percent), ratio);
    }
    if (metricRef == "cpu.clock") {
        const double value = FiniteNonNegativeOr(snapshot.cpu.clock.value.value_or(0.0));
        const double ratio = ResolveMetricRatio(*definition, value);
        return BuildResolvedMetric(snapshot,
            *definition,
            metricRef,
            FormatMetricValueText(*definition, metricRef, snapshot.cpu.clock.value),
            ratio);
    }
    if (metricRef == "cpu.ram") {
        const double total = FiniteNonNegativeOr(snapshot.cpu.memory.totalGb);
        const double used = FiniteNonNegativeOr(snapshot.cpu.memory.usedGb);
        const double ratio = ResolveMetricRatio(*definition, used, total);
        return BuildResolvedMetric(snapshot,
            *definition,
            metricRef,
            FormatMetricValueText(*definition, metricRef, snapshot.cpu.memory.usedGb, snapshot.cpu.memory.totalGb),
            ratio);
    }
    if (metricRef == "gpu.load") {
        const double percent = ClampFinite(snapshot.gpu.loadPercent, 0.0, 100.0);
        const double ratio = ResolveMetricRatio(*definition, percent, 100.0);
        return BuildResolvedMetric(
            snapshot, *definition, metricRef, FormatMetricValueText(*definition, metricRef, percent), ratio);
    }
    if (metricRef == "gpu.temp") {
        const double value = FiniteNonNegativeOr(snapshot.gpu.temperature.value.value_or(0.0));
        const double ratio = ResolveMetricRatio(*definition, value);
        return BuildResolvedMetric(snapshot,
            *definition,
            metricRef,
            FormatMetricValueText(*definition, metricRef, snapshot.gpu.temperature.value),
            ratio);
    }
    if (metricRef == "gpu.clock") {
        const double value = FiniteNonNegativeOr(snapshot.gpu.clock.value.value_or(0.0));
        const double ratio = ResolveMetricRatio(*definition, value);
        return BuildResolvedMetric(snapshot,
            *definition,
            metricRef,
            FormatMetricValueText(*definition, metricRef, snapshot.gpu.clock.value),
            ratio);
    }
    if (metricRef == "gpu.fan") {
        const double value = FiniteNonNegativeOr(snapshot.gpu.fan.value.value_or(0.0));
        const double ratio = ResolveMetricRatio(*definition, value);
        return BuildResolvedMetric(snapshot,
            *definition,
            metricRef,
            FormatMetricValueText(*definition, metricRef, snapshot.gpu.fan.value),
            ratio);
    }
    if (metricRef == "gpu.vram") {
        const double total = FiniteNonNegativeOr(snapshot.gpu.vram.totalGb);
        const double used = FiniteNonNegativeOr(snapshot.gpu.vram.usedGb);
        const double ratio = ResolveMetricRatio(*definition, used, total);
        return BuildResolvedMetric(snapshot,
            *definition,
            metricRef,
            FormatMetricValueText(*definition, metricRef, snapshot.gpu.vram.usedGb, snapshot.gpu.vram.totalGb),
            ratio);
    }
    if (metricRef.rfind("board.temp.", 0) == 0) {
        return ResolveBoardMetric(snapshot.boardTemperatures,
            snapshot,
            *definition,
            metricRef,
            metricRef.substr(std::string("board.temp.").size()));
    }
    if (metricRef.rfind("board.fan.", 0) == 0) {
        return ResolveBoardMetric(
            snapshot.boardFans, snapshot, *definition, metricRef, metricRef.substr(std::string("board.fan.").size()));
    }
    return std::nullopt;
}

}  // namespace

std::string ResolveMetricSampleValueText(const MetricsSectionConfig& metrics, const std::string& metricRef) {
    const MetricDefinitionConfig* definition = FindMetricDefinition(metrics, metricRef);
    return definition != nullptr ? BuildMetricSampleValueText(*definition, metricRef) : std::string{};
}

DashboardMetricSource::DashboardMetricSource(const SystemSnapshot& snapshot, const MetricsSectionConfig& metrics)
    : snapshot_(snapshot), metrics_(metrics) {}

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

const DashboardMetricValue& DashboardMetricSource::ResolveMetric(const std::string& metricRef) const {
    const auto cached = metricCache_.find(metricRef);
    if (cached != metricCache_.end()) {
        return cached->second;
    }

    DashboardMetricValue metric;
    if (auto resolved = ResolveMetricValue(snapshot_, metrics_, metricRef); resolved.has_value()) {
        metric = std::move(*resolved);
    }
    return metricCache_.emplace(metricRef, std::move(metric)).first->second;
}

const std::vector<DashboardMetricValue>& DashboardMetricSource::ResolveMetricList(
    const std::vector<std::string>& metricRefs) const {
    std::ostringstream cacheKey;
    for (const auto& metricRef : metricRefs) {
        cacheKey << metricRef << '\n';
    }

    const std::string key = cacheKey.str();
    const auto cached = metricListCache_.find(key);
    if (cached != metricListCache_.end()) {
        return cached->second;
    }

    std::vector<DashboardMetricValue> rows;
    rows.reserve(metricRefs.size());
    for (const auto& metricRef : metricRefs) {
        if (auto row = ResolveMetricValue(snapshot_, metrics_, metricRef); row.has_value()) {
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

    const MetricDefinitionConfig* definition = FindMetricDefinition(metrics_, metricRef);
    DashboardThroughputMetric metric;
    if (metricRef == "network.upload") {
        metric.valueMbps =
            ResolveDisplayedThroughputValue(snapshot_.network.uploadMbps, throughputSharedState_->networkUploadHistory);
        metric.history = throughputSharedState_->networkUploadHistory;
        metric.maxGraph = throughputSharedState_->networkMaxGraph;
        metric.guideStepMbps = 5.0;
    } else if (metricRef == "network.download") {
        metric.valueMbps = ResolveDisplayedThroughputValue(
            snapshot_.network.downloadMbps, throughputSharedState_->networkDownloadHistory);
        metric.history = throughputSharedState_->networkDownloadHistory;
        metric.maxGraph = throughputSharedState_->networkMaxGraph;
        metric.guideStepMbps = 5.0;
    } else if (metricRef == "storage.read") {
        metric.valueMbps =
            ResolveDisplayedThroughputValue(snapshot_.storage.readMbps, throughputSharedState_->storageReadHistory);
        metric.history = throughputSharedState_->storageReadHistory;
        metric.maxGraph = throughputSharedState_->storageMaxGraph;
        metric.guideStepMbps = GetStorageGuideStep(throughputSharedState_->storageMaxGraph);
    } else if (metricRef == "storage.write") {
        metric.valueMbps =
            ResolveDisplayedThroughputValue(snapshot_.storage.writeMbps, throughputSharedState_->storageWriteHistory);
        metric.history = throughputSharedState_->storageWriteHistory;
        metric.maxGraph = throughputSharedState_->storageMaxGraph;
        metric.guideStepMbps = GetStorageGuideStep(throughputSharedState_->storageMaxGraph);
    }
    metric.timeMarkerOffsetSamples = throughputSharedState_->timeMarkerOffsetSamples;
    metric.timeMarkerIntervalSamples = 20.0;
    if (definition != nullptr) {
        metric.label = definition->label;
        metric.valueText = FormatMetricValueText(*definition, metricRef, metric.valueMbps);
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
        const MetricDefinitionConfig* usageDefinition = FindMetricDefinition(metrics_, "drive.usage");
        const MetricDefinitionConfig* freeDefinition = FindMetricDefinition(metrics_, "drive.free");

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
                usageDefinition != nullptr ? FormatMetricValueText(*usageDefinition, "drive.usage", drive.usedPercent)
                                           : std::string{},
                freeDefinition != nullptr ? FormatMetricValueText(*freeDefinition, "drive.free", drive.freeGb)
                                          : std::string{}});
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
