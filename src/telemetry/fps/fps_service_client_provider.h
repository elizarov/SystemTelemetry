#pragma once

#include <memory>

#include "telemetry/fps_provider.h"

std::unique_ptr<FpsTelemetryProvider> CreatePresentedFpsProvider(Trace& trace);
