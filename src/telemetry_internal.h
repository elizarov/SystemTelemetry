#pragma once

#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "board_vendor.h"
#include "gpu_vendor.h"
#include "telemetry.h"
#include "telemetry_retained_history.h"
#include "trace.h"

struct DriveCounterState {
    std::string label;
    PDH_HCOUNTER readCounter = nullptr;
    PDH_HCOUNTER writeCounter = nullptr;
};

struct TelemetryCollector::Impl {
    struct StorageState {
        std::vector<std::string> resolvedDriveLetters;
        std::vector<StorageDriveCandidate> driveCandidates;
        PDH_HQUERY query = nullptr;
        PDH_HCOUNTER readCounter = nullptr;
        PDH_HCOUNTER writeCounter = nullptr;
        std::vector<DriveCounterState> driveCounters;
    };

    struct NetworkState {
        std::string resolvedAdapterName;
        std::string resolvedIpAddress = "N/A";
        std::vector<NetworkAdapterCandidate> adapterCandidates;
        ULONG selectedIndex = 0;
        uint64_t previousInOctets = 0;
        uint64_t previousOutOctets = 0;
        std::chrono::steady_clock::time_point previousTick{};
    };

    ~Impl();

    void UpdateCpu();
    void UpdateGpu();
    void InitializeGpuAdapterInfo();
    void UpdateMemory();
    void ApplyBoardVendorSample(const BoardVendorTelemetrySample& sample);
    void ApplyGpuVendorSample(const GpuVendorTelemetrySample& sample);
    void ResolveStorageSelection();
    void CollectStorageMetrics(bool initializeOnly);
    void UpdateStorageThroughput(bool initializeOnly);
    void RefreshDriveUsage();
    void ResolveNetworkSelection();
    void CollectNetworkMetrics(bool initializeOnly);
    double SumCounterArray(PDH_HCOUNTER counter, bool require3d);
    void Trace(const char* text) const;
    void Trace(const std::string& text) const;

    TelemetrySettings settings_;
    ResolvedTelemetrySelections resolvedSelections_;
    SystemSnapshot snapshot_;
    StorageState storage_;
    NetworkState network_;
    RetainedHistoryStore retainedHistoryStore_;
    tracing::Trace trace_;
    std::unique_ptr<GpuVendorTelemetryProvider> gpuProvider_;
    std::unique_ptr<BoardVendorTelemetryProvider> boardProvider_;
    std::string gpuProviderName_ = "None";
    std::string gpuProviderDiagnostics_ = "Provider not initialized.";
    bool gpuProviderAvailable_ = false;
    std::string boardProviderName_ = "None";
    std::string boardProviderDiagnostics_ = "Provider not initialized.";
    BoardVendorTelemetrySample boardProviderSample_{};
    bool boardProviderAvailable_ = false;

    PDH_HQUERY cpuQuery_ = nullptr;
    PDH_HCOUNTER cpuLoadCounter_ = nullptr;
    PDH_HCOUNTER cpuFrequencyCounter_ = nullptr;
    PDH_HQUERY gpuQuery_ = nullptr;
    PDH_HCOUNTER gpuLoadCounter_ = nullptr;
    PDH_HQUERY gpuMemoryQuery_ = nullptr;
    PDH_HCOUNTER gpuDedicatedCounter_ = nullptr;
};
