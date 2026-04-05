#include "dashboard_metrics.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>

namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string FormatScalarValue(const ScalarMetric& metric, int precision) {
    if (!metric.value.has_value()) {
        return "N/A";
    }
    char buffer[64];
    sprintf_s(buffer, "%.*f %s", precision, *metric.value, metric.unit.c_str());
    return buffer;
}

std::string FormatMemory(double usedGb, double totalGb) {
    char buffer[64];
    sprintf_s(buffer, "%.1f / %.0f GB", usedGb, totalGb);
    return buffer;
}

std::string FormatDriveFree(double freeGb) {
    char buffer[64];
    if (freeGb >= 1024.0) {
        sprintf_s(buffer, "%.1f TB free", freeGb / 1024.0);
    } else {
        sprintf_s(buffer, "%.0f GB free", freeGb);
    }
    return buffer;
}

std::vector<double> SmoothThroughputHistory(const std::vector<double>& history) {
    if (history.empty()) {
        return {};
    }

    std::vector<double> smoothed;
    smoothed.reserve(history.size());
    smoothed.push_back(history.front());
    for (size_t i = 1; i < history.size(); ++i) {
        smoothed.push_back((history[i - 1] + history[i]) / 2.0);
    }
    return smoothed;
}

double GetThroughputGraphMax(const std::vector<double>& firstHistory, const std::vector<double>& secondHistory) {
    double rawMax = 10.0;
    for (double value : firstHistory) {
        rawMax = std::max(rawMax, value);
    }
    for (double value : secondHistory) {
        rawMax = std::max(rawMax, value);
    }
    return std::max(10.0, std::ceil(rawMax / 5.0) * 5.0);
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

std::optional<DashboardMetricRow> ResolveNamedBoardMetric(const std::vector<NamedScalarMetric>& metrics,
    const std::string& name, const char* suffix, double scale) {
    for (const auto& metric : metrics) {
        if (ToLower(metric.name) != ToLower(name)) {
            continue;
        }
        return DashboardMetricRow{BuildBoardMetricLabel(metric.name, suffix), FormatScalarValue(metric.metric, 0),
            metric.metric.value.value_or(0.0) / scale};
    }

    ScalarMetric unavailable{std::nullopt, std::string(suffix) == "Temp" ? "\xC2\xB0""C" : "RPM"};
    return DashboardMetricRow{BuildBoardMetricLabel(name, suffix), FormatScalarValue(unavailable, 0), 0.0};
}

std::optional<DashboardMetricRow> ResolveMetricRow(const SystemSnapshot& snapshot, const std::string& metricRef) {
    const std::string lowered = ToLower(metricRef);
    if (lowered == "cpu.clock") {
        return DashboardMetricRow{"Clock", FormatScalarValue(snapshot.cpu.clock, 2),
            snapshot.cpu.clock.value.value_or(0.0) / 5.0};
    }
    if (lowered == "cpu.ram" || lowered == "cpu.memory") {
        const double total = snapshot.cpu.memory.totalGb;
        return DashboardMetricRow{"RAM", FormatMemory(snapshot.cpu.memory.usedGb, total),
            total > 0.0 ? snapshot.cpu.memory.usedGb / total : 0.0};
    }
    if (lowered == "gpu.temp" || lowered == "gpu.temperature") {
        return DashboardMetricRow{"Temp", FormatScalarValue(snapshot.gpu.temperature, 0),
            snapshot.gpu.temperature.value.value_or(0.0) / 100.0};
    }
    if (lowered == "gpu.clock") {
        return DashboardMetricRow{"Clock", FormatScalarValue(snapshot.gpu.clock, 0),
            snapshot.gpu.clock.value.value_or(0.0) / 2600.0};
    }
    if (lowered == "gpu.fan") {
        return DashboardMetricRow{"Fan", FormatScalarValue(snapshot.gpu.fan, 0),
            snapshot.gpu.fan.value.value_or(0.0) / 3000.0};
    }
    if (lowered == "gpu.vram" || lowered == "gpu.memory") {
        const double total = std::max(1.0, snapshot.gpu.vram.totalGb);
        return DashboardMetricRow{"VRAM", FormatMemory(snapshot.gpu.vram.usedGb, total),
            snapshot.gpu.vram.totalGb > 0.0 ? snapshot.gpu.vram.usedGb / snapshot.gpu.vram.totalGb : 0.0};
    }
    if (lowered.rfind("board.temp.", 0) == 0) {
        return ResolveNamedBoardMetric(snapshot.boardTemperatures, metricRef.substr(std::string("board.temp.").size()),
            "Temp", 100.0);
    }
    if (lowered.rfind("board.fan.", 0) == 0) {
        return ResolveNamedBoardMetric(snapshot.boardFans, metricRef.substr(std::string("board.fan.").size()),
            "Fan", 3000.0);
    }
    return std::nullopt;
}

void ApplyMetricLabelOverride(DashboardMetricRow& row, const std::string& labelOverride) {
    if (!labelOverride.empty()) {
        row.label = labelOverride;
    }
}

}  // namespace

DashboardMetricSource::DashboardMetricSource(const SystemSnapshot& snapshot) : snapshot_(snapshot) {}

std::string DashboardMetricSource::ResolveText(const std::string& metricRef) const {
    const std::string lowered = ToLower(metricRef);
    if (lowered == "cpu.name") {
        return snapshot_.cpu.name;
    }
    if (lowered == "gpu.name") {
        return snapshot_.gpu.name;
    }
    return "N/A";
}

double DashboardMetricSource::ResolveGaugePercent(const std::string& metricRef) const {
    const std::string lowered = ToLower(metricRef);
    if (lowered == "cpu.load" || lowered == "cpu.load_percent") {
        return snapshot_.cpu.loadPercent;
    }
    if (lowered == "gpu.load" || lowered == "gpu.load_percent") {
        return snapshot_.gpu.loadPercent;
    }
    return 0.0;
}

std::vector<DashboardMetricRow> DashboardMetricSource::ResolveMetricList(const std::vector<DashboardMetricListEntry>& metricRefs) const {
    std::vector<DashboardMetricRow> rows;
    for (const auto& metricRef : metricRefs) {
        if (auto row = ResolveMetricRow(snapshot_, metricRef.metricRef); row.has_value()) {
            ApplyMetricLabelOverride(*row, metricRef.labelOverride);
            rows.push_back(*row);
        }
    }
    return rows;
}

DashboardThroughputMetric DashboardMetricSource::ResolveThroughput(const std::string& metricRef) const {
    const std::string lowered = ToLower(metricRef);
    const auto networkUploadHistory = SmoothThroughputHistory(snapshot_.network.uploadHistory);
    const auto networkDownloadHistory = SmoothThroughputHistory(snapshot_.network.downloadHistory);
    const auto storageReadHistory = SmoothThroughputHistory(snapshot_.storage.readHistory);
    const auto storageWriteHistory = SmoothThroughputHistory(snapshot_.storage.writeHistory);
    const double networkMaxGraph = GetThroughputGraphMax(networkUploadHistory, networkDownloadHistory);
    const double storageMaxGraph = GetThroughputGraphMax(storageReadHistory, storageWriteHistory);
    const double timeMarkerOffsetSamples = GetTimeMarkerOffsetSamples(snapshot_.now);
    if (lowered == "network.upload") {
        return DashboardThroughputMetric{
            "Up", snapshot_.network.uploadMbps, networkUploadHistory, networkMaxGraph, 5.0, timeMarkerOffsetSamples, 20.0};
    }
    if (lowered == "network.download") {
        return DashboardThroughputMetric{
            "Down", snapshot_.network.downloadMbps, networkDownloadHistory, networkMaxGraph, 5.0, timeMarkerOffsetSamples, 20.0};
    }
    if (lowered == "storage.read") {
        return DashboardThroughputMetric{"Read", snapshot_.storage.readMbps, storageReadHistory,
            storageMaxGraph, GetStorageGuideStep(storageMaxGraph), timeMarkerOffsetSamples, 20.0};
    }
    if (lowered == "storage.write") {
        return DashboardThroughputMetric{"Write", snapshot_.storage.writeMbps, storageWriteHistory,
            storageMaxGraph, GetStorageGuideStep(storageMaxGraph), timeMarkerOffsetSamples, 20.0};
    }
    return DashboardThroughputMetric{};
}

std::string DashboardMetricSource::ResolveNetworkFooter() const {
    if (snapshot_.network.adapterName.empty()) {
        return snapshot_.network.ipAddress;
    }
    return snapshot_.network.adapterName + " | " + snapshot_.network.ipAddress;
}

std::vector<DashboardDriveRow> DashboardMetricSource::ResolveDriveRows(const std::vector<std::string>& drives) const {
    std::vector<DriveInfo> ordered;
    for (const auto& driveName : drives) {
        const auto it = std::find_if(snapshot_.drives.begin(), snapshot_.drives.end(), [&](const DriveInfo& drive) {
            return ToLower(drive.label) == ToLower(driveName + ":") || ToLower(drive.label) == ToLower(driveName);
        });
        if (it != snapshot_.drives.end()) {
            ordered.push_back(*it);
        }
    }
    if (ordered.empty()) {
        ordered = snapshot_.drives;
    }

    std::vector<DashboardDriveRow> rows;
    for (const auto& drive : ordered) {
        rows.push_back(DashboardDriveRow{drive.label, drive.usedPercent, FormatDriveFree(drive.freeGb)});
    }
    return rows;
}

std::string DashboardMetricSource::ResolveClockTime() const {
    char buffer[32];
    sprintf_s(buffer, "%02d:%02d", snapshot_.now.wHour, snapshot_.now.wMinute);
    return buffer;
}

std::string DashboardMetricSource::ResolveClockDate() const {
    char buffer[32];
    sprintf_s(buffer, "%04d-%02d-%02d", snapshot_.now.wYear, snapshot_.now.wMonth, snapshot_.now.wDay);
    return buffer;
}
