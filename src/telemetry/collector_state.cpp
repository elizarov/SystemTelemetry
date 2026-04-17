#include "telemetry/collector_state.h"

TelemetryCollectorState::~TelemetryCollectorState() {
    if (cpu_.query != nullptr) {
        PdhCloseQuery(cpu_.query);
    }
    if (gpu_.query != nullptr) {
        PdhCloseQuery(gpu_.query);
    }
    if (gpu_.memoryQuery != nullptr) {
        PdhCloseQuery(gpu_.memoryQuery);
    }
    if (storage_.query != nullptr) {
        PdhCloseQuery(storage_.query);
    }
    WSACleanup();
}
