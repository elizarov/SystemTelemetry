#include "telemetry/fps/fps_etw_provider.h"

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <evntcons.h>
#include <evntrace.h>
#include <optional>
#include <pdhmsg.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "telemetry/fps/impl/gpu_raw_counter_map.h"
#include "telemetry/impl/collector_support.h"
#include "util/lightweight_mutex.h"
#include "util/resource_strings.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "util/win32_format.h"

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
constexpr char kGpuEngine3dMarker[] = "engtype_3D";
constexpr char kGpuEnginePidMarker[] = "pid_";
constexpr char kAllGpuAdaptersFilterLabel[] = "all";

struct EtwSessionProperties {
    EVENT_TRACE_PROPERTIES properties{};
    char loggerName[MAX_PATH]{};
};

enum class PresentEventSource {
    Runtime,
    Kernel,
};

const char* PresentEventSourceName(PresentEventSource source) {
    return source == PresentEventSource::Runtime ? "runtime" : "dxgkrnl";
}

void SetGpuUsageStatusText(std::string& text, const char* label, long status) {
    AssignFormat(text, " %s=%ld", label, status);
}

void SetGpuUsageTwoStatusText(std::string& text, long collectStatus, const char* label, long status) {
    AssignFormat(text, " gpu3d_collect=%ld %s=%ld", collectStatus, label, status);
}

std::string FallbackProcessName(DWORD processId) {
    return FormatText("pid:%lu", static_cast<unsigned long>(processId));
}

bool IsPermissionDenied(ULONG status) {
    return status == ERROR_ACCESS_DENIED;
}

std::string BuildSessionName() {
    return FormatText("CaseDashPresentedFps-%lu", static_cast<unsigned long>(GetCurrentProcessId()));
}

void LowerAsciiInPlace(char* value, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (value[i] >= 'A' && value[i] <= 'Z') {
            value[i] = static_cast<char>(value[i] - 'A' + 'a');
        }
    }
}

std::string CleanProcessDisplayName(char* path, size_t pathLength) {
    size_t nameStart = 0;
    for (size_t i = 0; i < pathLength; ++i) {
        if (path[i] == '\\' || path[i] == '/') {
            nameStart = i + 1;
        }
    }
    size_t nameEnd = pathLength;
    for (size_t i = nameStart; i < pathLength; ++i) {
        if (path[i] == '.') {
            nameEnd = i;
        }
    }
    LowerAsciiInPlace(path + nameStart, nameEnd - nameStart);
    return std::string(path + nameStart, nameEnd - nameStart);
}

std::string QueryProcessBaseName(DWORD processId, bool& permissionRequired) {
    permissionRequired = false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        permissionRequired = GetLastError() == ERROR_ACCESS_DENIED;
        return {};
    }

    char path[MAX_PATH]{};
    DWORD pathLength = static_cast<DWORD>(std::size(path));
    const BOOL ok = QueryFullProcessImageNameA(process, 0, path, &pathLength);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process);
    permissionRequired = error == ERROR_ACCESS_DENIED;
    return ok ? CleanProcessDisplayName(path, pathLength) : std::string{};
}

bool IsExcludedProcessName(const std::string& processName) {
    return processName.empty() || processName == "casedash" || processName == "dwm";
}

char LowerAscii(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool ContainsAsciiInsensitive(const char* value, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    if (value == nullptr) {
        return false;
    }
    for (const char* cursor = value; *cursor != '\0'; ++cursor) {
        size_t matched = 0;
        while (
            matched < needle.size() &&
            cursor[matched] != '\0' &&
            LowerAscii(cursor[matched]) == LowerAscii(needle[matched])
        ) {
            ++matched;
        }
        if (matched == needle.size()) {
            return true;
        }
    }
    return false;
}

DWORD ExtractProcessIdFromGpuEngineInstance(const char* instance) {
    if (instance == nullptr) {
        return 0;
    }
    const char* marker = std::strstr(instance, kGpuEnginePidMarker);
    if (marker == nullptr) {
        return 0;
    }
    marker += 4;
    char* end = nullptr;
    const unsigned long value = std::strtoul(marker, &end, 10);
    return end != marker ? static_cast<DWORD>(value) : 0;
}

bool IsGpu3dEngineInstance(const char* instance) {
    return instance != nullptr && std::strstr(instance, kGpuEngine3dMarker) != nullptr;
}

class PresentedFpsEtwProvider final : public FpsTelemetryProvider {
public:
    explicit PresentedFpsEtwProvider(Trace& trace) : trace_(trace) {}

    ~PresentedFpsEtwProvider() override {
        Stop();
    }

    bool Initialize() override {
        LightweightMutexLock lock(mutex_);
        if (initialized_) {
            return true;
        }

        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
            diagnostics_ = ResourceStringText(RES_STR("QueryPerformanceFrequency failed."));
            trace_.WriteFmt(TracePrefix::FpsEtw, RES_STR("initialize_failed diagnostics=\"%s\""), diagnostics_.c_str());
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

        ULONG status = StartTraceA(&sessionHandle_, sessionName_.c_str(), &sessionProps.properties);
        if (trace_.Enabled(TracePrefix::FpsEtw)) {
            trace_.WriteFmt(TracePrefix::FpsEtw, RES_STR("start_trace status=%s"), FormatWin32Error(status).c_str());
        }
        if (status == ERROR_ALREADY_EXISTS) {
            ControlTraceA(0, sessionName_.c_str(), &sessionProps.properties, EVENT_TRACE_CONTROL_STOP);
            status = StartTraceA(&sessionHandle_, sessionName_.c_str(), &sessionProps.properties);
            if (trace_.Enabled(TracePrefix::FpsEtw)) {
                trace_.WriteFmt(
                    TracePrefix::FpsEtw,
                    RES_STR("start_trace_retry status=%s"),
                    FormatWin32Error(status).c_str()
                );
            }
        }
        if (status != ERROR_SUCCESS) {
            permissionRequired_ = IsPermissionDenied(status);
            diagnostics_ = FormatText(RES_STR("Failed to start FPS ETW session: %s"), FormatWin32Error(status).c_str());
            return false;
        }

        const ULONG dxgiStatus = EnableProvider(kDxgiProviderGuid, kRuntimePresentKeyword, kEtwTraceLevel);
        const ULONG d3d9Status = EnableProvider(kD3d9ProviderGuid, kRuntimePresentKeyword, kEtwTraceLevel);
        const ULONG dxgkrnlStatus =
            EnableProvider(kDxgKrnlProviderGuid, kDxgKrnlPresentKeyword, TRACE_LEVEL_INFORMATION);
        dxgiEnabled_ = dxgiStatus == ERROR_SUCCESS;
        d3d9Enabled_ = d3d9Status == ERROR_SUCCESS;
        dxgkrnlEnabled_ = dxgkrnlStatus == ERROR_SUCCESS;
        if (trace_.Enabled(TracePrefix::FpsEtw)) {
            const std::string dxgiText = FormatWin32Error(dxgiStatus);
            const std::string d3d9Text = FormatWin32Error(d3d9Status);
            const std::string dxgkrnlText = FormatWin32Error(dxgkrnlStatus);
            trace_.WriteFmt(
                TracePrefix::FpsEtw,
                RES_STR("enable dxgi=%s d3d9=%s dxgkrnl=%s"),
                dxgiText.c_str(),
                d3d9Text.c_str(),
                dxgkrnlText.c_str()
            );
        }
        if (!dxgiEnabled_ && !d3d9Enabled_ && !dxgkrnlEnabled_) {
            permissionRequired_ =
                IsPermissionDenied(dxgiStatus) || IsPermissionDenied(d3d9Status) || IsPermissionDenied(dxgkrnlStatus);
            diagnostics_ = FormatText(
                RES_STR("Failed to enable FPS ETW providers: dxgi=%s d3d9=%s dxgkrnl=%s"),
                FormatWin32Error(dxgiStatus).c_str(),
                FormatWin32Error(d3d9Status).c_str(),
                FormatWin32Error(dxgkrnlStatus).c_str()
            );
            StopLocked();
            return false;
        }

        EVENT_TRACE_LOGFILEA traceLog{};
        traceLog.LoggerName = sessionName_.data();
        traceLog.ProcessTraceMode =
            PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
        traceLog.EventRecordCallback = &PresentedFpsEtwProvider::OnEventRecord;
        traceLog.Context = this;
        traceHandle_ = OpenTraceA(&traceLog);
        if (traceHandle_ == INVALID_PROCESSTRACE_HANDLE) {
            diagnostics_ =
                FormatText(RES_STR("Failed to open FPS ETW trace: %s"), FormatWin32Error(GetLastError()).c_str());
            StopLocked();
            return false;
        }

        processingThread_ = CreateThread(nullptr, 0, &PresentedFpsEtwProvider::ProcessTraceThread, this, 0, nullptr);
        if (processingThread_ == nullptr) {
            diagnostics_ = FormatText(
                RES_STR("Failed to create FPS ETW processing thread: %s"),
                FormatWin32Error(GetLastError()).c_str()
            );
            StopLocked();
            return false;
        }

        diagnostics_ = ResourceStringText(RES_STR("Presented FPS ETW provider active."));
        permissionRequired_ = false;
        initialized_ = true;
        trace_.WriteFmt(
            TracePrefix::FpsEtw,
            RES_STR("initialize_done dxgi=%s d3d9=%s dxgkrnl=%s"),
            Trace::BoolText(dxgiEnabled_),
            Trace::BoolText(d3d9Enabled_),
            Trace::BoolText(dxgkrnlEnabled_)
        );
        return true;
    }

    FpsTelemetrySample Sample(const FpsTelemetrySampleOptions& options) override {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);

        LightweightMutexLock lock(mutex_);
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

        UpdateGpu3dUsageLocked(options);
        const bool requireSelectedGpuActivity = !options.gpuAdapterLuidToken.empty();
        const ProcessEventSelection runtimeSelection =
            SelectBestProcessLocked(runtimeEventsByProcess_, minimumQpc, requireSelectedGpuActivity);
        const ProcessEventSelection kernelSelection =
            SelectBestProcessLocked(kernelEventsByProcess_, minimumQpc, requireSelectedGpuActivity);
        ProcessEventSelection bestSelection = SelectBestSourceLocked(runtimeSelection, kernelSelection);
        bestSelection = ApplyProcessHysteresisLocked(bestSelection, minimumQpc, requireSelectedGpuActivity);

        FpsTelemetrySample blockedByGpuSelection;
        if (BuildUnavailableDominantGpuSampleLocked(bestSelection, blockedByGpuSelection)) {
            ResetSelectionLocked();
            return blockedByGpuSelection;
        }

        if (bestSelection.processId == 0 || bestSelection.count < 2) {
            ResetSelectionLocked();
            if (requireSelectedGpuActivity && topGpu3dUsage_ < kGpu3dActiveThresholdPercent) {
                sample.available = true;
                sample.fps = 0.0;
                sample.diagnostics = BuildDiagnosticsLocked(FormatText(
                    RES_STR(" selected adapter idle/off; fps=value=0.0%s"),
                    GpuUsageDiagnostics().c_str()
                ));
                return sample;
            }
            sample.available = false;
            sample.diagnostics = BuildDiagnosticsLocked(FormatText(
                RES_STR(" No presenting application selected.%s"),
                GpuUsageDiagnostics().c_str()
            ));
            return sample;
        }

        const double rawFps = static_cast<double>(bestSelection.count) / kFpsWindowSeconds;
        if (
            requireSelectedGpuActivity && Gpu3dUsageForProcess(bestSelection.processId) < kGpu3dActiveThresholdPercent
        ) {
            ResetSelectionLocked();
            sample.available = false;
            sample.processId = bestSelection.processId;
            sample.processName = ResolveProcessNameLocked(bestSelection.processId);
            sample.permissionRequired = IsProcessNamePermissionRequiredLocked(bestSelection.processId);
            sample.diagnostics = BuildDiagnosticsLocked(FormatText(
                RES_STR(
                    " selected adapter has no active GPU Engine 3D presenter. process=%s source=%s "
                    "window_count=%zu raw_fps=value=%.1f%s"
                ),
                sample.processName.c_str(),
                PresentEventSourceName(bestSelection.source),
                bestSelection.count,
                rawFps,
                GpuUsageDiagnosticsForProcess(bestSelection.processId).c_str()
            ));
            return sample;
        }
        const double fps = SmoothFpsLocked(rawFps, bestSelection);
        sample.processId = bestSelection.processId;
        sample.processName = ResolveProcessNameLocked(bestSelection.processId);
        sample.permissionRequired = IsProcessNamePermissionRequiredLocked(bestSelection.processId);
        sample.available = true;
        sample.fps = fps;
        std::string detail = FormatText(RES_STR(" process=%s"), sample.processName.c_str());
        AppendFormat(
            detail,
            RES_STR(" source=%s window_count=%zu raw_fps=value=%.1f smoothed_fps=value=%.1f"),
            PresentEventSourceName(bestSelection.source),
            bestSelection.count,
            rawFps,
            fps
        );
        AppendFormat(detail, RES_STR("%s"), GpuUsageDiagnostics().c_str());
        sample.diagnostics = BuildDiagnosticsLocked(detail);
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
        std::string name;
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
                bucket.events.begin(),
                bucket.events.begin() + static_cast<std::ptrdiff_t>(bucket.firstEvent)
            );
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

    ProcessEventSelection SelectBestProcessLocked(
        ProcessPresentEventBuckets& eventsByProcess,
        uint64_t minimumQpc,
        bool requireGpu3dActivity
    ) {
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

            const std::string& processName = ResolveProcessNameLocked(it->processId);
            if (
                !IsExcludedProcessName(processName) &&
                (!requireGpu3dActivity || Gpu3dUsageForProcess(it->processId) >= kGpu3dActiveThresholdPercent) &&
                IsBetterSelectionLocked(it->processId, eventCount, selection)
            ) {
                selection.processId = it->processId;
                selection.count = eventCount;
            }
            ++it;
        }

        return selection;
    }

    ProcessEventSelection SelectBestSourceLocked(
        const ProcessEventSelection& runtimeSelection,
        const ProcessEventSelection& kernelSelection
    ) const {
        if (runtimeSelection.processId == 0) {
            return kernelSelection;
        }
        if (kernelSelection.processId == 0) {
            return runtimeSelection;
        }

        const double kernelGpu3d = Gpu3dUsageForProcess(kernelSelection.processId);
        const double runtimeGpu3d = Gpu3dUsageForProcess(runtimeSelection.processId);
        if (
            kernelGpu3d >= kGpu3dActiveThresholdPercent &&
            kernelGpu3d >= (std::max)(runtimeGpu3d, 0.1) * kGpu3dDominanceRatio
        ) {
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
        if (
            candidateGpu3d >= kGpu3dActiveThresholdPercent &&
            candidateGpu3d >= (std::max)(currentGpu3d, 0.1) * kGpu3dDominanceRatio
        ) {
            return true;
        }
        if (
            currentGpu3d >= kGpu3dActiveThresholdPercent &&
            currentGpu3d >= (std::max)(candidateGpu3d, 0.1) * kGpu3dDominanceRatio
        ) {
            return false;
        }
        return count > current.count;
    }

    bool BuildUnavailableDominantGpuSampleLocked(const ProcessEventSelection& selected, FpsTelemetrySample& sample) {
        if (topGpu3dProcessId_ == 0 || topGpu3dUsage_ < kGpu3dActiveThresholdPercent) {
            return false;
        }

        const double selectedGpu3d = Gpu3dUsageForProcess(selected.processId);
        if (
            topGpu3dProcessId_ != selected.processId &&
            topGpu3dUsage_ < (std::max)(selectedGpu3d, 0.1) * kGpu3dDominanceRatio
        ) {
            return false;
        }
        if (topGpu3dProcessId_ == selected.processId && selected.count >= 2) {
            return false;
        }

        sample.processId = topGpu3dProcessId_;
        sample.processName = ResolveProcessNameLocked(topGpu3dProcessId_);
        sample.available = false;
        sample.permissionRequired = IsProcessNamePermissionRequiredLocked(topGpu3dProcessId_);
        std::string detail = FormatText(
            RES_STR(" top GPU 3D application has no matching present events. process=%s selected_process=%s"),
            sample.processName.c_str(),
            ResolveProcessNameLocked(selected.processId).c_str()
        );
        AppendFormat(
            detail,
            RES_STR(" selected_source=%s selected_window_count=%zu"),
            PresentEventSourceName(selected.source),
            selected.count
        );
        AppendFormat(detail, RES_STR("%s"), GpuUsageDiagnosticsForProcess(selected.processId).c_str());
        sample.diagnostics = BuildDiagnosticsLocked(detail);
        return true;
    }

    ProcessEventSelection ApplyProcessHysteresisLocked(
        const ProcessEventSelection& candidate,
        uint64_t minimumQpc,
        bool requireGpu3dActivity
    ) {
        if (selectedProcessId_ == 0 || candidate.processId == selectedProcessId_) {
            return candidate;
        }
        if (requireGpu3dActivity && Gpu3dUsageForProcess(selectedProcessId_) < kGpu3dActiveThresholdPercent) {
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
        if (
            candidate.processId == 0 ||
            static_cast<double>(candidate.count) < static_cast<double>(previous.count) * kProcessSwitchHysteresisRatio
        ) {
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

        const PDH_STATUS openStatus = PdhOpenQueryA(nullptr, 0, &gpuQuery_);
        if (openStatus != ERROR_SUCCESS || gpuQuery_ == nullptr) {
            SetGpuUsageStatusText(gpuUsageDiagnostics_, "gpu3d_open", static_cast<long>(openStatus));
            gpuQuery_ = nullptr;
            return;
        }

        const PDH_STATUS addStatus =
            AddCounterCompat(gpuQuery_, "\\GPU Engine(*)\\Utilization Percentage", &gpu3dCounter_);
        if (addStatus != ERROR_SUCCESS || gpu3dCounter_ == nullptr) {
            SetGpuUsageStatusText(gpuUsageDiagnostics_, "gpu3d_add", static_cast<long>(addStatus));
            PdhCloseQuery(gpuQuery_);
            gpuQuery_ = nullptr;
            return;
        }
        const PDH_STATUS collectStatus = PdhCollectQueryData(gpuQuery_);
        SetGpuUsageStatusText(gpuUsageDiagnostics_, "gpu3d_collect", static_cast<long>(collectStatus));
        CapturePreviousGpu3dRawValuesLocked();
    }

    void CapturePreviousGpu3dRawValuesLocked() {
        previousGpuRawByInstance_.Clear();
        if (gpuQuery_ == nullptr || gpu3dCounter_ == nullptr) {
            return;
        }

        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        PDH_STATUS status = PdhGetRawCounterArrayA(gpu3dCounter_, &bufferSize, &itemCount, nullptr);
        if (status != PDH_MORE_DATA) {
            return;
        }

        gpuCounterArrayBuffer_.resize(bufferSize);
        auto* items = reinterpret_cast<PDH_RAW_COUNTER_ITEM_A*>(gpuCounterArrayBuffer_.data());
        status = PdhGetRawCounterArrayA(gpu3dCounter_, &bufferSize, &itemCount, items);
        if (status != ERROR_SUCCESS) {
            return;
        }

        previousGpuRawByInstance_.Reserve(itemCount);
        for (DWORD i = 0; i < itemCount; ++i) {
            if (IsGpu3dEngineInstance(items[i].szName) && items[i].RawValue.CStatus == ERROR_SUCCESS) {
                previousGpuRawByInstance_.Set(items[i].szName, items[i].RawValue);
            }
        }
    }

    void UpdateGpu3dUsageLocked(const FpsTelemetrySampleOptions& options) {
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
        PDH_STATUS status = PdhGetRawCounterArrayA(gpu3dCounter_, &bufferSize, &itemCount, nullptr);
        if (status != PDH_MORE_DATA) {
            SetGpuUsageTwoStatusText(
                gpuUsageDiagnostics_,
                static_cast<long>(collectStatus),
                "gpu3d_prepare",
                static_cast<long>(status)
            );
            return;
        }

        gpuCounterArrayBuffer_.resize(bufferSize);
        auto* items = reinterpret_cast<PDH_RAW_COUNTER_ITEM_A*>(gpuCounterArrayBuffer_.data());
        status = PdhGetRawCounterArrayA(gpu3dCounter_, &bufferSize, &itemCount, items);
        if (status != ERROR_SUCCESS) {
            SetGpuUsageTwoStatusText(
                gpuUsageDiagnostics_,
                static_cast<long>(collectStatus),
                "gpu3d_fetch",
                static_cast<long>(status)
            );
            return;
        }

        const std::string_view instanceFilter(options.gpuAdapterLuidToken);
        DWORD matchedInstances = 0;
        currentGpuRawByInstance_.Clear();
        currentGpuRawByInstance_.Reserve(itemCount);
        for (DWORD i = 0; i < itemCount; ++i) {
            const char* instance = items[i].szName;
            if (!IsGpu3dEngineInstance(instance) || items[i].RawValue.CStatus != ERROR_SUCCESS) {
                continue;
            }
            currentGpuRawByInstance_.Set(instance, items[i].RawValue);

            if (!ContainsAsciiInsensitive(instance, instanceFilter)) {
                continue;
            }
            ++matchedInstances;

            const DWORD processId = ExtractProcessIdFromGpuEngineInstance(instance);
            if (processId == 0 || IsExcludedProcessName(ResolveProcessNameLocked(processId))) {
                continue;
            }

            const PDH_RAW_COUNTER* previous = previousGpuRawByInstance_.Find(instance);
            if (previous == nullptr) {
                continue;
            }

            PDH_FMT_COUNTERVALUE formatted{};
            PDH_RAW_COUNTER previousRaw = *previous;
            PDH_RAW_COUNTER currentRaw = items[i].RawValue;
            const PDH_STATUS formatStatus =
                PdhCalculateCounterFromRawValue(gpu3dCounter_, PDH_FMT_DOUBLE, &previousRaw, &currentRaw, &formatted);
            if (formatStatus != ERROR_SUCCESS || formatted.CStatus != ERROR_SUCCESS) {
                continue;
            }

            const double value = formatted.doubleValue;
            if (!std::isfinite(value) || value <= 0.0) {
                continue;
            }
            double& total = Gpu3dUsageSlot(processId);
            total += value;
            if (total > topGpu3dUsage_) {
                topGpu3dUsage_ = total;
                topGpu3dProcessId_ = processId;
            }
        }
        previousGpuRawByInstance_.Swap(currentGpuRawByInstance_);

        AssignFormat(
            gpuUsageDiagnostics_,
            RES_STR(" gpu3d_collect=%ld gpu3d_fetch=%ld gpu3d_filter=\"%s\" gpu3d_matched=%lu top_gpu3d_process="),
            static_cast<long>(collectStatus),
            static_cast<long>(status),
            options.gpuAdapterLuidToken.empty() ? kAllGpuAdaptersFilterLabel : options.gpuAdapterLuidToken.c_str(),
            static_cast<unsigned long>(matchedInstances)
        );
        AppendFormat(gpuUsageDiagnostics_, RES_STR("%s"), ResolveProcessNameLocked(topGpu3dProcessId_).c_str());
        AppendFormat(
            gpuUsageDiagnostics_,
            RES_STR(" top_gpu3d_pid=%lu top_gpu3d=value=%.1f"),
            static_cast<unsigned long>(topGpu3dProcessId_),
            topGpu3dUsage_
        );
    }

    std::string GpuUsageDiagnostics() const {
        return GpuUsageDiagnosticsForProcess(selectedProcessId_);
    }

    std::string GpuUsageDiagnosticsForProcess(DWORD processId) const {
        if (processId == 0) {
            return gpuUsageDiagnostics_;
        }
        std::string diagnostics = gpuUsageDiagnostics_;
        AppendFormat(diagnostics, RES_STR(" selected_gpu3d=value=%.1f"), Gpu3dUsageForProcess(processId));
        return diagnostics;
    }

    ULONG EnableProvider(const GUID& providerGuid, uint64_t anyKeyword, UCHAR level) const {
        return EnableTraceEx2(
            sessionHandle_,
            &providerGuid,
            EVENT_CONTROL_CODE_ENABLE_PROVIDER,
            level,
            anyKeyword,
            0,
            0,
            nullptr
        );
    }

    void Stop() {
        HANDLE threadToJoin = nullptr;
        {
            LightweightMutexLock lock(mutex_);
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
            ControlTraceA(sessionHandle_, sessionName_.c_str(), &sessionProps.properties, EVENT_TRACE_CONTROL_STOP);
            sessionHandle_ = 0;
        }
        if (gpuQuery_ != nullptr) {
            PdhCloseQuery(gpuQuery_);
            gpuQuery_ = nullptr;
            gpu3dCounter_ = nullptr;
            gpuQueryInitialized_ = false;
            previousGpuRawByInstance_.Clear();
            currentGpuRawByInstance_.Clear();
        }
        initialized_ = false;
    }

    const std::string& ResolveProcessNameLocked(DWORD processId) {
        const ProcessNameCacheEntry* cached = FindProcessNameCache(processId);
        if (cached != nullptr) {
            return cached->name;
        }

        bool permissionRequired = false;
        std::string processName = QueryProcessBaseName(processId, permissionRequired);
        if (processName.empty()) {
            processName = permissionRequired ? "!admin" : FallbackProcessName(processId);
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
        std::string text = diagnostics_;
        AppendFormat(
            text,
            RES_STR(" runtime_events=%llu dxgkrnl_events=%llu"),
            static_cast<unsigned long long>(runtimePresentEvents_),
            static_cast<unsigned long long>(kernelPresentEvents_)
        );
        AppendFormat(text, RES_STR("%s"), suffix.c_str());
        return text;
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

        LightweightMutexLock lock(mutex_);
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
            trace_.WriteFmt(
                TracePrefix::FpsEtw,
                RES_STR("present source=%s pid=%lu event_id=%u source_events=%llu"),
                PresentEventSourceName(source),
                static_cast<unsigned long>(header.ProcessId),
                static_cast<unsigned>(header.EventDescriptor.Id),
                static_cast<unsigned long long>(sourceCount)
            );
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
            LightweightMutexLock threadLock(mutex_);
            handle = traceHandle_;
        }
        const ULONG processStatus = ProcessTrace(&handle, 1, nullptr, nullptr);
        trace_.WriteFmt(
            TracePrefix::FpsEtw,
            RES_STR("process_trace_done status=%s"),
            FormatWin32Error(processStatus).c_str()
        );
    }

    Trace& trace_;
    mutable LightweightMutex mutex_;
    TRACEHANDLE sessionHandle_ = 0;
    TRACEHANDLE traceHandle_ = INVALID_PROCESSTRACE_HANDLE;
    HANDLE processingThread_ = nullptr;
    std::string sessionName_;
    // Size: active presenting process sets are tiny; flat buckets avoid unordered_map machinery in this provider.
    ProcessPresentEventBuckets runtimeEventsByProcess_;
    ProcessPresentEventBuckets kernelEventsByProcess_;
    std::vector<ProcessNameCacheEntry> processNames_;
    std::vector<ProcessGpuUsage> gpu3dUsageByProcess_;
    std::vector<unsigned char> gpuCounterArrayBuffer_;
    GpuRawCounterMap previousGpuRawByInstance_;
    GpuRawCounterMap currentGpuRawByInstance_;
    std::optional<double> smoothedFps_;
    PDH_HQUERY gpuQuery_ = nullptr;
    PDH_HCOUNTER gpu3dCounter_ = nullptr;
    DWORD selectedProcessId_ = 0;
    DWORD topGpu3dProcessId_ = 0;
    double topGpu3dUsage_ = 0.0;
    PresentEventSource selectedSource_ = PresentEventSource::Runtime;
    std::string diagnostics_ = ResourceStringText(RES_STR("FPS ETW provider not initialized."));
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
