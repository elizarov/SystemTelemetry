#include "telemetry/board_service_sample.h"

#include "telemetry/board/board_vendor.h"
#include "telemetry/board/lenovo/board_lenovo_vantage.h"

BoardVendorTelemetrySample CaptureBoardSensorsServiceSample(Trace& trace) {
    return CaptureLenovoHardwareScanServiceSample(trace, ExtractBoardVendorInfo());
}
