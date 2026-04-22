#include "telemetry/board/board_vendor.h"

#include "telemetry/board/gigabyte/board_gigabyte_siv.h"
#include "util/trace.h"

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(tracing::Trace* trace) {
    return CreateGigabyteBoardTelemetryProvider(trace);
}
