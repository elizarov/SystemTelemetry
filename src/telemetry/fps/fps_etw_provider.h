#pragma once

#include <memory>
#include <optional>
#include <string>

#include "util/trace.h"

struct FpsTelemetrySample {
    std::optional<double> fps;
    unsigned long processId = 0;
    std::string processName;
    std::string diagnostics = "FPS ETW provider not initialized.";
    bool available = false;
    bool permissionRequired = false;
};

class FpsTelemetryProvider {
public:
    virtual ~FpsTelemetryProvider() = default;
    virtual bool Initialize() = 0;
    virtual FpsTelemetrySample Sample() = 0;
};

std::unique_ptr<FpsTelemetryProvider> CreatePresentedFpsEtwProvider(Trace& trace);
