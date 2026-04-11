#include <gtest/gtest.h>

#include "telemetry_network_source.h"

TEST(TelemetryNetworkSource, EnumeratesSnapshotNetworkCandidateFromResolvedSnapshot) {
    SystemSnapshot snapshot;
    snapshot.network.adapterName = "Ethernet";
    snapshot.network.ipAddress = "192.168.1.5";

    const auto candidates = EnumerateSnapshotNetworkCandidates(snapshot);

    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates[0].adapterName, "Ethernet");
    EXPECT_EQ(candidates[0].ipAddress, "192.168.1.5");
    EXPECT_TRUE(!candidates[0].selected);
}

TEST(TelemetryNetworkSource, ResolvesConfiguredNetworkCandidateByExactMatchFirst) {
    const std::vector<NetworkAdapterCandidate> candidates = {
        {.adapterName = "Ethernet 2", .ipAddress = "10.0.0.2"},
        {.adapterName = "Ethernet", .ipAddress = "10.0.0.3"},
    };

    const auto resolved = ResolveConfiguredNetworkCandidate("ethernet", candidates);

    EXPECT_EQ(resolved.adapterName, "Ethernet");
    EXPECT_EQ(resolved.ipAddress, "10.0.0.3");
}

TEST(TelemetryNetworkSource, ResolvesConfiguredNetworkCandidateByPartialMatchWhenExactIsAbsent) {
    const std::vector<NetworkAdapterCandidate> candidates = {
        {.adapterName = "USB Ethernet Adapter", .ipAddress = "10.0.0.2"},
        {.adapterName = "Wi-Fi", .ipAddress = "10.0.0.3"},
    };

    const auto resolved = ResolveConfiguredNetworkCandidate("ethernet", candidates);

    EXPECT_EQ(resolved.adapterName, "USB Ethernet Adapter");
    EXPECT_EQ(resolved.ipAddress, "10.0.0.2");
}

TEST(TelemetryNetworkSource, ResolvesEmptyConfiguredNetworkCandidateToFirstAvailableCandidate) {
    const std::vector<NetworkAdapterCandidate> candidates = {
        {.adapterName = "Ethernet", .ipAddress = "10.0.0.3"},
    };

    const auto resolved = ResolveConfiguredNetworkCandidate("", candidates);

    EXPECT_EQ(resolved.adapterName, "Ethernet");
    EXPECT_EQ(resolved.ipAddress, "10.0.0.3");
}

TEST(TelemetryNetworkSource, MarksOnlyResolvedNetworkCandidateAsSelected) {
    std::vector<NetworkAdapterCandidate> candidates = {
        {.adapterName = "Ethernet", .ipAddress = "10.0.0.2"},
        {.adapterName = "Ethernet", .ipAddress = "10.0.0.3"},
    };

    MarkSelectedNetworkAdapterCandidates(candidates, {.adapterName = "Ethernet", .ipAddress = "10.0.0.3"});

    EXPECT_TRUE(!candidates[0].selected);
    EXPECT_TRUE(candidates[1].selected);
}
