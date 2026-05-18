#pragma once

#include <memory>

class LenovoHardwareScanCaptureSink {
public:
    virtual ~LenovoHardwareScanCaptureSink() = default;

    virtual void AddTemperatureReading(const wchar_t* title, double celsius) = 0;
    virtual void SetDiagnostics(const wchar_t* diagnostics) = 0;
    virtual void TraceAssemblyLoaded(const wchar_t* path) = 0;
    virtual void TraceClientStatus(const wchar_t* status) = 0;
    virtual void TraceExecutionResult(const wchar_t* result) = 0;
    virtual void TraceInitializeException(const wchar_t* diagnostics) = 0;
    virtual void TraceModuleLoadResult(const wchar_t* result) = 0;
    virtual void TraceSnapshotException(const wchar_t* diagnostics) = 0;
};

struct LenovoHardwareScanCaptureOptions {
    bool includeCpuTemperature = true;
    bool includeGpuTemperature = true;
    bool includeStorageTemperature = true;
    bool includeMotherboardTemperature = true;
    bool includeBatteryTemperature = true;
};

class LenovoHardwareScanRuntime {
public:
    LenovoHardwareScanRuntime();
    ~LenovoHardwareScanRuntime();

    LenovoHardwareScanRuntime(const LenovoHardwareScanRuntime&) = delete;
    LenovoHardwareScanRuntime& operator=(const LenovoHardwareScanRuntime&) = delete;

    bool Capture(const wchar_t* addinDirectory,
        const LenovoHardwareScanCaptureOptions& options,
        LenovoHardwareScanCaptureSink& sink);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
