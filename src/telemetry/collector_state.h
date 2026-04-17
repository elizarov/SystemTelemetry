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
#include "telemetry/retained_history.h"
#include "trace.h"

struct DriveCounterState {
    std::string label;
    PDH_HCOUNTER readCounter = nullptr;
    PDH_HCOUNTER writeCounter = nullptr;
};

struct TelemetryCollectorState {
    struct CpuState {
        PDH_HQUERY query = nullptr;
        PDH_HCOUNTER loadCounter = nullptr;
        PDH_HCOUNTER frequencyCounter = nullptr;
    };

    struct GpuState {
        std::unique_ptr<GpuVendorTelemetryProvider> provider;
        std::string providerName = "None";
        std::string providerDiagnostics = "Provider not initialized.";
        bool providerAvailable = false;
        PDH_HQUERY query = nullptr;
        PDH_HCOUNTER loadCounter = nullptr;
        PDH_HQUERY memoryQuery = nullptr;
        PDH_HCOUNTER dedicatedCounter = nullptr;
    };

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

    ~TelemetryCollectorState();

    TelemetrySettings settings_;
    ResolvedTelemetrySelections resolvedSelections_;
    SystemSnapshot snapshot_;
    CpuState cpu_;
    GpuState gpu_;
    StorageState storage_;
    NetworkState network_;
    RetainedHistoryStore retainedHistoryStore_;
    tracing::Trace trace_;
    std::unique_ptr<BoardVendorTelemetryProvider> boardProvider_;
    std::string boardProviderName_ = "None";
    std::string boardProviderDiagnostics_ = "Provider not initialized.";
    BoardVendorTelemetrySample boardProviderSample_{};
    bool boardProviderAvailable_ = false;
};
