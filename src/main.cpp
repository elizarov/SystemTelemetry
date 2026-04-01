#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <shellapi.h>
#include <wbemidl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 480;
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT_PTR kMoveTimerId = 2;
constexpr UINT kRefreshTimerMs = 250;
constexpr UINT kMoveTimerMs = 16;
constexpr COLORREF kBlack = RGB(0, 0, 0);
constexpr COLORREF kWhite = RGB(255, 255, 255);
constexpr COLORREF kAccent = RGB(0, 191, 255);
constexpr COLORREF kPanelBorder = RGB(235, 235, 235);
constexpr COLORREF kMuted = RGB(165, 180, 190);
constexpr COLORREF kTrack = RGB(45, 52, 58);
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kCommandMove = 1001;
constexpr UINT kCommandBringOnTop = 1002;
constexpr UINT kCommandUpdateConfig = 1003;
constexpr UINT kCommandExit = 1004;
constexpr wchar_t kWindowClassName[] = L"SystemTelemetryDashboard";

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
    ScalarMetric temperature{std::nullopt, L"\x00B0""C"};
    ScalarMetric power{std::nullopt, L"W"};
    ScalarMetric clock{std::nullopt, L"GHz"};
    ScalarMetric fan{std::nullopt, L"RPM"};
    MemoryMetric memory;
};

struct GpuTelemetry {
    std::wstring name = L"GPU";
    double loadPercent = 0.0;
    ScalarMetric temperature{std::nullopt, L"\x00B0""C"};
    ScalarMetric power{std::nullopt, L"W"};
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

std::wstring Trim(const std::wstring& input) {
    const auto first = input.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"";
    }
    const auto last = input.find_last_not_of(L" \t\r\n");
    return input.substr(first, last - first + 1);
}

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring EscapeWql(const std::wstring& value) {
    std::wstring escaped;
    for (wchar_t ch : value) {
        if (ch == L'\\' || ch == L'\'') {
            escaped.push_back(L'\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

class WmiSession {
public:
    bool Initialize() {
        if (initialized_) {
            return true;
        }

        const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE) {
            return false;
        }
        comInitialized_ = SUCCEEDED(coInit);

        const HRESULT security = CoInitializeSecurity(
            nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr, EOAC_NONE, nullptr);
        if (FAILED(security) && security != RPC_E_TOO_LATE) {
            return false;
        }

        const HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<void**>(&locator_));
        if (FAILED(hr)) {
            return false;
        }

        initialized_ = true;
        return true;
    }

    ~WmiSession() {
        for (auto& pair : services_) {
            if (pair.second != nullptr) {
                pair.second->Release();
            }
        }
        if (locator_ != nullptr) {
            locator_->Release();
        }
        if (comInitialized_) {
            CoUninitialize();
        }
    }

    std::optional<std::wstring> QueryFirstString(
        const std::wstring& namespaceName, const std::wstring& query, const std::wstring& property);
    std::optional<double> QueryFirstDouble(
        const std::wstring& namespaceName, const std::wstring& query, const std::wstring& property);

private:
    bool QueryFirstVariant(
        const std::wstring& namespaceName, const std::wstring& query,
        const std::wstring& property, VARIANT& value);
    IWbemServices* GetServices(const std::wstring& namespaceName);

    bool initialized_ = false;
    bool comInitialized_ = false;
    IWbemLocator* locator_ = nullptr;
    std::map<std::wstring, IWbemServices*> services_;
};

std::optional<std::wstring> WmiSession::QueryFirstString(
    const std::wstring& namespaceName, const std::wstring& query, const std::wstring& property) {
    VARIANT value{};
    VariantInit(&value);
    if (!QueryFirstVariant(namespaceName, query, property, value)) {
        return std::nullopt;
    }

    std::optional<std::wstring> result;
    if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
        result = std::wstring(value.bstrVal);
    }
    VariantClear(&value);
    return result;
}

std::optional<double> WmiSession::QueryFirstDouble(
    const std::wstring& namespaceName, const std::wstring& query, const std::wstring& property) {
    VARIANT value{};
    VariantInit(&value);
    if (!QueryFirstVariant(namespaceName, query, property, value)) {
        return std::nullopt;
    }

    std::optional<double> result;
    switch (value.vt) {
    case VT_R4:
        result = value.fltVal;
        break;
    case VT_R8:
        result = value.dblVal;
        break;
    case VT_I4:
    case VT_INT:
        result = static_cast<double>(value.intVal);
        break;
    case VT_UI4:
        result = static_cast<double>(value.uintVal);
        break;
    case VT_I8:
        result = static_cast<double>(value.llVal);
        break;
    case VT_UI8:
        result = static_cast<double>(value.ullVal);
        break;
    case VT_BSTR:
        if (value.bstrVal != nullptr) {
            try {
                result = std::stod(value.bstrVal);
            } catch (...) {
                result = std::nullopt;
            }
        }
        break;
    default:
        break;
    }
    VariantClear(&value);
    return result;
}

bool WmiSession::QueryFirstVariant(
    const std::wstring& namespaceName, const std::wstring& query,
    const std::wstring& property, VARIANT& value) {
    IWbemServices* services = GetServices(namespaceName);
    if (services == nullptr) {
        return false;
    }

    BSTR language = SysAllocString(L"WQL");
    BSTR queryText = SysAllocString(query.c_str());
    IEnumWbemClassObject* enumerator = nullptr;
    const HRESULT hr = services->ExecQuery(
        language, queryText,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &enumerator);
    SysFreeString(language);
    SysFreeString(queryText);
    if (FAILED(hr) || enumerator == nullptr) {
        return false;
    }

    IWbemClassObject* object = nullptr;
    ULONG returned = 0;
    const HRESULT nextHr = enumerator->Next(WBEM_INFINITE, 1, &object, &returned);
    if (FAILED(nextHr) || returned == 0 || object == nullptr) {
        enumerator->Release();
        return false;
    }

    const HRESULT getHr = object->Get(property.c_str(), 0, &value, nullptr, nullptr);
    object->Release();
    enumerator->Release();
    return SUCCEEDED(getHr);
}

IWbemServices* WmiSession::GetServices(const std::wstring& namespaceName) {
    if (!initialized_ && !Initialize()) {
        return nullptr;
    }

    const auto found = services_.find(namespaceName);
    if (found != services_.end()) {
        return found->second;
    }

    BSTR ns = SysAllocString(namespaceName.c_str());
    IWbemServices* services = nullptr;
    HRESULT hr = locator_->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    SysFreeString(ns);
    if (FAILED(hr) || services == nullptr) {
        return nullptr;
    }

    hr = CoSetProxyBlanket(
        services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        services->Release();
        return nullptr;
    }

    services_[namespaceName] = services;
    return services;
}

typedef PDH_STATUS(WINAPI* PdhAddEnglishCounterWFn)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);

PDH_STATUS AddCounterCompat(PDH_HQUERY query, const std::wstring& path, PDH_HCOUNTER* counter) {
    static PdhAddEnglishCounterWFn addEnglish = reinterpret_cast<PdhAddEnglishCounterWFn>(
        GetProcAddress(GetModuleHandleW(L"pdh.dll"), "PdhAddEnglishCounterW"));
    if (addEnglish != nullptr) {
        return addEnglish(query, path.c_str(), 0, counter);
    }
    return PdhAddCounterW(query, path.c_str(), 0, counter);
}

bool ContainsInsensitive(const std::wstring& value, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLower(value).find(ToLower(needle)) != std::wstring::npos;
}

std::wstring FormatValue(const ScalarMetric& metric, int precision = 1) {
    if (!metric.value.has_value()) {
        return L"N/A";
    }
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.*f %ls", precision, *metric.value, metric.unit.c_str());
    return buffer;
}

std::wstring FormatMemory(double usedGb, double totalGb) {
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.1f / %.0f GB", usedGb, totalGb);
    return buffer;
}

std::wstring FormatDriveFree(double freeGb) {
    wchar_t buffer[64];
    if (freeGb >= 1024.0) {
        swprintf_s(buffer, L"%.1f TB free", freeGb / 1024.0);
    } else {
        swprintf_s(buffer, L"%.0f GB free", freeGb);
    }
    return buffer;
}

std::wstring FormatSpeed(double mbps) {
    wchar_t buffer[64];
    if (mbps >= 100.0) {
        swprintf_s(buffer, L"%.0f MB/s", mbps);
    } else {
        swprintf_s(buffer, L"%.1f MB/s", mbps);
    }
    return buffer;
}

class TelemetryCollector {
public:
    bool Initialize(const AppConfig& config);
    const SystemSnapshot& Snapshot() const;
    void UpdateSnapshot();

private:
    void UpdateCpu();
    void UpdateGpu();
    void UpdateMemory();
    void UpdateSensors();
    void EnumerateDrives();
    void RefreshDriveUsage();
    void UpdateNetworkState(bool initializeOnly);
    std::optional<double> QuerySensor(const std::wstring& key);
    double SumCounterArray(PDH_HCOUNTER counter, bool require3d);
    std::wstring FindAdapterIp(ULONG interfaceIndex);
    static void PushHistory(std::vector<double>& history, double value);

    AppConfig config_;
    SystemSnapshot snapshot_;
    WmiSession wmi_;

    PDH_HQUERY cpuQuery_ = nullptr;
    PDH_HCOUNTER cpuLoadCounter_ = nullptr;
    PDH_HCOUNTER cpuFrequencyCounter_ = nullptr;
    PDH_HQUERY gpuQuery_ = nullptr;
    PDH_HCOUNTER gpuLoadCounter_ = nullptr;
    PDH_HQUERY gpuMemoryQuery_ = nullptr;
    PDH_HCOUNTER gpuDedicatedCounter_ = nullptr;

    ULONG selectedIndex_ = 0;
    uint64_t previousInOctets_ = 0;
    uint64_t previousOutOctets_ = 0;
    std::chrono::steady_clock::time_point previousNetworkTick_{};
    std::chrono::steady_clock::time_point lastFast_{};
    std::chrono::steady_clock::time_point lastSensors_{};
    std::chrono::steady_clock::time_point lastNetwork_{};
    std::chrono::steady_clock::time_point lastStorage_{};
};

bool TelemetryCollector::Initialize(const AppConfig& config) {
    config_ = config;
    snapshot_.network.uploadHistory.assign(60, 0.0);
    snapshot_.network.downloadHistory.assign(60, 0.0);
    WSADATA wsaData{};
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    const auto cpuName = wmi_.QueryFirstString(L"root\\cimv2", L"SELECT Name FROM Win32_Processor", L"Name");
    if (cpuName.has_value()) {
        snapshot_.cpu.name = *cpuName;
    }
    const auto gpuName = wmi_.QueryFirstString(L"root\\cimv2", L"SELECT Name FROM Win32_VideoController", L"Name");
    if (gpuName.has_value()) {
        snapshot_.gpu.name = *gpuName;
    }
    const auto gpuRam = wmi_.QueryFirstDouble(L"root\\cimv2", L"SELECT AdapterRAM FROM Win32_VideoController", L"AdapterRAM");
    if (gpuRam.has_value() && *gpuRam > 0.0) {
        snapshot_.gpu.vram.totalGb = *gpuRam / (1024.0 * 1024.0 * 1024.0);
    }

    PdhOpenQueryW(nullptr, 0, &cpuQuery_);
    AddCounterCompat(cpuQuery_, L"\\Processor Information(_Total)\\% Processor Utility", &cpuLoadCounter_);
    if (cpuLoadCounter_ == nullptr) {
        AddCounterCompat(cpuQuery_, L"\\Processor(_Total)\\% Processor Time", &cpuLoadCounter_);
    }
    AddCounterCompat(cpuQuery_, L"\\Processor Information(_Total)\\Processor Frequency", &cpuFrequencyCounter_);
    PdhCollectQueryData(cpuQuery_);

    PdhOpenQueryW(nullptr, 0, &gpuQuery_);
    AddCounterCompat(gpuQuery_, L"\\GPU Engine(*)\\Utilization Percentage", &gpuLoadCounter_);
    PdhCollectQueryData(gpuQuery_);

    PdhOpenQueryW(nullptr, 0, &gpuMemoryQuery_);
    AddCounterCompat(gpuMemoryQuery_, L"\\GPU Adapter Memory(*)\\Dedicated Usage", &gpuDedicatedCounter_);
    PdhCollectQueryData(gpuMemoryQuery_);

    EnumerateDrives();
    UpdateNetworkState(true);
    UpdateMemory();
    UpdateSensors();
    GetLocalTime(&snapshot_.now);
    return true;
}

const SystemSnapshot& TelemetryCollector::Snapshot() const {
    return snapshot_;
}

void TelemetryCollector::UpdateSnapshot() {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastFast_ >= std::chrono::milliseconds(750)) {
        UpdateCpu();
        UpdateGpu();
        lastFast_ = now;
    }
    if (now - lastNetwork_ >= std::chrono::milliseconds(500)) {
        UpdateNetworkState(false);
        lastNetwork_ = now;
    }
    if (now - lastSensors_ >= std::chrono::seconds(1)) {
        UpdateSensors();
        UpdateMemory();
        lastSensors_ = now;
    }
    if (now - lastStorage_ >= std::chrono::seconds(8)) {
        RefreshDriveUsage();
        lastStorage_ = now;
    }
    GetLocalTime(&snapshot_.now);
}

void TelemetryCollector::UpdateCpu() {
    if (cpuQuery_ == nullptr) {
        return;
    }
    PdhCollectQueryData(cpuQuery_);

    PDH_FMT_COUNTERVALUE value{};
    if (cpuLoadCounter_ != nullptr &&
        PdhGetFormattedCounterValue(cpuLoadCounter_, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
        snapshot_.cpu.loadPercent = std::clamp(value.doubleValue, 0.0, 100.0);
    }
    if (cpuFrequencyCounter_ != nullptr &&
        PdhGetFormattedCounterValue(cpuFrequencyCounter_, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
        snapshot_.cpu.clock.value = value.doubleValue / 1000.0;
        snapshot_.cpu.clock.unit = L"GHz";
    }
}

double TelemetryCollector::SumCounterArray(PDH_HCOUNTER counter, bool require3d) {
    if (counter == nullptr) {
        return 0.0;
    }

    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA) {
        return 0.0;
    }

    std::vector<BYTE> buffer(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS) {
        return 0.0;
    }

    double total = 0.0;
    for (DWORD i = 0; i < itemCount; ++i) {
        const std::wstring instance = items[i].szName != nullptr ? items[i].szName : L"";
        if (require3d && instance.find(L"engtype_3D") == std::wstring::npos) {
            continue;
        }
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS) {
            continue;
        }
        total += items[i].FmtValue.doubleValue;
    }
    return total;
}

void TelemetryCollector::UpdateGpu() {
    if (gpuQuery_ != nullptr) {
        PdhCollectQueryData(gpuQuery_);
        const double load3d = SumCounterArray(gpuLoadCounter_, true);
        const double loadAll = SumCounterArray(gpuLoadCounter_, false);
        snapshot_.gpu.loadPercent = std::clamp(load3d > 0.0 ? load3d : loadAll, 0.0, 100.0);
    }

    if (gpuMemoryQuery_ != nullptr) {
        PdhCollectQueryData(gpuMemoryQuery_);
        const double bytes = SumCounterArray(gpuDedicatedCounter_, false);
        snapshot_.gpu.vram.usedGb = bytes / (1024.0 * 1024.0 * 1024.0);
    }
}

void TelemetryCollector::UpdateMemory() {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        snapshot_.cpu.memory.totalGb = memory.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        snapshot_.cpu.memory.usedGb =
            (memory.ullTotalPhys - memory.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
}

std::optional<double> TelemetryCollector::QuerySensor(const std::wstring& key) {
    const auto found = config_.sensors.find(ToLower(key));
    if (found == config_.sensors.end() || !found->second.IsConfigured()) {
        return std::nullopt;
    }

    const auto& binding = found->second;
    for (const auto& ns : binding.namespaces) {
        const std::wstring query = L"SELECT " + binding.valueField + L" FROM Sensor WHERE " +
            binding.matchField + L"='" + EscapeWql(binding.matchValue) + L"'";
        const auto value = wmi_.QueryFirstDouble(ns, query, binding.valueField);
        if (value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

void TelemetryCollector::UpdateSensors() {
    snapshot_.cpu.temperature.value = QuerySensor(L"cpu_temp");
    snapshot_.cpu.power.value = QuerySensor(L"cpu_power");
    snapshot_.cpu.fan.value = QuerySensor(L"cpu_fan");
    if (!snapshot_.cpu.clock.value.has_value()) {
        snapshot_.cpu.clock.value = QuerySensor(L"cpu_clock");
    }

    snapshot_.gpu.temperature.value = QuerySensor(L"gpu_temp");
    snapshot_.gpu.power.value = QuerySensor(L"gpu_power");
    snapshot_.gpu.fan.value = QuerySensor(L"gpu_fan");
    if (const auto gpuClock = QuerySensor(L"gpu_clock"); gpuClock.has_value()) {
        snapshot_.gpu.clock.value = *gpuClock;
    }
}

void TelemetryCollector::EnumerateDrives() {
    for (const auto& drive : config_.driveLetters) {
        if (!drive.empty()) {
            snapshot_.drives.push_back(DriveInfo{drive.substr(0, 1) + L":"});
        }
    }
    RefreshDriveUsage();
}

void TelemetryCollector::RefreshDriveUsage() {
    for (auto& drive : snapshot_.drives) {
        const std::wstring root = drive.label + L"\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) {
            continue;
        }

        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER totalBytes{};
        if (!GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, nullptr) || totalBytes.QuadPart == 0) {
            continue;
        }

        const double totalGb = totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        const double freeGb = freeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        drive.freeGb = freeGb;
        drive.usedPercent = std::clamp((1.0 - (freeGb / totalGb)) * 100.0, 0.0, 100.0);
    }
}

void TelemetryCollector::PushHistory(std::vector<double>& history, double value) {
    if (history.empty()) {
        return;
    }
    history.erase(history.begin());
    history.push_back(value);
}

std::wstring TelemetryCollector::FindAdapterIp(ULONG interfaceIndex) {
    ULONG size = 0;
    GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, nullptr, &size);
    std::vector<BYTE> buffer(size);
    auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
            nullptr, addresses, &size) != NO_ERROR) {
        return L"N/A";
    }

    for (auto* current = addresses; current != nullptr; current = current->Next) {
        if (current->IfIndex != interfaceIndex) {
            continue;
        }
        for (auto* unicast = current->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            wchar_t address[128];
            DWORD length = ARRAYSIZE(address);
            if (WSAAddressToStringW(unicast->Address.lpSockaddr,
                    static_cast<DWORD>(unicast->Address.iSockaddrLength),
                    nullptr, address, &length) == 0) {
                std::wstring ip = address;
                if (ip.find(L':') == std::wstring::npos) {
                    return ip;
                }
            }
        }
    }

    return L"N/A";
}

void TelemetryCollector::UpdateNetworkState(bool initializeOnly) {
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || table == nullptr) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    MIB_IF_ROW2* selected = nullptr;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        auto& row = table->Table[i];
        const bool isCandidate =
            row.Type != IF_TYPE_SOFTWARE_LOOPBACK &&
            row.OperStatus == IfOperStatusUp &&
            (config_.networkAdapter.empty() ||
                ContainsInsensitive(row.Alias, config_.networkAdapter) ||
                ContainsInsensitive(row.Description, config_.networkAdapter));
        if (isCandidate) {
            selected = &row;
            break;
        }
    }

    if (selected != nullptr) {
        snapshot_.network.adapterName = selected->Alias[0] != L'\0' ? selected->Alias : selected->Description;
        if (selectedIndex_ != selected->InterfaceIndex) {
            selectedIndex_ = selected->InterfaceIndex;
            previousInOctets_ = selected->InOctets;
            previousOutOctets_ = selected->OutOctets;
            previousNetworkTick_ = now;
        } else if (!initializeOnly) {
            const double seconds = std::chrono::duration<double>(now - previousNetworkTick_).count();
            if (seconds > 0.0) {
                snapshot_.network.downloadMbps =
                    ((selected->InOctets - previousInOctets_) / seconds) / (1024.0 * 1024.0);
                snapshot_.network.uploadMbps =
                    ((selected->OutOctets - previousOutOctets_) / seconds) / (1024.0 * 1024.0);
                PushHistory(snapshot_.network.uploadHistory, snapshot_.network.uploadMbps);
                PushHistory(snapshot_.network.downloadHistory, snapshot_.network.downloadMbps);
            }
            previousInOctets_ = selected->InOctets;
            previousOutOctets_ = selected->OutOctets;
            previousNetworkTick_ = now;
        }

        snapshot_.network.ipAddress = FindAdapterIp(selected->InterfaceIndex);
    }

    FreeMibTable(table);
}

struct MonitorPlacementInfo {
    std::wstring deviceName;
    std::wstring monitorName = L"Unknown";
    std::wstring configMonitorName = L"";
    RECT monitorRect{};
    POINT relativePosition{};
};

struct MonitorIdentity {
    std::wstring displayName;
    std::wstring configName;
};

std::wstring SimplifyDeviceName(const std::wstring& deviceName) {
    if (deviceName.rfind(L"\\\\.\\", 0) == 0) {
        return deviceName.substr(4);
    }
    return deviceName;
}

bool IsUsefulFriendlyName(const std::wstring& name) {
    const std::wstring lowered = ToLower(name);
    return !name.empty() &&
        lowered != L"generic pnp monitor" &&
        lowered.find(L"\\\\?\\display") != 0;
}

MonitorIdentity GetMonitorIdentity(const std::wstring& deviceName);

std::optional<RECT> FindTargetMonitor(const std::wstring& requestedName) {
    if (requestedName.empty()) {
        return std::nullopt;
    }
    struct SearchContext {
        std::wstring requestedName;
        std::optional<RECT> result;
    } context{requestedName, std::nullopt};

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* context = reinterpret_cast<SearchContext*>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) {
                return TRUE;
            }

            const MonitorIdentity identity = GetMonitorIdentity(info.szDevice);
            if (ContainsInsensitive(identity.displayName, context->requestedName) ||
                ContainsInsensitive(identity.configName, context->requestedName) ||
                ContainsInsensitive(info.szDevice, context->requestedName)) {
                context->result = info.rcMonitor;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));

    return context.result;
}

MonitorIdentity GetMonitorIdentity(const std::wstring& deviceName) {
    MonitorIdentity identity;
    identity.displayName = SimplifyDeviceName(deviceName);
    identity.configName = deviceName;

    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return identity;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
        ERROR_SUCCESS) {
        return identity;
    }

    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(sourceName);
        sourceName.header.adapterId = paths[i].sourceInfo.adapterId;
        sourceName.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS) {
            continue;
        }

        if (_wcsicmp(sourceName.viewGdiDeviceName, deviceName.c_str()) != 0) {
            continue;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(targetName);
        targetName.header.adapterId = paths[i].targetInfo.adapterId;
        targetName.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&targetName.header) != ERROR_SUCCESS) {
            continue;
        }

        const std::wstring friendlyName = targetName.monitorFriendlyDeviceName;
        const std::wstring monitorPath = targetName.monitorDevicePath;
        if (IsUsefulFriendlyName(friendlyName)) {
            identity.displayName = friendlyName + L" (" + SimplifyDeviceName(deviceName) + L")";
            identity.configName = friendlyName;
        } else if (!monitorPath.empty()) {
            identity.displayName = SimplifyDeviceName(deviceName);
            identity.configName = monitorPath;
        } else {
            identity.displayName = SimplifyDeviceName(deviceName);
            identity.configName = deviceName;
        }
        return identity;
    }

    return identity;
}

MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd) {
    MonitorPlacementInfo info;
    RECT windowRect{};
    GetWindowRect(hwnd, &windowRect);

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        info.deviceName = monitorInfo.szDevice;
        const MonitorIdentity identity = GetMonitorIdentity(monitorInfo.szDevice);
        info.monitorName = identity.displayName;
        info.configMonitorName = identity.configName;
        info.monitorRect = monitorInfo.rcMonitor;
        info.relativePosition.x = windowRect.left - monitorInfo.rcMonitor.left;
        info.relativePosition.y = windowRect.top - monitorInfo.rcMonitor.top;
    }
    return info;
}

HFONT CreateUiFont(int height, int weight, const wchar_t* face) {
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, face);
}

void ShutdownPreviousInstance() {
    HWND existing = FindWindowW(kWindowClassName, nullptr);
    if (existing == nullptr) {
        return;
    }

    const DWORD existingProcessId = [&]() {
        DWORD processId = 0;
        GetWindowThreadProcessId(existing, &processId);
        return processId;
    }();

    if (existingProcessId == GetCurrentProcessId()) {
        return;
    }

    PostMessageW(existing, WM_CLOSE, 0, 0);
    for (int attempt = 0; attempt < 40; ++attempt) {
        Sleep(100);
        existing = FindWindowW(kWindowClassName, nullptr);
        if (existing == nullptr) {
            return;
        }
    }
}

std::filesystem::path GetRuntimeConfigPath() {
    return std::filesystem::current_path() / L"config.ini";
}

class DashboardApp {
public:
    bool Initialize(HINSTANCE instance);
    int Run();

private:
    static LRESULT CALLBACK WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();
    void ShowContextMenu(POINT screenPoint);
    void BringOnTop();
    void UpdateConfigFromCurrentPlacement();
    void StartMoveMode();
    void StopMoveMode();
    void UpdateMoveTracking();
    void DrawMoveOverlay(HDC hdc);
    bool CreateTrayIcon();
    void RemoveTrayIcon();

    void DrawTextBlock(HDC hdc, const RECT& rect, const std::wstring& text, HFONT font,
        COLORREF color, UINT format);
    void DrawPanel(HDC hdc, const RECT& rect, const std::wstring& title);
    POINT PolarPoint(int cx, int cy, int radius, double angleDegrees);
    void DrawGauge(HDC hdc, int cx, int cy, int radius, double percent, const std::wstring& label);
    void DrawMetricRow(HDC hdc, const RECT& rect, const std::wstring& label, const std::wstring& value, double ratio);
    void DrawProcessorPanel(HDC hdc, const RECT& rect, const ProcessorTelemetry& cpu);
    void DrawGpuPanel(HDC hdc, const RECT& rect, const GpuTelemetry& gpu);
    void DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue);
    void DrawNetworkPanel(HDC hdc, const RECT& rect, const NetworkTelemetry& network);
    void DrawStoragePanel(HDC hdc, const RECT& rect, const std::vector<DriveInfo>& drives);
    void DrawTimePanel(HDC hdc, const RECT& rect, const SYSTEMTIME& now);
    void DrawLayout(HDC hdc, const SystemSnapshot& snapshot);

    struct Fonts {
        HFONT title = nullptr;
        HFONT big = nullptr;
        HFONT value = nullptr;
        HFONT label = nullptr;
        HFONT smallFont = nullptr;
    } fonts_;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    AppConfig config_;
    TelemetryCollector telemetry_;
    bool isMoving_ = false;
    NOTIFYICONDATAW trayIcon_{};
    MonitorPlacementInfo movePlacementInfo_{};
};

bool DashboardApp::Initialize(HINSTANCE instance) {
    instance_ = instance;
    config_ = LoadConfig(GetRuntimeConfigPath());
    telemetry_.Initialize(config_);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = &DashboardApp::WndProcSetup;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBlack);
    if (!RegisterClassW(&wc)) {
        return false;
    }

    RECT placement{100, 100, 100 + kWindowWidth, 100 + kWindowHeight};
    if (const auto monitor = FindTargetMonitor(config_.monitorName); monitor.has_value()) {
        placement.left = monitor->left + config_.positionX;
        placement.top = monitor->top + config_.positionY;
    } else {
        placement.left = 100 + config_.positionX;
        placement.top = 100 + config_.positionY;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"System Telemetry",
        WS_POPUP,
        placement.left,
        placement.top,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        this);
    return hwnd_ != nullptr;
}

void DashboardApp::BringOnTop() {
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd_);
}

void DashboardApp::UpdateConfigFromCurrentPlacement() {
    const MonitorPlacementInfo placement = GetMonitorPlacementForWindow(hwnd_);
    const std::filesystem::path configPath = GetRuntimeConfigPath();
    const std::wstring monitorName = !placement.configMonitorName.empty()
        ? placement.configMonitorName
        : placement.deviceName;
    if (!SaveDisplayConfig(configPath, monitorName, placement.relativePosition.x, placement.relativePosition.y)) {
        const std::wstring message = L"Failed to update " + configPath.wstring() + L".";
        MessageBoxW(hwnd_, message.c_str(), L"System Telemetry", MB_ICONERROR);
        return;
    }

    config_.monitorName = monitorName;
    config_.positionX = placement.relativePosition.x;
    config_.positionY = placement.relativePosition.y;
}

bool DashboardApp::CreateTrayIcon() {
    trayIcon_ = {};
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = hwnd_;
    trayIcon_.uID = 1;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    trayIcon_.uCallbackMessage = kTrayMessage;
    trayIcon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(trayIcon_.szTip, L"System Telemetry");
    return Shell_NotifyIconW(NIM_ADD, &trayIcon_) == TRUE;
}

void DashboardApp::RemoveTrayIcon() {
    if (trayIcon_.cbSize != 0) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
    }
}

void DashboardApp::StartMoveMode() {
    isMoving_ = true;
    SetTimer(hwnd_, kMoveTimerId, kMoveTimerMs, nullptr);
    UpdateMoveTracking();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::StopMoveMode() {
    if (!isMoving_) {
        return;
    }
    isMoving_ = false;
    KillTimer(hwnd_, kMoveTimerId);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::UpdateMoveTracking() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return;
    }

    const int x = cursor.x - (kWindowWidth / 2);
    const int y = cursor.y - 24;
    SetWindowPos(hwnd_, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
}

void DashboardApp::ShowContextMenu(POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCommandMove, L"Move");
    AppendMenuW(menu, MF_STRING, kCommandBringOnTop, L"Bring On Top");
    AppendMenuW(menu, MF_STRING, kCommandUpdateConfig, L"Update Config");
    AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");
    SetForegroundWindow(hwnd_);
    const UINT selected = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x, screenPoint.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    switch (selected) {
    case kCommandMove:
        StartMoveMode();
        break;
    case kCommandBringOnTop:
        BringOnTop();
        break;
    case kCommandUpdateConfig:
        UpdateConfigFromCurrentPlacement();
        break;
    case kCommandExit:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

void DashboardApp::DrawMoveOverlay(HDC hdc) {
    RECT overlay{16, 16, 420, 112};
    HBRUSH fill = CreateSolidBrush(RGB(0, 0, 0));
    HPEN border = CreatePen(PS_SOLID, 1, kAccent);
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    HGDIOBJ oldPen = SelectObject(hdc, border);
    RoundRect(hdc, overlay.left, overlay.top, overlay.right, overlay.bottom, 14, 14);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(fill);
    DeleteObject(border);

    RECT titleRect{overlay.left + 12, overlay.top + 8, overlay.right - 12, overlay.top + 28};
    RECT monitorRect{overlay.left + 12, overlay.top + 34, overlay.right - 12, overlay.top + 56};
    RECT positionRect{overlay.left + 12, overlay.top + 58, overlay.right - 12, overlay.top + 80};
    RECT hintRect{overlay.left + 12, overlay.top + 82, overlay.right - 12, overlay.bottom - 12};

    wchar_t positionText[96];
    swprintf_s(positionText, L"Pos: x=%ld y=%ld", movePlacementInfo_.relativePosition.x, movePlacementInfo_.relativePosition.y);

    DrawTextBlock(hdc, titleRect, L"Move Mode", fonts_.label, kAccent, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, monitorRect, L"Monitor: " + movePlacementInfo_.monitorName, fonts_.smallFont, kWhite,
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextBlock(hdc, positionRect, positionText, fonts_.smallFont, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, hintRect, L"Left-click to place. Copy monitor name and x/y into config.", fonts_.smallFont,
        kMuted, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
}

int DashboardApp::Run() {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK DashboardApp::WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* app = static_cast<DashboardApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&DashboardApp::WndProcThunk));
        app->hwnd_ = hwnd;
        return app->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK DashboardApp::WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<DashboardApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return app != nullptr ? app->HandleMessage(message, wParam, lParam)
                          : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT DashboardApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        fonts_.title = CreateUiFont(18, FW_BOLD, L"Segoe UI Semibold");
        fonts_.big = CreateUiFont(40, FW_BOLD, L"Segoe UI Semibold");
        fonts_.value = CreateUiFont(17, FW_BOLD, L"Segoe UI Semibold");
        fonts_.label = CreateUiFont(14, FW_NORMAL, L"Segoe UI");
        fonts_.smallFont = CreateUiFont(12, FW_NORMAL, L"Segoe UI");
        SetTimer(hwnd_, kRefreshTimerId, kRefreshTimerMs, nullptr);
        CreateTrayIcon();
        return 0;
    case WM_TIMER:
        if (wParam == kMoveTimerId) {
            UpdateMoveTracking();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        telemetry_.UpdateSnapshot();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_CONTEXTMENU: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (point.x == -1 && point.y == -1) {
            RECT rect{};
            GetWindowRect(hwnd_, &rect);
            point.x = rect.left + 24;
            point.y = rect.top + 24;
        }
        if (isMoving_) {
            StopMoveMode();
        }
        ShowContextMenu(point);
        return 0;
    }
    case WM_LBUTTONUP:
        if (isMoving_) {
            StopMoveMode();
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && isMoving_) {
            StopMoveMode();
            return 0;
        }
        break;
    case kTrayMessage:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT point{};
            GetCursorPos(&point);
            ShowContextMenu(point);
            return 0;
        }
        break;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(hwnd_, kRefreshTimerId);
        KillTimer(hwnd_, kMoveTimerId);
        RemoveTrayIcon();
        DeleteObject(fonts_.title);
        DeleteObject(fonts_.big);
        DeleteObject(fonts_.value);
        DeleteObject(fonts_.label);
        DeleteObject(fonts_.smallFont);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void DashboardApp::Paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);

    HDC memDc = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

    HBRUSH background = CreateSolidBrush(kBlack);
    FillRect(memDc, &client, background);
    DeleteObject(background);
    SetBkMode(memDc, TRANSPARENT);

    DrawLayout(memDc, telemetry_.Snapshot());
    if (isMoving_) {
        DrawMoveOverlay(memDc);
    }

    BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    EndPaint(hwnd_, &ps);
}

void DashboardApp::DrawTextBlock(HDC hdc, const RECT& rect, const std::wstring& text, HFONT font,
    COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    RECT copy = rect;
    DrawTextW(hdc, text.c_str(), -1, &copy, format);
    SelectObject(hdc, oldFont);
}

void DashboardApp::DrawPanel(HDC hdc, const RECT& rect, const std::wstring& title) {
    HPEN border = CreatePen(PS_SOLID, 1, kPanelBorder);
    HBRUSH fill = CreateSolidBrush(RGB(6, 8, 11));
    HGDIOBJ oldPen = SelectObject(hdc, border);
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 18, 18);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(fill);
    DeleteObject(border);

    RECT titleRect = rect;
    titleRect.left += 14;
    titleRect.top += 8;
    titleRect.right -= 14;
    titleRect.bottom = titleRect.top + 24;
    DrawTextBlock(hdc, titleRect, title, fonts_.title, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

POINT DashboardApp::PolarPoint(int cx, int cy, int radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return POINT{
        cx + static_cast<LONG>(std::round(std::cos(radians) * radius)),
        cy - static_cast<LONG>(std::round(std::sin(radians) * radius))
    };
}

void DashboardApp::DrawGauge(HDC hdc, int cx, int cy, int radius, double percent, const std::wstring& label) {
    HPEN trackPen = CreatePen(PS_SOLID, 10, kTrack);
    HPEN accentPen = CreatePen(PS_SOLID, 10, kAccent);
    HGDIOBJ oldPen = SelectObject(hdc, trackPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    const RECT bounds{cx - radius, cy - radius, cx + radius, cy + radius};
    const POINT startTrack = PolarPoint(cx, cy, radius, 135.0);
    const POINT endTrack = PolarPoint(cx, cy, radius, -135.0);
    Arc(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom,
        startTrack.x, startTrack.y, endTrack.x, endTrack.y);

    SelectObject(hdc, accentPen);
    const double sweep = 270.0 * std::clamp(percent, 0.0, 100.0) / 100.0;
    const POINT endValue = PolarPoint(cx, cy, radius, 135.0 - sweep);
    Arc(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom,
        startTrack.x, startTrack.y, endValue.x, endValue.y);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(trackPen);
    DeleteObject(accentPen);

    RECT numberRect{cx - 42, cy - 28, cx + 42, cy + 18};
    wchar_t number[16];
    swprintf_s(number, L"%.0f%%", percent);
    DrawTextBlock(hdc, numberRect, number, fonts_.big, kWhite, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    RECT labelRect{cx - 42, cy + 18, cx + 42, cy + 42};
    DrawTextBlock(hdc, labelRect, label, fonts_.smallFont, kMuted, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void DashboardApp::DrawMetricRow(
    HDC hdc, const RECT& rect, const std::wstring& label, const std::wstring& value, double ratio) {
    RECT labelRect{rect.left, rect.top, rect.left + 74, rect.bottom};
    RECT valueRect{rect.left + 82, rect.top, rect.right, rect.bottom};
    DrawTextBlock(hdc, labelRect, label, fonts_.label, kMuted, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, valueRect, value, fonts_.value, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT barRect{rect.left + 82, rect.bottom - 5, rect.right, rect.bottom - 1};
    HBRUSH track = CreateSolidBrush(kTrack);
    FillRect(hdc, &barRect, track);
    DeleteObject(track);

    RECT fill = barRect;
    fill.right = fill.left + static_cast<int>((fill.right - fill.left) * std::clamp(ratio, 0.0, 1.0));
    HBRUSH accent = CreateSolidBrush(kAccent);
    FillRect(hdc, &fill, accent);
    DeleteObject(accent);
}

void DashboardApp::DrawProcessorPanel(HDC hdc, const RECT& rect, const ProcessorTelemetry& cpu) {
    DrawPanel(hdc, rect, L"CPU");
    RECT nameRect{rect.left + 16, rect.top + 34, rect.right - 16, rect.top + 58};
    DrawTextBlock(hdc, nameRect, cpu.name, fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawGauge(hdc, rect.left + 92, rect.top + 132, 52, cpu.loadPercent, L"Load");

    int y = rect.top + 76;
    const int rowHeight = 34;
    RECT rows{rect.left + 164, y, rect.right - 18, y + rowHeight};
    DrawMetricRow(hdc, rows, L"Temp", FormatValue(cpu.temperature, 0), cpu.temperature.value.value_or(0.0) / 100.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Power", FormatValue(cpu.power, 1), cpu.power.value.value_or(0.0) / 150.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Clock", FormatValue(cpu.clock, 2), cpu.clock.value.value_or(0.0) / 5.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Fan", FormatValue(cpu.fan, 0), cpu.fan.value.value_or(0.0) / 4000.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"RAM", FormatMemory(cpu.memory.usedGb, cpu.memory.totalGb),
        cpu.memory.totalGb > 0.0 ? cpu.memory.usedGb / cpu.memory.totalGb : 0.0);
}

void DashboardApp::DrawGpuPanel(HDC hdc, const RECT& rect, const GpuTelemetry& gpu) {
    DrawPanel(hdc, rect, L"GPU");
    RECT nameRect{rect.left + 16, rect.top + 34, rect.right - 16, rect.top + 58};
    DrawTextBlock(hdc, nameRect, gpu.name, fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawGauge(hdc, rect.left + 92, rect.top + 132, 52, gpu.loadPercent, L"Load");

    int y = rect.top + 76;
    const int rowHeight = 34;
    RECT rows{rect.left + 164, y, rect.right - 18, y + rowHeight};
    DrawMetricRow(hdc, rows, L"Temp", FormatValue(gpu.temperature, 0), gpu.temperature.value.value_or(0.0) / 100.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Power", FormatValue(gpu.power, 1), gpu.power.value.value_or(0.0) / 350.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Clock", FormatValue(gpu.clock, 0), gpu.clock.value.value_or(0.0) / 2600.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Fan", FormatValue(gpu.fan, 0), gpu.fan.value.value_or(0.0) / 3000.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"VRAM", FormatMemory(gpu.vram.usedGb, std::max(1.0, gpu.vram.totalGb)),
        gpu.vram.totalGb > 0.0 ? gpu.vram.usedGb / gpu.vram.totalGb : 0.0);
}

void DashboardApp::DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue) {
    HBRUSH bg = CreateSolidBrush(RGB(10, 12, 15));
    FillRect(hdc, &rect, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 2, kAccent);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    const int width = std::max<int>(1, rect.right - rect.left - 1);
    const int height = std::max<int>(1, rect.bottom - rect.top - 1);
    for (size_t i = 1; i < history.size(); ++i) {
        const double v1 = std::clamp(history[i - 1] / maxValue, 0.0, 1.0);
        const double v2 = std::clamp(history[i] / maxValue, 0.0, 1.0);
        const int x1 = rect.left + static_cast<int>((i - 1) * width / std::max<size_t>(1, history.size() - 1));
        const int x2 = rect.left + static_cast<int>(i * width / std::max<size_t>(1, history.size() - 1));
        const int y1 = rect.bottom - 1 - static_cast<int>(v1 * height);
        const int y2 = rect.bottom - 1 - static_cast<int>(v2 * height);
        MoveToEx(hdc, x1, y1, nullptr);
        LineTo(hdc, x2, y2);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DashboardApp::DrawNetworkPanel(HDC hdc, const RECT& rect, const NetworkTelemetry& network) {
    DrawPanel(hdc, rect, L"Network");
    RECT upRect{rect.left + 16, rect.top + 38, rect.right - 16, rect.top + 62};
    RECT downRect{rect.left + 16, rect.top + 64, rect.right - 16, rect.top + 88};
    DrawTextBlock(hdc, upRect, L"Up   " + FormatSpeed(network.uploadMbps), fonts_.value, kWhite,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, downRect, L"Down " + FormatSpeed(network.downloadMbps), fonts_.value, kWhite,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const double maxGraph = std::max({10.0, network.uploadMbps * 1.5, network.downloadMbps * 1.5});
    RECT uploadGraph{rect.left + 16, rect.top + 98, rect.right - 16, rect.top + 128};
    RECT downloadGraph{rect.left + 16, rect.top + 136, rect.right - 16, rect.top + 166};
    DrawGraph(hdc, uploadGraph, network.uploadHistory, maxGraph);
    DrawGraph(hdc, downloadGraph, network.downloadHistory, maxGraph);

    RECT nameRect{rect.left + 16, rect.bottom - 46, rect.right - 16, rect.bottom - 24};
    RECT ipRect{rect.left + 16, rect.bottom - 24, rect.right - 16, rect.bottom - 6};
    DrawTextBlock(hdc, nameRect, network.adapterName, fonts_.smallFont, kMuted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextBlock(hdc, ipRect, network.ipAddress, fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DashboardApp::DrawStoragePanel(HDC hdc, const RECT& rect, const std::vector<DriveInfo>& drives) {
    DrawPanel(hdc, rect, L"Storage");
    int y = rect.top + 42;
    for (const auto& drive : drives) {
        RECT labelRect{rect.left + 16, y, rect.left + 42, y + 20};
        RECT pctRect{rect.right - 140, y, rect.right - 94, y + 20};
        RECT freeRect{rect.right - 92, y, rect.right - 16, y + 20};
        RECT barRect{rect.left + 48, y + 4, rect.right - 150, y + 16};

        DrawTextBlock(hdc, labelRect, drive.label, fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        HBRUSH track = CreateSolidBrush(kTrack);
        FillRect(hdc, &barRect, track);
        DeleteObject(track);

        RECT fill = barRect;
        fill.right = fill.left + static_cast<int>((fill.right - fill.left) * (drive.usedPercent / 100.0));
        HBRUSH accent = CreateSolidBrush(kAccent);
        FillRect(hdc, &fill, accent);
        DeleteObject(accent);

        wchar_t percent[16];
        swprintf_s(percent, L"%.0f%%", drive.usedPercent);
        DrawTextBlock(hdc, pctRect, percent, fonts_.label, kWhite, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        DrawTextBlock(hdc, freeRect, FormatDriveFree(drive.freeGb), fonts_.smallFont, kMuted,
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        y += 34;
    }
}

void DashboardApp::DrawTimePanel(HDC hdc, const RECT& rect, const SYSTEMTIME& now) {
    DrawPanel(hdc, rect, L"Time");
    wchar_t timeBuffer[32];
    wchar_t dateBuffer[32];
    swprintf_s(timeBuffer, L"%02d:%02d", now.wHour, now.wMinute);
    swprintf_s(dateBuffer, L"%04d-%02d-%02d", now.wYear, now.wMonth, now.wDay);

    RECT timeRect{rect.left + 16, rect.top + 46, rect.right - 16, rect.top + 116};
    RECT dateRect{rect.left + 16, rect.top + 120, rect.right - 16, rect.top + 148};
    DrawTextBlock(hdc, timeRect, timeBuffer, fonts_.big, kWhite, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, dateRect, dateBuffer, fonts_.value, kMuted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
}

void DashboardApp::DrawLayout(HDC hdc, const SystemSnapshot& snapshot) {
    const RECT cpuRect{10, 10, 395, 270};
    const RECT gpuRect{405, 10, 790, 270};
    const RECT networkRect{10, 280, 210, 470};
    const RECT storageRect{220, 280, 618, 470};
    const RECT timeRect{628, 280, 790, 470};

    DrawProcessorPanel(hdc, cpuRect, snapshot.cpu);
    DrawGpuPanel(hdc, gpuRect, snapshot.gpu);
    DrawNetworkPanel(hdc, networkRect, snapshot.network);
    DrawStoragePanel(hdc, storageRect, snapshot.drives);
    DrawTimePanel(hdc, timeRect, snapshot.now);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    ShutdownPreviousInstance();

    DashboardApp app;
    if (!app.Initialize(instance)) {
        MessageBoxW(nullptr, L"Failed to initialize the telemetry dashboard.", L"System Telemetry", MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
