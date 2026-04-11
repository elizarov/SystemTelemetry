#include "telemetry_runtime_state.h"

void RuntimeCandidateView::SyncNetworkFromTelemetry(const TelemetryCollector& telemetry) {
    networkAdapters = telemetry.NetworkAdapterCandidates();
}

void RuntimeCandidateView::SyncStorageFromTelemetry(const TelemetryCollector& telemetry) {
    storageDrives = telemetry.StorageDriveCandidates();
}
