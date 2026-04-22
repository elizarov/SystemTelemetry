#include "telemetry/impl/collector_fake.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "telemetry/impl/collector_storage_selection.h"
#include "telemetry/impl/retained_history.h"
#include "util/strings.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

struct ResolvedNetworkCandidate {
    std::string adapterName;
    std::string ipAddress = "N/A";
};

constexpr size_t kSyntheticHistorySamples = 60;

double SyntheticWave(size_t sampleIndex,
    uint64_t tick,
    double base,
    double amplitudeA,
    double periodA,
    double amplitudeB,
    double periodB) {
    const double position = static_cast<double>(sampleIndex) + static_cast<double>(tick) * 3.0;
    return base + std::sin(position / periodA) * amplitudeA + std::cos((position + 7.0) / periodB) * amplitudeB;
}

std::vector<double> BuildSyntheticHistory(
    uint64_t tick, double base, double amplitudeA, double periodA, double amplitudeB, double periodB) {
    std::vector<double> samples;
    samples.reserve(kSyntheticHistorySamples);
    for (size_t i = 0; i < kSyntheticHistorySamples; ++i) {
        samples.push_back((std::max)(0.0, SyntheticWave(i, tick, base, amplitudeA, periodA, amplitudeB, periodB)));
    }
    return samples;
}

uint32_t SyntheticNoiseHash(uint64_t tick, size_t sampleIndex, uint32_t seed) {
    uint64_t value = (static_cast<uint64_t>(sampleIndex) + 1ull) * 0x9E3779B97F4A7C15ull;
    value ^= (tick + 0xD1B54A32D192ED03ull) * 0x94D049BB133111EBull;
    value ^= static_cast<uint64_t>(seed) * 0xBF58476D1CE4E5B9ull;
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return static_cast<uint32_t>(value >> 32);
}

double SyntheticNoiseUnit(uint64_t tick, size_t sampleIndex, uint32_t seed) {
    return static_cast<double>(SyntheticNoiseHash(tick, sampleIndex, seed)) / 4294967295.0;
}

double SyntheticNoiseSigned(uint64_t tick, size_t sampleIndex, uint32_t seed) {
    return SyntheticNoiseUnit(tick, sampleIndex, seed) * 2.0 - 1.0;
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

std::vector<double> BuildSyntheticThroughputHistory(uint64_t tick,
    double base,
    double driftA,
    double periodA,
    double driftB,
    double periodB,
    double jitterAmplitude,
    double burstAmplitude,
    double burstPeriod,
    double burstOffset,
    double burstWidth,
    double dipAmplitude,
    double dipPeriod,
    double dipOffset,
    double dipWidth,
    uint32_t seed) {
    std::vector<double> samples;
    samples.reserve(kSyntheticHistorySamples);
    for (size_t i = 0; i < kSyntheticHistorySamples; ++i) {
        const double position = static_cast<double>(i) + static_cast<double>(tick) * 2.5;
        const double drift = std::sin(position / periodA) * driftA + std::cos((position + 9.0) / periodB) * driftB;
        const double fastJitter = SyntheticNoiseSigned(tick, i, seed) * jitterAmplitude;
        const double slowJitter =
            SyntheticNoiseSigned(tick / 2, (i + static_cast<size_t>(tick)) / 4, seed ^ 0xA511E9B3u) *
            (jitterAmplitude * 0.65);
        const double burst = SyntheticPulse(position, burstPeriod, burstOffset, burstWidth) * burstAmplitude;
        const double microBurst =
            SyntheticPulse(position, burstPeriod * 0.53 + 3.0, burstOffset * 0.61 + 1.5, burstWidth * 0.55 + 0.35) *
            (burstAmplitude * 0.35);
        const double dip = SyntheticPulse(position, dipPeriod, dipOffset, dipWidth) * dipAmplitude;
        const double value = base + drift + fastJitter + slowJitter + burst + microBurst - dip;
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

void AddSyntheticHistory(SystemSnapshot& snapshot, const std::string& seriesRef, std::vector<double> samples) {
    RetainedHistorySeries history;
    history.seriesRef = seriesRef;
    history.samples = std::move(samples);
    snapshot.retainedHistoryIndexByRef.emplace(history.seriesRef, snapshot.retainedHistories.size());
    snapshot.retainedHistories.push_back(std::move(history));
}

double LastHistorySample(const std::vector<double>& samples) {
    return samples.empty() ? 0.0 : samples.back();
}

double ComputeDriveTotalGb(double freeGb, double usedPercent) {
    const double freeRatio = 1.0 - (usedPercent / 100.0);
    return freeRatio > 0.0 ? freeGb / freeRatio : 0.0;
}

DriveInfo BuildSyntheticDrive(const std::string& label,
    const std::string& volumeLabel,
    double usedPercent,
    double freeGb,
    double readMbps,
    double writeMbps) {
    DriveInfo drive;
    drive.label = label;
    drive.volumeLabel = volumeLabel;
    drive.usedPercent = usedPercent;
    drive.freeGb = freeGb;
    drive.totalGb = ComputeDriveTotalGb(freeGb, usedPercent);
    drive.readMbps = readMbps;
    drive.writeMbps = writeMbps;
    drive.driveType = DRIVE_FIXED;
    return drive;
}

TelemetryDump BuildSyntheticTelemetryDump(uint64_t tick) {
    TelemetryDump dump;
    SystemSnapshot& snapshot = dump.snapshot;

    const std::vector<double> cpuLoad = BuildSyntheticHistory(tick, 43.0, 18.0, 5.0, 7.0, 8.5);
    const std::vector<double> cpuClock = BuildSyntheticHistory(tick, 4.42, 0.18, 7.0, 0.08, 13.0);
    const std::vector<double> cpuRam = BuildSyntheticHistory(tick, 27.6, 0.35, 9.0, 0.18, 17.0);
    const std::vector<double> gpuLoad = BuildSyntheticHistory(tick, 72.0, 14.0, 5.6, 8.0, 10.5);
    const std::vector<double> gpuTemp = BuildSyntheticHistory(tick, 62.0, 4.5, 9.0, 1.8, 14.0);
    const std::vector<double> gpuClock = BuildSyntheticHistory(tick, 2085.0, 155.0, 6.3, 65.0, 11.0);
    const std::vector<double> gpuFan = BuildSyntheticHistory(tick, 1325.0, 170.0, 7.4, 60.0, 13.0);
    const std::vector<double> gpuVram = BuildSyntheticHistory(tick, 5.9, 1.1, 8.0, 0.5, 15.0);
    const std::vector<double> boardTempCpu = BuildSyntheticHistory(tick, 65.0, 5.0, 7.0, 2.0, 12.0);
    const std::vector<double> boardFanCpu = BuildSyntheticHistory(tick, 1380.0, 180.0, 6.8, 70.0, 11.0);
    const std::vector<double> boardFanSystem = BuildSyntheticHistory(tick, 905.0, 85.0, 8.0, 30.0, 13.0);
    const std::vector<double> networkUpload = BuildSyntheticThroughputHistory(
        tick, 22.0, 7.5, 7.0, 4.0, 16.0, 3.2, 12.0, 18.0, 5.5, 2.4, 6.5, 29.0, 12.0, 3.0, 0x13579BDFu);
    const std::vector<double> networkDownload = BuildSyntheticThroughputHistory(
        tick, 198.0, 56.0, 6.1, 34.0, 12.5, 19.0, 92.0, 17.0, 2.8, 2.6, 48.0, 27.0, 8.0, 3.1, 0x2468ACE1u);
    const std::vector<double> storageRead = BuildSyntheticThroughputHistory(
        tick, 146.0, 48.0, 5.7, 28.0, 11.0, 16.0, 118.0, 15.0, 3.7, 2.2, 64.0, 24.0, 6.5, 2.6, 0xA5C31E27u);
    const std::vector<double> storageWrite = BuildSyntheticThroughputHistory(
        tick, 44.0, 18.0, 6.0, 11.0, 13.5, 8.5, 52.0, 21.0, 9.0, 2.8, 19.0, 26.0, 4.0, 3.2, 0x5EED1234u);

    snapshot.cpu.name = "AMD Ryzen 9 5900X HyperDrive";
    snapshot.cpu.loadPercent = LastHistorySample(cpuLoad);
    snapshot.cpu.clock = ScalarMetric{LastHistorySample(cpuClock), ScalarMetricUnit::Gigahertz};
    snapshot.cpu.memory = MemoryMetric{LastHistorySample(cpuRam), 63.943493};

    snapshot.gpu.name = "AMD Radeon RX 8800 XT FluxDrive";
    snapshot.gpu.loadPercent = LastHistorySample(gpuLoad);
    snapshot.gpu.temperature = ScalarMetric{LastHistorySample(gpuTemp), ScalarMetricUnit::Celsius};
    snapshot.gpu.clock = ScalarMetric{LastHistorySample(gpuClock), ScalarMetricUnit::Megahertz};
    snapshot.gpu.fan = ScalarMetric{LastHistorySample(gpuFan), ScalarMetricUnit::Rpm};
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

    AddSyntheticHistory(snapshot, "cpu.load", cpuLoad);
    AddSyntheticHistory(snapshot, "cpu.clock", cpuClock);
    AddSyntheticHistory(snapshot, "cpu.ram", cpuRam);
    AddSyntheticHistory(snapshot, "gpu.load", gpuLoad);
    AddSyntheticHistory(snapshot, "gpu.temp", gpuTemp);
    AddSyntheticHistory(snapshot, "gpu.clock", gpuClock);
    AddSyntheticHistory(snapshot, "gpu.fan", gpuFan);
    AddSyntheticHistory(snapshot, "gpu.vram", gpuVram);
    AddSyntheticHistory(snapshot, "board.temp.cpu", boardTempCpu);
    AddSyntheticHistory(snapshot, "board.fan.cpu", boardFanCpu);
    AddSyntheticHistory(snapshot, "board.fan.system", boardFanSystem);
    AddSyntheticHistory(snapshot, "network.upload", networkUpload);
    AddSyntheticHistory(snapshot, "network.download", networkDownload);
    AddSyntheticHistory(snapshot, "storage.read", storageRead);
    AddSyntheticHistory(snapshot, "storage.write", storageWrite);

    snapshot.now = BuildSyntheticTimestamp(tick);
    snapshot.revision = 1;

    dump.boardProvider.boardManufacturer = "Gigabyte Technology Co., Ltd.";
    dump.boardProvider.boardProduct = "X570 AORUS ULTRA";
    dump.boardProvider.driverLibrary = "Synthetic";
    dump.boardProvider.requestedFanNames = {"cpu", "system"};
    dump.boardProvider.requestedTemperatureNames = {"cpu"};
    dump.boardProvider.availableFanNames = {"CPU", "System 1", "Pump"};
    dump.boardProvider.availableTemperatureNames = {"CPU", "VRM MOS", "Chipset"};
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

ResolvedNetworkCandidate ResolveConfiguredNetworkCandidate(
    const std::string& configuredAdapterName, const std::vector<NetworkAdapterCandidate>& availableCandidates) {
    ResolvedNetworkCandidate resolved;
    if (availableCandidates.empty()) {
        return resolved;
    }

    const auto exactIt =
        std::find_if(availableCandidates.begin(), availableCandidates.end(), [&](const auto& candidate) {
            return !configuredAdapterName.empty() && EqualsInsensitive(candidate.adapterName, configuredAdapterName);
        });
    if (exactIt != availableCandidates.end()) {
        resolved.adapterName = exactIt->adapterName;
        resolved.ipAddress = exactIt->ipAddress;
        return resolved;
    }

    const auto partialIt =
        std::find_if(availableCandidates.begin(), availableCandidates.end(), [&](const auto& candidate) {
            return !configuredAdapterName.empty() && ContainsInsensitive(candidate.adapterName, configuredAdapterName);
        });
    if (partialIt != availableCandidates.end()) {
        resolved.adapterName = partialIt->adapterName;
        resolved.ipAddress = partialIt->ipAddress;
        return resolved;
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

    std::sort(candidates.begin(),
        candidates.end(),
        [](const StorageDriveCandidate& lhs, const StorageDriveCandidate& rhs) { return lhs.letter < rhs.letter; });
    return candidates;
}

void MarkSelectedStorageDriveCandidates(
    std::vector<StorageDriveCandidate>& candidates, const std::vector<std::string>& selectedDrives) {
    for (auto& candidate : candidates) {
        candidate.selected =
            std::find(selectedDrives.begin(), selectedDrives.end(), candidate.letter) != selectedDrives.end();
    }
}

std::filesystem::path ResolveFakePath(
    const std::filesystem::path& workingDirectory, const std::filesystem::path& configuredPath) {
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
    FakeTelemetryCollector(std::filesystem::path fakePath, bool showDialogs, TelemetryDumpLoader loadFakeDump)
        : fakePath_(std::move(fakePath)), useSyntheticSource_(fakePath_.empty()), showDialogs_(showDialogs),
          loadFakeDump_(loadFakeDump) {}

    bool Initialize(const TelemetrySettings& settings, std::ostream* traceStream) override {
        selectionSettings_ = settings.selection;
        trace_.SetOutput(traceStream);
        trace_.Write(useSyntheticSource_ ? std::string("fake:initialize_begin source=synthetic")
                                         : "fake:initialize_begin path=\"" + Utf8FromWide(fakePath_.wstring()) + "\"");
        if (!ReloadFakeDump(true)) {
            trace_.Write("fake:initialize_failed");
            return false;
        }
        trace_.Write("fake:initialize_done");
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

    void SetSelectedStorageDrives(std::vector<std::string> driveLetters) override {
        selectionSettings_.configuredDrives = NormalizeConfiguredStorageDriveLetters(driveLetters);
        RefreshStorageSelection();
    }

    void RefreshSelectionsAndSnapshot() override {
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

    bool ReloadFakeDump(bool required) {
        if (useSyntheticSource_) {
            sourceDump_ = BuildSyntheticTelemetryDump(syntheticTick_++);
            dump_ = sourceDump_;
            RefreshSelectionsAndSnapshot();
            lastReload_ = std::chrono::steady_clock::now();
            trace_.Write("fake:load_done source=synthetic");
            return true;
        }

        std::ifstream input(fakePath_, std::ios::binary);
        if (!input.is_open()) {
            trace_.Write("fake:load_failed reason=open path=\"" + Utf8FromWide(fakePath_.wstring()) + "\"");
            if (required && showDialogs_) {
                const std::wstring message =
                    WideFromUtf8("Failed to open fake telemetry file:\n" + Utf8FromWide(fakePath_.wstring()));
                MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
            }
            return false;
        }

        if (loadFakeDump_ == nullptr) {
            trace_.Write("fake:load_failed reason=loader_unavailable");
            if (required && showDialogs_) {
                MessageBoxW(nullptr, L"Fake telemetry dump loading is unavailable.", L"System Telemetry", MB_ICONERROR);
            }
            return false;
        }

        TelemetryDump loaded;
        std::string error;
        if (!loadFakeDump_(input, loaded, &error)) {
            trace_.Write("fake:load_failed reason=parse error=\"" + error + "\"");
            if (required && showDialogs_) {
                const std::wstring message = WideFromUtf8("Failed to parse fake telemetry file:\n" + error);
                MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
            }
            return false;
        }

        sourceDump_ = std::move(loaded);
        dump_ = sourceDump_;
        RefreshSelectionsAndSnapshot();
        lastReload_ = std::chrono::steady_clock::now();
        trace_.Write("fake:load_done path=\"" + Utf8FromWide(fakePath_.wstring()) + "\"");
        return true;
    }

    std::filesystem::path fakePath_;
    bool useSyntheticSource_ = false;
    bool showDialogs_ = true;
    TelemetryDumpLoader loadFakeDump_ = nullptr;
    TelemetrySelectionSettings selectionSettings_{};
    ResolvedTelemetrySelections resolvedSelections_{};
    TelemetryDump sourceDump_{};
    TelemetryDump dump_{};
    std::vector<NetworkAdapterCandidate> networkAdapters_{};
    std::vector<StorageDriveCandidate> storageDrives_{};
    ResolvedNetworkCandidate resolvedNetwork_{};
    std::vector<std::string> resolvedStorageDrives_{};
    tracing::Trace trace_;
    std::chrono::steady_clock::time_point lastReload_{};
    uint64_t syntheticTick_ = 0;
};

}  // namespace

std::unique_ptr<TelemetryCollector> CreateFakeTelemetryCollector(const std::filesystem::path& workingDirectory,
    const std::filesystem::path& configuredPath,
    bool showDialogs,
    TelemetryDumpLoader loadFakeDump) {
    return std::make_unique<FakeTelemetryCollector>(
        ResolveFakePath(workingDirectory, configuredPath), showDialogs, loadFakeDump);
}
