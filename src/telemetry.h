#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

#include "config.h"

struct ScalarMetric {
    std::optional<double> value;
    std::wstring unit;
};

struct MemoryMetric {
    double usedGb = 0.0;
    double totalGb = 0.0;
};

struct DriveInfo {
    std::wstring label;
    double usedPercent = 0.0;
    double freeGb = 0.0;
};

struct ProcessorTelemetry {
    std::wstring name = L"CPU";
    double loadPercent = 0.0;
    ScalarMetric clock{std::nullopt, L"GHz"};
    MemoryMetric memory;
};

struct GpuTelemetry {
    std::wstring name = L"GPU";
    double loadPercent = 0.0;
    ScalarMetric temperature{std::nullopt, L"\x00B0""C"};
    ScalarMetric clock{std::nullopt, L"MHz"};
    ScalarMetric fan{std::nullopt, L"RPM"};
    MemoryMetric vram;
};

struct NetworkTelemetry {
    std::wstring adapterName = L"Auto";
    double uploadMbps = 0.0;
    double downloadMbps = 0.0;
    std::wstring ipAddress = L"N/A";
    std::vector<double> uploadHistory;
    std::vector<double> downloadHistory;
};

struct SystemSnapshot {
    ProcessorTelemetry cpu;
    GpuTelemetry gpu;
    NetworkTelemetry network;
    std::vector<DriveInfo> drives;
    SYSTEMTIME now{};
};

class TelemetryCollector {
public:
    TelemetryCollector();
    ~TelemetryCollector();

    TelemetryCollector(const TelemetryCollector&) = delete;
    TelemetryCollector& operator=(const TelemetryCollector&) = delete;
    TelemetryCollector(TelemetryCollector&&) noexcept;
    TelemetryCollector& operator=(TelemetryCollector&&) noexcept;

    bool Initialize(const AppConfig& config);
    const SystemSnapshot& Snapshot() const;
    void UpdateSnapshot();
    std::wstring DumpText() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
