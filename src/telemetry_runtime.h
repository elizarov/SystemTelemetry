#pragma once

#include <memory>
#include <ostream>

#include "config.h"
#include "telemetry.h"

class TelemetryRuntime {
public:
    virtual ~TelemetryRuntime() = default;
    virtual bool Initialize(const AppConfig& config, std::ostream* traceStream) = 0;
    virtual const SystemSnapshot& Snapshot() const = 0;
    virtual TelemetryDump Dump() const = 0;
    virtual AppConfig EffectiveConfig() const = 0;
    virtual const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const = 0;
    virtual const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const = 0;
    virtual void SetEffectiveConfig(const AppConfig& config) = 0;
    virtual void SetPreferredNetworkAdapterName(const std::string& adapterName) = 0;
    virtual void SetSelectedStorageDrives(const std::vector<std::string>& driveLetters) = 0;
    virtual void RefreshSelectionsAndSnapshot() = 0;
    virtual void UpdateSnapshot() = 0;
};
