#include "telemetry/collector_storage.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include "numeric_safety.h"
#include "telemetry/collector_state.h"
#include "telemetry/collector_support.h"
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

bool IsSelectableStorageDriveType(UINT driveType) {
    return driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE;
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
        candidate.selected =
            std::find(selectedDrives.begin(), selectedDrives.end(), candidate.letter) != selectedDrives.end();
    }
}

void RefreshDriveUsage(TelemetryCollectorState& state) {
    for (auto& drive : state.snapshot_.drives) {
        const std::wstring root = WideFromUtf8(drive.label + "\\");
        const UINT driveType = GetDriveTypeW(root.c_str());
        drive.driveType = driveType;
        if (!IsSelectableStorageDriveType(driveType)) {
            state.trace_.Write(("telemetry:drive_skip label=" + drive.label + " type=" + std::to_string(driveType)).c_str());
            continue;
        }

        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER totalBytes{};
        const BOOL diskOk = GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, nullptr);
        if (!diskOk || totalBytes.QuadPart == 0) {
            state.trace_.Write(("telemetry:drive_space label=" + drive.label + " ok=" +
                                tracing::Trace::BoolText(diskOk != FALSE) +
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
        const auto counterIt = std::find_if(state.storage_.driveCounters.begin(),
            state.storage_.driveCounters.end(),
            [&](const DriveCounterState& counters) { return counters.label == drive.label; });
        PDH_STATUS readStatus = PDH_CSTATUS_NO_OBJECT;
        PDH_STATUS writeStatus = PDH_CSTATUS_NO_OBJECT;
        PDH_FMT_COUNTERVALUE value{};
        if (counterIt != state.storage_.driveCounters.end() && counterIt->readCounter != nullptr) {
            readStatus = PdhGetFormattedCounterValue(counterIt->readCounter, PDH_FMT_DOUBLE, nullptr, &value);
            if (readStatus == ERROR_SUCCESS) {
                drive.readMbps = FiniteNonNegativeOr(value.doubleValue / (1024.0 * 1024.0));
            }
        }
        if (counterIt != state.storage_.driveCounters.end() && counterIt->writeCounter != nullptr) {
            writeStatus = PdhGetFormattedCounterValue(counterIt->writeCounter, PDH_FMT_DOUBLE, nullptr, &value);
            if (writeStatus == ERROR_SUCCESS) {
                drive.writeMbps = FiniteNonNegativeOr(value.doubleValue / (1024.0 * 1024.0));
            }
        }
        state.trace_.Write(("telemetry:drive_space label=" + drive.label +
                            " total_bytes=" + std::to_string(totalBytes.QuadPart) +
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

void UpdateStorageThroughput(TelemetryCollectorState& state, bool initializeOnly) {
    if (state.storage_.query == nullptr) {
        state.trace_.Write("telemetry:storage_rates skipped=no_query");
        return;
    }

    const PDH_STATUS collectStatus = PdhCollectQueryData(state.storage_.query);
    state.trace_.Write(
        ("telemetry:storage_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus)).c_str());

    PDH_FMT_COUNTERVALUE value{};
    PDH_STATUS readStatus = PDH_INVALID_DATA;
    PDH_STATUS writeStatus = PDH_INVALID_DATA;

    if (state.storage_.readCounter != nullptr &&
        (readStatus = PdhGetFormattedCounterValue(state.storage_.readCounter, PDH_FMT_DOUBLE, nullptr, &value)) ==
            ERROR_SUCCESS) {
        state.snapshot_.storage.readMbps = FiniteNonNegativeOr(value.doubleValue / (1024.0 * 1024.0));
    } else if (!initializeOnly) {
        state.snapshot_.storage.readMbps = 0.0;
    }

    if (state.storage_.writeCounter != nullptr &&
        (writeStatus = PdhGetFormattedCounterValue(state.storage_.writeCounter, PDH_FMT_DOUBLE, nullptr, &value)) ==
            ERROR_SUCCESS) {
        state.snapshot_.storage.writeMbps = FiniteNonNegativeOr(value.doubleValue / (1024.0 * 1024.0));
    } else if (!initializeOnly) {
        state.snapshot_.storage.writeMbps = 0.0;
    }

    if (!initializeOnly) {
        state.retainedHistoryStore_.PushSample(state.snapshot_, "storage.read", state.snapshot_.storage.readMbps);
        state.retainedHistoryStore_.PushSample(state.snapshot_, "storage.write", state.snapshot_.storage.writeMbps);
    }

    state.trace_.Write(("telemetry:storage_rates " + tracing::Trace::FormatPdhStatus("read_status", readStatus) +
                        " " + tracing::Trace::FormatPdhStatus("write_status", writeStatus) +
                        " read_mbps=" + tracing::Trace::FormatValueDouble("value", state.snapshot_.storage.readMbps, 3) +
                        " write_mbps=" +
                        tracing::Trace::FormatValueDouble("value", state.snapshot_.storage.writeMbps, 3))
                           .c_str());
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

void ResolveStorageSelection(TelemetryCollectorState& state) {
    state.storage_.driveCandidates = EnumerateStorageDriveCandidates();
    state.storage_.resolvedDriveLetters =
        ResolveConfiguredStorageDrives(state.settings_.selection.configuredDrives, state.storage_.driveCandidates);
    state.resolvedSelections_.drives = state.storage_.resolvedDriveLetters;
    MarkSelectedStorageDriveCandidates(state.storage_.driveCandidates, state.storage_.resolvedDriveLetters);

    state.snapshot_.drives.clear();
    state.storage_.driveCounters.clear();
    for (const auto& letter : state.storage_.resolvedDriveLetters) {
        const std::string label = letter + ":";
        DriveInfo info;
        info.label = label;
        state.snapshot_.drives.push_back(std::move(info));
        state.trace_.Write(("telemetry:drive_config label=" + label).c_str());

        if (state.storage_.query != nullptr) {
            const std::wstring logicalDisk = WideFromUtf8(label);
            DriveCounterState counters;
            counters.label = label;
            const std::wstring readPath = L"\\LogicalDisk(" + logicalDisk + L")\\Disk Read Bytes/sec";
            const std::wstring writePath = L"\\LogicalDisk(" + logicalDisk + L")\\Disk Write Bytes/sec";
            const PDH_STATUS readStatus =
                AddCounterCompat(state.storage_.query, readPath.c_str(), &counters.readCounter);
            const PDH_STATUS writeStatus =
                AddCounterCompat(state.storage_.query, writePath.c_str(), &counters.writeCounter);
            state.trace_.Write(("telemetry:pdh_add drive_read label=" + label + " path=\"" + Utf8FromWide(readPath) +
                                "\" " + tracing::Trace::FormatPdhStatus("status", readStatus))
                                   .c_str());
            state.trace_.Write(("telemetry:pdh_add drive_write label=" + label + " path=\"" + Utf8FromWide(writePath) +
                                "\" " + tracing::Trace::FormatPdhStatus("status", writeStatus))
                                   .c_str());
            state.storage_.driveCounters.push_back(std::move(counters));
        }
    }
    if (state.storage_.driveCandidates.empty()) {
        state.trace_.Write("telemetry:storage_candidates skipped=no_drives");
    }
    state.trace_.Write(("telemetry:storage_candidates count=" + std::to_string(state.storage_.driveCandidates.size()))
                           .c_str());
    state.trace_.Write(("telemetry:drive_enumerate count=" + std::to_string(state.snapshot_.drives.size())).c_str());
    RefreshDriveUsage(state);
}

void UpdateStorageMetrics(TelemetryCollectorState& state, bool initializeOnly) {
    UpdateStorageThroughput(state, initializeOnly);
    RefreshDriveUsage(state);
}
