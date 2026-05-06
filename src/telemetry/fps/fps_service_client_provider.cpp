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
#include "util/trace.h"

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

std::string Win32ErrorText(DWORD status) {
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

std::optional<FpsTelemetrySample> QueryServiceSample(std::string& diagnostics) {
    diagnostics.clear();
    if (!WaitNamedPipeW(kFpsServicePipeName, kPipeConnectTimeoutMs)) {
        diagnostics = "CashDash service pipe is unavailable: " + Win32ErrorText(GetLastError());
        return std::nullopt;
    }

    Handle pipe(CreateFileW(
        kFpsServicePipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (pipe.Get() == INVALID_HANDLE_VALUE) {
        diagnostics = "Failed to connect to CashDash service pipe: " + Win32ErrorText(GetLastError());
        return std::nullopt;
    }

    const std::vector<char> request = BuildFpsServiceRequest();
    DWORD written = 0;
    if (!WriteFile(pipe.Get(), request.data(), static_cast<DWORD>(request.size()), &written, nullptr) ||
        written != request.size()) {
        diagnostics = "Failed to write CashDash service request: " + Win32ErrorText(GetLastError());
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
            diagnostics = "Failed to read FPS service response: " + Win32ErrorText(error);
            return std::nullopt;
        }
        if (read == 0) {
            break;
        }
        if (response.size() + read > kMaximumPipeResponseBytes) {
            diagnostics = "FPS service response is too large.";
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
    explicit FpsServiceClientProvider(Trace& trace) : trace_(trace) {}

    bool Initialize() override {
        if (initialized_) {
            return true;
        }

        std::string diagnostics;
        const std::optional<FpsTelemetrySample> sample = QueryServiceSample(diagnostics);
        if (!sample.has_value()) {
            diagnostics_ = diagnostics.empty() ? "FPS service did not return a sample." : diagnostics;
            trace_.Write(
                TracePrefix::FpsServiceClient, "initialize_failed diagnostics=" + Trace::QuoteText(diagnostics_));
            return false;
        }

        cachedSample_ = *sample;
        diagnostics_ = sample->diagnostics.empty() ? "FPS service provider active." : sample->diagnostics;
        initialized_ = true;
        trace_.Write(TracePrefix::FpsServiceClient, "initialize_done diagnostics=" + Trace::QuoteText(diagnostics_));
        return true;
    }

    FpsTelemetrySample Sample() override {
        std::string diagnostics;
        const std::optional<FpsTelemetrySample> sample = QueryServiceSample(diagnostics);
        if (sample.has_value()) {
            cachedSample_ = *sample;
            return *sample;
        }

        FpsTelemetrySample unavailable;
        unavailable.diagnostics = diagnostics.empty() ? "FPS service sample unavailable." : diagnostics;
        if (cachedSample_.has_value()) {
            unavailable.processId = cachedSample_->processId;
            unavailable.processName = cachedSample_->processName;
        }
        trace_.WriteLazy(TracePrefix::FpsServiceClient,
            [&] { return "sample_failed diagnostics=" + Trace::QuoteText(unavailable.diagnostics); });
        return unavailable;
    }

private:
    Trace& trace_;
    std::string diagnostics_ = "FPS service client not initialized.";
    std::optional<FpsTelemetrySample> cachedSample_;
    bool initialized_ = false;
};

std::unique_ptr<FpsTelemetryProvider> CreateFpsServiceClientProvider(Trace& trace) {
    return std::make_unique<FpsServiceClientProvider>(trace);
}

class FpsHybridProvider final : public FpsTelemetryProvider {
public:
    explicit FpsHybridProvider(Trace& trace) : trace_(trace) {}

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

    FpsTelemetrySample Sample() override {
        if (serviceProvider_ != nullptr) {
            return serviceProvider_->Sample();
        }

        ++serviceRetrySample_;
        if (serviceRetrySample_ >= kServiceRetrySampleInterval) {
            serviceRetrySample_ = 0;
            if (TryInitializeServiceProvider()) {
                trace_.Write(TracePrefix::FpsProvider, "service_recovered");
                return serviceProvider_->Sample();
            }
        }

        if (localProvider_ != nullptr) {
            return localProvider_->Sample();
        }

        FpsTelemetrySample sample;
        sample.diagnostics = "No FPS provider initialized.";
        return sample;
    }

private:
    bool TryInitializeServiceProvider() {
        auto provider = CreateFpsServiceClientProvider(trace_);
        if (provider != nullptr && provider->Initialize()) {
            serviceProvider_ = std::move(provider);
            return true;
        }

        trace_.Write(TracePrefix::FpsProvider, "service_unavailable fallback=local_etw");
        return false;
    }

    Trace& trace_;
    std::unique_ptr<FpsTelemetryProvider> serviceProvider_;
    std::unique_ptr<FpsTelemetryProvider> localProvider_;
    int serviceRetrySample_ = kServiceRetrySampleInterval;
    bool localProviderInitialized_ = false;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<FpsTelemetryProvider> CreatePresentedFpsProvider(Trace& trace) {
    return std::make_unique<FpsHybridProvider>(trace);
}
