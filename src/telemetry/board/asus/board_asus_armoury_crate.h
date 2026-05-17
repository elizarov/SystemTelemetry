#pragma once

#include <memory>

#include "telemetry/board/board_vendor.h"

std::unique_ptr<BoardVendorTelemetryProvider> CreateAsusBoardTelemetryProvider(Trace& trace, BoardVendorInfo info);
