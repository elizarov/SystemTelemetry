#pragma once

#include <memory>

#include "telemetry/board/board_vendor.h"

std::unique_ptr<BoardVendorTelemetryProvider> CreateLenovoBoardTelemetryProvider(
    Trace& trace, BoardVendorInfo info, const BoardVendorTelemetryProviderOptions& options);
BoardVendorTelemetrySample CaptureLenovoGameZoneWmiSensorSample(Trace& trace, BoardVendorInfo info);
BoardVendorTelemetrySample CaptureLenovoHardwareScanServiceSample(Trace& trace, BoardVendorInfo info);
