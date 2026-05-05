#include "telemetry/fps/fps_etw_provider.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <evntcons.h>
#include <evntrace.h>
#include <mutex>
#include <optional>
#include <pdhmsg.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "telemetry/impl/collector_support.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

constexpr GUID kDxgiProviderGuid = {0xca11c036, 0x0102, 0x4a2d, {0xa6, 0xad, 0xf0, 0x3c, 0xfe, 0xd5, 0xdc, 0x9c}};
constexpr GUID kD3d9ProviderGuid = {0x783aca0a, 0x790e, 0x4d7f, {0x84, 0x51, 0xaa, 0x85, 0x05, 0x11, 0xc6, 0xb9}};
constexpr GUID kDxgKrnlProviderGuid = {0x802ec45a, 0x1e99, 0x4b83, {0x99, 0x20, 0x87, 0xc9, 0x82, 0x77, 0xba, 0x9d}};
constexpr UCHAR kEtwTraceLevel = TRACE_LEVEL_VERBOSE;
constexpr uint64_t kRuntimePresentKeyword = 0x8000000000000002ull;
constexpr uint64_t kDxgKrnlPresentKeyword = 0x4000000008000001ull;
constexpr USHORT kDxgiPresentStartEventId = 0x002a;
constexpr USHORT kDxgiPresentMultiplaneOverlayStartEventId = 0x0037;
constexpr USHORT kD3d9PresentStartEventId = 0x0001;
constexpr USHORT kDxgKrnlPresentInfoEventId = 0x00b8;
constexpr double kFpsWindowSeconds = 3.0;
constexpr double kFpsSmoothingAlpha = 0.35;
constexpr double kProcessSwitchHysteresisRatio = 1.35;
constexpr double kGpu3dActiveThresholdPercent = 5.0;
constexpr double kGpu3dDominanceRatio = 3.0;
constexpr size_t kMaximumEventsPerProcess = 4096;

struct EtwSessionProperties {
    EVENT_TRACE_PROPERTIES properties{};
    wchar_t loggerName[MAX_PATH]{};
};

enum class PresentEventSource {
    Runtime,
    Kernel,
};

const char* PresentEventSourceName(PresentEventSource source) {
    return source == PresentEventSource::Runtime ? "runtime" : "dxgkrnl";
}

std::string Win32ErrorText(ULONG status) {
    char message[256]{};
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        status,
        0,
        message,
        static_cast<DWORD>(std::size(message)),
        nullptr);
    std::string text = std::to_string(static_cast<unsigned long>(status));
    if (length > 0) {
        size_t trimmedLength = length;
        while (trimmedLength > 0 && (message[trimmedLength - 1] == '\r' || message[trimmedLength - 1] == '\n')) {
            message[trimmedLength - 1] = '\0';
            --trimmedLength;
        }
        text += " (";
        text += message;
        text += ")";
    }
    return text;
}

bool IsPermissionDenied(ULONG status) {
    return status == ERROR_ACCESS_DENIED;
}

std::wstring BuildSessionName() {
    return L"CaseDashPresentedFps-" + std::to_wstring(GetCurrentProcessId());
}

std::wstring LowerAscii(std::wstring value) {
    for (wchar_t& ch : value) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return value;
}

std::wstring BaseName(std::wstring path) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.erase(0, slash + 1);
    }
    return path;
}

std::wstring CleanProcessDisplayName(std::wstring processName) {
    processName = BaseName(processName);
    const size_t dot = processName.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        processName.erase(dot);
    }
    return LowerAscii(processName);
}

std::wstring QueryProcessBaseName(DWORD processId, bool& permissionRequired) {
    permissionRequired = false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        permissionRequired = GetLastError() == ERROR_ACCESS_DENIED;
        return {};
    }

    wchar_t path[MAX_PATH]{};
    DWORD pathLength = static_cast<DWORD>(std::size(path));
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &pathLength);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process);
    permissionRequired = error == ERROR_ACCESS_DENIED;
    return ok ? CleanProcessDisplayName(std::wstring(path, pathLength)) : std::wstring{};
}

bool IsExcludedProcessName(const std::wstring& processName) {
    const std::wstring lowerName = LowerAscii(processName);
    return lowerName.empty() || lowerName == L"casedash" || lowerName == L"dwm";
}

DWORD ExtractProcessIdFromGpuEngineInstance(const wchar_t* instance) {
    if (instance == nullptr) {
        return 0;
    }
    const wchar_t* marker = wcsstr(instance, L"pid_");
    if (marker == nullptr) {
        return 0;
    }
    marker += 4;
    wchar_t* end = nullptr;
    const unsigned long value = wcstoul(marker, &end, 10);
    return end != marker ? static_cast<DWORD>(value) : 0;
}

bool IsGpu3dEngineInstance(const wchar_t* instance) {
    return instance != nullptr && wcsstr(instance, L"engtype_3D") != nullptr;
}

class PresentedFpsEtwProvider final : public FpsTelemetryProvider {
public:
    explicit PresentedFpsEtwProvider(Trace& trace) : trace_(trace) {}

    ~PresentedFpsEtwProvider() override {
        Stop();
    }

    bool Initialize() override {
        std::lock_guard lock(mutex_);
        if (initialized_) {
            return true;
        }

        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
            diagnostics_ = "QueryPerformanceFrequency failed.";
            trace_.Write("fps_etw:initialize_failed diagnostics=\"" + diagnostics_ + "\"");
            return false;
        }
        qpcFrequency_ = frequency.QuadPart;

        sessionName_ = BuildSessionName();
        EtwSessionProperties sessionProps{};
        sessionProps.properties.Wnode.BufferSize = sizeof(sessionProps);
        sessionProps.properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        sessionProps.properties.Wnode.ClientContext = 1;
        sessionProps.properties.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        sessionProps.properties.LoggerNameOffset = offsetof(EtwSessionProperties, loggerName);
        sessionProps.properties.BufferSize = 64;
        sessionProps.properties.MinimumBuffers = 4;
        sessionProps.properties.MaximumBuffers = 16;

        ULONG status = StartTraceW(&sessionHandle_, sessionName_.c_str(), &sessionProps.properties);
        trace_.Write("fps_etw:start_trace status=" + Win32ErrorText(status));
        if (status == ERROR_ALREADY_EXISTS) {
            ControlTraceW(0, sessionName_.c_str(), &sessionProps.properties, EVENT_TRACE_CONTROL_STOP);
            status = StartTraceW(&sessionHandle_, sessionName_.c_str(), &sessionProps.properties);
            trace_.Write("fps_etw:start_trace_retry status=" + Win32ErrorText(status));
        }
        if (status != ERROR_SUCCESS) {
            permissionRequired_ = IsPermissionDenied(status);
            diagnostics_ = "Failed to start FPS ETW session: " + Win32ErrorText(status);
            return false;
        }

        const ULONG dxgiStatus = EnableProvider(kDxgiProviderGuid, kRuntimePresentKeyword, kEtwTraceLevel);
        const ULONG d3d9Status = EnableProvider(kD3d9ProviderGuid, kRuntimePresentKeyword, kEtwTraceLevel);
        const ULONG dxgkrnlStatus =
            EnableProvider(kDxgKrnlProviderGuid, kDxgKrnlPresentKeyword, TRACE_LEVEL_INFORMATION);
        dxgiEnabled_ = dxgiStatus == ERROR_SUCCESS;
        d3d9Enabled_ = d3d9Status == ERROR_SUCCESS;
        dxgkrnlEnabled_ = dxgkrnlStatus == ERROR_SUCCESS;
        trace_.Write("fps_etw:enable dxgi=" + Win32ErrorText(dxgiStatus) + " d3d9=" + Win32ErrorText(d3d9Status) +
                     " dxgkrnl=" + Win32ErrorText(dxgkrnlStatus));
        if (!dxgiEnabled_ && !d3d9Enabled_ && !dxgkrnlEnabled_) {
            permissionRequired_ =
                IsPermissionDenied(dxgiStatus) || IsPermissionDenied(d3d9Status) || IsPermissionDenied(dxgkrnlStatus);
            diagnostics_ = "Failed to enable FPS ETW providers: dxgi=" + Win32ErrorText(dxgiStatus) +
                           " d3d9=" + Win32ErrorText(d3d9Status) + " dxgkrnl=" + Win32ErrorText(dxgkrnlStatus);
            StopLocked();
            return false;
        }

        EVENT_TRACE_LOGFILEW traceLog{};
        traceLog.LoggerName = sessionName_.data();
        traceLog.ProcessTraceMode =
            PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
        traceLog.EventRecordCallback = &PresentedFpsEtwProvider::OnEventRecord;
        traceLog.Context = this;
        traceHandle_ = OpenTraceW(&traceLog);
        if (traceHandle_ == INVALID_PROCESSTRACE_HANDLE) {
            diagnostics_ = "Failed to open FPS ETW trace: " + Win32ErrorText(GetLastError());
            StopLocked();
            return false;
        }

        processingThread_ = CreateThread(nullptr, 0, &PresentedFpsEtwProvider::ProcessTraceThread, this, 0, nullptr);
        if (processingThread_ == nullptr) {
            diagnostics_ = "Failed to create FPS ETW processing thread: " + Win32ErrorText(GetLastError());
            StopLocked();
            return false;
        }

        diagnostics_ = "Presented FPS ETW provider active.";
        permissionRequired_ = false;
        initialized_ = true;
        trace_.Write("fps_etw:initialize_done dxgi=" + Trace::BoolText(dxgiEnabled_) +
                     " d3d9=" + Trace::BoolText(d3d9Enabled_) + " dxgkrnl=" + Trace::BoolText(dxgkrnlEnabled_));
        return true;
    }

    FpsTelemetrySample Sample() override {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);

        std::lock_guard lock(mutex_);
        FpsTelemetrySample sample;
        sample.diagnostics = diagnostics_;
        sample.available = initialized_;
        sample.permissionRequired = permissionRequired_;
        if (!initialized_ || qpcFrequency_ <= 0) {
            return sample;
        }

        const uint64_t nowQpc = static_cast<uint64_t>(now.QuadPart);
        const uint64_t windowTicks = static_cast<uint64_t>(static_cast<double>(qpcFrequency_) * kFpsWindowSeconds);
        const uint64_t minimumQpc = nowQpc > windowTicks ? nowQpc - windowTicks : 0;

        UpdateGpu3dUsageLocked();
        const ProcessEventSelection runtimeSelection = SelectBestProcessLocked(runtimeEventsByProcess_, minimumQpc);
        const ProcessEventSelection kernelSelection = SelectBestProcessLocked(kernelEventsByProcess_, minimumQpc);
        ProcessEventSelection bestSelection = SelectBestSourceLocked(runtimeSelection, kernelSelection);
        bestSelection = ApplyProcessHysteresisLocked(bestSelection, minimumQpc);

        const std::optional<FpsTelemetrySample> blockedByGpuSelection =
            BuildUnavailableDominantGpuSampleLocked(bestSelection);
        if (blockedByGpuSelection.has_value()) {
            ResetSelectionLocked();
            return *blockedByGpuSelection;
        }

        if (bestSelection.processId == 0 || bestSelection.count < 2) {
            ResetSelectionLocked();
            sample.available = false;
            sample.diagnostics = BuildDiagnosticsLocked(" No presenting application selected." + GpuUsageDiagnostics());
            return sample;
        }

        const double rawFps = static_cast<double>(bestSelection.count) / kFpsWindowSeconds;
        const double fps = SmoothFpsLocked(rawFps, bestSelection);
        sample.processId = bestSelection.processId;
        sample.processName = Utf8FromWide(ResolveProcessNameLocked(bestSelection.processId));
        sample.permissionRequired = IsProcessNamePermissionRequiredLocked(bestSelection.processId);
        sample.available = true;
        sample.fps = fps;
        sample.diagnostics = BuildDiagnosticsLocked(
            " process=" + sample.processName + " source=" + PresentEventSourceName(bestSelection.source) +
            " window_count=" + std::to_string(bestSelection.count) + " " +
            Trace::FormatValueDouble("raw_fps", rawFps, 1) + " " + Trace::FormatValueDouble("smoothed_fps", fps, 1) +
            GpuUsageDiagnostics());
        return sample;
    }

private:
    struct ProcessEventSelection {
        DWORD processId = 0;
        size_t count = 0;
        PresentEventSource source = PresentEventSource::Runtime;
    };

    struct ProcessPresentEvents {
        DWORD processId = 0;
        std::vector<uint64_t> events;
        size_t firstEvent = 0;
    };

    struct ProcessNameCacheEntry {
        DWORD processId = 0;
        std::wstring name;
        bool permissionRequired = false;
    };

    struct ProcessGpuUsage {
        DWORD processId = 0;
        double usage = 0.0;
    };

    using ProcessPresentEventBuckets = std::vector<ProcessPresentEvents>;

    ProcessPresentEvents* FindProcessPresentEvents(ProcessPresentEventBuckets& buckets, DWORD processId) {
        for (ProcessPresentEvents& bucket : buckets) {
            if (bucket.processId == processId) {
                return &bucket;
            }
        }
        return nullptr;
    }

    ProcessPresentEvents& EnsureProcessPresentEvents(ProcessPresentEventBuckets& buckets, DWORD processId) {
        if (ProcessPresentEvents* bucket = FindProcessPresentEvents(buckets, processId); bucket != nullptr) {
            return *bucket;
        }
        buckets.push_back(ProcessPresentEvents{processId, {}});
        return buckets.back();
    }

    size_t PresentEventCount(const ProcessPresentEvents& bucket) const {
        return bucket.firstEvent <= bucket.events.size() ? bucket.events.size() - bucket.firstEvent : 0;
    }

    void CompactPresentEvents(ProcessPresentEvents& bucket) {
        if (bucket.firstEvent == 0) {
            return;
        }
        if (bucket.firstEvent >= bucket.events.size()) {
            bucket.events.clear();
            bucket.firstEvent = 0;
            return;
        }
        if (bucket.firstEvent >= 256 && bucket.firstEvent * 2 >= bucket.events.size()) {
            bucket.events.erase(
                bucket.events.begin(), bucket.events.begin() + static_cast<std::ptrdiff_t>(bucket.firstEvent));
            bucket.firstEvent = 0;
        }
    }

    void TrimPresentEventsBefore(ProcessPresentEvents& bucket, uint64_t minimumQpc) {
        while (bucket.firstEvent < bucket.events.size() && bucket.events[bucket.firstEvent] < minimumQpc) {
            ++bucket.firstEvent;
        }
        CompactPresentEvents(bucket);
    }

    void PushPresentEvent(ProcessPresentEvents& bucket, uint64_t qpc) {
        if (PresentEventCount(bucket) >= kMaximumEventsPerProcess) {
            ++bucket.firstEvent;
        }
        bucket.events.push_back(qpc);
        CompactPresentEvents(bucket);
    }

    ProcessEventSelection SelectBestProcessLocked(ProcessPresentEventBuckets& eventsByProcess, uint64_t minimumQpc) {
        ProcessEventSelection selection;
        selection.source =
            &eventsByProcess == &runtimeEventsByProcess_ ? PresentEventSource::Runtime : PresentEventSource::Kernel;

        for (auto it = eventsByProcess.begin(); it != eventsByProcess.end();) {
            TrimPresentEventsBefore(*it, minimumQpc);
            const size_t eventCount = PresentEventCount(*it);
            if (eventCount == 0) {
                EraseProcessNameCache(it->processId);
                it = eventsByProcess.erase(it);
                continue;
            }

            const std::wstring processName = ResolveProcessNameLocked(it->processId);
            if (!IsExcludedProcessName(processName) && IsBetterSelectionLocked(it->processId, eventCount, selection)) {
                selection.processId = it->processId;
                selection.count = eventCount;
            }
            ++it;
        }

        return selection;
    }

    ProcessEventSelection SelectBestSourceLocked(
        const ProcessEventSelection& runtimeSelection, const ProcessEventSelection& kernelSelection) const {
        if (runtimeSelection.processId == 0) {
            return kernelSelection;
        }
        if (kernelSelection.processId == 0) {
            return runtimeSelection;
        }

        const double kernelGpu3d = Gpu3dUsageForProcess(kernelSelection.processId);
        const double runtimeGpu3d = Gpu3dUsageForProcess(runtimeSelection.processId);
        if (kernelGpu3d >= kGpu3dActiveThresholdPercent &&
            kernelGpu3d >= (std::max)(runtimeGpu3d, 0.1) * kGpu3dDominanceRatio) {
            return kernelSelection;
        }
        return runtimeSelection;
    }

    bool IsBetterSelectionLocked(DWORD processId, size_t count, const ProcessEventSelection& current) {
        if (current.processId == 0) {
            return true;
        }

        const double candidateGpu3d = Gpu3dUsageForProcess(processId);
        const double currentGpu3d = Gpu3dUsageForProcess(current.processId);
        if (candidateGpu3d >= kGpu3dActiveThresholdPercent &&
            candidateGpu3d >= (std::max)(currentGpu3d, 0.1) * kGpu3dDominanceRatio) {
            return true;
        }
        if (currentGpu3d >= kGpu3dActiveThresholdPercent &&
            currentGpu3d >= (std::max)(candidateGpu3d, 0.1) * kGpu3dDominanceRatio) {
            return false;
        }
        return count > current.count;
    }

    std::optional<FpsTelemetrySample> BuildUnavailableDominantGpuSampleLocked(const ProcessEventSelection& selected) {
        if (topGpu3dProcessId_ == 0 || topGpu3dUsage_ < kGpu3dActiveThresholdPercent) {
            return std::nullopt;
        }

        const double selectedGpu3d = Gpu3dUsageForProcess(selected.processId);
        if (topGpu3dProcessId_ != selected.processId &&
            topGpu3dUsage_ < (std::max)(selectedGpu3d, 0.1) * kGpu3dDominanceRatio) {
            return std::nullopt;
        }
        if (topGpu3dProcessId_ == selected.processId && selected.count >= 2) {
            return std::nullopt;
        }

        FpsTelemetrySample sample;
        sample.processId = topGpu3dProcessId_;
        sample.processName = Utf8FromWide(ResolveProcessNameLocked(topGpu3dProcessId_));
        sample.available = false;
        sample.permissionRequired = IsProcessNamePermissionRequiredLocked(topGpu3dProcessId_);
        sample.diagnostics = BuildDiagnosticsLocked(
            " top GPU 3D application has no matching present events. process=" + sample.processName +
            " selected_process=" + Utf8FromWide(ResolveProcessNameLocked(selected.processId)) + " selected_source=" +
            PresentEventSourceName(selected.source) + " selected_window_count=" + std::to_string(selected.count) +
            GpuUsageDiagnosticsForProcess(selected.processId));
        return sample;
    }

    ProcessEventSelection ApplyProcessHysteresisLocked(const ProcessEventSelection& candidate, uint64_t minimumQpc) {
        if (selectedProcessId_ == 0 || candidate.processId == selectedProcessId_) {
            return candidate;
        }

        ProcessPresentEventBuckets& previousEvents =
            selectedSource_ == PresentEventSource::Runtime ? runtimeEventsByProcess_ : kernelEventsByProcess_;
        ProcessPresentEvents* previousBucket = FindProcessPresentEvents(previousEvents, selectedProcessId_);
        if (previousBucket == nullptr) {
            return candidate;
        }

        TrimPresentEventsBefore(*previousBucket, minimumQpc);
        const size_t previousCount = PresentEventCount(*previousBucket);
        if (previousCount < 2) {
            return candidate;
        }

        ProcessEventSelection previous;
        previous.processId = selectedProcessId_;
        previous.count = previousCount;
        previous.source = selectedSource_;
        // Preserve the active presenter through short ETW delivery bursts and near-ties between presenting apps.
        if (candidate.processId == 0 || static_cast<double>(candidate.count) <
                                            static_cast<double>(previous.count) * kProcessSwitchHysteresisRatio) {
            return previous;
        }
        return candidate;
    }

    double SmoothFpsLocked(double rawFps, const ProcessEventSelection& selection) {
        const bool sameSelection = selection.processId == selectedProcessId_ && selection.source == selectedSource_;
        if (!sameSelection || !smoothedFps_.has_value()) {
            smoothedFps_ = rawFps;
        } else {
            smoothedFps_ = (*smoothedFps_ * (1.0 - kFpsSmoothingAlpha)) + (rawFps * kFpsSmoothingAlpha);
        }
        selectedProcessId_ = selection.processId;
        selectedSource_ = selection.source;
        return *smoothedFps_;
    }

    void ResetSelectionLocked() {
        smoothedFps_.reset();
        selectedProcessId_ = 0;
    }

    double Gpu3dUsageForProcess(DWORD processId) const {
        for (const ProcessGpuUsage& entry : gpu3dUsageByProcess_) {
            if (entry.processId == processId) {
                return entry.usage;
            }
        }
        return 0.0;
    }

    double& Gpu3dUsageSlot(DWORD processId) {
        for (ProcessGpuUsage& entry : gpu3dUsageByProcess_) {
            if (entry.processId == processId) {
                return entry.usage;
            }
        }
        gpu3dUsageByProcess_.push_back(ProcessGpuUsage{processId, 0.0});
        return gpu3dUsageByProcess_.back().usage;
    }

    ProcessNameCacheEntry* FindProcessNameCache(DWORD processId) {
        for (ProcessNameCacheEntry& entry : processNames_) {
            if (entry.processId == processId) {
                return &entry;
            }
        }
        return nullptr;
    }

    const ProcessNameCacheEntry* FindProcessNameCache(DWORD processId) const {
        for (const ProcessNameCacheEntry& entry : processNames_) {
            if (entry.processId == processId) {
                return &entry;
            }
        }
        return nullptr;
    }

    void EraseProcessNameCache(DWORD processId) {
        for (auto it = processNames_.begin(); it != processNames_.end(); ++it) {
            if (it->processId == processId) {
                processNames_.erase(it);
                return;
            }
        }
    }

    void InitializeGpu3dUsageLocked() {
        if (gpuQuery_ != nullptr || gpuQueryInitialized_) {
            return;
        }
        gpuQueryInitialized_ = true;

        const PDH_STATUS openStatus = PdhOpenQueryW(nullptr, 0, &gpuQuery_);
        if (openStatus != ERROR_SUCCESS || gpuQuery_ == nullptr) {
            gpuUsageDiagnostics_ = " gpu3d_open=" + PdhStatusCodeString(openStatus);
            gpuQuery_ = nullptr;
            return;
        }

        const PDH_STATUS addStatus =
            AddCounterCompat(gpuQuery_, L"\\GPU Engine(*)\\Utilization Percentage", &gpu3dCounter_);
        if (addStatus != ERROR_SUCCESS || gpu3dCounter_ == nullptr) {
            gpuUsageDiagnostics_ = " gpu3d_add=" + PdhStatusCodeString(addStatus);
            PdhCloseQuery(gpuQuery_);
            gpuQuery_ = nullptr;
            return;
        }
        const PDH_STATUS collectStatus = PdhCollectQueryData(gpuQuery_);
        gpuUsageDiagnostics_ = " gpu3d_collect=" + PdhStatusCodeString(collectStatus);
        CapturePreviousGpu3dRawValuesLocked();
    }

    void CapturePreviousGpu3dRawValuesLocked() {
        previousGpuRawByInstance_.clear();
        if (gpuQuery_ == nullptr || gpu3dCounter_ == nullptr) {
            return;
        }

        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        PDH_STATUS status = PdhGetRawCounterArrayW(gpu3dCounter_, &bufferSize, &itemCount, nullptr);
        if (status != PDH_MORE_DATA) {
            return;
        }

        gpuCounterArrayBuffer_.resize(bufferSize);
        auto* items = reinterpret_cast<PDH_RAW_COUNTER_ITEM_W*>(gpuCounterArrayBuffer_.data());
        status = PdhGetRawCounterArrayW(gpu3dCounter_, &bufferSize, &itemCount, items);
        if (status != ERROR_SUCCESS) {
            return;
        }

        previousGpuRawByInstance_.reserve(itemCount);
        for (DWORD i = 0; i < itemCount; ++i) {
            if (IsGpu3dEngineInstance(items[i].szName) && items[i].RawValue.CStatus == ERROR_SUCCESS) {
                previousGpuRawByInstance_[items[i].szName] = items[i].RawValue;
            }
        }
    }

    void UpdateGpu3dUsageLocked() {
        InitializeGpu3dUsageLocked();
        gpu3dUsageByProcess_.clear();
        topGpu3dProcessId_ = 0;
        topGpu3dUsage_ = 0.0;
        if (gpuQuery_ == nullptr || gpu3dCounter_ == nullptr) {
            return;
        }

        const PDH_STATUS collectStatus = PdhCollectQueryData(gpuQuery_);
        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        PDH_STATUS status = PdhGetRawCounterArrayW(gpu3dCounter_, &bufferSize, &itemCount, nullptr);
        if (status != PDH_MORE_DATA) {
            gpuUsageDiagnostics_ = " gpu3d_collect=" + PdhStatusCodeString(collectStatus) +
                                   " gpu3d_prepare=" + PdhStatusCodeString(status);
            return;
        }

        gpuCounterArrayBuffer_.resize(bufferSize);
        auto* items = reinterpret_cast<PDH_RAW_COUNTER_ITEM_W*>(gpuCounterArrayBuffer_.data());
        status = PdhGetRawCounterArrayW(gpu3dCounter_, &bufferSize, &itemCount, items);
        if (status != ERROR_SUCCESS) {
            gpuUsageDiagnostics_ =
                " gpu3d_collect=" + PdhStatusCodeString(collectStatus) + " gpu3d_fetch=" + PdhStatusCodeString(status);
            return;
        }

        currentGpuRawByInstance_.clear();
        currentGpuRawByInstance_.reserve(itemCount);
        for (DWORD i = 0; i < itemCount; ++i) {
            const wchar_t* instance = items[i].szName;
            if (!IsGpu3dEngineInstance(instance) || items[i].RawValue.CStatus != ERROR_SUCCESS) {
                continue;
            }
            currentGpuRawByInstance_[instance] = items[i].RawValue;

            const DWORD processId = ExtractProcessIdFromGpuEngineInstance(instance);
            if (processId == 0 || IsExcludedProcessName(ResolveProcessNameLocked(processId))) {
                continue;
            }

            const auto previous = previousGpuRawByInstance_.find(instance);
            if (previous == previousGpuRawByInstance_.end()) {
                continue;
            }

            PDH_FMT_COUNTERVALUE formatted{};
            PDH_RAW_COUNTER previousRaw = previous->second;
            PDH_RAW_COUNTER currentRaw = items[i].RawValue;
            const PDH_STATUS formatStatus =
                PdhCalculateCounterFromRawValue(gpu3dCounter_, PDH_FMT_DOUBLE, &previousRaw, &currentRaw, &formatted);
            if (formatStatus != ERROR_SUCCESS || formatted.CStatus != ERROR_SUCCESS) {
                continue;
            }

            const double value = formatted.doubleValue;
            if (value <= 0.0) {
                continue;
            }
            double& total = Gpu3dUsageSlot(processId);
            total += value;
            if (total > topGpu3dUsage_) {
                topGpu3dUsage_ = total;
                topGpu3dProcessId_ = processId;
            }
        }
        previousGpuRawByInstance_.swap(currentGpuRawByInstance_);

        gpuUsageDiagnostics_ = " gpu3d_collect=" + PdhStatusCodeString(collectStatus) +
                               " gpu3d_fetch=" + PdhStatusCodeString(status) +
                               " top_gpu3d_process=" + Utf8FromWide(ResolveProcessNameLocked(topGpu3dProcessId_)) +
                               " top_gpu3d_pid=" + std::to_string(static_cast<unsigned long>(topGpu3dProcessId_)) +
                               " " + Trace::FormatValueDouble("top_gpu3d", topGpu3dUsage_, 1);
    }

    std::string GpuUsageDiagnostics() const {
        return GpuUsageDiagnosticsForProcess(selectedProcessId_);
    }

    std::string GpuUsageDiagnosticsForProcess(DWORD processId) const {
        if (processId == 0) {
            return gpuUsageDiagnostics_;
        }
        return gpuUsageDiagnostics_ + " " +
               Trace::FormatValueDouble("selected_gpu3d", Gpu3dUsageForProcess(processId), 1);
    }

    ULONG EnableProvider(const GUID& providerGuid, uint64_t anyKeyword, UCHAR level) const {
        return EnableTraceEx2(
            sessionHandle_, &providerGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, level, anyKeyword, 0, 0, nullptr);
    }

    void Stop() {
        HANDLE threadToJoin = nullptr;
        {
            std::lock_guard lock(mutex_);
            StopLocked();
            threadToJoin = processingThread_;
            processingThread_ = nullptr;
        }
        if (threadToJoin != nullptr) {
            WaitForSingleObject(threadToJoin, INFINITE);
            CloseHandle(threadToJoin);
        }
    }

    void StopLocked() {
        if (traceHandle_ != INVALID_PROCESSTRACE_HANDLE) {
            CloseTrace(traceHandle_);
            traceHandle_ = INVALID_PROCESSTRACE_HANDLE;
        }
        if (sessionHandle_ != 0) {
            EtwSessionProperties sessionProps{};
            sessionProps.properties.Wnode.BufferSize = sizeof(sessionProps);
            sessionProps.properties.LoggerNameOffset = offsetof(EtwSessionProperties, loggerName);
            ControlTraceW(sessionHandle_, sessionName_.c_str(), &sessionProps.properties, EVENT_TRACE_CONTROL_STOP);
            sessionHandle_ = 0;
        }
        if (gpuQuery_ != nullptr) {
            PdhCloseQuery(gpuQuery_);
            gpuQuery_ = nullptr;
            gpu3dCounter_ = nullptr;
            gpuQueryInitialized_ = false;
            previousGpuRawByInstance_.clear();
            currentGpuRawByInstance_.clear();
        }
        initialized_ = false;
    }

    std::wstring ResolveProcessNameLocked(DWORD processId) {
        const ProcessNameCacheEntry* cached = FindProcessNameCache(processId);
        if (cached != nullptr) {
            return cached->name;
        }

        bool permissionRequired = false;
        std::wstring processName = QueryProcessBaseName(processId, permissionRequired);
        if (processName.empty()) {
            processName = permissionRequired ? L"!admin" : L"pid:" + std::to_wstring(processId);
        }
        ProcessNameCacheEntry entry;
        entry.processId = processId;
        entry.name = std::move(processName);
        entry.permissionRequired = permissionRequired;
        processNames_.push_back(std::move(entry));
        return processNames_.back().name;
    }

    bool IsProcessNamePermissionRequiredLocked(DWORD processId) const {
        const ProcessNameCacheEntry* entry = FindProcessNameCache(processId);
        return entry != nullptr && entry->permissionRequired;
    }

    std::string BuildDiagnosticsLocked(const std::string& suffix) const {
        return diagnostics_ + " runtime_events=" + std::to_string(runtimePresentEvents_) +
               " dxgkrnl_events=" + std::to_string(kernelPresentEvents_) + suffix;
    }

    void RecordPresent(EVENT_RECORD* eventRecord) {
        const EVENT_HEADER& header = eventRecord->EventHeader;
        if (header.ProcessId == 0 || header.ProcessId == GetCurrentProcessId()) {
            return;
        }

        const bool isDxgiPresent =
            header.ProviderId == kDxgiProviderGuid && header.EventDescriptor.Id == kDxgiPresentStartEventId;
        const bool isDxgiPresentMpo = header.ProviderId == kDxgiProviderGuid &&
                                      header.EventDescriptor.Id == kDxgiPresentMultiplaneOverlayStartEventId;
        const bool isD3d9Present =
            header.ProviderId == kD3d9ProviderGuid && header.EventDescriptor.Id == kD3d9PresentStartEventId;
        const bool isDxgKrnlPresent =
            header.ProviderId == kDxgKrnlProviderGuid && header.EventDescriptor.Id == kDxgKrnlPresentInfoEventId;
        if (!isDxgiPresent && !isDxgiPresentMpo && !isD3d9Present && !isDxgKrnlPresent) {
            return;
        }

        LARGE_INTEGER receivedAt{};
        QueryPerformanceCounter(&receivedAt);

        std::lock_guard lock(mutex_);
        const PresentEventSource source = isDxgKrnlPresent ? PresentEventSource::Kernel : PresentEventSource::Runtime;
        ProcessPresentEventBuckets& buckets =
            source == PresentEventSource::Runtime ? runtimeEventsByProcess_ : kernelEventsByProcess_;
        ProcessPresentEvents& events = EnsureProcessPresentEvents(buckets, header.ProcessId);
        // Some fallback DxgKrnl paths are delivered with timestamps that do not compare cleanly to the
        // consumer's QPC clock. Use receive time so the rolling counter stays stable across providers.
        PushPresentEvent(events, static_cast<uint64_t>(receivedAt.QuadPart));

        uint64_t& sourceCount = source == PresentEventSource::Runtime ? runtimePresentEvents_ : kernelPresentEvents_;
        ++sourceCount;
        if (sourceCount <= 5 || sourceCount % 300 == 0) {
            trace_.Write("fps_etw:present source=" + std::string(PresentEventSourceName(source)) +
                         " pid=" + std::to_string(static_cast<unsigned long>(header.ProcessId)) + " event_id=" +
                         std::to_string(header.EventDescriptor.Id) + " source_events=" + std::to_string(sourceCount));
        }
    }

    static void WINAPI OnEventRecord(EVENT_RECORD* eventRecord) {
        auto* provider = static_cast<PresentedFpsEtwProvider*>(eventRecord->UserContext);
        if (provider != nullptr) {
            provider->RecordPresent(eventRecord);
        }
    }

    static DWORD WINAPI ProcessTraceThread(void* context) {
        static_cast<PresentedFpsEtwProvider*>(context)->ProcessTraceLoop();
        return 0;
    }

    void ProcessTraceLoop() {
        TRACEHANDLE handle = INVALID_PROCESSTRACE_HANDLE;
        {
            std::lock_guard threadLock(mutex_);
            handle = traceHandle_;
        }
        const ULONG processStatus = ProcessTrace(&handle, 1, nullptr, nullptr);
        trace_.Write("fps_etw:process_trace_done status=" + Win32ErrorText(processStatus));
    }

    Trace& trace_;
    mutable std::mutex mutex_;
    TRACEHANDLE sessionHandle_ = 0;
    TRACEHANDLE traceHandle_ = INVALID_PROCESSTRACE_HANDLE;
    HANDLE processingThread_ = nullptr;
    std::wstring sessionName_;
    // Size: active presenting process sets are tiny; flat buckets avoid unordered_map machinery in this provider.
    ProcessPresentEventBuckets runtimeEventsByProcess_;
    ProcessPresentEventBuckets kernelEventsByProcess_;
    std::vector<ProcessNameCacheEntry> processNames_;
    std::vector<ProcessGpuUsage> gpu3dUsageByProcess_;
    // Size/perf: GPU Engine exposes hundreds of per-engine instances on process-heavy machines, so raw lookup stays hashed.
    std::unordered_map<std::wstring, PDH_RAW_COUNTER> previousGpuRawByInstance_;
    std::unordered_map<std::wstring, PDH_RAW_COUNTER> currentGpuRawByInstance_;
    std::vector<unsigned char> gpuCounterArrayBuffer_;
    std::optional<double> smoothedFps_;
    PDH_HQUERY gpuQuery_ = nullptr;
    PDH_HCOUNTER gpu3dCounter_ = nullptr;
    DWORD selectedProcessId_ = 0;
    DWORD topGpu3dProcessId_ = 0;
    double topGpu3dUsage_ = 0.0;
    PresentEventSource selectedSource_ = PresentEventSource::Runtime;
    std::string diagnostics_ = "FPS ETW provider not initialized.";
    std::string gpuUsageDiagnostics_;
    uint64_t runtimePresentEvents_ = 0;
    uint64_t kernelPresentEvents_ = 0;
    long long qpcFrequency_ = 0;
    bool dxgiEnabled_ = false;
    bool d3d9Enabled_ = false;
    bool dxgkrnlEnabled_ = false;
    bool permissionRequired_ = false;
    bool initialized_ = false;
    bool gpuQueryInitialized_ = false;
};

}  // namespace

std::unique_ptr<FpsTelemetryProvider> CreatePresentedFpsEtwProvider(Trace& trace) {
    return std::make_unique<PresentedFpsEtwProvider>(trace);
}
