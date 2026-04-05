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

std::optional<DashboardMetricRow> ResolveMetricRow(const SystemSnapshot& snapshot, const std::string& metricRef) {
    const std::string lowered = ToLower(metricRef);
    if (lowered == "cpu.temp" || lowered == "cpu.temperature") {
        return DashboardMetricRow{"Temp", FormatScalarValue(snapshot.cpu.temperature, 0),
            snapshot.cpu.temperature.value.value_or(0.0) / 100.0};
    }
    if (lowered == "cpu.clock") {
        return DashboardMetricRow{"Clock", FormatScalarValue(snapshot.cpu.clock, 2),
            snapshot.cpu.clock.value.value_or(0.0) / 5.0};
    }
    if (lowered == "cpu.fan") {
        return DashboardMetricRow{"Fan", FormatScalarValue(snapshot.cpu.fan, 0),
            snapshot.cpu.fan.value.value_or(0.0) / 3000.0};
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
    return std::nullopt;
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

std::vector<DashboardMetricRow> DashboardMetricSource::ResolveMetricList(const std::vector<std::string>& metricRefs) const {
    std::vector<DashboardMetricRow> rows;
    for (const auto& metricRef : metricRefs) {
        if (const auto row = ResolveMetricRow(snapshot_, metricRef); row.has_value()) {
            rows.push_back(*row);
        }
    }
    return rows;
}

DashboardThroughputMetric DashboardMetricSource::ResolveThroughput(const std::string& metricRef) const {
    const std::string lowered = ToLower(metricRef);
    if (lowered == "network.upload") {
        return DashboardThroughputMetric{"Up", snapshot_.network.uploadMbps, snapshot_.network.uploadHistory,
            GetThroughputGraphMax(snapshot_.network.uploadHistory, snapshot_.network.downloadHistory)};
    }
    if (lowered == "network.download") {
        return DashboardThroughputMetric{"Down", snapshot_.network.downloadMbps, snapshot_.network.downloadHistory,
            GetThroughputGraphMax(snapshot_.network.uploadHistory, snapshot_.network.downloadHistory)};
    }
    if (lowered == "storage.read") {
        return DashboardThroughputMetric{"Read", snapshot_.storage.readMbps, snapshot_.storage.readHistory,
            GetThroughputGraphMax(snapshot_.storage.readHistory, snapshot_.storage.writeHistory)};
    }
    if (lowered == "storage.write") {
        return DashboardThroughputMetric{"Write", snapshot_.storage.writeMbps, snapshot_.storage.writeHistory,
            GetThroughputGraphMax(snapshot_.storage.readHistory, snapshot_.storage.writeHistory)};
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
