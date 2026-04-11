#include "telemetry_internal.h"
#include "telemetry_storage_source.h"
#include "telemetry_support.h"

#include <algorithm>
#include <vector>

#include "utf8.h"

namespace {

std::string ReadVolumeLabel(const std::wstring& root) {
    wchar_t volumeName[MAX_PATH] = {};
    if (!GetVolumeInformationW(
            root.c_str(), volumeName, ARRAYSIZE(volumeName), nullptr, nullptr, nullptr, nullptr, 0)) {
        return {};
    }
    return Utf8FromWide(volumeName);
}

std::vector<std::string> SelectFixedDriveLetters(const std::vector<StorageDriveCandidate>& availableDrives) {
    std::vector<std::string> drives;
    for (const auto& drive : availableDrives) {
        if (drive.driveType != DRIVE_FIXED) {
            continue;
        }
        if (std::find(drives.begin(), drives.end(), drive.letter) == drives.end()) {
            drives.push_back(drive.letter);
        }
    }
    return drives;
}

}  // namespace

std::string NormalizeStorageDriveLetter(const std::string& drive) {
    if (drive.empty()) {
        return {};
    }
    const unsigned char ch = static_cast<unsigned char>(drive.front());
    if (!std::isalpha(ch)) {
        return {};
    }
    return std::string(1, static_cast<char>(std::toupper(ch)));
}

bool IsSelectableStorageDriveType(UINT driveType) {
    return driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE;
}

std::vector<StorageDriveCandidate> EnumerateStorageDriveCandidates() {
    std::vector<StorageDriveCandidate> candidates;
    const DWORD bufferLength = GetLogicalDriveStringsW(0, nullptr);
    if (bufferLength == 0) {
        return candidates;
    }

    std::vector<wchar_t> buffer(bufferLength + 1, L'\0');
    const DWORD copied = GetLogicalDriveStringsW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (copied == 0 || copied >= buffer.size()) {
        return {};
    }

    for (const wchar_t* current = buffer.data(); *current != L'\0'; current += wcslen(current) + 1) {
        const std::wstring root(current);
        if (root.size() < 2 || !iswalpha(root[0])) {
            continue;
        }

        const UINT driveType = GetDriveTypeW(root.c_str());
        if (!IsSelectableStorageDriveType(driveType)) {
            continue;
        }

        ULARGE_INTEGER totalBytes{};
        if (!GetDiskFreeSpaceExW(root.c_str(), nullptr, &totalBytes, nullptr) || totalBytes.QuadPart == 0) {
            continue;
        }

        StorageDriveCandidate candidate;
        candidate.letter = std::string(1, static_cast<char>(towupper(root[0])));
        candidate.volumeLabel = ReadVolumeLabel(root);
        candidate.totalGb = totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        candidate.driveType = driveType;
        candidates.push_back(std::move(candidate));
    }

    std::sort(candidates.begin(),
        candidates.end(),
        [](const StorageDriveCandidate& lhs, const StorageDriveCandidate& rhs) { return lhs.letter < rhs.letter; });
    return candidates;
}

std::vector<StorageDriveCandidate> EnumerateSnapshotStorageDriveCandidates(const SystemSnapshot& snapshot) {
    std::vector<StorageDriveCandidate> candidates;
    candidates.reserve(snapshot.drives.size());
    for (const auto& drive : snapshot.drives) {
        if (!IsSelectableStorageDriveType(drive.driveType)) {
            continue;
        }

        StorageDriveCandidate candidate;
        candidate.letter = NormalizeStorageDriveLetter(drive.label);
        if (candidate.letter.empty()) {
            continue;
        }
        candidate.volumeLabel = drive.volumeLabel;
        candidate.totalGb = drive.totalGb;
        candidate.driveType = drive.driveType;
        candidates.push_back(std::move(candidate));
    }

    std::sort(candidates.begin(),
        candidates.end(),
        [](const StorageDriveCandidate& lhs, const StorageDriveCandidate& rhs) { return lhs.letter < rhs.letter; });
    return candidates;
}

std::vector<std::string> ResolveConfiguredStorageDrives(
    const std::vector<std::string>& configuredDrives, const std::vector<StorageDriveCandidate>& availableDrives) {
    std::vector<std::string> resolvedDrives;
    resolvedDrives.reserve(configuredDrives.size());
    for (const auto& drive : configuredDrives) {
        const std::string letter = NormalizeStorageDriveLetter(drive);
        if (letter.empty()) {
            continue;
        }
        if (std::find(resolvedDrives.begin(), resolvedDrives.end(), letter) == resolvedDrives.end()) {
            resolvedDrives.push_back(letter);
        }
    }
    if (!resolvedDrives.empty() || !configuredDrives.empty()) {
        return resolvedDrives;
    }
    return SelectFixedDriveLetters(availableDrives);
}

void MarkSelectedStorageDriveCandidates(
    std::vector<StorageDriveCandidate>& candidates, const std::vector<std::string>& selectedDrives) {
    for (auto& candidate : candidates) {
        candidate.selected = std::find(selectedDrives.begin(), selectedDrives.end(), candidate.letter) != selectedDrives.end();
    }
}

void TelemetryCollector::Impl::ResolveStorageSelection() {
    storage_.driveCandidates = EnumerateStorageDriveCandidates();
    storage_.resolvedDriveLetters = ResolveConfiguredStorageDrives(config_.storage.drives, storage_.driveCandidates);
    MarkSelectedStorageDriveCandidates(storage_.driveCandidates, storage_.resolvedDriveLetters);

    snapshot_.drives.clear();
    storage_.driveCounters.clear();
    for (const auto& letter : storage_.resolvedDriveLetters) {
        const std::string label = letter + ":";
        DriveInfo info;
        info.label = label;
        snapshot_.drives.push_back(std::move(info));
        Trace(("telemetry:drive_config label=" + label).c_str());

        if (storage_.query != nullptr) {
            const std::wstring logicalDisk = WideFromUtf8(label);
            DriveCounterState counters;
            counters.label = label;
            const std::wstring readPath = L"\\LogicalDisk(" + logicalDisk + L")\\Disk Read Bytes/sec";
            const std::wstring writePath = L"\\LogicalDisk(" + logicalDisk + L")\\Disk Write Bytes/sec";
            const PDH_STATUS readStatus = AddCounterCompat(storage_.query, readPath.c_str(), &counters.readCounter);
            const PDH_STATUS writeStatus = AddCounterCompat(storage_.query, writePath.c_str(), &counters.writeCounter);
            Trace(("telemetry:pdh_add drive_read label=" + label + " path=\"" + Utf8FromWide(readPath) + "\" " +
                   tracing::Trace::FormatPdhStatus("status", readStatus))
                    .c_str());
            Trace(("telemetry:pdh_add drive_write label=" + label + " path=\"" + Utf8FromWide(writePath) + "\" " +
                   tracing::Trace::FormatPdhStatus("status", writeStatus))
                    .c_str());
            storage_.driveCounters.push_back(std::move(counters));
        }
    }
    if (storage_.driveCandidates.empty()) {
        Trace("telemetry:storage_candidates skipped=no_drives");
    }
    Trace(("telemetry:storage_candidates count=" + std::to_string(storage_.driveCandidates.size())).c_str());
    Trace(("telemetry:drive_enumerate count=" + std::to_string(snapshot_.drives.size())).c_str());
    RefreshDriveUsage();
}

void TelemetryCollector::Impl::UpdateStorageThroughput(bool initializeOnly) {
    if (storage_.query == nullptr) {
        Trace("telemetry:storage_rates skipped=no_query");
        return;
    }

    const PDH_STATUS collectStatus = PdhCollectQueryData(storage_.query);
    Trace(("telemetry:storage_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus)).c_str());

    PDH_FMT_COUNTERVALUE value{};
    PDH_STATUS readStatus = PDH_INVALID_DATA;
    PDH_STATUS writeStatus = PDH_INVALID_DATA;

    if (storage_.readCounter != nullptr &&
        (readStatus = PdhGetFormattedCounterValue(storage_.readCounter, PDH_FMT_DOUBLE, nullptr, &value)) ==
            ERROR_SUCCESS) {
        snapshot_.storage.readMbps = (std::max)(0.0, value.doubleValue / (1024.0 * 1024.0));
    } else if (!initializeOnly) {
        snapshot_.storage.readMbps = 0.0;
    }

    if (storage_.writeCounter != nullptr &&
        (writeStatus = PdhGetFormattedCounterValue(storage_.writeCounter, PDH_FMT_DOUBLE, nullptr, &value)) ==
            ERROR_SUCCESS) {
        snapshot_.storage.writeMbps = (std::max)(0.0, value.doubleValue / (1024.0 * 1024.0));
    } else if (!initializeOnly) {
        snapshot_.storage.writeMbps = 0.0;
    }

    if (!initializeOnly) {
        retainedHistoryStore_.PushSample(snapshot_, "storage.read", snapshot_.storage.readMbps);
        retainedHistoryStore_.PushSample(snapshot_, "storage.write", snapshot_.storage.writeMbps);
    }

    Trace(("telemetry:storage_rates " + tracing::Trace::FormatPdhStatus("read_status", readStatus) + " " +
           tracing::Trace::FormatPdhStatus("write_status", writeStatus) +
           " read_mbps=" + tracing::Trace::FormatValueDouble("value", snapshot_.storage.readMbps, 3) +
           " write_mbps=" + tracing::Trace::FormatValueDouble("value", snapshot_.storage.writeMbps, 3))
            .c_str());
}

void TelemetryCollector::Impl::CollectStorageMetrics(bool initializeOnly) {
    UpdateStorageThroughput(initializeOnly);
    RefreshDriveUsage();
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
                   " total_bytes=" + std::to_string(totalBytes.QuadPart))
                    .c_str());
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
        const auto counterIt = std::find_if(storage_.driveCounters.begin(),
            storage_.driveCounters.end(),
            [&](const DriveCounterState& counters) { return counters.label == drive.label; });
        PDH_STATUS readStatus = PDH_CSTATUS_NO_OBJECT;
        PDH_STATUS writeStatus = PDH_CSTATUS_NO_OBJECT;
        PDH_FMT_COUNTERVALUE value{};
        if (counterIt != storage_.driveCounters.end() && counterIt->readCounter != nullptr) {
            readStatus = PdhGetFormattedCounterValue(counterIt->readCounter, PDH_FMT_DOUBLE, nullptr, &value);
            if (readStatus == ERROR_SUCCESS) {
                drive.readMbps = (std::max)(0.0, value.doubleValue / (1024.0 * 1024.0));
            }
        }
        if (counterIt != storage_.driveCounters.end() && counterIt->writeCounter != nullptr) {
            writeStatus = PdhGetFormattedCounterValue(counterIt->writeCounter, PDH_FMT_DOUBLE, nullptr, &value);
            if (writeStatus == ERROR_SUCCESS) {
                drive.writeMbps = (std::max)(0.0, value.doubleValue / (1024.0 * 1024.0));
            }
        }
        Trace(("telemetry:drive_space label=" + drive.label + " total_bytes=" + std::to_string(totalBytes.QuadPart) +
               " free_bytes=" + std::to_string(freeBytes.QuadPart) +
               " used_percent=" + tracing::Trace::FormatValueDouble("value", drive.usedPercent, 1) +
               " free_gb=" + tracing::Trace::FormatValueDouble("value", drive.freeGb, 1) +
               " read_status=" + tracing::Trace::FormatPdhStatus("status", readStatus) +
               " write_status=" + tracing::Trace::FormatPdhStatus("status", writeStatus) +
               " read_mbps=" + tracing::Trace::FormatValueDouble("value", drive.readMbps, 3) +
               " write_mbps=" + tracing::Trace::FormatValueDouble("value", drive.writeMbps, 3))
                .c_str());
    }
}
