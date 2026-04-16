#include "telemetry_runtime.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "app_strings.h"
#include "snapshot_dump.h"
#include "trace.h"
#include "utf8.h"

namespace {

struct ResolvedNetworkCandidate {
    std::string adapterName;
    std::string ipAddress = "N/A";
};

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

    const auto exactIt = std::find_if(availableCandidates.begin(), availableCandidates.end(), [&](const auto& candidate) {
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

    for (const auto& drive : availableDrives) {
        if (drive.driveType != DRIVE_FIXED) {
            continue;
        }
        if (std::find(resolvedDrives.begin(), resolvedDrives.end(), drive.letter) == resolvedDrives.end()) {
            resolvedDrives.push_back(drive.letter);
        }
    }
    return resolvedDrives;
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
        return workingDirectory / L"telemetry_fake.txt";
    }
    if (configuredPath.is_absolute()) {
        return configuredPath;
    }
    return workingDirectory / configuredPath;
}

class FakeTelemetryRuntime : public TelemetryRuntime {
public:
    FakeTelemetryRuntime(std::filesystem::path fakePath, bool showDialogs)
        : fakePath_(std::move(fakePath)), showDialogs_(showDialogs) {}

    bool Initialize(const TelemetrySettings& settings, std::ostream* traceStream) override {
        selectionSettings_ = settings.selection;
        trace_.SetOutput(traceStream);
        trace_.Write("fake:initialize_begin path=\"" + Utf8FromWide(fakePath_.wstring()) + "\"");
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

    void SetPreferredNetworkAdapterName(const std::string& adapterName) override {
        selectionSettings_.preferredAdapterName = adapterName;
        RefreshNetworkSelection();
    }

    void SetSelectedStorageDrives(const std::vector<std::string>& driveLetters) override {
        selectionSettings_.configuredDrives = driveLetters;
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
        resolvedStorageDrives_ = ResolveConfiguredStorageDrives(selectionSettings_.configuredDrives, storageDrives_);
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

        TelemetryDump loaded;
        std::string error;
        if (!LoadTelemetryDump(input, loaded, &error)) {
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
    bool showDialogs_ = true;
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
};

}  // namespace

std::unique_ptr<TelemetryRuntime> CreateFakeTelemetryRuntime(
    const std::filesystem::path& workingDirectory, const std::filesystem::path& configuredPath, bool showDialogs) {
    return std::make_unique<FakeTelemetryRuntime>(ResolveFakePath(workingDirectory, configuredPath), showDialogs);
}
