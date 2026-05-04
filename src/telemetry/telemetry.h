#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "config/telemetry_settings.h"
#include "telemetry/board/board_vendor.h"
#include "telemetry/metric_types.h"
#include "util/file_path.h"
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
    ScalarMetric fps{std::nullopt, ScalarMetricUnit::Fps};
    std::string fpsAppName;
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

struct TelemetryUpdate {
    TelemetryDump dump;
    ResolvedTelemetrySelections resolvedSelections;
    std::vector<NetworkAdapterCandidate> networkAdapterCandidates;
    std::vector<StorageDriveCandidate> storageDriveCandidates;
};

using TelemetryDumpLoader = bool (*)(std::string_view input, TelemetryDump& dump, std::string* error);

struct TelemetryCollectorOptions {
    bool fake = false;
    FilePath fakePath;
    TelemetryDumpLoader loadFakeDump = nullptr;
};

class TelemetryUpdateSink {
public:
    virtual void OnTelemetryUpdate(const TelemetryUpdate& update) = 0;

protected:
    ~TelemetryUpdateSink() = default;
};

class TelemetryRuntime {
public:
    virtual ~TelemetryRuntime() = default;

    TelemetryRuntime(const TelemetryRuntime&) = delete;
    TelemetryRuntime& operator=(const TelemetryRuntime&) = delete;
    TelemetryRuntime(TelemetryRuntime&&) = delete;
    TelemetryRuntime& operator=(TelemetryRuntime&&) = delete;

    // Thread-safe. Blocks until the telemetry worker has stopped, then guarantees no later callback invocations.
    virtual void Shutdown() = 0;

    // Thread-safe. Blocks behind any active telemetry collection, applies settings on the telemetry-owned collector,
    // publishes one fresh update, and leaves the 500 ms worker cadence running.
    virtual void Reconfigure(const TelemetrySettings& settings) = 0;

    // Thread-safe. Blocks behind any active telemetry collection, changes the network selection on the telemetry-owned
    // collector, publishes one fresh update, and leaves the 500 ms worker cadence running.
    virtual void SetPreferredNetworkAdapterName(std::string adapterName) = 0;

    // Thread-safe. Blocks behind any active telemetry collection, changes the storage selection on the telemetry-owned
    // collector, publishes one fresh update, and leaves the 500 ms worker cadence running.
    virtual void SetSelectedStorageDrives(std::vector<std::string> driveLetters) = 0;

    // Thread-safe. Blocks behind any active telemetry collection, refreshes runtime selections on the telemetry-owned
    // collector, publishes one fresh update, and leaves the 500 ms worker cadence running.
    virtual void RefreshSelections() = 0;

    // Thread-safe. Returns a copy of the latest published telemetry update without invoking the callback.
    virtual TelemetryUpdate Latest() const = 0;

protected:
    TelemetryRuntime() = default;
};

// The callback is invoked from the telemetry worker thread. It must not call back into TelemetryRuntime, touch UI state
// directly, or retain references from the passed update after returning; copy any data needed by another thread.
// Creation initializes telemetry synchronously before returning so startup errors are reported deterministically.
std::unique_ptr<TelemetryRuntime> CreateTelemetryRuntime(const TelemetryCollectorOptions& options,
    const FilePath& workingDirectory,
    const TelemetrySettings& settings,
    Trace& trace,
    TelemetryUpdateSink* callback,
    std::string* errorText = nullptr);
