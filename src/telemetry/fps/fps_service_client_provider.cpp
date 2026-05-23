#include "telemetry/fps/fps_service_client_provider.h"

#include <windows.h>

#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "telemetry/fps/fps_etw_provider.h"
#include "telemetry/fps_service_protocol.h"
#include "util/resource_strings.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "util/win32_format.h"

namespace {

constexpr DWORD kPipeConnectTimeoutMs = 100;
constexpr DWORD kPipeReadChunkBytes = 4096;
constexpr DWORD kMaximumPipeResponseBytes = 16 * 1024;
constexpr int kServiceRetrySampleInterval = 10;

std::string CleanProcessDisplayName(std::string processName) {
    const size_t slash = processName.find_last_of("\\/");
    if (slash != std::string::npos) {
        processName.erase(0, slash + 1);
    }
    const size_t dot = processName.find_last_of('.');
    if (dot != std::string::npos) {
        processName.erase(dot);
    }
    for (char& ch : processName) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return processName;
}

class Handle {
public:
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}

    ~Handle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    HANDLE Get() const {
        return handle_;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

FpsTelemetrySampleOptions OptionsForAdapter(const std::optional<GpuAdapterInfo>& adapter) {
    FpsTelemetrySampleOptions options;
    if (adapter.has_value()) {
        options.gpuAdapterLuidToken = GpuAdapterPdhLuidToken(*adapter).value_or(std::string{});
    }
    return options;
}

const FpsTelemetrySampleOptions& EffectiveOptions(
    const FpsTelemetrySampleOptions& requested,
    const FpsTelemetrySampleOptions& fallback
) {
    return requested.gpuAdapterLuidToken.empty() ? fallback : requested;
}

std::optional<FpsTelemetrySample> QueryServiceSample(
    const FpsTelemetrySampleOptions& options,
    std::string& diagnostics
) {
    diagnostics.clear();
    if (!WaitNamedPipeA(kFpsServicePipeName, kPipeConnectTimeoutMs)) {
        diagnostics =
            FormatText(RES_STR("CashDash service pipe is unavailable: %s"), FormatWin32Error(GetLastError()).c_str());
        return std::nullopt;
    }

    Handle pipe(CreateFileA(
        kFpsServicePipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    ));
    if (pipe.Get() == INVALID_HANDLE_VALUE) {
        diagnostics = FormatText(
            RES_STR("Failed to connect to CashDash service pipe: %s"),
            FormatWin32Error(GetLastError()).c_str()
        );
        return std::nullopt;
    }

    const std::vector<char> request = BuildFpsServiceRequest(options);
    DWORD written = 0;
    if (
        !WriteFile(pipe.Get(), request.data(), static_cast<DWORD>(request.size()), &written, nullptr) ||
        written != request.size()
    ) {
        diagnostics = FormatText(
            RES_STR("Failed to write CashDash service request: %s"),
            FormatWin32Error(GetLastError()).c_str()
        );
        return std::nullopt;
    }

    std::vector<char> response;
    for (;;) {
        char buffer[kPipeReadChunkBytes]{};
        DWORD read = 0;
        if (!ReadFile(pipe.Get(), buffer, static_cast<DWORD>(std::size(buffer)), &read, nullptr)) {
            const DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                break;
            }
            diagnostics =
                FormatText(RES_STR("Failed to read FPS service response: %s"), FormatWin32Error(error).c_str());
            return std::nullopt;
        }
        if (read == 0) {
            break;
        }
        if (response.size() + read > kMaximumPipeResponseBytes) {
            diagnostics = ResourceStringText(RES_STR("FPS service response is too large."));
            return std::nullopt;
        }
        response.insert(response.end(), buffer, buffer + read);
    }

    std::optional<FpsTelemetrySample> sample = ParseFpsServiceResponse(response.data(), response.size(), diagnostics);
    if (sample.has_value()) {
        sample->processName = CleanProcessDisplayName(std::move(sample->processName));
    }
    return sample;
}

class FpsServiceClientProvider final : public FpsTelemetryProvider {
public:
    FpsServiceClientProvider(
        Trace& trace,
        FpsTelemetrySampleOptions defaultOptions
    ) :
        trace_(trace),
        defaultOptions_(std::move(defaultOptions)) {}

    bool Initialize() override {
        if (initialized_) {
            return true;
        }

        std::string diagnostics;
        const std::optional<FpsTelemetrySample> sample = QueryServiceSample(defaultOptions_, diagnostics);
        if (!sample.has_value()) {
            diagnostics_ =
                diagnostics.empty() ? ResourceStringText(RES_STR("FPS service did not return a sample.")) : diagnostics;
            trace_.WriteFmt(
                TracePrefix::FpsServiceClient,
                RES_STR("initialize_failed diagnostics=\"%s\""),
                diagnostics_.c_str()
            );
            return false;
        }

        cachedSample_ = *sample;
        diagnostics_ = sample->diagnostics.empty() ? ResourceStringText(RES_STR("FPS service provider active.")) :
            sample->diagnostics;
        initialized_ = true;
        trace_.WriteFmt(
            TracePrefix::FpsServiceClient,
            RES_STR("initialize_done diagnostics=\"%s\""),
            diagnostics_.c_str()
        );
        return true;
    }

    FpsTelemetrySample Sample(const FpsTelemetrySampleOptions& options) override {
        std::string diagnostics;
        const std::optional<FpsTelemetrySample> sample =
            QueryServiceSample(EffectiveOptions(options, defaultOptions_), diagnostics);
        if (sample.has_value()) {
            cachedSample_ = *sample;
            return *sample;
        }

        FpsTelemetrySample unavailable;
        unavailable.diagnostics =
            diagnostics.empty() ? ResourceStringText(RES_STR("FPS service sample unavailable.")) : diagnostics;
        if (cachedSample_.has_value()) {
            unavailable.processId = cachedSample_->processId;
            unavailable.processName = cachedSample_->processName;
        }
        trace_.WriteLazy(TracePrefix::FpsServiceClient, [&] {
            return FormatText("sample_failed diagnostics=\"%s\"", unavailable.diagnostics.c_str());
        });
        return unavailable;
    }

private:
    Trace& trace_;
    FpsTelemetrySampleOptions defaultOptions_;
    std::string diagnostics_ = ResourceStringText(RES_STR("FPS service client not initialized."));
    std::optional<FpsTelemetrySample> cachedSample_;
    bool initialized_ = false;
};

std::unique_ptr<FpsTelemetryProvider> CreateFpsServiceClientProvider(
    Trace& trace,
    const FpsTelemetrySampleOptions& defaultOptions
) {
    return std::make_unique<FpsServiceClientProvider>(trace, defaultOptions);
}

class FpsHybridProvider final : public FpsTelemetryProvider {
public:
    FpsHybridProvider(
        Trace& trace,
        FpsTelemetrySampleOptions defaultOptions
    ) :
        trace_(trace),
        defaultOptions_(std::move(defaultOptions)) {}

    bool Initialize() override {
        if (TryInitializeServiceProvider()) {
            initialized_ = true;
            return true;
        }

        localProvider_ = CreatePresentedFpsEtwProvider(trace_);
        localProviderInitialized_ = localProvider_ != nullptr && localProvider_->Initialize();
        initialized_ = localProviderInitialized_;
        return initialized_;
    }

    FpsTelemetrySample Sample(const FpsTelemetrySampleOptions& options) override {
        const FpsTelemetrySampleOptions& effectiveOptions = EffectiveOptions(options, defaultOptions_);
        if (serviceProvider_ != nullptr) {
            return serviceProvider_->Sample(effectiveOptions);
        }

        ++serviceRetrySample_;
        if (serviceRetrySample_ >= kServiceRetrySampleInterval) {
            serviceRetrySample_ = 0;
            if (TryInitializeServiceProvider()) {
                trace_.Write(TracePrefix::FpsProvider, RES_STR("service_recovered"));
                return serviceProvider_->Sample(effectiveOptions);
            }
        }

        if (localProvider_ != nullptr) {
            return localProvider_->Sample(effectiveOptions);
        }

        FpsTelemetrySample sample;
        sample.diagnostics = ResourceStringText(RES_STR("No FPS provider initialized."));
        return sample;
    }

private:
    bool TryInitializeServiceProvider() {
        auto provider = CreateFpsServiceClientProvider(trace_, defaultOptions_);
        if (provider != nullptr && provider->Initialize()) {
            serviceProvider_ = std::move(provider);
            return true;
        }

        trace_.Write(TracePrefix::FpsProvider, RES_STR("service_unavailable fallback=local_etw"));
        return false;
    }

    Trace& trace_;
    FpsTelemetrySampleOptions defaultOptions_;
    std::unique_ptr<FpsTelemetryProvider> serviceProvider_;
    std::unique_ptr<FpsTelemetryProvider> localProvider_;
    int serviceRetrySample_ = kServiceRetrySampleInterval;
    bool localProviderInitialized_ = false;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<FpsTelemetryProvider> CreatePresentedFpsProvider(
    Trace& trace,
    const std::optional<GpuAdapterInfo>& adapter
) {
    return std::make_unique<FpsHybridProvider>(trace, OptionsForAdapter(adapter));
}
