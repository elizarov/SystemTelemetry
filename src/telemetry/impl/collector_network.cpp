#include "telemetry/impl/collector_network.h"

#include <ws2tcpip.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "telemetry/impl/collector_state.h"
#include "util/numeric_safety.h"
#include "util/strings.h"
#include "util/text_encoding.h"

namespace {

struct AdapterSelectionInfo {
    bool matched = false;
    bool hasIpv4 = false;
    bool hasGateway = false;
    std::string ipAddress = "N/A";
};

struct NetworkCandidateState {
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
};

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

            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            char address[INET_ADDRSTRLEN] = {};
            if (InetNtopA(AF_INET, const_cast<IN_ADDR*>(&ipv4->sin_addr), address, ARRAYSIZE(address)) != nullptr) {
                info.hasIpv4 = true;
                info.ipAddress = address;
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
    return ContainsInsensitive(alias, preferredAdapterName) || ContainsInsensitive(description, preferredAdapterName) ?
        1 :
        0;
}

bool IsAutomaticNetworkCandidatePreferred(
    const NetworkCandidateState& candidate, const NetworkCandidateState* selected) {
    if (selected == nullptr) {
        return true;
    }
    return (candidate.info.hasGateway && !selected->info.hasGateway) ||
        (candidate.info.hasGateway == selected->info.hasGateway && candidate.info.hasIpv4 && !selected->info.hasIpv4) ||
        (candidate.info.hasGateway == selected->info.hasGateway && candidate.info.hasIpv4 == selected->info.hasIpv4 &&
            candidate.hardwareInterface && !selected->hardwareInterface) ||
        (candidate.info.hasGateway == selected->info.hasGateway && candidate.info.hasIpv4 == selected->info.hasIpv4 &&
            candidate.hardwareInterface == selected->hardwareInterface && candidate.connectorPresent &&
            !selected->connectorPresent) ||
        (candidate.info.hasGateway == selected->info.hasGateway && candidate.info.hasIpv4 == selected->info.hasIpv4 &&
            candidate.hardwareInterface == selected->hardwareInterface &&
            candidate.connectorPresent == selected->connectorPresent && candidate.traffic > selected->traffic);
}

bool HasPreferredNetworkCandidate(
    const MIB_IF_TABLE2& table, const IP_ADAPTER_ADDRESSES* addresses, const std::string& preferredAdapterName) {
    if (preferredAdapterName.empty()) {
        return false;
    }
    for (ULONG i = 0; i < table.NumEntries; ++i) {
        const auto& row = table.Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) {
            continue;
        }

        const AdapterSelectionInfo info = BuildAdapterSelectionInfo(row, addresses);
        if (!info.hasIpv4) {
            continue;
        }
        if (PreferredAdapterMatchRank(TextFromWide(row.Alias), TextFromWide(row.Description), preferredAdapterName) >
            0) {
            return true;
        }
    }
    return false;
}

}  // namespace

void ResolveNetworkSelection(RealTelemetryCollectorState& state) {
    PMIB_IF_TABLE2 table = nullptr;
    const DWORD tableStatus = GetIfTable2(&table);
    if (tableStatus != NO_ERROR || table == nullptr) {
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("network_table status=%lu table=%s"),
            static_cast<unsigned long>(tableStatus),
            Trace::BoolText(table != nullptr));
        return;
    }
    state.trace_.WriteFmt(TracePrefix::Telemetry,
        RES_STR("network_table status=%lu entries=%lu"),
        static_cast<unsigned long>(tableStatus),
        static_cast<unsigned long>(table->NumEntries));

    ULONG addressBufferSize = 0;
    const ULONG addressProbeStatus = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, nullptr, &addressBufferSize);
    state.trace_.WriteFmt(TracePrefix::Telemetry,
        RES_STR("network_ip_probe status=%lu size=%lu"),
        static_cast<unsigned long>(addressProbeStatus),
        static_cast<unsigned long>(addressBufferSize));

    std::vector<BYTE> addressBuffer;
    IP_ADAPTER_ADDRESSES* addresses = nullptr;
    ULONG addressFetchStatus = addressProbeStatus;
    if (addressProbeStatus == ERROR_BUFFER_OVERFLOW && addressBufferSize > 0) {
        addressBuffer.resize(addressBufferSize);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(addressBuffer.data());
        addressFetchStatus = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, addresses, &addressBufferSize);
    }
    state.trace_.WriteFmt(TracePrefix::Telemetry,
        RES_STR("network_ip_fetch status=%lu size=%lu"),
        static_cast<unsigned long>(addressFetchStatus),
        static_cast<unsigned long>(addressBufferSize));
    if (addressFetchStatus != NO_ERROR) {
        addresses = nullptr;
    }

    const bool configuredCandidateAvailable =
        HasPreferredNetworkCandidate(*table, addresses, state.settings_.selection.preferredAdapterName);
    NetworkCandidateState selectedCandidate;
    NetworkCandidateState* selected = nullptr;
    size_t selectedAdapterCandidateIndex = 0;
    state.network_.adapterCandidates.clear();
    state.network_.adapterCandidates.reserve(static_cast<size_t>(table->NumEntries));
    state.resolvedSelections_.adapterName.clear();
    state.network_.resolvedIpAddress = "N/A";
    state.network_.selectedIndex = 0;
    state.network_.previousInOctets = 0;
    state.network_.previousOutOctets = 0;
    state.network_.previousTick = {};
    state.snapshot_.network.uploadMbps = 0.0;
    state.snapshot_.network.downloadMbps = 0.0;
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
        candidateState.alias = TextFromWide(row.Alias);
        candidateState.description = TextFromWide(row.Description);
        candidateState.inOctets = row.InOctets;
        candidateState.outOctets = row.OutOctets;
        candidateState.traffic = candidateState.inOctets + candidateState.outOctets;
        candidateState.hardwareInterface = row.InterfaceAndOperStatusFlags.HardwareInterface != FALSE;
        candidateState.connectorPresent = row.InterfaceAndOperStatusFlags.ConnectorPresent != FALSE;
        candidateState.matchRank = PreferredAdapterMatchRank(
            candidateState.alias, candidateState.description, state.settings_.selection.preferredAdapterName);
        const bool configuredExactMatch = configuredCandidateAvailable && candidateState.matchRank == 2;
        const bool configuredPartialMatch = configuredCandidateAvailable && candidateState.matchRank == 1;
        if (configuredCandidateAvailable && !configuredExactMatch && !configuredPartialMatch) {
            continue;
        }

        state.trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("network_candidate interface=%u alias=\"%s\" description=\"%s\" exact_match=%s partial_match=%s "
                    "matched=%s "
                    "has_ipv4=%s has_gateway=%s hardware=%s connector=%s traffic=%llu ip=%s"),
            candidateState.interfaceIndex,
            candidateState.alias.c_str(),
            candidateState.description.c_str(),
            Trace::BoolText(configuredExactMatch),
            Trace::BoolText(configuredPartialMatch),
            Trace::BoolText(candidateState.info.matched),
            Trace::BoolText(candidateState.info.hasIpv4),
            Trace::BoolText(candidateState.info.hasGateway),
            Trace::BoolText(candidateState.hardwareInterface),
            Trace::BoolText(candidateState.connectorPresent),
            static_cast<unsigned long long>(candidateState.traffic),
            candidateState.info.ipAddress.c_str());

        NetworkAdapterCandidate adapterCandidate;
        adapterCandidate.adapterName =
            !candidateState.alias.empty() ? candidateState.alias : candidateState.description;
        adapterCandidate.ipAddress = candidateState.info.ipAddress;
        state.network_.adapterCandidates.push_back(std::move(adapterCandidate));
        const size_t adapterCandidateIndex = state.network_.adapterCandidates.size() - 1;
        const auto selectCandidate = [&] {
            if (selected != nullptr) {
                state.network_.adapterCandidates[selectedAdapterCandidateIndex].selected = false;
            }
            selectedCandidate = std::move(candidateState);
            selected = &selectedCandidate;
            selectedAdapterCandidateIndex = adapterCandidateIndex;
            state.network_.adapterCandidates[adapterCandidateIndex].selected = true;
        };

        if (!configuredCandidateAvailable) {
            if (IsAutomaticNetworkCandidatePreferred(candidateState, selected)) {
                selectCandidate();
            }
        } else if (selected == nullptr || configuredExactMatch ||
            (candidateState.matchRank == selected->matchRank &&
                (candidateState.info.hasGateway || candidateState.info.hasIpv4))) {
            selectCandidate();
            if (configuredExactMatch && (selected->info.hasGateway || selected->info.hasIpv4)) {
                break;
            }
        }
    }

    if (selected != nullptr) {
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR(
                "network_selected interface=%u alias=\"%s\" description=\"%s\" has_ipv4=%s has_gateway=%s traffic=%llu "
                "ip=%s"),
            selected->interfaceIndex,
            selected->alias.c_str(),
            selected->description.c_str(),
            Trace::BoolText(selected->info.hasIpv4),
            Trace::BoolText(selected->info.hasGateway),
            static_cast<unsigned long long>(selected->traffic),
            selected->info.ipAddress.c_str());
        state.snapshot_.network.adapterName = !selected->alias.empty() ? selected->alias : selected->description;
        state.resolvedSelections_.adapterName = state.snapshot_.network.adapterName;
        state.snapshot_.network.ipAddress = selected->info.ipAddress;
        state.network_.resolvedIpAddress = selected->info.ipAddress;
        state.network_.selectedIndex = selected->interfaceIndex;
        state.network_.previousInOctets = selected->inOctets;
        state.network_.previousOutOctets = selected->outOctets;
        state.network_.previousTick = std::chrono::steady_clock::now();
        if (selected->info.hasIpv4) {
            state.trace_.WriteFmt(TracePrefix::Telemetry,
                RES_STR("network_ip_found interface=%u ip=%s"),
                selected->interfaceIndex,
                selected->info.ipAddress.c_str());
        } else {
            state.trace_.WriteFmt(
                TracePrefix::Telemetry, RES_STR("network_ip_missing interface=%u"), selected->interfaceIndex);
        }
    } else {
        state.snapshot_.network.adapterName = state.settings_.selection.preferredAdapterName.empty() ? "Auto" :
            state.settings_.selection.preferredAdapterName;
        state.snapshot_.network.ipAddress = "N/A";
        state.trace_.Write(TracePrefix::Telemetry, RES_STR("network_selected interface=none"));
    }

    FreeMibTable(table);
    state.trace_.Write(TracePrefix::Telemetry, RES_STR("network_table_free status=done"));
}

void UpdateNetworkMetrics(RealTelemetryCollectorState& state, bool initializeOnly) {
    if (state.network_.selectedIndex == 0) {
        if (!initializeOnly) {
            state.snapshot_.network.uploadMbps = 0.0;
            state.snapshot_.network.downloadMbps = 0.0;
        }
        state.trace_.Write(TracePrefix::Telemetry, RES_STR("network_rates skipped=no_selection"));
        return;
    }

    MIB_IF_ROW2 selected{};
    selected.InterfaceIndex = state.network_.selectedIndex;
    const DWORD rowStatus = GetIfEntry2(&selected);
    if (rowStatus != NO_ERROR) {
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("network_row status=%lu interface=%lu"),
            static_cast<unsigned long>(rowStatus),
            state.network_.selectedIndex);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (selected.OperStatus != IfOperStatusUp) {
        if (!initializeOnly) {
            state.snapshot_.network.uploadMbps = 0.0;
            state.snapshot_.network.downloadMbps = 0.0;
        }
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("network_rates skipped=selection_missing interface=%lu"),
            state.network_.selectedIndex);
        return;
    }

    state.snapshot_.network.adapterName = state.resolvedSelections_.adapterName.empty() ?
        state.snapshot_.network.adapterName :
        state.resolvedSelections_.adapterName;
    state.snapshot_.network.ipAddress = state.network_.resolvedIpAddress;
    if (!initializeOnly && state.network_.previousTick.time_since_epoch().count() != 0) {
        const double seconds = std::chrono::duration<double>(now - state.network_.previousTick).count();
        if (IsFiniteDouble(seconds) && seconds > 0.0) {
            const uint64_t inDelta = selected.InOctets >= state.network_.previousInOctets ?
                (selected.InOctets - state.network_.previousInOctets) :
                0;
            const uint64_t outDelta = selected.OutOctets >= state.network_.previousOutOctets ?
                (selected.OutOctets - state.network_.previousOutOctets) :
                0;
            state.snapshot_.network.downloadMbps =
                FiniteNonNegativeOr((static_cast<double>(inDelta) / seconds) / (1024.0 * 1024.0));
            state.snapshot_.network.uploadMbps =
                FiniteNonNegativeOr((static_cast<double>(outDelta) / seconds) / (1024.0 * 1024.0));
            state.retainedHistoryStore_.PushSample(
                state.snapshot_, RetainedHistoryKey::NetworkUpload, state.snapshot_.network.uploadMbps);
            state.retainedHistoryStore_.PushSample(
                state.snapshot_, RetainedHistoryKey::NetworkDownload, state.snapshot_.network.downloadMbps);
            state.trace_.WriteFmt(TracePrefix::Telemetry,
                RES_STR(
                    "network_rates interface=%lu seconds=value=%.3f upload_mbps=value=%.3f download_mbps=value=%.3f"),
                selected.InterfaceIndex,
                seconds,
                state.snapshot_.network.uploadMbps,
                state.snapshot_.network.downloadMbps);
        } else {
            state.snapshot_.network.uploadMbps = 0.0;
            state.snapshot_.network.downloadMbps = 0.0;
        }
    }

    state.network_.previousInOctets = selected.InOctets;
    state.network_.previousOutOctets = selected.OutOctets;
    state.network_.previousTick = now;
}
