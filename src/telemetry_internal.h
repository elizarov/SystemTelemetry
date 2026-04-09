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
    ~Impl();

    void UpdateCpu();
    void UpdateGpu();
    void InitializeGpuAdapterInfo();
    void UpdateMemory();
    void ApplyBoardVendorSample(const BoardVendorTelemetrySample& sample);
    void ApplyGpuVendorSample(const GpuVendorTelemetrySample& sample);
    void EnumerateDrives();
    void UpdateStorageThroughput(bool initializeOnly);
    void RefreshStorageDriveCandidates();
    void RefreshDriveUsage();
    void UpdateNetworkState(bool initializeOnly);
    double SumCounterArray(PDH_HCOUNTER counter, bool require3d);
    void Trace(const char* text) const;
    void Trace(const std::string& text) const;

    AppConfig config_;
    SystemSnapshot snapshot_;
    std::vector<NetworkAdapterCandidate> networkAdapterCandidates_;
    std::vector<StorageDriveCandidate> storageDriveCandidates_;
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
    PDH_HQUERY storageQuery_ = nullptr;
    PDH_HCOUNTER storageReadCounter_ = nullptr;
    PDH_HCOUNTER storageWriteCounter_ = nullptr;
    std::vector<DriveCounterState> driveCounters_;

    ULONG selectedIndex_ = 0;
    uint64_t previousInOctets_ = 0;
    uint64_t previousOutOctets_ = 0;
    std::chrono::steady_clock::time_point previousNetworkTick_{};
};
