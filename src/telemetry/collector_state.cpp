#include "telemetry/collector_state.h"

TelemetryCollectorState::~TelemetryCollectorState() {
    if (cpuQuery_ != nullptr) {
        PdhCloseQuery(cpuQuery_);
    }
    if (gpuQuery_ != nullptr) {
        PdhCloseQuery(gpuQuery_);
    }
    if (gpuMemoryQuery_ != nullptr) {
        PdhCloseQuery(gpuMemoryQuery_);
    }
    if (storage_.query != nullptr) {
        PdhCloseQuery(storage_.query);
    }
    WSACleanup();
}
