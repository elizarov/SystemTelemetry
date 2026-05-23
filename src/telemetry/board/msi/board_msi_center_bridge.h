#pragma once

#include <memory>

class MsiCenterCaptureSink {
public:
    virtual ~MsiCenterCaptureSink() = default;

    virtual void AddFanReading(const wchar_t* title, double rpm) = 0;
    virtual void AddTemperatureReading(const wchar_t* title, double celsius) = 0;
    virtual void SetDiagnostics(const wchar_t* diagnostics) = 0;
    virtual void TraceAssemblyLoaded(const wchar_t* path) = 0;
    virtual void TraceQuerySuccess(int fanCount, int temperatureCount) = 0;
    virtual void TraceInitializeException(const wchar_t* diagnostics) = 0;
    virtual void TraceSnapshotException(const wchar_t* diagnostics) = 0;
};

class MsiCenterRuntime {
public:
    MsiCenterRuntime();
    ~MsiCenterRuntime();

    MsiCenterRuntime(const MsiCenterRuntime&) = delete;
    MsiCenterRuntime& operator=(const MsiCenterRuntime&) = delete;

    bool Capture(const char* msiCenterDirectory, MsiCenterCaptureSink& sink);

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
};
