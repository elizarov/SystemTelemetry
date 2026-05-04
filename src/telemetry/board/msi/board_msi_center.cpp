#include "telemetry/board/msi/board_msi_center.h"

#include <windows.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <winreg.h>

#include "telemetry/board/board_vendor.h"
#include "telemetry/board/msi/board_msi_center_bridge.h"
#include "telemetry/impl/system_info_support.h"
#include "util/file_path.h"
#include "util/strings.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

constexpr wchar_t kMsiUninstallKey[] = L"SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
constexpr wchar_t kBiosKey[] = L"HARDWARE\\DESCRIPTION\\System\\BIOS";

struct MsiCenterSnapshot {
    bool success = false;
    std::string diagnostics;
    std::vector<BoardSensorReading> fans;
    std::vector<BoardSensorReading> temperatures;
};

std::optional<std::wstring> FindInstalledMsiCenterDirectory() {
    HKEY uninstallKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kMsiUninstallKey, 0, KEY_READ, &uninstallKey) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD index = 0;
    wchar_t childName[256];
    DWORD childNameLength = ARRAYSIZE(childName);
    while (RegEnumKeyExW(uninstallKey, index, childName, &childNameLength, nullptr, nullptr, nullptr, nullptr) ==
           ERROR_SUCCESS) {
        HKEY childKey = nullptr;
        if (RegOpenKeyExW(uninstallKey, childName, 0, KEY_READ, &childKey) == ERROR_SUCCESS) {
            const auto displayName = ReadRegistryWideString(childKey, nullptr, L"DisplayName");
            const bool isMsiCenterSdk =
                displayName.has_value() && ContainsInsensitive(Utf8FromWide(*displayName), "MSI Center SDK");
            if (isMsiCenterSdk) {
                const auto installLocation = ReadRegistryWideString(childKey, nullptr, L"InstallLocation");
                if (installLocation.has_value() && !installLocation->empty()) {
                    const DWORD attributes = GetFileAttributesW(installLocation->c_str());
                    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                        RegCloseKey(childKey);
                        RegCloseKey(uninstallKey);
                        return installLocation;
                    }
                }
            }
            RegCloseKey(childKey);
        }
        ++index;
        childNameLength = ARRAYSIZE(childName);
    }

    RegCloseKey(uninstallKey);
    return std::nullopt;
}

class MsiCenterCapture final : public MsiCenterCaptureSink {
public:
    explicit MsiCenterCapture(Trace& trace) : trace_(trace) {}

    void AddFanReading(const wchar_t* title, double rpm) override {
        snapshot_.fans.push_back(BoardSensorReading{Utf8FromWide(title != nullptr ? title : L""), rpm});
    }

    void AddTemperatureReading(const wchar_t* title, double celsius) override {
        snapshot_.temperatures.push_back(BoardSensorReading{Utf8FromWide(title != nullptr ? title : L""), celsius});
    }

    void SetDiagnostics(const wchar_t* diagnostics) override {
        snapshot_.diagnostics = Utf8FromWide(diagnostics != nullptr ? diagnostics : L"");
    }

    void TraceAssemblyLoaded(const wchar_t* path) override {
        trace_.Write("msi_center:assembly_loaded path=\"" + Utf8FromWide(path != nullptr ? path : L"") + "\"");
    }

    void TraceQuerySuccess(int fanCount, int temperatureCount) override {
        trace_.WriteLazy([&] {
            return "msi_center:snapshot_done fan_count=" + std::to_string(fanCount) +
                   " temp_count=" + std::to_string(temperatureCount);
        });
    }

    void TraceInitializeException(const wchar_t* diagnostics) override {
        trace_.Write("msi_center:initialize_exception " + Utf8FromWide(diagnostics != nullptr ? diagnostics : L""));
    }

    void TraceSnapshotException(const wchar_t* diagnostics) override {
        trace_.WriteLazy([&] {
            return "msi_center:snapshot_exception " + Utf8FromWide(diagnostics != nullptr ? diagnostics : L"");
        });
    }

    MsiCenterSnapshot FinishSuccess() {
        snapshot_.success = true;
        snapshot_.diagnostics =
            "MSI Center hardware-monitor query completed. fan_count=" + std::to_string(snapshot_.fans.size()) +
            " temp_count=" + std::to_string(snapshot_.temperatures.size());
        return std::move(snapshot_);
    }

    MsiCenterSnapshot FinishFailure() {
        return std::move(snapshot_);
    }

private:
    Trace& trace_;
    MsiCenterSnapshot snapshot_;
};

class MsiCenterBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    explicit MsiCenterBoardTelemetryProvider(Trace& trace) : trace_(trace) {}

    bool Initialize(const BoardTelemetrySettings& settings) override {
        settings_ = settings;
        trace().Write("msi_center:initialize_begin");

        boardManufacturer_ = ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardManufacturer").value_or("");
        boardProduct_ = ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardProduct").value_or("");
        trace().Write("msi_center:board manufacturer=\"" + boardManufacturer_ + "\" product=\"" + boardProduct_ + "\"");

        if (!ContainsInsensitive(boardManufacturer_, "micro-star") && !ContainsInsensitive(boardManufacturer_, "msi")) {
            diagnostics_ = "Baseboard manufacturer is not MSI.";
            return false;
        }

        msiCenterDirectory_ = FindInstalledMsiCenterDirectory();
        if (!msiCenterDirectory_.has_value()) {
            diagnostics_ = "MSI Center SDK directory was not found in the registry.";
            return false;
        }

        loadedLibrary_ = Utf8FromWide((FilePath(*msiCenterDirectory_) / L"CS_CommonAPI.dll").wstring());
        diagnostics_ = "MSI Center provider ready.";
        temperatureMetricTemplate_ =
            CreateRequestedBoardMetrics(settings_.requestedTemperatureNames, ScalarMetricUnit::Celsius);
        fanMetricTemplate_ = CreateRequestedBoardMetrics(settings_.requestedFanNames, ScalarMetricUnit::Rpm);
        requestedTemperatureIndexBySourceName_.clear();
        requestedFanIndexBySourceName_.clear();
        for (size_t i = 0; i < temperatureMetricTemplate_.size(); ++i) {
            AppendRequestedBoardMetricIndex(requestedTemperatureIndexBySourceName_,
                ResolveTemperatureSensorName(temperatureMetricTemplate_[i].name),
                i);
        }
        for (size_t i = 0; i < fanMetricTemplate_.size(); ++i) {
            AppendRequestedBoardMetricIndex(
                requestedFanIndexBySourceName_, ResolveFanSensorName(fanMetricTemplate_[i].name), i);
        }
        requestedDiagnosticsSuffix_.clear();
        if (!settings_.requestedTemperatureNames.empty()) {
            requestedDiagnosticsSuffix_ += " requested_temps=" + JoinNames(settings_.requestedTemperatureNames);
        }
        if (!settings_.requestedFanNames.empty()) {
            requestedDiagnosticsSuffix_ += " requested_fans=" + JoinNames(settings_.requestedFanNames);
        }
        initialized_ = true;
        return true;
    }

    BoardVendorTelemetrySample Sample() override {
        BoardVendorTelemetrySample sample;
        sample.providerName = "MSI";
        sample.requestedFanNames = settings_.requestedFanNames;
        sample.requestedTemperatureNames = settings_.requestedTemperatureNames;
        sample.availableFanNames = availableFanNames_;
        sample.availableTemperatureNames = availableTemperatureNames_;
        sample.boardManufacturer = boardManufacturer_;
        sample.boardProduct = boardProduct_;
        sample.driverLibrary = loadedLibrary_;
        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = diagnostics_ + requestedDiagnosticsSuffix_;

        if (!initialized_ || !msiCenterDirectory_.has_value()) {
            return sample;
        }

        MsiCenterCapture capture(trace());
        const std::wstring& msiCenterDirectory = *msiCenterDirectory_;
        const bool captured = runtime_.Capture(msiCenterDirectory.c_str(), capture);
        MsiCenterSnapshot snapshot = captured ? capture.FinishSuccess() : capture.FinishFailure();
        if (!captured) {
            diagnostics_ = snapshot.diagnostics;
            sample.diagnostics = diagnostics_ + requestedDiagnosticsSuffix_;
            return sample;
        }

        diagnostics_ = snapshot.diagnostics;
        availableFanNames_ = ExtractBoardSensorNames(snapshot.fans);
        availableTemperatureNames_ = ExtractBoardSensorNames(snapshot.temperatures);
        sample.availableFanNames = availableFanNames_;
        sample.availableTemperatureNames = availableTemperatureNames_;

        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        ResetBoardMetricValues(sample.temperatures);
        ResetBoardMetricValues(sample.fans);
        ApplyBoardSensorReadingsToMetrics(
            snapshot.temperatures, requestedTemperatureIndexBySourceName_, sample.temperatures);
        ApplyBoardSensorReadingsToMetrics(snapshot.fans, requestedFanIndexBySourceName_, sample.fans);
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = diagnostics_ + requestedDiagnosticsSuffix_;
        return sample;
    }

private:
    std::string ResolveTemperatureSensorName(const std::string& logicalName) const {
        return ResolveMappedBoardSensorName(settings_.temperatureSensorNames, logicalName);
    }

    std::string ResolveFanSensorName(const std::string& logicalName) const {
        return ResolveMappedBoardSensorName(settings_.fanSensorNames, logicalName);
    }

    Trace& trace() {
        return trace_;
    }

    Trace& trace_;
    BoardTelemetrySettings settings_{};
    MsiCenterRuntime runtime_;
    std::optional<std::wstring> msiCenterDirectory_;
    std::string boardManufacturer_;
    std::string boardProduct_;
    std::string loadedLibrary_;
    std::string diagnostics_ = "MSI Center provider not initialized.";
    std::string requestedDiagnosticsSuffix_;
    std::vector<std::string> availableFanNames_;
    std::vector<std::string> availableTemperatureNames_;
    std::vector<NamedScalarMetric> fanMetricTemplate_;
    std::vector<NamedScalarMetric> temperatureMetricTemplate_;
    BoardMetricIndexBySourceName requestedFanIndexBySourceName_;
    BoardMetricIndexBySourceName requestedTemperatureIndexBySourceName_;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<BoardVendorTelemetryProvider> CreateMsiBoardTelemetryProvider(Trace& trace) {
    return std::make_unique<MsiCenterBoardTelemetryProvider>(trace);
}
