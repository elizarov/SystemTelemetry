#pragma once

#include <memory>
#include <optional>
#include <ostream>
#include <string>
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
    double usedPercent = 0.0;
    double freeGb = 0.0;
};

struct MetricHistorySeries {
    std::string metricRef;
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

struct StorageTelemetry {
    double readMbps = 0.0;
    double writeMbps = 0.0;
    std::vector<double> readHistory;
    std::vector<double> writeHistory;
};

struct SystemSnapshot {
    ProcessorTelemetry cpu;
    GpuTelemetry gpu;
    std::vector<NamedScalarMetric> boardTemperatures;
    std::vector<NamedScalarMetric> boardFans;
    std::vector<MetricHistorySeries> metricHistories;
    NetworkTelemetry network;
    StorageTelemetry storage;
    std::vector<DriveInfo> drives;
    SYSTEMTIME now{};
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
    void UpdateSnapshot();
    void WriteDump(std::ostream& output) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
