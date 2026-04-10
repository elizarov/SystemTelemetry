#pragma once

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

#include "board_vendor.h"
#include "config.h"
#include "gpu_vendor.h"
#include "metric_types.h"

struct MemoryMetric {
    double usedGb = 0.0;
    double totalGb = 0.0;
};

struct DriveInfo {
    std::string label;
    std::string volumeLabel;
    double totalGb = 0.0;
    double usedPercent = 0.0;
    double freeGb = 0.0;
    double readMbps = 0.0;
    double writeMbps = 0.0;
    UINT driveType = DRIVE_UNKNOWN;
};

struct StorageDriveCandidate {
    std::string letter;
    std::string volumeLabel;
    double totalGb = 0.0;
    UINT driveType = DRIVE_UNKNOWN;
    bool selected = false;
};

struct RetainedHistorySeries {
    std::string seriesRef;
    std::vector<double> samples;
};

struct ProcessorTelemetry {
    std::string name = "CPU";
    double loadPercent = 0.0;
    ScalarMetric clock{std::nullopt, "GHz"};
    MemoryMetric memory;
};

struct GpuTelemetry {
    std::string name = "GPU";
    double loadPercent = 0.0;
    ScalarMetric temperature{std::nullopt, "°C"};
    ScalarMetric clock{std::nullopt, "MHz"};
    ScalarMetric fan{std::nullopt, "RPM"};
    MemoryMetric vram;
};

struct NetworkTelemetry {
    std::string adapterName = "Auto";
    double uploadMbps = 0.0;
    double downloadMbps = 0.0;
    std::string ipAddress = "N/A";
};

struct NetworkAdapterCandidate {
    std::string adapterName;
    std::string ipAddress = "N/A";
    bool selected = false;
};

struct StorageTelemetry {
    double readMbps = 0.0;
    double writeMbps = 0.0;
};

struct SystemSnapshot {
    ProcessorTelemetry cpu;
    GpuTelemetry gpu;
    std::vector<NamedScalarMetric> boardTemperatures;
    std::vector<NamedScalarMetric> boardFans;
    std::vector<RetainedHistorySeries> retainedHistories;
    std::unordered_map<std::string, size_t> retainedHistoryIndexByRef;
    NetworkTelemetry network;
    StorageTelemetry storage;
    std::vector<DriveInfo> drives;
    SYSTEMTIME now{};
};

inline void RebuildRetainedHistoryIndex(SystemSnapshot& snapshot) {
    snapshot.retainedHistoryIndexByRef.clear();
    snapshot.retainedHistoryIndexByRef.reserve(snapshot.retainedHistories.size());
    for (size_t i = 0; i < snapshot.retainedHistories.size(); ++i) {
        snapshot.retainedHistoryIndexByRef[snapshot.retainedHistories[i].seriesRef] = i;
    }
}

struct GpuProviderTelemetryState {
    std::string providerName = "None";
    std::string diagnostics;
    bool available = false;
};

struct TelemetryDump {
    SystemSnapshot snapshot;
    GpuProviderTelemetryState gpuProvider;
    BoardVendorTelemetrySample boardProvider;
};

class TelemetryCollector {
public:
    TelemetryCollector();
    ~TelemetryCollector();

    TelemetryCollector(const TelemetryCollector&) = delete;
    TelemetryCollector& operator=(const TelemetryCollector&) = delete;
    TelemetryCollector(TelemetryCollector&&) noexcept;
    TelemetryCollector& operator=(TelemetryCollector&&) noexcept;

    bool Initialize(const AppConfig& config, std::ostream* traceStream = nullptr);
    const SystemSnapshot& Snapshot() const;
    TelemetryDump Dump() const;
    AppConfig EffectiveConfig() const;
    const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const;
    const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const;
    void SetPreferredNetworkAdapterName(std::string adapterName);
    void SetSelectedStorageDrives(std::vector<std::string> driveLetters);
    void UpdateSnapshot();
    void WriteDump(std::ostream& output) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
