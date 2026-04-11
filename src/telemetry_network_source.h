#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "telemetry.h"

struct ResolvedNetworkCandidate {
    std::string adapterName;
    std::string ipAddress = "N/A";
};

inline std::string ToLowerNetworkSelectionAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::vector<NetworkAdapterCandidate> EnumerateSnapshotNetworkCandidates(const SystemSnapshot& snapshot) {
    std::vector<NetworkAdapterCandidate> candidates;
    if (snapshot.network.adapterName.empty() || snapshot.network.adapterName == "Auto") {
        return candidates;
    }

    NetworkAdapterCandidate candidate;
    candidate.adapterName = snapshot.network.adapterName;
    candidate.ipAddress = snapshot.network.ipAddress.empty() ? "N/A" : snapshot.network.ipAddress;
    candidates.push_back(std::move(candidate));
    return candidates;
}

inline ResolvedNetworkCandidate ResolveConfiguredNetworkCandidate(
    const std::string& configuredAdapterName, const std::vector<NetworkAdapterCandidate>& availableCandidates) {
    ResolvedNetworkCandidate resolved;
    if (availableCandidates.empty()) {
        return resolved;
    }

    const std::string configuredLower = ToLowerNetworkSelectionAscii(configuredAdapterName);
    const auto exactIt = std::find_if(availableCandidates.begin(), availableCandidates.end(), [&](const auto& candidate) {
        return !configuredLower.empty() && ToLowerNetworkSelectionAscii(candidate.adapterName) == configuredLower;
    });
    if (exactIt != availableCandidates.end()) {
        resolved.adapterName = exactIt->adapterName;
        resolved.ipAddress = exactIt->ipAddress;
        return resolved;
    }

    const auto partialIt =
        std::find_if(availableCandidates.begin(), availableCandidates.end(), [&](const auto& candidate) {
            return !configuredLower.empty() &&
                   ToLowerNetworkSelectionAscii(candidate.adapterName).find(configuredLower) != std::string::npos;
        });
    if (partialIt != availableCandidates.end()) {
        resolved.adapterName = partialIt->adapterName;
        resolved.ipAddress = partialIt->ipAddress;
        return resolved;
    }

    resolved.adapterName = availableCandidates.front().adapterName;
    resolved.ipAddress = availableCandidates.front().ipAddress;
    return resolved;
}

inline void MarkSelectedNetworkAdapterCandidates(
    std::vector<NetworkAdapterCandidate>& candidates, const ResolvedNetworkCandidate& selectedCandidate) {
    bool selected = false;
    for (auto& candidate : candidates) {
        const bool sameName = candidate.adapterName == selectedCandidate.adapterName;
        const bool sameIp = selectedCandidate.ipAddress.empty() || selectedCandidate.ipAddress == "N/A" ||
                            candidate.ipAddress == selectedCandidate.ipAddress;
        candidate.selected = !selected && sameName && sameIp;
        selected = selected || candidate.selected;
    }
}
