#pragma once

#include <memory>

#include "telemetry/board/board_vendor.h"
#include "util/trace.h"

std::unique_ptr<BoardVendorTelemetryProvider> CreateGigabyteBoardTelemetryProvider(Trace* trace);
