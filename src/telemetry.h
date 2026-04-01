#pragma once

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>
#include <windows.h>

#include "config.h"

struct ScalarMetric {
    std::optional<double> value;
    std::string unit;
};

struct MemoryMetric {
    double usedGb = 0.0;
    double totalGb = 0.0;
};

struct DriveInfo {
    std::string label;
    double usedPercent = 0.0;
    double freeGb = 0.0;
};

struct ProcessorTelemetry {
    std::string name = "CPU";
    double loadPercent = 0.0;
    ScalarMetric clock{std::nullopt, "GHz"};
    ScalarMetric fan{std::nullopt, "RPM"};
    MemoryMetric memory;
};

struct GpuTelemetry {
    std::string name = "GPU";
    double loadPercent = 0.0;
    ScalarMetric temperature{std::nullopt, "\xC2\xB0""C"};
    ScalarMetric clock{std::nullopt, "MHz"};
    ScalarMetric fan{std::nullopt, "RPM"};
    MemoryMetric vram;
};

struct NetworkTelemetry {
    std::string adapterName = "Auto";
    double uploadMbps = 0.0;
    double downloadMbps = 0.0;
    std::string ipAddress = "N/A";
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

    bool Initialize(const AppConfig& config, std::ostream* traceStream = nullptr);
    const SystemSnapshot& Snapshot() const;
    void UpdateSnapshot();
    void DumpText(std::ostream& output) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
