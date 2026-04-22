#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <dxgi.h>
#include <iphlpapi.h>
#include <memory>
#include <netioapi.h>
#include <ostream>
#include <pdh.h>
#include <pdhmsg.h>
#include <string>
#include <vector>

#include "telemetry/board/board_vendor.h"
#include "telemetry/gpu/gpu_vendor.h"
#include "telemetry/impl/retained_history.h"
#include "telemetry/telemetry.h"
#include "util/trace.h"

struct DriveCounterState {
    std::string label;
    std::wstring rootPath;
    PDH_HCOUNTER readCounter = nullptr;
    PDH_HCOUNTER writeCounter = nullptr;
};

struct RealTelemetryCollectorState {
    struct BoardState {
        std::unique_ptr<BoardVendorTelemetryProvider> provider;
        std::string providerName = "None";
        std::string providerDiagnostics = "Provider not initialized.";
        std::string boardManufacturer;
        std::string boardProduct;
        std::string driverLibrary;
        std::vector<std::string> requestedFanNames;
        std::vector<std::string> requestedTemperatureNames;
        std::vector<std::string> availableFanNames;
        std::vector<std::string> availableTemperatureNames;
        bool providerAvailable = false;
    };

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
        std::vector<BYTE> counterArrayBuffer;
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

    ~RealTelemetryCollectorState();

    TelemetrySettings settings_;
    ResolvedTelemetrySelections resolvedSelections_;
    SystemSnapshot snapshot_;
    BoardState board_;
    CpuState cpu_;
    GpuState gpu_;
    StorageState storage_;
    NetworkState network_;
    RetainedHistoryStore retainedHistoryStore_;
    tracing::Trace trace_;
};
