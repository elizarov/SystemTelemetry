#pragma once

#include <memory>

#include "telemetry/board/board_vendor.h"

std::unique_ptr<BoardVendorTelemetryProvider> CreateLenovoBoardTelemetryProvider(Trace& trace, BoardVendorInfo info);
BoardVendorTelemetrySample CaptureLenovoBoardServiceSample(Trace& trace, BoardVendorInfo info);
