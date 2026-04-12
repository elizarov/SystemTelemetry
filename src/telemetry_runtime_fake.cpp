#include "telemetry_runtime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "snapshot_dump.h"
#include "telemetry_network_source.h"
#include "telemetry_storage_source.h"
#include "trace.h"
#include "utf8.h"

namespace {

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

    bool Initialize(const AppConfig& config, std::ostream* traceStream) override {
        effectiveConfig_ = config;
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

    AppConfig EffectiveConfig() const override {
        AppConfig config = effectiveConfig_;
        if (!resolvedNetwork_.adapterName.empty()) {
            config.network.adapterName = resolvedNetwork_.adapterName;
        }
        config.storage.drives = resolvedStorageDrives_;
        return config;
    }

    const std::vector<NetworkAdapterCandidate>& NetworkAdapterCandidates() const override {
        return networkAdapters_;
    }

    const std::vector<StorageDriveCandidate>& StorageDriveCandidates() const override {
        return storageDrives_;
    }

    void SetEffectiveConfig(const AppConfig& config) override {
        effectiveConfig_ = config;
    }

    void SetPreferredNetworkAdapterName(const std::string& adapterName) override {
        effectiveConfig_.network.adapterName = adapterName;
        RefreshNetworkSelection();
    }

    void SetSelectedStorageDrives(const std::vector<std::string>& driveLetters) override {
        effectiveConfig_.storage.drives = driveLetters;
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
        resolvedNetwork_ =
            ResolveConfiguredNetworkCandidate(effectiveConfig_.network.adapterName, networkAdapters_);
        MarkSelectedNetworkAdapterCandidates(networkAdapters_, resolvedNetwork_);

        dump_.snapshot.network.adapterName =
            resolvedNetwork_.adapterName.empty() ? "Auto" : resolvedNetwork_.adapterName;
        dump_.snapshot.network.ipAddress = resolvedNetwork_.ipAddress.empty() ? "N/A" : resolvedNetwork_.ipAddress;
    }

    void RefreshStorageSelection() {
        storageDrives_ = EnumerateSnapshotStorageDriveCandidates(sourceDump_.snapshot);
        resolvedStorageDrives_ = ResolveConfiguredStorageDrives(effectiveConfig_.storage.drives, storageDrives_);
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
    AppConfig effectiveConfig_{};
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
