#pragma once

#include <memory>

class GigabyteSivCaptureSink {
public:
    virtual ~GigabyteSivCaptureSink() = default;

    virtual void AddFanReading(const wchar_t* title, double rpm) = 0;
    virtual void AddTemperatureReading(const wchar_t* title, double celsius) = 0;
    virtual void SetDiagnostics(const wchar_t* diagnostics) = 0;
    virtual void TraceAssemblyPreload(const wchar_t* path) = 0;
    virtual void TraceMonitorCreated(const wchar_t* typeName) = 0;
    virtual void TraceInitializeSuccess() = 0;
    virtual void TraceInitializeException(const wchar_t* diagnostics) = 0;
    virtual void TraceSnapshotException(const wchar_t* diagnostics) = 0;
};

class GigabyteSivRuntime {
public:
    GigabyteSivRuntime();
    ~GigabyteSivRuntime();

    GigabyteSivRuntime(const GigabyteSivRuntime&) = delete;
    GigabyteSivRuntime& operator=(const GigabyteSivRuntime&) = delete;

    bool Capture(const wchar_t* sivDirectory, GigabyteSivCaptureSink& sink);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
