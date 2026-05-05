#include "telemetry/impl/collector_network.h"

#include <cstring>
#include <string>
#include <vector>

#include "telemetry/impl/collector_state.h"
#include "util/numeric_safety.h"
#include "util/strings.h"
#include "util/utf8.h"

namespace {

struct AdapterSelectionInfo {
    bool matched = false;
    bool hasIpv4 = false;
    bool hasGateway = false;
    std::string ipAddress = "N/A";
};

struct NetworkCandidateState {
    NetworkAdapterCandidate candidate;
    AdapterSelectionInfo info;
    std::string alias;
    std::string description;
    ULONG interfaceIndex = 0;
    uint64_t inOctets = 0;
    uint64_t outOctets = 0;
    uint64_t traffic = 0;
    int matchRank = 0;
    bool hardwareInterface = false;
    bool connectorPresent = false;
    bool visible = false;
};

std::string Win32StatusCodeString(DWORD status) {
    return std::to_string(static_cast<unsigned long>(status));
}

bool AdapterMatchesRow(const IP_ADAPTER_ADDRESSES& adapter, const MIB_IF_ROW2& row) {
    return adapter.Luid.Value == row.InterfaceLuid.Value || adapter.IfIndex == row.InterfaceIndex ||
           adapter.Ipv6IfIndex == row.InterfaceIndex ||
           (adapter.FriendlyName != nullptr && _wcsicmp(adapter.FriendlyName, row.Alias) == 0) ||
           (adapter.Description != nullptr && _wcsicmp(adapter.Description, row.Description) == 0);
}

bool HasUsableGateway(const IP_ADAPTER_ADDRESSES& adapter) {
    for (auto* gateway = adapter.FirstGatewayAddress; gateway != nullptr; gateway = gateway->Next) {
        const sockaddr* address = gateway->Address.lpSockaddr;
        if (address == nullptr) {
            continue;
        }
        if (address->sa_family == AF_INET) {
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
            if (ipv4->sin_addr.S_un.S_addr != 0) {
                return true;
            }
        } else if (address->sa_family == AF_INET6) {
            const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
            static const IN6_ADDR zeroAddress{};
            if (memcmp(&ipv6->sin6_addr, &zeroAddress, sizeof(zeroAddress)) != 0) {
                return true;
            }
        }
    }
    return false;
}

AdapterSelectionInfo BuildAdapterSelectionInfo(const MIB_IF_ROW2& row, const IP_ADAPTER_ADDRESSES* addresses) {
    AdapterSelectionInfo info;
    for (auto* current = addresses; current != nullptr; current = current->Next) {
        if (!AdapterMatchesRow(*current, row)) {
            continue;
        }

        info.matched = true;
        info.hasGateway = HasUsableGateway(*current);
        for (auto* unicast = current->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr == nullptr || unicast->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }

            wchar_t address[128];
            DWORD length = ARRAYSIZE(address);
            if (WSAAddressToStringW(unicast->Address.lpSockaddr,
                    static_cast<DWORD>(unicast->Address.iSockaddrLength),
                    nullptr,
                    address,
                    &length) == 0) {
                info.hasIpv4 = true;
                info.ipAddress = Utf8FromWide(address);
                break;
            }
        }
        break;
    }
    return info;
}

int PreferredAdapterMatchRank(
    const std::string& alias, const std::string& description, const std::string& preferredAdapterName) {
    if (preferredAdapterName.empty()) {
        return 0;
    }
    if (EqualsInsensitive(alias, preferredAdapterName) || EqualsInsensitive(description, preferredAdapterName)) {
        return 2;
    }
    return ContainsInsensitive(alias, preferredAdapterName) || ContainsInsensitive(description, preferredAdapterName)
               ? 1
               : 0;
}

bool IsAutomaticNetworkCandidatePreferred(
    const NetworkCandidateState& candidate, const NetworkCandidateState* selected) {
    if (selected == nullptr) {
        return true;
    }
    return (candidate.info.hasGateway && !selected->info.hasGateway) ||
           (candidate.info.hasGateway == selected->info.hasGateway && candidate.info.hasIpv4 &&
               !selected->info.hasIpv4) ||
           (candidate.info.hasGateway == selected->info.hasGateway &&
               candidate.info.hasIpv4 == selected->info.hasIpv4 && candidate.hardwareInterface &&
               !selected->hardwareInterface) ||
           (candidate.info.hasGateway == selected->info.hasGateway &&
               candidate.info.hasIpv4 == selected->info.hasIpv4 &&
               candidate.hardwareInterface == selected->hardwareInterface && candidate.connectorPresent &&
               !selected->connectorPresent) ||
           (candidate.info.hasGateway == selected->info.hasGateway &&
               candidate.info.hasIpv4 == selected->info.hasIpv4 &&
               candidate.hardwareInterface == selected->hardwareInterface &&
               candidate.connectorPresent == selected->connectorPresent && candidate.traffic > selected->traffic);
}

}  // namespace

void ResolveNetworkSelection(RealTelemetryCollectorState& state) {
    PMIB_IF_TABLE2 table = nullptr;
    const DWORD tableStatus = GetIfTable2(&table);
    if (tableStatus != NO_ERROR || table == nullptr) {
        state.trace_.Write(("telemetry:network_table status=" + Win32StatusCodeString(tableStatus) +
                            " table=" + Trace::BoolText(table != nullptr))
                .c_str());
        return;
    }
    state.trace_.Write(("telemetry:network_table status=" + Win32StatusCodeString(tableStatus) +
                        " entries=" + std::to_string(table->NumEntries))
            .c_str());

    ULONG addressBufferSize = 0;
    const ULONG addressProbeStatus = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, nullptr, &addressBufferSize);
    state.trace_.Write(("telemetry:network_ip_probe status=" + Win32StatusCodeString(addressProbeStatus) +
                        " size=" + std::to_string(addressBufferSize))
            .c_str());

    std::vector<BYTE> addressBuffer;
    IP_ADAPTER_ADDRESSES* addresses = nullptr;
    ULONG addressFetchStatus = addressProbeStatus;
    if (addressProbeStatus == ERROR_BUFFER_OVERFLOW && addressBufferSize > 0) {
        addressBuffer.resize(addressBufferSize);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(addressBuffer.data());
        addressFetchStatus = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, addresses, &addressBufferSize);
    }
    state.trace_.Write(("telemetry:network_ip_fetch status=" + Win32StatusCodeString(addressFetchStatus) +
                        " size=" + std::to_string(addressBufferSize))
            .c_str());
    if (addressFetchStatus != NO_ERROR) {
        addresses = nullptr;
    }

    std::vector<NetworkCandidateState> candidates;
    bool configuredCandidateAvailable = false;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        auto& row = table->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) {
            continue;
        }

        const AdapterSelectionInfo info = BuildAdapterSelectionInfo(row, addresses);
        if (!info.hasIpv4) {
            continue;
        }

        NetworkCandidateState candidateState;
        candidateState.interfaceIndex = row.InterfaceIndex;
        candidateState.info = info;
        candidateState.alias = Utf8FromWide(row.Alias);
        candidateState.description = Utf8FromWide(row.Description);
        candidateState.inOctets = row.InOctets;
        candidateState.outOctets = row.OutOctets;
        candidateState.traffic = candidateState.inOctets + candidateState.outOctets;
        candidateState.hardwareInterface = row.InterfaceAndOperStatusFlags.HardwareInterface != FALSE;
        candidateState.connectorPresent = row.InterfaceAndOperStatusFlags.ConnectorPresent != FALSE;
        candidateState.matchRank = PreferredAdapterMatchRank(
            candidateState.alias, candidateState.description, state.settings_.selection.preferredAdapterName);
        configuredCandidateAvailable = configuredCandidateAvailable || candidateState.matchRank > 0;
        candidateState.candidate.adapterName =
            !candidateState.alias.empty() ? candidateState.alias : candidateState.description;
        candidateState.candidate.ipAddress = info.ipAddress;
        candidates.push_back(std::move(candidateState));
    }

    NetworkCandidateState* selected = nullptr;
    for (auto& candidate : candidates) {
        const bool configuredExactMatch = configuredCandidateAvailable && candidate.matchRank == 2;
        const bool configuredPartialMatch = configuredCandidateAvailable && candidate.matchRank == 1;
        if (configuredCandidateAvailable && !configuredExactMatch && !configuredPartialMatch) {
            continue;
        }

        candidate.visible = true;
        state.trace_.Write(
            ("telemetry:network_candidate interface=" + std::to_string(candidate.interfaceIndex) + " alias=\"" +
                candidate.alias + "\" description=\"" + candidate.description + "\"" + " exact_match=" +
                Trace::BoolText(configuredExactMatch) + " partial_match=" + Trace::BoolText(configuredPartialMatch) +
                " matched=" + Trace::BoolText(candidate.info.matched) + " has_ipv4=" +
                Trace::BoolText(candidate.info.hasIpv4) + " has_gateway=" + Trace::BoolText(candidate.info.hasGateway) +
                " hardware=" + Trace::BoolText(candidate.hardwareInterface) +
                " connector=" + Trace::BoolText(candidate.connectorPresent) +
                " traffic=" + std::to_string(candidate.traffic) + " ip=" + candidate.info.ipAddress)
                .c_str());

        if (!configuredCandidateAvailable) {
            if (IsAutomaticNetworkCandidatePreferred(candidate, selected)) {
                selected = &candidate;
            }
        } else if (selected == nullptr || configuredExactMatch ||
                   (candidate.matchRank == selected->matchRank &&
                       (candidate.info.hasGateway || candidate.info.hasIpv4))) {
            selected = &candidate;
            if (configuredExactMatch && (candidate.info.hasGateway || candidate.info.hasIpv4)) {
                break;
            }
        }
    }

    state.network_.adapterCandidates.clear();
    state.network_.adapterCandidates.reserve(candidates.size());
    state.resolvedSelections_.adapterName.clear();
    state.network_.resolvedIpAddress = "N/A";
    state.network_.selectedIndex = 0;
    state.network_.previousInOctets = 0;
    state.network_.previousOutOctets = 0;
    state.network_.previousTick = {};
    state.snapshot_.network.uploadMbps = 0.0;
    state.snapshot_.network.downloadMbps = 0.0;
    for (auto& candidate : candidates) {
        if (!candidate.visible) {
            continue;
        }
        if (selected != nullptr && candidate.interfaceIndex == selected->interfaceIndex) {
            candidate.candidate.selected = true;
        }
        state.network_.adapterCandidates.push_back(std::move(candidate.candidate));
    }

    if (selected != nullptr) {
        state.trace_.Write(("telemetry:network_selected interface=" + std::to_string(selected->interfaceIndex) +
                            " alias=\"" + selected->alias + "\" description=\"" + selected->description +
                            "\" has_ipv4=" + Trace::BoolText(selected->info.hasIpv4) +
                            " has_gateway=" + Trace::BoolText(selected->info.hasGateway) +
                            " traffic=" + std::to_string(selected->traffic) + " ip=" + selected->info.ipAddress)
                .c_str());
        state.snapshot_.network.adapterName = !selected->alias.empty() ? selected->alias : selected->description;
        state.resolvedSelections_.adapterName = state.snapshot_.network.adapterName;
        state.snapshot_.network.ipAddress = selected->info.ipAddress;
        state.network_.resolvedIpAddress = selected->info.ipAddress;
        state.network_.selectedIndex = selected->interfaceIndex;
        state.network_.previousInOctets = selected->inOctets;
        state.network_.previousOutOctets = selected->outOctets;
        state.network_.previousTick = std::chrono::steady_clock::now();
        if (selected->info.hasIpv4) {
            state.trace_.Write(("telemetry:network_ip_found interface=" + std::to_string(selected->interfaceIndex) +
                                " ip=" + selected->info.ipAddress)
                    .c_str());
        } else {
            state.trace_.Write(
                ("telemetry:network_ip_missing interface=" + std::to_string(selected->interfaceIndex)).c_str());
        }
    } else {
        state.snapshot_.network.adapterName = state.settings_.selection.preferredAdapterName.empty()
                                                  ? "Auto"
                                                  : state.settings_.selection.preferredAdapterName;
        state.snapshot_.network.ipAddress = "N/A";
        state.trace_.Write("telemetry:network_selected interface=none");
    }

    FreeMibTable(table);
    state.trace_.Write("telemetry:network_table_free status=done");
}

void UpdateNetworkMetrics(RealTelemetryCollectorState& state, bool initializeOnly) {
    if (state.network_.selectedIndex == 0) {
        if (!initializeOnly) {
            state.snapshot_.network.uploadMbps = 0.0;
            state.snapshot_.network.downloadMbps = 0.0;
        }
        state.trace_.Write("telemetry:network_rates skipped=no_selection");
        return;
    }

    MIB_IF_ROW2 selected{};
    selected.InterfaceIndex = state.network_.selectedIndex;
    const DWORD rowStatus = GetIfEntry2(&selected);
    if (rowStatus != NO_ERROR) {
        state.trace_.WriteLazy([&] {
            return "telemetry:network_row status=" + Win32StatusCodeString(rowStatus) +
                   " interface=" + std::to_string(state.network_.selectedIndex);
        });
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (selected.OperStatus != IfOperStatusUp) {
        if (!initializeOnly) {
            state.snapshot_.network.uploadMbps = 0.0;
            state.snapshot_.network.downloadMbps = 0.0;
        }
        state.trace_.WriteLazy([&] {
            return "telemetry:network_rates skipped=selection_missing interface=" +
                   std::to_string(state.network_.selectedIndex);
        });
        return;
    }

    state.snapshot_.network.adapterName = state.resolvedSelections_.adapterName.empty()
                                              ? state.snapshot_.network.adapterName
                                              : state.resolvedSelections_.adapterName;
    state.snapshot_.network.ipAddress = state.network_.resolvedIpAddress;
    if (!initializeOnly && state.network_.previousTick.time_since_epoch().count() != 0) {
        const double seconds = std::chrono::duration<double>(now - state.network_.previousTick).count();
        if (IsFiniteDouble(seconds) && seconds > 0.0) {
            const uint64_t inDelta = selected.InOctets >= state.network_.previousInOctets
                                         ? (selected.InOctets - state.network_.previousInOctets)
                                         : 0;
            const uint64_t outDelta = selected.OutOctets >= state.network_.previousOutOctets
                                          ? (selected.OutOctets - state.network_.previousOutOctets)
                                          : 0;
            state.snapshot_.network.downloadMbps =
                FiniteNonNegativeOr((static_cast<double>(inDelta) / seconds) / (1024.0 * 1024.0));
            state.snapshot_.network.uploadMbps =
                FiniteNonNegativeOr((static_cast<double>(outDelta) / seconds) / (1024.0 * 1024.0));
            state.retainedHistoryStore_.PushSample(
                state.snapshot_, "network.upload", state.snapshot_.network.uploadMbps);
            state.retainedHistoryStore_.PushSample(
                state.snapshot_, "network.download", state.snapshot_.network.downloadMbps);
            state.trace_.WriteLazy([&] {
                return "telemetry:network_rates interface=" + std::to_string(selected.InterfaceIndex) +
                       " seconds=" + Trace::FormatValueDouble("value", seconds, 3) +
                       " upload_mbps=" + Trace::FormatValueDouble("value", state.snapshot_.network.uploadMbps, 3) +
                       " download_mbps=" + Trace::FormatValueDouble("value", state.snapshot_.network.downloadMbps, 3);
            });
        } else {
            state.snapshot_.network.uploadMbps = 0.0;
            state.snapshot_.network.downloadMbps = 0.0;
        }
    }

    state.network_.previousInOctets = selected.InOctets;
    state.network_.previousOutOctets = selected.OutOctets;
    state.network_.previousTick = now;
}
