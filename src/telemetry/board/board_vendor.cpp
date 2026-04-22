#include "telemetry/board/board_vendor.h"

#include "util/trace.h"

std::unique_ptr<BoardVendorTelemetryProvider> CreateGigabyteBoardTelemetryProvider(tracing::Trace* trace);

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(tracing::Trace* trace) {
    return CreateGigabyteBoardTelemetryProvider(trace);
}
