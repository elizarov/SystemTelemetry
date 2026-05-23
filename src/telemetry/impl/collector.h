#pragma once

#include <memory>
#include <string>
#include <vector>

#include "telemetry/telemetry.h"

class TelemetryCollector {
public:
    virtual ~TelemetryCollector() = default;

    TelemetryCollector(const TelemetryCollector&) = delete;
    TelemetryCollector& operator=(const TelemetryCollector&) = delete;
    TelemetryCollector(TelemetryCollector&&) = delete;
    TelemetryCollector& operator=(TelemetryCollector&&) = delete;

    virtual bool Initialize(const TelemetrySettings& settings, std::string* errorText = nullptr) = 0;
    virtual const SystemSnapshot& Snapshot() const = 0;
    virtual TelemetryDump Dump() const = 0;
    virtual const ResolvedTelemetrySelections& ResolvedSelections() const = 0;
    virtual const std::vector<GpuAdapterCandidate>& GpuAdapterCandidates() const = 0;
    virtual const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const = 0;
    virtual const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const = 0;
    virtual void ApplySettings(const TelemetrySettings& settings) = 0;
    virtual void SetPreferredNetworkAdapterName(std::string adapterName) = 0;
    virtual void SetPreferredGpuAdapterName(std::string adapterName) = 0;
    virtual void SetSelectedStorageDrives(std::vector<std::string> driveLetters) = 0;
    virtual void RefreshSelectionsAndSnapshot() = 0;
    virtual void UpdateSnapshot() = 0;

protected:
    TelemetryCollector() = default;
};

std::unique_ptr<TelemetryCollector> CreateTelemetryCollector(
    const TelemetryCollectorOptions& options,
    const FilePath& workingDirectory,
    Trace& trace
);
