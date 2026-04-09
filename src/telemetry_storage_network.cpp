#include "telemetry_internal.h"

#include <algorithm>
#include <vector>

#include "utf8.h"

void TelemetryCollector::Impl::EnumerateDrives() {
    snapshot_.drives.clear();
    driveCounters_.clear();
    for (const auto& drive : config_.storage.drives) {
        const std::string letter = NormalizeDriveLetter(drive);
        if (letter.empty()) {
            continue;
        }

        const std::string label = letter + ":";
        DriveInfo info;
        info.label = label;
        snapshot_.drives.push_back(std::move(info));
        Trace(("telemetry:drive_config label=" + label).c_str());

        if (storageQuery_ != nullptr) {
            const std::wstring logicalDisk = WideFromUtf8(label);
            DriveCounterState counters;
            counters.label = label;
            const std::wstring readPath = L"\\LogicalDisk(" + logicalDisk + L")\\Disk Read Bytes/sec";
            const std::wstring writePath = L"\\LogicalDisk(" + logicalDisk + L")\\Disk Write Bytes/sec";
            const PDH_STATUS readStatus = AddCounterCompat(storageQuery_, readPath.c_str(), &counters.readCounter);
            const PDH_STATUS writeStatus = AddCounterCompat(storageQuery_, writePath.c_str(), &counters.writeCounter);
            Trace(("telemetry:pdh_add drive_read label=" + label + " path=\"" + Utf8FromWide(readPath) + "\" " +
                tracing::Trace::FormatPdhStatus("status", readStatus)).c_str());
            Trace(("telemetry:pdh_add drive_write label=" + label + " path=\"" + Utf8FromWide(writePath) + "\" " +
                tracing::Trace::FormatPdhStatus("status", writeStatus)).c_str());
            driveCounters_.push_back(std::move(counters));
        }
    }
    Trace(("telemetry:drive_enumerate count=" + std::to_string(snapshot_.drives.size())).c_str());
    RefreshDriveUsage();
}

void TelemetryCollector::Impl::RefreshStorageDriveCandidates() {
    storageDriveCandidates_.clear();

    const DWORD bufferLength = GetLogicalDriveStringsW(0, nullptr);
    if (bufferLength == 0) {
        Trace("telemetry:storage_candidates skipped=no_drives");
        return;
    }

    std::vector<wchar_t> buffer(bufferLength + 1, L'\0');
    const DWORD copied = GetLogicalDriveStringsW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (copied == 0 || copied >= buffer.size()) {
        Trace("telemetry:storage_candidates skipped=query_failed");
        return;
    }

    for (const wchar_t* current = buffer.data(); *current != L'\0'; current += wcslen(current) + 1) {
        const std::wstring root(current);
        if (root.size() < 2 || !iswalpha(root[0])) {
            continue;
        }

        const UINT driveType = GetDriveTypeW(root.c_str());
        if (!IsSelectableStorageDriveType(driveType)) {
            Trace(("telemetry:storage_candidate_skip root=\"" + Utf8FromWide(root) + "\" type=" + std::to_string(driveType)).c_str());
            continue;
        }

        ULARGE_INTEGER totalBytes{};
        if (!GetDiskFreeSpaceExW(root.c_str(), nullptr, &totalBytes, nullptr) || totalBytes.QuadPart == 0) {
            Trace(("telemetry:storage_candidate_skip root=\"" + Utf8FromWide(root) + "\" type=" + std::to_string(driveType) +
                " total_bytes=" + std::to_string(totalBytes.QuadPart)).c_str());
            continue;
        }

        StorageDriveCandidate candidate;
        candidate.letter = std::string(1, static_cast<char>(towupper(root[0])));
        candidate.volumeLabel = ReadVolumeLabel(root);
        candidate.totalGb = totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        candidate.driveType = driveType;
        candidate.selected = std::find(config_.storage.drives.begin(), config_.storage.drives.end(), candidate.letter) != config_.storage.drives.end();
        storageDriveCandidates_.push_back(std::move(candidate));
    }

    std::sort(storageDriveCandidates_.begin(), storageDriveCandidates_.end(), [](const StorageDriveCandidate& lhs, const StorageDriveCandidate& rhs) {
        return lhs.letter < rhs.letter;
    });
    Trace(("telemetry:storage_candidates count=" + std::to_string(storageDriveCandidates_.size())).c_str());
}

void TelemetryCollector::Impl::UpdateStorageThroughput(bool initializeOnly) {
    if (storageQuery_ == nullptr) {
        Trace("telemetry:storage_rates skipped=no_query");
        return;
    }

    const PDH_STATUS collectStatus = PdhCollectQueryData(storageQuery_);
    Trace(("telemetry:storage_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus)).c_str());

    PDH_FMT_COUNTERVALUE value{};
    PDH_STATUS readStatus = PDH_INVALID_DATA;
    PDH_STATUS writeStatus = PDH_INVALID_DATA;

    if (storageReadCounter_ != nullptr &&
        (readStatus = PdhGetFormattedCounterValue(storageReadCounter_, PDH_FMT_DOUBLE, nullptr, &value)) == ERROR_SUCCESS) {
        snapshot_.storage.readMbps = std::max(0.0, value.doubleValue / (1024.0 * 1024.0));
    } else if (!initializeOnly) {
        snapshot_.storage.readMbps = 0.0;
    }

    if (storageWriteCounter_ != nullptr &&
        (writeStatus = PdhGetFormattedCounterValue(storageWriteCounter_, PDH_FMT_DOUBLE, nullptr, &value)) == ERROR_SUCCESS) {
        snapshot_.storage.writeMbps = std::max(0.0, value.doubleValue / (1024.0 * 1024.0));
    } else if (!initializeOnly) {
        snapshot_.storage.writeMbps = 0.0;
    }

    if (!initializeOnly) {
        PushRetainedHistorySample("storage.read", snapshot_.storage.readMbps);
        PushRetainedHistorySample("storage.write", snapshot_.storage.writeMbps);
    }

    Trace(("telemetry:storage_rates " +
        tracing::Trace::FormatPdhStatus("read_status", readStatus) + " " +
        tracing::Trace::FormatPdhStatus("write_status", writeStatus) +
        " read_mbps=" + tracing::Trace::FormatValueDouble("value", snapshot_.storage.readMbps, 3) +
        " write_mbps=" + tracing::Trace::FormatValueDouble("value", snapshot_.storage.writeMbps, 3)).c_str());
}

void TelemetryCollector::Impl::RefreshDriveUsage() {
    for (auto& drive : snapshot_.drives) {
        const std::wstring root = WideFromUtf8(drive.label + "\\");
        const UINT driveType = GetDriveTypeW(root.c_str());
        drive.driveType = driveType;
        if (!IsSelectableStorageDriveType(driveType)) {
            Trace(("telemetry:drive_skip label=" + drive.label + " type=" + std::to_string(driveType)).c_str());
            continue;
        }

        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER totalBytes{};
        const BOOL diskOk = GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, nullptr);
        if (!diskOk || totalBytes.QuadPart == 0) {
            Trace(("telemetry:drive_space label=" + drive.label + " ok=" + tracing::Trace::BoolText(diskOk != FALSE) +
                " total_bytes=" + std::to_string(totalBytes.QuadPart)).c_str());
            continue;
        }

        const double totalGb = totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        const double freeGb = freeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        drive.totalGb = totalGb;
        drive.freeGb = freeGb;
        drive.volumeLabel = ReadVolumeLabel(root);
        drive.usedPercent = std::clamp((1.0 - (freeGb / totalGb)) * 100.0, 0.0, 100.0);
        drive.readMbps = 0.0;
        drive.writeMbps = 0.0;
        const auto counterIt = std::find_if(driveCounters_.begin(), driveCounters_.end(), [&](const DriveCounterState& counters) {
            return counters.label == drive.label;
        });
        PDH_STATUS readStatus = PDH_CSTATUS_NO_OBJECT;
        PDH_STATUS writeStatus = PDH_CSTATUS_NO_OBJECT;
        PDH_FMT_COUNTERVALUE value{};
        if (counterIt != driveCounters_.end() && counterIt->readCounter != nullptr) {
            readStatus = PdhGetFormattedCounterValue(counterIt->readCounter, PDH_FMT_DOUBLE, nullptr, &value);
            if (readStatus == ERROR_SUCCESS) {
                drive.readMbps = std::max(0.0, value.doubleValue / (1024.0 * 1024.0));
            }
        }
        if (counterIt != driveCounters_.end() && counterIt->writeCounter != nullptr) {
            writeStatus = PdhGetFormattedCounterValue(counterIt->writeCounter, PDH_FMT_DOUBLE, nullptr, &value);
            if (writeStatus == ERROR_SUCCESS) {
                drive.writeMbps = std::max(0.0, value.doubleValue / (1024.0 * 1024.0));
            }
        }
        Trace(("telemetry:drive_space label=" + drive.label +
            " total_bytes=" + std::to_string(totalBytes.QuadPart) +
            " free_bytes=" + std::to_string(freeBytes.QuadPart) +
            " used_percent=" + tracing::Trace::FormatValueDouble("value", drive.usedPercent, 1) +
            " free_gb=" + tracing::Trace::FormatValueDouble("value", drive.freeGb, 1) +
            " read_status=" + tracing::Trace::FormatPdhStatus("status", readStatus) +
            " write_status=" + tracing::Trace::FormatPdhStatus("status", writeStatus) +
            " read_mbps=" + tracing::Trace::FormatValueDouble("value", drive.readMbps, 3) +
            " write_mbps=" + tracing::Trace::FormatValueDouble("value", drive.writeMbps, 3)).c_str());
    }
}

void TelemetryCollector::Impl::PushHistorySample(std::vector<double>& history, double value) {
    if (history.empty()) {
        return;
    }
    history.erase(history.begin());
    history.push_back(value);
}

void TelemetryCollector::Impl::PushRetainedHistorySample(const std::string& seriesRef, double value) {
    auto it = snapshot_.retainedHistoryIndexByRef.find(seriesRef);
    if (it == snapshot_.retainedHistoryIndexByRef.end()) {
        const size_t index = snapshot_.retainedHistories.size();
        snapshot_.retainedHistories.push_back(CreateRetainedHistorySeries(seriesRef));
        snapshot_.retainedHistoryIndexByRef.emplace(seriesRef, index);
        it = snapshot_.retainedHistoryIndexByRef.find(seriesRef);
    }
    PushHistorySample(snapshot_.retainedHistories[it->second].samples, value);
}

void TelemetryCollector::Impl::PushBoardMetricHistorySamples() {
    for (const auto& metric : snapshot_.boardTemperatures) {
        PushRetainedHistorySample("board.temp." + metric.name,
            ResolveScaleRatio(metric.metric.value.value_or(0.0), config_.metricScales.boardTemperatureC));
    }
    for (const auto& metric : snapshot_.boardFans) {
        PushRetainedHistorySample("board.fan." + metric.name,
            ResolveScaleRatio(metric.metric.value.value_or(0.0), config_.metricScales.boardFanRpm));
    }
}

void TelemetryCollector::Impl::UpdateNetworkState(bool initializeOnly) {
    PMIB_IF_TABLE2 table = nullptr;
    const DWORD tableStatus = GetIfTable2(&table);
    if (tableStatus != NO_ERROR || table == nullptr) {
        Trace(("telemetry:network_table " + tracing::Trace::FormatWin32Status("status", tableStatus) +
            " table=" + tracing::Trace::BoolText(table != nullptr)).c_str());
        return;
    }
    Trace(("telemetry:network_table " + tracing::Trace::FormatWin32Status("status", tableStatus) +
        " entries=" + std::to_string(table->NumEntries) +
        " initialize_only=" + tracing::Trace::BoolText(initializeOnly)).c_str());

    ULONG addressBufferSize = 0;
    const ULONG addressProbeStatus = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, nullptr, &addressBufferSize);
    Trace(("telemetry:network_ip_probe " + tracing::Trace::FormatWin32Status("status", addressProbeStatus) +
        " size=" + std::to_string(addressBufferSize)).c_str());

    std::vector<BYTE> addressBuffer;
    IP_ADAPTER_ADDRESSES* addresses = nullptr;
    ULONG addressFetchStatus = addressProbeStatus;
    if (addressProbeStatus == ERROR_BUFFER_OVERFLOW && addressBufferSize > 0) {
        addressBuffer.resize(addressBufferSize);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(addressBuffer.data());
        addressFetchStatus = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, addresses, &addressBufferSize);
    }
    Trace(("telemetry:network_ip_fetch " + tracing::Trace::FormatWin32Status("status", addressFetchStatus) +
        " size=" + std::to_string(addressBufferSize)).c_str());
    if (addressFetchStatus != NO_ERROR) {
        addresses = nullptr;
    }

    const auto now = std::chrono::steady_clock::now();
    MIB_IF_ROW2* selected = nullptr;
    uint64_t selectedTraffic = 0;
    AdapterSelectionInfo selectedInfo;
    std::vector<NetworkCandidateState> candidates;
    bool configuredCandidateAvailable = false;
    if (!config_.network.adapterName.empty()) {
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const auto& row = table->Table[i];
            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) {
                continue;
            }
            const AdapterSelectionInfo info = BuildAdapterSelectionInfo(row, addresses);
            if (!info.hasIpv4) {
                continue;
            }
            if (EqualsInsensitive(row.Alias, config_.network.adapterName) ||
                EqualsInsensitive(row.Description, config_.network.adapterName) ||
                ContainsInsensitive(row.Alias, config_.network.adapterName) ||
                ContainsInsensitive(row.Description, config_.network.adapterName)) {
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

        const bool configuredExactMatch =
            configuredCandidateAvailable &&
            (EqualsInsensitive(row.Alias, config_.network.adapterName) ||
                EqualsInsensitive(row.Description, config_.network.adapterName));
        const bool configuredPartialMatch =
            configuredCandidateAvailable &&
            !configuredExactMatch &&
            (ContainsInsensitive(row.Alias, config_.network.adapterName) ||
                ContainsInsensitive(row.Description, config_.network.adapterName));
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

        Trace(("telemetry:network_candidate interface=" + std::to_string(row.InterfaceIndex) +
            " alias=\"" + Utf8FromWide(row.Alias) + "\" description=\"" + Utf8FromWide(row.Description) + "\"" +
            " exact_match=" + tracing::Trace::BoolText(configuredExactMatch) +
            " partial_match=" + tracing::Trace::BoolText(configuredPartialMatch) +
            " matched=" + tracing::Trace::BoolText(info.matched) +
            " has_ipv4=" + tracing::Trace::BoolText(info.hasIpv4) +
            " has_gateway=" + tracing::Trace::BoolText(info.hasGateway) +
            " hardware=" + tracing::Trace::BoolText(hardwareInterface) +
            " connector=" + tracing::Trace::BoolText(connectorPresent) +
            " traffic=" + std::to_string(traffic) +
            " ip=" + info.ipAddress).c_str());

        NetworkCandidateState candidateState;
        candidateState.interfaceIndex = row.InterfaceIndex;
        candidateState.candidate.adapterName = Utf8FromWide(
            row.Alias[0] != L'\0' ? std::wstring_view(row.Alias) : std::wstring_view(row.Description));
        candidateState.candidate.ipAddress = info.ipAddress;
        candidates.push_back(std::move(candidateState));

        if (!configuredCandidateAvailable) {
            const bool candidatePreferred =
                selected == nullptr ||
                (info.hasGateway && !selectedInfo.hasGateway) ||
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
        } else if (
            selected == nullptr ||
            (configuredExactMatch && !(
                EqualsInsensitive(selected->Alias, config_.network.adapterName) ||
                EqualsInsensitive(selected->Description, config_.network.adapterName))) ||
            (configuredExactMatch ==
                (EqualsInsensitive(selected->Alias, config_.network.adapterName) ||
                    EqualsInsensitive(selected->Description, config_.network.adapterName)) &&
                (info.hasGateway || info.hasIpv4))) {
            selected = &row;
            selectedTraffic = traffic;
            selectedInfo = info;
            if (configuredExactMatch && (info.hasGateway || info.hasIpv4)) {
                break;
            }
        }
    }

    networkAdapterCandidates_.clear();
    networkAdapterCandidates_.reserve(candidates.size());
    for (auto& candidate : candidates) {
        if (selected != nullptr && candidate.interfaceIndex == selected->InterfaceIndex) {
            candidate.candidate.selected = true;
        }
        networkAdapterCandidates_.push_back(std::move(candidate.candidate));
    }

    if (selected != nullptr) {
        Trace(("telemetry:network_selected interface=" + std::to_string(selected->InterfaceIndex) +
            " alias=\"" + Utf8FromWide(selected->Alias) + "\" description=\"" + Utf8FromWide(selected->Description) +
            "\" has_ipv4=" + tracing::Trace::BoolText(selectedInfo.hasIpv4) +
            " has_gateway=" + tracing::Trace::BoolText(selectedInfo.hasGateway) +
            " traffic=" + std::to_string(selectedTraffic) +
            " ip=" + selectedInfo.ipAddress).c_str());
        snapshot_.network.adapterName = Utf8FromWide(
            selected->Alias[0] != L'\0' ? std::wstring_view(selected->Alias) : std::wstring_view(selected->Description));
        if (selectedIndex_ != selected->InterfaceIndex) {
            selectedIndex_ = selected->InterfaceIndex;
            previousInOctets_ = selected->InOctets;
            previousOutOctets_ = selected->OutOctets;
            previousNetworkTick_ = now;
        } else if (!initializeOnly) {
            const double seconds = std::chrono::duration<double>(now - previousNetworkTick_).count();
            if (seconds > 0.0) {
                snapshot_.network.downloadMbps =
                    ((selected->InOctets - previousInOctets_) / seconds) / (1024.0 * 1024.0);
                snapshot_.network.uploadMbps =
                    ((selected->OutOctets - previousOutOctets_) / seconds) / (1024.0 * 1024.0);
                PushRetainedHistorySample("network.upload", snapshot_.network.uploadMbps);
                PushRetainedHistorySample("network.download", snapshot_.network.downloadMbps);
                Trace(("telemetry:network_rates interface=" + std::to_string(selected->InterfaceIndex) +
                    " seconds=" + tracing::Trace::FormatValueDouble("value", seconds, 3) +
                    " upload_mbps=" + tracing::Trace::FormatValueDouble("value", snapshot_.network.uploadMbps, 3) +
                    " download_mbps=" + tracing::Trace::FormatValueDouble("value", snapshot_.network.downloadMbps, 3)).c_str());
            }
            previousInOctets_ = selected->InOctets;
            previousOutOctets_ = selected->OutOctets;
            previousNetworkTick_ = now;
        }

        snapshot_.network.ipAddress = selectedInfo.ipAddress;
        if (selectedInfo.hasIpv4) {
            Trace(("telemetry:network_ip_found interface=" + std::to_string(selected->InterfaceIndex) +
                " ip=" + selectedInfo.ipAddress).c_str());
        } else {
            Trace(("telemetry:network_ip_missing interface=" + std::to_string(selected->InterfaceIndex)).c_str());
        }
    } else {
        Trace("telemetry:network_selected interface=none");
    }

    FreeMibTable(table);
    Trace("telemetry:network_table_free status=done");
}
