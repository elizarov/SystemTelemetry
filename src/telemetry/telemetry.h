#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/telemetry_settings.h"
#include "telemetry/board/board_vendor.h"
#include "telemetry/gpu/gpu_vendor.h"
#include "telemetry/metric_types.h"
#include "util/trace.h"

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
    ScalarMetric clock{std::nullopt, ScalarMetricUnit::Gigahertz};
    MemoryMetric memory;
};

struct GpuTelemetry {
    std::string name = "GPU";
    double loadPercent = 0.0;
    ScalarMetric temperature{std::nullopt, ScalarMetricUnit::Celsius};
    ScalarMetric clock{std::nullopt, ScalarMetricUnit::Megahertz};
    ScalarMetric fan{std::nullopt, ScalarMetricUnit::Rpm};
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
    uint64_t revision = 0;
};

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
    virtual ~TelemetryCollector() = default;

    TelemetryCollector(const TelemetryCollector&) = delete;
    TelemetryCollector& operator=(const TelemetryCollector&) = delete;
    TelemetryCollector(TelemetryCollector&&) = delete;
    TelemetryCollector& operator=(TelemetryCollector&&) = delete;

    virtual bool Initialize(const TelemetrySettings& settings, std::string* errorText = nullptr) = 0;
    virtual const SystemSnapshot& Snapshot() const = 0;
    virtual TelemetryDump Dump() const = 0;
    virtual const ResolvedTelemetrySelections& ResolvedSelections() const = 0;
    virtual const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const = 0;
    virtual const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const = 0;
    virtual void ApplySettings(const TelemetrySettings& settings) = 0;
    virtual void SetPreferredNetworkAdapterName(std::string adapterName) = 0;
    virtual void SetSelectedStorageDrives(std::vector<std::string> driveLetters) = 0;
    virtual void RefreshSelectionsAndSnapshot() = 0;
    virtual void UpdateSnapshot() = 0;

protected:
    TelemetryCollector() = default;
};

using TelemetryDumpLoader = bool (*)(std::istream& input, TelemetryDump& dump, std::string* error);

struct TelemetryCollectorOptions {
    bool fake = false;
    std::filesystem::path fakePath;
    TelemetryDumpLoader loadFakeDump = nullptr;
};

std::unique_ptr<TelemetryCollector> CreateTelemetryCollector(
    const TelemetryCollectorOptions& options, const std::filesystem::path& workingDirectory, Trace& trace);
