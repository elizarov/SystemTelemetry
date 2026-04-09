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
        snapshot_.storage.readMbps = (std::max)(0.0, value.doubleValue / (1024.0 * 1024.0));
    } else if (!initializeOnly) {
        snapshot_.storage.readMbps = 0.0;
    }

    if (storageWriteCounter_ != nullptr &&
        (writeStatus = PdhGetFormattedCounterValue(storageWriteCounter_, PDH_FMT_DOUBLE, nullptr, &value)) == ERROR_SUCCESS) {
        snapshot_.storage.writeMbps = (std::max)(0.0, value.doubleValue / (1024.0 * 1024.0));
    } else if (!initializeOnly) {
        snapshot_.storage.writeMbps = 0.0;
    }

    if (!initializeOnly) {
        retainedHistoryStore_.PushSample(snapshot_, "storage.read", snapshot_.storage.readMbps);
        retainedHistoryStore_.PushSample(snapshot_, "storage.write", snapshot_.storage.writeMbps);
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
                drive.readMbps = (std::max)(0.0, value.doubleValue / (1024.0 * 1024.0));
            }
        }
        if (counterIt != driveCounters_.end() && counterIt->writeCounter != nullptr) {
            writeStatus = PdhGetFormattedCounterValue(counterIt->writeCounter, PDH_FMT_DOUBLE, nullptr, &value);
            if (writeStatus == ERROR_SUCCESS) {
                drive.writeMbps = (std::max)(0.0, value.doubleValue / (1024.0 * 1024.0));
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
