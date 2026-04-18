#include "telemetry/collector_network.h"

#include <cstring>
#include <vector>

#include "app_strings.h"
#include "numeric_safety.h"
#include "telemetry/collector_state.h"
#include "utf8.h"

namespace {

struct AdapterSelectionInfo {
    bool matched = false;
    bool hasIpv4 = false;
    bool hasGateway = false;
    std::string ipAddress = "N/A";
};

struct NetworkCandidateState {
    NetworkAdapterCandidate candidate;
    ULONG interfaceIndex = 0;
};

bool EqualsWideAndUtf8Insensitive(const wchar_t* value, const std::string& needle) {
    return value != nullptr && EqualsInsensitive(Utf8FromWide(value), needle);
}

bool ContainsWideAndUtf8Insensitive(const wchar_t* value, const std::string& needle) {
    return value != nullptr && ContainsInsensitive(Utf8FromWide(value), needle);
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

}  // namespace

void ResolveNetworkSelection(RealTelemetryCollectorState& state) {
    PMIB_IF_TABLE2 table = nullptr;
    const DWORD tableStatus = GetIfTable2(&table);
    if (tableStatus != NO_ERROR || table == nullptr) {
        state.trace_.Write(("telemetry:network_table " + tracing::Trace::FormatWin32Status("status", tableStatus) +
                            " table=" + tracing::Trace::BoolText(table != nullptr))
                .c_str());
        return;
    }
    state.trace_.Write(("telemetry:network_table " + tracing::Trace::FormatWin32Status("status", tableStatus) +
                        " entries=" + std::to_string(table->NumEntries))
            .c_str());

    ULONG addressBufferSize = 0;
    const ULONG addressProbeStatus = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, nullptr, &addressBufferSize);
    state.trace_.Write(
        ("telemetry:network_ip_probe " + tracing::Trace::FormatWin32Status("status", addressProbeStatus) +
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
    state.trace_.Write(
        ("telemetry:network_ip_fetch " + tracing::Trace::FormatWin32Status("status", addressFetchStatus) +
            " size=" + std::to_string(addressBufferSize))
            .c_str());
    if (addressFetchStatus != NO_ERROR) {
        addresses = nullptr;
    }

    MIB_IF_ROW2* selected = nullptr;
    uint64_t selectedTraffic = 0;
    AdapterSelectionInfo selectedInfo;
    std::vector<NetworkCandidateState> candidates;
    bool configuredCandidateAvailable = false;
    if (!state.settings_.selection.preferredAdapterName.empty()) {
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const auto& row = table->Table[i];
            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) {
                continue;
            }
            const AdapterSelectionInfo info = BuildAdapterSelectionInfo(row, addresses);
            if (!info.hasIpv4) {
                continue;
            }
            if (EqualsWideAndUtf8Insensitive(row.Alias, state.settings_.selection.preferredAdapterName) ||
                EqualsWideAndUtf8Insensitive(row.Description, state.settings_.selection.preferredAdapterName) ||
                ContainsWideAndUtf8Insensitive(row.Alias, state.settings_.selection.preferredAdapterName) ||
                ContainsWideAndUtf8Insensitive(row.Description, state.settings_.selection.preferredAdapterName)) {
                configuredCandidateAvailable = true;
                break;
            }
        }
    }
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        auto& row = table->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) {
            continue;
        }

        const bool selectedIsExactMatch =
            selected != nullptr &&
            (EqualsWideAndUtf8Insensitive(selected->Alias, state.settings_.selection.preferredAdapterName) ||
                EqualsWideAndUtf8Insensitive(selected->Description, state.settings_.selection.preferredAdapterName));
        const bool configuredExactMatch =
            configuredCandidateAvailable &&
            (EqualsWideAndUtf8Insensitive(row.Alias, state.settings_.selection.preferredAdapterName) ||
                EqualsWideAndUtf8Insensitive(row.Description, state.settings_.selection.preferredAdapterName));
        const bool configuredPartialMatch =
            configuredCandidateAvailable && !configuredExactMatch &&
            (ContainsWideAndUtf8Insensitive(row.Alias, state.settings_.selection.preferredAdapterName) ||
                ContainsWideAndUtf8Insensitive(row.Description, state.settings_.selection.preferredAdapterName));
        if (configuredCandidateAvailable && !configuredExactMatch && !configuredPartialMatch) {
            continue;
        }

        const AdapterSelectionInfo info = BuildAdapterSelectionInfo(row, addresses);
        if (!info.hasIpv4) {
            continue;
        }
        const uint64_t traffic = row.InOctets + row.OutOctets;
        const bool hardwareInterface = row.InterfaceAndOperStatusFlags.HardwareInterface != FALSE;
        const bool connectorPresent = row.InterfaceAndOperStatusFlags.ConnectorPresent != FALSE;

        state.trace_.Write((
            "telemetry:network_candidate interface=" + std::to_string(row.InterfaceIndex) + " alias=\"" +
            Utf8FromWide(row.Alias) + "\" description=\"" + Utf8FromWide(row.Description) + "\"" +
            " exact_match=" + tracing::Trace::BoolText(configuredExactMatch) + " partial_match=" +
            tracing::Trace::BoolText(configuredPartialMatch) + " matched=" + tracing::Trace::BoolText(info.matched) +
            " has_ipv4=" + tracing::Trace::BoolText(info.hasIpv4) + " has_gateway=" +
            tracing::Trace::BoolText(info.hasGateway) + " hardware=" + tracing::Trace::BoolText(hardwareInterface) +
            " connector=" + tracing::Trace::BoolText(connectorPresent) + " traffic=" + std::to_string(traffic) +
            " ip=" + info.ipAddress)
                .c_str());

        NetworkCandidateState candidateState;
        candidateState.interfaceIndex = row.InterfaceIndex;
        candidateState.candidate.adapterName =
            Utf8FromWide(row.Alias[0] != L'\0' ? std::wstring_view(row.Alias) : std::wstring_view(row.Description));
        candidateState.candidate.ipAddress = info.ipAddress;
        candidates.push_back(std::move(candidateState));

        if (!configuredCandidateAvailable) {
            const bool candidatePreferred =
                selected == nullptr || (info.hasGateway && !selectedInfo.hasGateway) ||
                (info.hasGateway == selectedInfo.hasGateway && info.hasIpv4 && !selectedInfo.hasIpv4) ||
                (info.hasGateway == selectedInfo.hasGateway && info.hasIpv4 == selectedInfo.hasIpv4 &&
                    hardwareInterface && !selected->InterfaceAndOperStatusFlags.HardwareInterface) ||
                (info.hasGateway == selectedInfo.hasGateway && info.hasIpv4 == selectedInfo.hasIpv4 &&
                    hardwareInterface == (selected->InterfaceAndOperStatusFlags.HardwareInterface != FALSE) &&
                    connectorPresent && !selected->InterfaceAndOperStatusFlags.ConnectorPresent) ||
                (info.hasGateway == selectedInfo.hasGateway && info.hasIpv4 == selectedInfo.hasIpv4 &&
                    hardwareInterface == (selected->InterfaceAndOperStatusFlags.HardwareInterface != FALSE) &&
                    connectorPresent == (selected->InterfaceAndOperStatusFlags.ConnectorPresent != FALSE) &&
                    traffic > selectedTraffic);
            if (candidatePreferred) {
                selected = &row;
                selectedTraffic = traffic;
                selectedInfo = info;
            }
        } else if (selected == nullptr || (configuredExactMatch && !selectedIsExactMatch) ||
                   (configuredExactMatch == selectedIsExactMatch && (info.hasGateway || info.hasIpv4))) {
            selected = &row;
            selectedTraffic = traffic;
            selectedInfo = info;
            if (configuredExactMatch && (info.hasGateway || info.hasIpv4)) {
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
        if (selected != nullptr && candidate.interfaceIndex == selected->InterfaceIndex) {
            candidate.candidate.selected = true;
        }
        state.network_.adapterCandidates.push_back(std::move(candidate.candidate));
    }

    if (selected != nullptr) {
        state.trace_.Write(
            ("telemetry:network_selected interface=" + std::to_string(selected->InterfaceIndex) + " alias=\"" +
                Utf8FromWide(selected->Alias) + "\" description=\"" + Utf8FromWide(selected->Description) +
                "\" has_ipv4=" + tracing::Trace::BoolText(selectedInfo.hasIpv4) +
                " has_gateway=" + tracing::Trace::BoolText(selectedInfo.hasGateway) +
                " traffic=" + std::to_string(selectedTraffic) + " ip=" + selectedInfo.ipAddress)
                .c_str());
        state.snapshot_.network.adapterName =
            Utf8FromWide(selected->Alias[0] != L'\0' ? std::wstring_view(selected->Alias)
                                                     : std::wstring_view(selected->Description));
        state.resolvedSelections_.adapterName = state.snapshot_.network.adapterName;
        state.snapshot_.network.ipAddress = selectedInfo.ipAddress;
        state.network_.resolvedIpAddress = selectedInfo.ipAddress;
        state.network_.selectedIndex = selected->InterfaceIndex;
        state.network_.previousInOctets = selected->InOctets;
        state.network_.previousOutOctets = selected->OutOctets;
        state.network_.previousTick = std::chrono::steady_clock::now();
        if (selectedInfo.hasIpv4) {
            state.trace_.Write(("telemetry:network_ip_found interface=" + std::to_string(selected->InterfaceIndex) +
                                " ip=" + selectedInfo.ipAddress)
                    .c_str());
        } else {
            state.trace_.Write(
                ("telemetry:network_ip_missing interface=" + std::to_string(selected->InterfaceIndex)).c_str());
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
            return "telemetry:network_row " + tracing::Trace::FormatWin32Status("status", rowStatus) +
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
                       " seconds=" + tracing::Trace::FormatValueDouble("value", seconds, 3) + " upload_mbps=" +
                       tracing::Trace::FormatValueDouble("value", state.snapshot_.network.uploadMbps, 3) +
                       " download_mbps=" +
                       tracing::Trace::FormatValueDouble("value", state.snapshot_.network.downloadMbps, 3);
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
