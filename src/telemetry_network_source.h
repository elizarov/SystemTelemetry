#pragma once

#include <string>
#include <vector>

#include "telemetry.h"

struct ResolvedNetworkCandidate {
    std::string adapterName;
    std::string ipAddress = "N/A";
};

std::vector<NetworkAdapterCandidate> EnumerateSnapshotNetworkCandidates(const SystemSnapshot& snapshot);
ResolvedNetworkCandidate ResolveConfiguredNetworkCandidate(
    const std::string& configuredAdapterName, const std::vector<NetworkAdapterCandidate>& availableCandidates);
void MarkSelectedNetworkAdapterCandidates(
    std::vector<NetworkAdapterCandidate>& candidates, const ResolvedNetworkCandidate& selectedCandidate);
