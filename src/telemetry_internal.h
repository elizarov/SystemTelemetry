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
#include <optional>
#include <string>
#include <vector>

#include "board_vendor.h"
#include "gpu_vendor.h"
#include "telemetry.h"
#include "trace.h"

constexpr size_t kRecentHistorySamples = 60;

struct DriveCounterState {
    std::string label;
    PDH_HCOUNTER readCounter = nullptr;
    PDH_HCOUNTER writeCounter = nullptr;
};

struct AdapterSelectionInfo {
    bool matched = false;
    bool hasIpv4 = false;
    bool hasGateway = false;
    std::string ipAddress = "N/A";
};

struct NetworkCandidateState {
    NetworkAdapterCandidate candidate;
    ULONG interfaceIndex = 0;
};

std::string NormalizeDriveLetter(const std::string& drive);
bool IsSelectableStorageDriveType(UINT driveType);
std::string ReadVolumeLabel(const std::wstring& root);
std::string ToLowerAscii(std::string value);
std::string FormatScalarMetric(const ScalarMetric& metric, int precision);
std::vector<NamedScalarMetric> CreateRequestedBoardMetrics(const std::vector<std::string>& names, const char* unit);
bool HasAvailableMetricValue(const std::vector<NamedScalarMetric>& metrics);
RetainedHistorySeries CreateRetainedHistorySeries(const std::string& seriesRef);
double ResolveScaleRatio(double value, double scale);
PDH_STATUS AddCounterCompat(PDH_HQUERY query, const wchar_t* path, PDH_HCOUNTER* counter);
bool ContainsInsensitive(const std::wstring& value, const std::string& needle);
bool EqualsInsensitive(const std::wstring& value, const std::string& needle);
std::string TrimAsciiWhitespace(std::string value);
std::string CollapseAsciiWhitespace(std::string value);
std::optional<std::wstring> ReadRegistryWideString(HKEY root, const wchar_t* subKey, const wchar_t* valueName);
std::optional<std::string> ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* valueName);
std::string DetectCpuName();
bool AdapterMatchesRow(const IP_ADAPTER_ADDRESSES& adapter, const MIB_IF_ROW2& row);
bool HasUsableGateway(const IP_ADAPTER_ADDRESSES& adapter);
AdapterSelectionInfo BuildAdapterSelectionInfo(const MIB_IF_ROW2& row, const IP_ADAPTER_ADDRESSES* addresses);

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
    void PushRetainedHistorySample(const std::string& seriesRef, double value);
    void PushBoardMetricHistorySamples();
    double SumCounterArray(PDH_HCOUNTER counter, bool require3d);
    static void PushHistorySample(std::vector<double>& history, double value);
    void Trace(const char* text) const;
    void Trace(const std::string& text) const;

    AppConfig config_;
    SystemSnapshot snapshot_;
    std::vector<NetworkAdapterCandidate> networkAdapterCandidates_;
    std::vector<StorageDriveCandidate> storageDriveCandidates_;
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
