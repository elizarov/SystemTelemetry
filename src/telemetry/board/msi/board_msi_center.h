#pragma once

#include <memory>

#include "telemetry/board/board_vendor.h"
#include "util/trace.h"

struct HardwareDependencyInjection;

std::unique_ptr<BoardVendorTelemetryProvider> CreateMsiBoardTelemetryProvider(Trace& trace, BoardVendorInfo info);
std::unique_ptr<BoardVendorTelemetryProvider> CreateMsiBoardTelemetryProvider(
    Trace& trace, BoardVendorInfo info, const HardwareDependencyInjection* injection);
