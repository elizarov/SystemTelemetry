#pragma once

#include <memory>
#include <optional>
#include <string>

#include "util/resource_strings.h"
#include "util/trace.h"

struct FpsTelemetrySample {
    std::optional<double> fps;
    unsigned long processId = 0;
    std::string processName;
    std::string diagnostics = ResourceStringText(RES_STR("FPS ETW provider not initialized."));
    bool available = false;
    bool permissionRequired = false;
};

struct FpsTelemetrySampleOptions {
    std::string gpuAdapterLuidToken;
};

class FpsTelemetryProvider {
public:
    virtual ~FpsTelemetryProvider() = default;
    virtual bool Initialize() = 0;
    virtual FpsTelemetrySample Sample(const FpsTelemetrySampleOptions& options = FpsTelemetrySampleOptions{}) = 0;
};

std::unique_ptr<FpsTelemetryProvider> CreateFpsServiceTelemetryProvider(Trace& trace);
