#include "telemetry/impl/collector_fake.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

#include "telemetry/impl/collector_storage_selection.h"
#include "util/file_path.h"
#include "util/strings.h"
#include "util/trace.h"

namespace {

struct ResolvedNetworkCandidate {
    std::string adapterName;
    std::string ipAddress = "N/A";
};

struct ResolvedGpuCandidate {
    std::string adapterName;
};

struct SyntheticHistorySpec {
    double base = 0.0;
    double amplitudeA = 0.0;
    double periodA = 0.0;
    double amplitudeB = 0.0;
    double periodB = 0.0;
};

struct SyntheticThroughputSpec {
    double base = 0.0;
    double driftA = 0.0;
    double periodA = 0.0;
    double driftB = 0.0;
    double periodB = 0.0;
    double jitterAmplitude = 0.0;
    double burstAmplitude = 0.0;
    double burstPeriod = 0.0;
    double burstOffset = 0.0;
    double burstWidth = 0.0;
    double dipAmplitude = 0.0;
    double dipPeriod = 0.0;
    double dipOffset = 0.0;
    double dipWidth = 0.0;
    uint32_t seed = 0;
};

constexpr size_t kSyntheticHistorySamples = 60;
constexpr double kSyntheticCpuMemoryTotalGb = 63.943493;
constexpr double kSyntheticCpuMemoryPeakRatio = 0.75;
constexpr const char* kSyntheticRequestedFanNames[] = {"cpu", "system"};
constexpr const char* kSyntheticRequestedTemperatureNames[] = {"cpu"};
constexpr const char* kSyntheticAvailableFanNames[] = {"CPU", "System 1", "Pump"};
constexpr const char* kSyntheticAvailableTemperatureNames[] = {"CPU", "VRM MOS", "Chipset"};
constexpr SyntheticHistorySpec kCpuLoadHistory{43.0, 18.0, 5.0, 7.0, 8.5};
constexpr SyntheticHistorySpec kCpuClockHistory{4.42, 0.18, 7.0, 0.08, 13.0};
constexpr SyntheticHistorySpec kCpuRamHistory{27.6, 0.35, 9.0, 0.18, 17.0};
constexpr SyntheticHistorySpec kGpuLoadHistory{72.0, 14.0, 5.6, 8.0, 10.5};
constexpr SyntheticHistorySpec kGpuTemperatureHistory{62.0, 4.5, 9.0, 1.8, 14.0};
constexpr SyntheticHistorySpec kGpuClockHistory{2085.0, 155.0, 6.3, 65.0, 11.0};
constexpr SyntheticHistorySpec kGpuFanHistory{1325.0, 170.0, 7.4, 60.0, 13.0};
constexpr SyntheticHistorySpec kGpuFpsHistory{118.0, 21.0, 6.6, 9.0, 12.5};
constexpr SyntheticHistorySpec kGpuVramHistory{5.9, 1.1, 8.0, 0.5, 15.0};
constexpr SyntheticHistorySpec kBoardTempCpuHistory{65.0, 5.0, 7.0, 2.0, 12.0};
constexpr SyntheticHistorySpec kBoardFanCpuHistory{1380.0, 180.0, 6.8, 70.0, 11.0};
constexpr SyntheticHistorySpec kBoardFanSystemHistory{905.0, 85.0, 8.0, 30.0, 13.0};
constexpr SyntheticThroughputSpec kNetworkUploadHistory{
    22.0, 7.5, 7.0, 4.0, 16.0, 3.2, 12.0, 18.0, 5.5, 2.4, 6.5, 29.0, 12.0, 3.0, 0x13579BDFu};
constexpr SyntheticThroughputSpec kNetworkDownloadHistory{
    198.0, 56.0, 6.1, 34.0, 12.5, 19.0, 92.0, 17.0, 2.8, 2.6, 48.0, 27.0, 8.0, 3.1, 0x2468ACE1u};
constexpr SyntheticThroughputSpec kStorageReadHistory{
    146.0, 48.0, 5.7, 28.0, 11.0, 16.0, 118.0, 15.0, 3.7, 2.2, 64.0, 24.0, 6.5, 2.6, 0xA5C31E27u};
constexpr SyntheticThroughputSpec kStorageWriteHistory{
    44.0, 18.0, 6.0, 11.0, 13.5, 8.5, 52.0, 21.0, 9.0, 2.8, 19.0, 26.0, 4.0, 3.2, 0x5EED1234u};

void AssignStringList(std::vector<std::string>& target, const char* const* values, size_t count) {
    target.clear();
    target.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        target.emplace_back(values[i]);
    }
}

std::string ReadBinaryFile(const FilePath& path) {
    return ReadFileBinary(path).value_or(std::string{});
}

std::vector<double> BuildSyntheticHistory(uint64_t tick, const SyntheticHistorySpec& spec) {
    std::vector<double> samples;
    samples.reserve(kSyntheticHistorySamples);
    for (size_t i = 0; i < kSyntheticHistorySamples; ++i) {
        const double position = static_cast<double>(i) + static_cast<double>(tick) * 3.0;
        const double value = spec.base + std::sin(position / spec.periodA) * spec.amplitudeA +
                             std::cos((position + 7.0) / spec.periodB) * spec.amplitudeB;
        samples.push_back((std::max)(0.0, value));
    }
    return samples;
}

double SyntheticNoiseSigned(uint64_t tick, size_t sampleIndex, uint32_t seed) {
    uint64_t value = (static_cast<uint64_t>(sampleIndex) + 1ull) * 0x9E3779B97F4A7C15ull;
    value ^= (tick + 0xD1B54A32D192ED03ull) * 0x94D049BB133111EBull;
    value ^= static_cast<uint64_t>(seed) * 0xBF58476D1CE4E5B9ull;
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return (static_cast<double>(static_cast<uint32_t>(value >> 32)) / 4294967295.0) * 2.0 - 1.0;
}

double SyntheticPulse(double position, double period, double offset, double width) {
    const double rawPhase = std::fmod(position + offset, period);
    const double phase = rawPhase < 0.0 ? rawPhase + period : rawPhase;
    const double distance = (std::min)(phase, period - phase);
    if (distance >= width) {
        return 0.0;
    }

    const double shaped = 1.0 - (distance / width);
    return shaped * shaped * (3.0 - 2.0 * shaped);
}

std::vector<double> BuildSyntheticThroughputHistory(uint64_t tick, const SyntheticThroughputSpec& spec) {
    std::vector<double> samples;
    samples.reserve(kSyntheticHistorySamples);
    for (size_t i = 0; i < kSyntheticHistorySamples; ++i) {
        const double position = static_cast<double>(i) + static_cast<double>(tick) * 2.5;
        const double drift =
            std::sin(position / spec.periodA) * spec.driftA + std::cos((position + 9.0) / spec.periodB) * spec.driftB;
        const double fastJitter = SyntheticNoiseSigned(tick, i, spec.seed) * spec.jitterAmplitude;
        const double slowJitter =
            SyntheticNoiseSigned(tick / 2, (i + static_cast<size_t>(tick)) / 4, spec.seed ^ 0xA511E9B3u) *
            (spec.jitterAmplitude * 0.65);
        const double burst =
            SyntheticPulse(position, spec.burstPeriod, spec.burstOffset, spec.burstWidth) * spec.burstAmplitude;
        const double microBurst =
            SyntheticPulse(
                position, spec.burstPeriod * 0.53 + 3.0, spec.burstOffset * 0.61 + 1.5, spec.burstWidth * 0.55 + 0.35) *
            (spec.burstAmplitude * 0.35);
        const double dip = SyntheticPulse(position, spec.dipPeriod, spec.dipOffset, spec.dipWidth) * spec.dipAmplitude;
        const double value = spec.base + drift + fastJitter + slowJitter + burst + microBurst - dip;
        samples.push_back((std::max)(0.0, value));
    }
    return samples;
}

SYSTEMTIME BuildSyntheticTimestamp(uint64_t tick) {
    SYSTEMTIME baseline{};
    baseline.wYear = 2015;
    baseline.wMonth = 10;
    baseline.wDay = 21;
    baseline.wHour = 16;
    baseline.wMinute = 29;
    baseline.wSecond = 0;
    baseline.wMilliseconds = 88;

    FILETIME fileTime{};
    if (!SystemTimeToFileTime(&baseline, &fileTime)) {
        return baseline;
    }

    ULARGE_INTEGER ticks100ns{};
    ticks100ns.LowPart = fileTime.dwLowDateTime;
    ticks100ns.HighPart = fileTime.dwHighDateTime;
    ticks100ns.QuadPart += tick * 10000000ull;
    fileTime.dwLowDateTime = ticks100ns.LowPart;
    fileTime.dwHighDateTime = ticks100ns.HighPart;

    SYSTEMTIME adjusted = baseline;
    if (!FileTimeToSystemTime(&fileTime, &adjusted)) {
        return baseline;
    }
    return adjusted;
}

void AddSyntheticHistory(SystemSnapshot& snapshot, const char* seriesRef, std::vector<double>&& samples) {
    RetainedHistorySeries history;
    history.seriesRef = seriesRef;
    history.samples = std::move(samples);
    snapshot.retainedHistories.push_back(std::move(history));
}

void AddSyntheticHistory(SystemSnapshot& snapshot, RetainedHistoryKey key, std::vector<double>&& samples) {
    RetainedHistorySeries history;
    history.seriesRef = RetainedHistorySeriesRef(key);
    history.samples = std::move(samples);
    const size_t index = snapshot.retainedHistories.size();
    if (index <= 0xffffu - 1u) {
        snapshot.retainedHistoryIndexByKey[static_cast<size_t>(key)] = static_cast<uint16_t>(index + 1u);
    }
    snapshot.retainedHistories.push_back(std::move(history));
}

double LastHistorySample(const std::vector<double>& samples) {
    return samples.empty() ? 0.0 : samples.back();
}

DriveInfo BuildSyntheticDrive(
    const char* label, const char* volumeLabel, double usedPercent, double freeGb, double readMbps, double writeMbps) {
    DriveInfo drive;
    drive.label = label;
    drive.volumeLabel = volumeLabel;
    drive.usedPercent = usedPercent;
    drive.freeGb = freeGb;
    const double freeRatio = 1.0 - (usedPercent / 100.0);
    drive.totalGb = freeRatio > 0.0 ? freeGb / freeRatio : 0.0;
    drive.readMbps = readMbps;
    drive.writeMbps = writeMbps;
    drive.driveType = DRIVE_FIXED;
    return drive;
}

TelemetryDump BuildSyntheticTelemetryDump(uint64_t tick) {
    TelemetryDump dump;
    SystemSnapshot& snapshot = dump.snapshot;

    std::vector<double> cpuLoad = BuildSyntheticHistory(tick, kCpuLoadHistory);
    std::vector<double> cpuClock = BuildSyntheticHistory(tick, kCpuClockHistory);
    std::vector<double> cpuRam = BuildSyntheticHistory(tick, kCpuRamHistory);
    if (!cpuRam.empty()) {
        // Keep the fake RAM peak marker clearly visible in generated guide-sheet screenshots.
        cpuRam[cpuRam.size() / 2] = kSyntheticCpuMemoryTotalGb * kSyntheticCpuMemoryPeakRatio;
    }
    std::vector<double> gpuLoad = BuildSyntheticHistory(tick, kGpuLoadHistory);
    std::vector<double> gpuTemp = BuildSyntheticHistory(tick, kGpuTemperatureHistory);
    std::vector<double> gpuClock = BuildSyntheticHistory(tick, kGpuClockHistory);
    std::vector<double> gpuFan = BuildSyntheticHistory(tick, kGpuFanHistory);
    std::vector<double> gpuFps = BuildSyntheticHistory(tick, kGpuFpsHistory);
    std::vector<double> gpuVram = BuildSyntheticHistory(tick, kGpuVramHistory);
    std::vector<double> boardTempCpu = BuildSyntheticHistory(tick, kBoardTempCpuHistory);
    std::vector<double> boardFanCpu = BuildSyntheticHistory(tick, kBoardFanCpuHistory);
    std::vector<double> boardFanSystem = BuildSyntheticHistory(tick, kBoardFanSystemHistory);
    std::vector<double> networkUpload = BuildSyntheticThroughputHistory(tick, kNetworkUploadHistory);
    std::vector<double> networkDownload = BuildSyntheticThroughputHistory(tick, kNetworkDownloadHistory);
    std::vector<double> storageRead = BuildSyntheticThroughputHistory(tick, kStorageReadHistory);
    std::vector<double> storageWrite = BuildSyntheticThroughputHistory(tick, kStorageWriteHistory);

    snapshot.cpu.name = "DeLorean 88X ChronoCore";
    snapshot.cpu.loadPercent = LastHistorySample(cpuLoad);
    snapshot.cpu.clock = ScalarMetric{LastHistorySample(cpuClock), ScalarMetricUnit::Gigahertz};
    snapshot.cpu.memory = MemoryMetric{LastHistorySample(cpuRam), kSyntheticCpuMemoryTotalGb};

    snapshot.gpu.name = "FluxDrive 8800 XT";
    snapshot.gpu.loadPercent = LastHistorySample(gpuLoad);
    snapshot.gpu.temperature = ScalarMetric{LastHistorySample(gpuTemp), ScalarMetricUnit::Celsius};
    snapshot.gpu.clock = ScalarMetric{LastHistorySample(gpuClock), ScalarMetricUnit::Megahertz};
    snapshot.gpu.fan = ScalarMetric{LastHistorySample(gpuFan), ScalarMetricUnit::Rpm};
    snapshot.gpu.fps = ScalarMetric{LastHistorySample(gpuFps), ScalarMetricUnit::Fps};
    snapshot.gpu.fpsAppName = "fluxsim";
    snapshot.gpu.vram = MemoryMetric{LastHistorySample(gpuVram), 15.984375};

    snapshot.boardTemperatures.push_back(
        {"cpu", ScalarMetric{LastHistorySample(boardTempCpu), ScalarMetricUnit::Celsius}});
    snapshot.boardFans.push_back({"cpu", ScalarMetric{LastHistorySample(boardFanCpu), ScalarMetricUnit::Rpm}});
    snapshot.boardFans.push_back({"system", ScalarMetric{LastHistorySample(boardFanSystem), ScalarMetricUnit::Rpm}});

    snapshot.network.adapterName = "Ethernet";
    snapshot.network.ipAddress = "192.168.3.60";
    snapshot.network.uploadMbps = LastHistorySample(networkUpload);
    snapshot.network.downloadMbps = LastHistorySample(networkDownload);

    snapshot.storage.readMbps = LastHistorySample(storageRead);
    snapshot.storage.writeMbps = LastHistorySample(storageWrite);

    snapshot.drives.push_back(BuildSyntheticDrive("C:", "System", 34.470511, 2440.911713, 28.0, 16.0));
    snapshot.drives.push_back(BuildSyntheticDrive("D:", "Games", 46.695821, 508.451069, 12.0, 4.0));
    snapshot.drives.push_back(BuildSyntheticDrive("E:", "Media", 32.099891, 5059.855675, 118.0, 21.0));
    snapshot.drives.push_back(BuildSyntheticDrive("F:", "Capture", 88.636454, 306.444996, 24.0, 37.0));

    AddSyntheticHistory(snapshot, RetainedHistoryKey::CpuLoad, std::move(cpuLoad));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::CpuClock, std::move(cpuClock));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::CpuRam, std::move(cpuRam));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::GpuLoad, std::move(gpuLoad));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::GpuTemperature, std::move(gpuTemp));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::GpuClock, std::move(gpuClock));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::GpuFan, std::move(gpuFan));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::GpuFps, std::move(gpuFps));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::GpuVram, std::move(gpuVram));
    AddSyntheticHistory(snapshot, "board.temp.cpu", std::move(boardTempCpu));
    AddSyntheticHistory(snapshot, "board.fan.cpu", std::move(boardFanCpu));
    AddSyntheticHistory(snapshot, "board.fan.system", std::move(boardFanSystem));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::NetworkUpload, std::move(networkUpload));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::NetworkDownload, std::move(networkDownload));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::StorageRead, std::move(storageRead));
    AddSyntheticHistory(snapshot, RetainedHistoryKey::StorageWrite, std::move(storageWrite));

    snapshot.now = BuildSyntheticTimestamp(tick);
    snapshot.revision = 1;

    dump.boardProvider.boardManufacturer = "Gigabyte Technology Co., Ltd.";
    dump.boardProvider.boardProduct = "X570 AORUS ULTRA";
    dump.boardProvider.driverLibrary = "Synthetic";
    AssignStringList(
        dump.boardProvider.requestedFanNames, kSyntheticRequestedFanNames, ARRAYSIZE(kSyntheticRequestedFanNames));
    AssignStringList(dump.boardProvider.requestedTemperatureNames,
        kSyntheticRequestedTemperatureNames,
        ARRAYSIZE(kSyntheticRequestedTemperatureNames));
    AssignStringList(
        dump.boardProvider.availableFanNames, kSyntheticAvailableFanNames, ARRAYSIZE(kSyntheticAvailableFanNames));
    AssignStringList(dump.boardProvider.availableTemperatureNames,
        kSyntheticAvailableTemperatureNames,
        ARRAYSIZE(kSyntheticAvailableTemperatureNames));
    dump.boardProvider.fans = snapshot.boardFans;
    dump.boardProvider.temperatures = snapshot.boardTemperatures;
    dump.boardProvider.providerName = "Synthetic";
    dump.boardProvider.diagnostics = "Built-in synthetic telemetry baseline.";
    dump.boardProvider.available = true;
    dump.gpuProvider.providerName = "Synthetic";
    dump.gpuProvider.diagnostics = "Built-in synthetic telemetry baseline.";
    dump.gpuProvider.available = true;
    return dump;
}

std::vector<NetworkAdapterCandidate> EnumerateSnapshotNetworkCandidates(const SystemSnapshot& snapshot) {
    std::vector<NetworkAdapterCandidate> candidates;
    if (snapshot.network.adapterName.empty() || snapshot.network.adapterName == "Auto") {
        return candidates;
    }

    NetworkAdapterCandidate candidate;
    candidate.adapterName = snapshot.network.adapterName;
    candidate.ipAddress = snapshot.network.ipAddress.empty() ? "N/A" : snapshot.network.ipAddress;
    candidates.push_back(std::move(candidate));
    return candidates;
}

std::vector<GpuAdapterCandidate> EnumerateSnapshotGpuAdapterCandidates(const SystemSnapshot& snapshot) {
    std::vector<GpuAdapterCandidate> candidates;
    if (snapshot.gpu.name.empty() || snapshot.gpu.name == "GPU") {
        return candidates;
    }

    GpuAdapterCandidate candidate;
    candidate.adapterName = snapshot.gpu.name;
    candidate.vendorName = "Synthetic";
    candidate.dedicatedVramGb = snapshot.gpu.vram.totalGb;
    candidates.push_back(std::move(candidate));
    return candidates;
}

ResolvedGpuCandidate ResolveConfiguredGpuCandidate(
    const std::string& configuredAdapterName, const std::vector<GpuAdapterCandidate>& availableCandidates) {
    ResolvedGpuCandidate resolved;
    if (availableCandidates.empty()) {
        return resolved;
    }

    if (!configuredAdapterName.empty()) {
        for (const auto& candidate : availableCandidates) {
            if (EqualsInsensitive(candidate.adapterName, configuredAdapterName)) {
                resolved.adapterName = candidate.adapterName;
                return resolved;
            }
        }
    }

    if (!configuredAdapterName.empty()) {
        for (const auto& candidate : availableCandidates) {
            if (ContainsInsensitive(candidate.adapterName, configuredAdapterName)) {
                resolved.adapterName = candidate.adapterName;
                return resolved;
            }
        }
    }

    resolved.adapterName = availableCandidates.front().adapterName;
    return resolved;
}

void MarkSelectedGpuAdapterCandidates(
    std::vector<GpuAdapterCandidate>& candidates, const ResolvedGpuCandidate& selectedCandidate) {
    bool selected = false;
    for (auto& candidate : candidates) {
        candidate.selected = !selected && candidate.adapterName == selectedCandidate.adapterName;
        selected = selected || candidate.selected;
    }
}

ResolvedNetworkCandidate ResolveConfiguredNetworkCandidate(
    const std::string& configuredAdapterName, const std::vector<NetworkAdapterCandidate>& availableCandidates) {
    ResolvedNetworkCandidate resolved;
    if (availableCandidates.empty()) {
        return resolved;
    }

    if (!configuredAdapterName.empty()) {
        for (const auto& candidate : availableCandidates) {
            if (EqualsInsensitive(candidate.adapterName, configuredAdapterName)) {
                resolved.adapterName = candidate.adapterName;
                resolved.ipAddress = candidate.ipAddress;
                return resolved;
            }
        }
    }

    if (!configuredAdapterName.empty()) {
        for (const auto& candidate : availableCandidates) {
            if (ContainsInsensitive(candidate.adapterName, configuredAdapterName)) {
                resolved.adapterName = candidate.adapterName;
                resolved.ipAddress = candidate.ipAddress;
                return resolved;
            }
        }
    }

    resolved.adapterName = availableCandidates.front().adapterName;
    resolved.ipAddress = availableCandidates.front().ipAddress;
    return resolved;
}

void MarkSelectedNetworkAdapterCandidates(
    std::vector<NetworkAdapterCandidate>& candidates, const ResolvedNetworkCandidate& selectedCandidate) {
    bool selected = false;
    for (auto& candidate : candidates) {
        const bool sameName = candidate.adapterName == selectedCandidate.adapterName;
        const bool sameIp = selectedCandidate.ipAddress.empty() || selectedCandidate.ipAddress == "N/A" ||
                            candidate.ipAddress == selectedCandidate.ipAddress;
        candidate.selected = !selected && sameName && sameIp;
        selected = selected || candidate.selected;
    }
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

FilePath ResolveFakePath(const FilePath& workingDirectory, const FilePath& configuredPath) {
    if (configuredPath.empty()) {
        return {};
    }
    if (configuredPath.is_absolute()) {
        return configuredPath;
    }
    return workingDirectory / configuredPath;
}

class FakeTelemetryCollector : public TelemetryCollector {
public:
    FakeTelemetryCollector(FilePath fakePath, TelemetryDumpLoader loadFakeDump, Trace& trace)
        : fakePath_(std::move(fakePath)), useSyntheticSource_(fakePath_.empty()), loadFakeDump_(loadFakeDump),
          trace_(trace) {}

    bool Initialize(const TelemetrySettings& settings, std::string* errorText) override {
        if (errorText != nullptr) {
            errorText->clear();
        }
        selectionSettings_ = settings.selection;
        if (useSyntheticSource_) {
            trace_.Write(TracePrefix::Fake, "initialize_begin source=synthetic");
        } else {
            trace_.Write(TracePrefix::Fake, "initialize_begin path=\"" + fakePath_.string() + "\"");
        }
        if (!ReloadFakeDump(true, errorText)) {
            trace_.Write(TracePrefix::Fake, "initialize_failed");
            return false;
        }
        trace_.Write(TracePrefix::Fake, "initialize_done");
        return true;
    }

    const SystemSnapshot& Snapshot() const override {
        return dump_.snapshot;
    }

    TelemetryDump Dump() const override {
        return dump_;
    }

    const ResolvedTelemetrySelections& ResolvedSelections() const override {
        return resolvedSelections_;
    }

    const std::vector<GpuAdapterCandidate>& GpuAdapterCandidates() const override {
        return gpuAdapters_;
    }

    const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const override {
        return networkAdapters_;
    }

    const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const override {
        return storageDrives_;
    }

    void ApplySettings(const TelemetrySettings& settings) override {
        selectionSettings_ = settings.selection;
        RefreshSelectionsAndSnapshot();
    }

    void SetPreferredNetworkAdapterName(std::string adapterName) override {
        selectionSettings_.preferredAdapterName = std::move(adapterName);
        RefreshNetworkSelection();
    }

    void SetPreferredGpuAdapterName(std::string adapterName) override {
        selectionSettings_.preferredGpuAdapterName = std::move(adapterName);
        RefreshGpuSelection();
    }

    void SetSelectedStorageDrives(std::vector<std::string> driveLetters) override {
        selectionSettings_.configuredDrives = NormalizeConfiguredStorageDriveLetters(driveLetters);
        RefreshStorageSelection();
    }

    void RefreshSelectionsAndSnapshot() override {
        RefreshGpuSelection();
        RefreshNetworkSelection();
        RefreshStorageSelection();
    }

    void UpdateSnapshot() override {
        const auto now = std::chrono::steady_clock::now();
        if (lastReload_.time_since_epoch().count() == 0 || now - lastReload_ >= std::chrono::seconds(1)) {
            ReloadFakeDump(false);
        }
    }

private:
    void RefreshGpuSelection() {
        gpuAdapters_ = EnumerateSnapshotGpuAdapterCandidates(sourceDump_.snapshot);
        resolvedGpu_ = ResolveConfiguredGpuCandidate(selectionSettings_.preferredGpuAdapterName, gpuAdapters_);
        MarkSelectedGpuAdapterCandidates(gpuAdapters_, resolvedGpu_);
        resolvedSelections_.gpuAdapterName = resolvedGpu_.adapterName;

        if (!resolvedGpu_.adapterName.empty()) {
            dump_.snapshot.gpu.name = resolvedGpu_.adapterName;
        }
        ++dump_.snapshot.revision;
    }

    void RefreshNetworkSelection() {
        networkAdapters_ = EnumerateSnapshotNetworkCandidates(sourceDump_.snapshot);
        resolvedNetwork_ = ResolveConfiguredNetworkCandidate(selectionSettings_.preferredAdapterName, networkAdapters_);
        MarkSelectedNetworkAdapterCandidates(networkAdapters_, resolvedNetwork_);
        resolvedSelections_.adapterName = resolvedNetwork_.adapterName;

        dump_.snapshot.network.adapterName =
            resolvedNetwork_.adapterName.empty() ? "Auto" : resolvedNetwork_.adapterName;
        dump_.snapshot.network.ipAddress = resolvedNetwork_.ipAddress.empty() ? "N/A" : resolvedNetwork_.ipAddress;
        ++dump_.snapshot.revision;
    }

    void RefreshStorageSelection() {
        storageDrives_ = EnumerateSnapshotStorageDriveCandidates(sourceDump_.snapshot);
        resolvedStorageDrives_ =
            ResolveConfiguredStorageDriveLetters(selectionSettings_.configuredDrives, storageDrives_);
        resolvedSelections_.drives = resolvedStorageDrives_;
        MarkSelectedStorageDriveCandidates(storageDrives_, resolvedStorageDrives_);

        dump_.snapshot.drives.clear();
        for (const auto& drive : sourceDump_.snapshot.drives) {
            const std::string letter = NormalizeStorageDriveLetter(drive.label);
            if (std::find(resolvedStorageDrives_.begin(), resolvedStorageDrives_.end(), letter) ==
                resolvedStorageDrives_.end()) {
                continue;
            }
            dump_.snapshot.drives.push_back(drive);
        }
        ++dump_.snapshot.revision;
    }

    bool ReloadFakeDump(bool required, std::string* errorText = nullptr) {
        if (useSyntheticSource_) {
            sourceDump_ = BuildSyntheticTelemetryDump(syntheticTick_++);
            dump_ = sourceDump_;
            RefreshSelectionsAndSnapshot();
            lastReload_ = std::chrono::steady_clock::now();
            trace_.Write(TracePrefix::Fake, "load_done source=synthetic");
            return true;
        }

        const std::string input = ReadBinaryFile(fakePath_);
        if (input.empty()) {
            trace_.Write(TracePrefix::Fake, "load_failed reason=open path=\"" + fakePath_.string() + "\"");
            if (required && errorText != nullptr) {
                *errorText = "Failed to open fake telemetry file:\n" + fakePath_.string();
            }
            return false;
        }

        if (loadFakeDump_ == nullptr) {
            trace_.Write(TracePrefix::Fake, "load_failed reason=loader_unavailable");
            if (required && errorText != nullptr) {
                *errorText = "Fake telemetry dump loading is unavailable.";
            }
            return false;
        }

        TelemetryDump loaded;
        std::string error;
        if (!loadFakeDump_(input, loaded, &error)) {
            trace_.Write(TracePrefix::Fake, "load_failed reason=parse error=\"" + error + "\"");
            if (required && errorText != nullptr) {
                *errorText = "Failed to parse fake telemetry file:\n" + error;
            }
            return false;
        }

        sourceDump_ = std::move(loaded);
        dump_ = sourceDump_;
        RefreshSelectionsAndSnapshot();
        lastReload_ = std::chrono::steady_clock::now();
        trace_.Write(TracePrefix::Fake, "load_done path=\"" + fakePath_.string() + "\"");
        return true;
    }

    FilePath fakePath_;
    bool useSyntheticSource_ = false;
    TelemetryDumpLoader loadFakeDump_ = nullptr;
    TelemetrySelectionSettings selectionSettings_{};
    ResolvedTelemetrySelections resolvedSelections_{};
    TelemetryDump sourceDump_{};
    TelemetryDump dump_{};
    std::vector<GpuAdapterCandidate> gpuAdapters_{};
    std::vector<NetworkAdapterCandidate> networkAdapters_{};
    std::vector<StorageDriveCandidate> storageDrives_{};
    ResolvedGpuCandidate resolvedGpu_{};
    ResolvedNetworkCandidate resolvedNetwork_{};
    std::vector<std::string> resolvedStorageDrives_{};
    Trace& trace_;
    std::chrono::steady_clock::time_point lastReload_{};
    uint64_t syntheticTick_ = 0;
};

}  // namespace

std::unique_ptr<TelemetryCollector> CreateFakeTelemetryCollector(
    const FilePath& workingDirectory, const FilePath& configuredPath, TelemetryDumpLoader loadFakeDump, Trace& trace) {
    return std::make_unique<FakeTelemetryCollector>(
        ResolveFakePath(workingDirectory, configuredPath), loadFakeDump, trace);
}
