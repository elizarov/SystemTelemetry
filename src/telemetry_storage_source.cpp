#include "telemetry_internal.h"
#include "telemetry_support.h"

#include <algorithm>
#include <vector>

#include "utf8.h"

void TelemetryCollector::Impl::EnumerateDrives() {
    snapshot_.drives.clear();
    storage_.driveCounters.clear();
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
    Trace(("telemetry:drive_enumerate count=" + std::to_string(snapshot_.drives.size())).c_str());
    RefreshDriveUsage();
}

void TelemetryCollector::Impl::RefreshStorageDriveCandidates() {
    storage_.driveCandidates = EnumerateStorageDriveCandidates(config_.storage.drives);
    if (storage_.driveCandidates.empty()) {
        Trace("telemetry:storage_candidates skipped=no_drives");
    }
    Trace(("telemetry:storage_candidates count=" + std::to_string(storage_.driveCandidates.size())).c_str());
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
