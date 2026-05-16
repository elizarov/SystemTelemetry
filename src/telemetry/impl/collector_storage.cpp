#include "telemetry/impl/collector_storage.h"

#include <algorithm>
#include <vector>

#include "telemetry/impl/collector_state.h"
#include "telemetry/impl/collector_storage_selection.h"
#include "telemetry/impl/collector_support.h"
#include "util/numeric_safety.h"
#include "util/utf8.h"

namespace {

std::string ReadVolumeLabel(const std::wstring& root) {
    wchar_t volumeName[MAX_PATH] = {};
    if (!GetVolumeInformationW(
            root.c_str(), volumeName, ARRAYSIZE(volumeName), nullptr, nullptr, nullptr, nullptr, 0)) {
        return {};
    }
    return Utf8FromWide(volumeName);
}

std::vector<StorageDriveCandidate> EnumerateStorageDriveCandidates() {
    std::vector<StorageDriveCandidate> candidates;
    const DWORD bufferLength = GetLogicalDriveStringsW(0, nullptr);
    if (bufferLength == 0) {
        return candidates;
    }

    std::vector<wchar_t> buffer(bufferLength + 1, wchar_t{});
    const DWORD copied = GetLogicalDriveStringsW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (copied == 0 || copied >= buffer.size()) {
        return {};
    }

    for (const wchar_t* current = buffer.data(); *current != wchar_t{}; current += wcslen(current) + 1) {
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

    SortStorageDriveCandidatesByLetter(candidates);
    return candidates;
}

void MarkSelectedStorageDriveCandidates(
    std::vector<StorageDriveCandidate>& candidates, const std::vector<std::string>& selectedDrives) {
    for (auto& candidate : candidates) {
        candidate.selected =
            std::find(selectedDrives.begin(), selectedDrives.end(), candidate.letter) != selectedDrives.end();
    }
}

void RefreshDriveUsage(RealTelemetryCollectorState& state) {
    for (size_t i = 0; i < state.snapshot_.drives.size(); ++i) {
        auto& drive = state.snapshot_.drives[i];
        DriveCounterState* counters =
            i < state.storage_.driveCounters.size() ? &state.storage_.driveCounters[i] : nullptr;
        const std::wstring root = WideFromUtf8(counters != nullptr ? counters->rootPath : drive.label + "\\");
        const UINT driveType = GetDriveTypeW(root.c_str());
        drive.driveType = driveType;
        if (!IsSelectableStorageDriveType(driveType)) {
            state.trace_.WriteLazyFmt(
                TracePrefix::Telemetry, "drive_skip label=%s type=%u", drive.label.c_str(), driveType);
            continue;
        }

        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER totalBytes{};
        const BOOL diskOk = GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, nullptr);
        if (!diskOk || totalBytes.QuadPart == 0) {
            state.trace_.WriteLazyFmt(TracePrefix::Telemetry,
                "drive_space label=%s ok=%s total_bytes=%llu",
                drive.label.c_str(),
                Trace::BoolText(diskOk != FALSE),
                static_cast<unsigned long long>(totalBytes.QuadPart));
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
        PDH_STATUS readStatus = PDH_CSTATUS_NO_OBJECT;
        PDH_STATUS writeStatus = PDH_CSTATUS_NO_OBJECT;
        PDH_FMT_COUNTERVALUE value{};
        if (counters != nullptr && counters->readCounter != nullptr) {
            readStatus = PdhGetFormattedCounterValue(counters->readCounter, PDH_FMT_DOUBLE, nullptr, &value);
            if (readStatus == ERROR_SUCCESS) {
                drive.readMbps = FiniteNonNegativeOr(value.doubleValue / (1024.0 * 1024.0));
            }
        }
        if (counters != nullptr && counters->writeCounter != nullptr) {
            writeStatus = PdhGetFormattedCounterValue(counters->writeCounter, PDH_FMT_DOUBLE, nullptr, &value);
            if (writeStatus == ERROR_SUCCESS) {
                drive.writeMbps = FiniteNonNegativeOr(value.doubleValue / (1024.0 * 1024.0));
            }
        }
        state.trace_.WriteLazyFmt(TracePrefix::Telemetry,
            "drive_space label=%s total_bytes=%llu free_bytes=%llu used_percent=value=%.1f free_gb=value=%.1f "
            "read_status=%ld write_status=%ld read_mbps=value=%.3f write_mbps=value=%.3f",
            drive.label.c_str(),
            static_cast<unsigned long long>(totalBytes.QuadPart),
            static_cast<unsigned long long>(freeBytes.QuadPart),
            drive.usedPercent,
            drive.freeGb,
            static_cast<long>(readStatus),
            static_cast<long>(writeStatus),
            drive.readMbps,
            drive.writeMbps);
    }
}

void UpdateStorageThroughput(RealTelemetryCollectorState& state, bool initializeOnly) {
    if (state.storage_.query == nullptr) {
        state.trace_.Write(TracePrefix::Telemetry, "storage_rates skipped=no_query");
        return;
    }

    const PDH_STATUS collectStatus = PdhCollectQueryData(state.storage_.query);
    state.trace_.WriteLazyFmt(TracePrefix::Telemetry, "storage_collect status=%ld", static_cast<long>(collectStatus));

    PDH_FMT_COUNTERVALUE value{};
    PDH_STATUS readStatus = PDH_INVALID_DATA;
    PDH_STATUS writeStatus = PDH_INVALID_DATA;

    if (state.storage_.readCounter != nullptr) {
        readStatus = PdhGetFormattedCounterValue(state.storage_.readCounter, PDH_FMT_DOUBLE, nullptr, &value);
        if (readStatus == ERROR_SUCCESS) {
            state.snapshot_.storage.readMbps = FiniteNonNegativeOr(value.doubleValue / (1024.0 * 1024.0));
        } else if (!initializeOnly) {
            state.snapshot_.storage.readMbps = 0.0;
        }
    } else if (!initializeOnly) {
        state.snapshot_.storage.readMbps = 0.0;
    }

    if (state.storage_.writeCounter != nullptr) {
        writeStatus = PdhGetFormattedCounterValue(state.storage_.writeCounter, PDH_FMT_DOUBLE, nullptr, &value);
        if (writeStatus == ERROR_SUCCESS) {
            state.snapshot_.storage.writeMbps = FiniteNonNegativeOr(value.doubleValue / (1024.0 * 1024.0));
        } else if (!initializeOnly) {
            state.snapshot_.storage.writeMbps = 0.0;
        }
    } else if (!initializeOnly) {
        state.snapshot_.storage.writeMbps = 0.0;
    }

    if (!initializeOnly) {
        state.retainedHistoryStore_.PushSample(
            state.snapshot_, RetainedHistoryKey::StorageRead, state.snapshot_.storage.readMbps);
        state.retainedHistoryStore_.PushSample(
            state.snapshot_, RetainedHistoryKey::StorageWrite, state.snapshot_.storage.writeMbps);
    }

    state.trace_.WriteLazyFmt(TracePrefix::Telemetry,
        "storage_rates read_status=%ld write_status=%ld read_mbps=value=%.3f write_mbps=value=%.3f",
        static_cast<long>(readStatus),
        static_cast<long>(writeStatus),
        state.snapshot_.storage.readMbps,
        state.snapshot_.storage.writeMbps);
}

}  // namespace

void InitializeStorageCollector(RealTelemetryCollectorState& state) {
    const PDH_STATUS queryStatus = PdhOpenQueryW(nullptr, 0, &state.storage_.query);
    state.trace_.WriteFmt(TracePrefix::Telemetry, "pdh_open storage_query status=%ld", static_cast<long>(queryStatus));
    const PDH_STATUS readStatus = AddCounterCompat(
        state.storage_.query, "\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", &state.storage_.readCounter);
    state.trace_.WriteFmt(TracePrefix::Telemetry,
        "pdh_add storage_read path=\"\\\\PhysicalDisk(_Total)\\\\Disk Read Bytes/sec\" status=%ld",
        static_cast<long>(readStatus));
    const PDH_STATUS writeStatus = AddCounterCompat(
        state.storage_.query, "\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", &state.storage_.writeCounter);
    state.trace_.WriteFmt(TracePrefix::Telemetry,
        "pdh_add storage_write path=\"\\\\PhysicalDisk(_Total)\\\\Disk Write Bytes/sec\" status=%ld",
        static_cast<long>(writeStatus));
    const PDH_STATUS collectStatus = PdhCollectQueryData(state.storage_.query);
    state.trace_.WriteFmt(
        TracePrefix::Telemetry, "pdh_collect storage_query status=%ld", static_cast<long>(collectStatus));
}

void ResolveStorageSelection(RealTelemetryCollectorState& state) {
    state.storage_.driveCandidates = EnumerateStorageDriveCandidates();
    state.storage_.resolvedDriveLetters = ResolveConfiguredStorageDriveLetters(
        state.settings_.selection.configuredDrives, state.storage_.driveCandidates);
    state.resolvedSelections_.drives = state.storage_.resolvedDriveLetters;
    MarkSelectedStorageDriveCandidates(state.storage_.driveCandidates, state.storage_.resolvedDriveLetters);

    state.snapshot_.drives.clear();
    state.storage_.driveCounters.clear();
    for (const auto& letter : state.storage_.resolvedDriveLetters) {
        const std::string label = letter + ":";
        DriveInfo info;
        info.label = label;
        state.snapshot_.drives.push_back(std::move(info));
        state.trace_.WriteFmt(TracePrefix::Telemetry, "drive_config label=%s", label.c_str());

        if (state.storage_.query != nullptr) {
            DriveCounterState counters;
            counters.label = label;
            std::string rootLabel = label;
            rootLabel += "\\";
            counters.rootPath = rootLabel;
            std::string readPath = "\\LogicalDisk(";
            readPath += label;
            readPath += ")\\Disk Read Bytes/sec";
            std::string writePath = "\\LogicalDisk(";
            writePath += label;
            writePath += ")\\Disk Write Bytes/sec";
            const PDH_STATUS readStatus = AddCounterCompat(state.storage_.query, readPath, &counters.readCounter);
            const PDH_STATUS writeStatus = AddCounterCompat(state.storage_.query, writePath, &counters.writeCounter);
            state.trace_.WriteFmt(TracePrefix::Telemetry,
                "pdh_add drive_read label=%s path=\"%s\" status=%ld",
                label.c_str(),
                readPath.c_str(),
                static_cast<long>(readStatus));
            state.trace_.WriteFmt(TracePrefix::Telemetry,
                "pdh_add drive_write label=%s path=\"%s\" status=%ld",
                label.c_str(),
                writePath.c_str(),
                static_cast<long>(writeStatus));
            state.storage_.driveCounters.push_back(std::move(counters));
        }
    }
    if (state.storage_.driveCandidates.empty()) {
        state.trace_.Write(TracePrefix::Telemetry, "storage_candidates skipped=no_drives");
    }
    state.trace_.WriteFmt(
        TracePrefix::Telemetry, "storage_candidates count=%zu", state.storage_.driveCandidates.size());
    state.trace_.WriteFmt(TracePrefix::Telemetry, "drive_enumerate count=%zu", state.snapshot_.drives.size());
    RefreshDriveUsage(state);
}

void UpdateStorageMetrics(RealTelemetryCollectorState& state, bool initializeOnly) {
    UpdateStorageThroughput(state, initializeOnly);
    RefreshDriveUsage(state);
}
