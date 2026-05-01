#include "telemetry/board/gigabyte/board_gigabyte_siv.h"

#include <windows.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <winreg.h>

#include "telemetry/board/board_vendor.h"
#include "telemetry/board/gigabyte/board_gigabyte_siv_bridge.h"
#include "telemetry/impl/system_info_support.h"
#include "util/file_path.h"
#include "util/strings.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

constexpr wchar_t kEngineEnvironmentControlDll[] = L"Gigabyte.Engine.EnvironmentControl.dll";
constexpr wchar_t kSivUninstallKey[] = L"SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
constexpr wchar_t kBiosKey[] = L"HARDWARE\\DESCRIPTION\\System\\BIOS";

struct GigabyteSivFanReading {
    std::string title;
    std::optional<double> rpm;
};

struct GigabyteSivTemperatureReading {
    std::string title;
    std::optional<double> celsius;
};

struct GigabyteSivSnapshot {
    bool success = false;
    std::string diagnostics;
    std::vector<GigabyteSivFanReading> fans;
    std::vector<GigabyteSivTemperatureReading> temperatures;
};

std::optional<std::wstring> FindInstalledSivDirectory() {
    HKEY uninstallKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kSivUninstallKey, 0, KEY_READ, &uninstallKey) != ERROR_SUCCESS) {
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
            const bool isSiv =
                displayName.has_value() && (EqualsInsensitive(*displayName, L"SIV") ||
                                               EqualsInsensitive(*displayName, L"System Information Viewer"));
            if (isSiv) {
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

std::string ResolveMappedSensorName(
    const std::unordered_map<std::string, std::string>& sensorNames, const std::string& logicalName) {
    const auto it = sensorNames.find(logicalName);
    if (it != sensorNames.end() && !it->second.empty()) {
        return it->second;
    }
    return logicalName;
}

template <typename Reading> std::vector<std::string> ExtractSensorNames(const std::vector<Reading>& readings) {
    std::vector<std::string> names;
    names.reserve(readings.size());
    for (const auto& reading : readings) {
        if (!reading.title.empty()) {
            names.push_back(reading.title);
        }
    }
    return names;
}

void AppendRequestedMetricIndex(
    std::unordered_map<std::string, std::vector<size_t>>& indexBySourceName, std::string sourceName, size_t index) {
    auto& indices = indexBySourceName[std::move(sourceName)];
    if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
        indices.push_back(index);
    }
}

void ResetMetricValues(std::vector<NamedScalarMetric>& metrics) {
    for (auto& metric : metrics) {
        metric.metric.value.reset();
    }
}

class GigabyteSivCapture final : public GigabyteSivCaptureSink {
public:
    explicit GigabyteSivCapture(Trace& trace) : trace_(trace) {}

    void AddFanReading(const wchar_t* title, double rpm) override {
        snapshot_.fans.push_back(GigabyteSivFanReading{Utf8FromWide(title != nullptr ? title : L""), rpm});
    }

    void AddTemperatureReading(const wchar_t* title, double celsius) override {
        snapshot_.temperatures.push_back(
            GigabyteSivTemperatureReading{Utf8FromWide(title != nullptr ? title : L""), celsius});
    }

    void SetDiagnostics(const wchar_t* diagnostics) override {
        snapshot_.diagnostics = Utf8FromWide(diagnostics != nullptr ? diagnostics : L"");
    }

    void TraceAssemblyPreload(const wchar_t* path) override {
        trace_.Write("gigabyte_siv:assembly_preload path=\"" + Utf8FromWide(path != nullptr ? path : L"") + "\"");
    }

    void TraceMonitorCreated(const wchar_t* typeName) override {
        trace_.Write(
            "gigabyte_siv:monitor_created type=\"" + Utf8FromWide(typeName != nullptr ? typeName : L"") + "\"");
    }

    void TraceInitializeSuccess() override {
        trace_.Write("gigabyte_siv:initialize_success source=HwRegister");
    }

    void TraceInitializeException(const wchar_t* diagnostics) override {
        trace_.Write("gigabyte_siv:initialize_exception " + Utf8FromWide(diagnostics != nullptr ? diagnostics : L""));
    }

    void TraceSnapshotException(const wchar_t* diagnostics) override {
        trace_.WriteLazy([&] {
            return "gigabyte_siv:snapshot_exception " + Utf8FromWide(diagnostics != nullptr ? diagnostics : L"");
        });
    }

    GigabyteSivSnapshot FinishSuccess() {
        snapshot_.success = true;
        snapshot_.diagnostics =
            "Gigabyte SIV hardware-monitor query completed. fan_count=" + std::to_string(snapshot_.fans.size()) +
            " temp_count=" + std::to_string(snapshot_.temperatures.size());
        trace_.WriteLazy([&] {
            return "gigabyte_siv:snapshot_done fan_count=" + std::to_string(snapshot_.fans.size()) +
                   " temp_count=" + std::to_string(snapshot_.temperatures.size());
        });
        return std::move(snapshot_);
    }

    GigabyteSivSnapshot FinishFailure() {
        return std::move(snapshot_);
    }

private:
    Trace& trace_;
    GigabyteSivSnapshot snapshot_;
};

class GigabyteSivBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    explicit GigabyteSivBoardTelemetryProvider(Trace& trace) : trace_(trace) {}

    bool Initialize(const BoardTelemetrySettings& settings) override {
        settings_ = settings;
        trace().Write("gigabyte_siv:initialize_begin");

        boardManufacturer_ = ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardManufacturer").value_or("");
        boardProduct_ = ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardProduct").value_or("");
        trace().Write(
            "gigabyte_siv:board manufacturer=\"" + boardManufacturer_ + "\" product=\"" + boardProduct_ + "\"");

        if (!ContainsInsensitive(boardManufacturer_, "gigabyte")) {
            diagnostics_ = "Baseboard manufacturer is not Gigabyte.";
            return false;
        }

        sivDirectory_ = FindInstalledSivDirectory();

        if (!sivDirectory_.has_value()) {
            diagnostics_ = "Gigabyte SIV directory was not found in the registry.";
            return false;
        }

        loadedLibrary_ = Utf8FromWide((FilePath(*sivDirectory_) / kEngineEnvironmentControlDll).wstring());
        diagnostics_ = "Gigabyte SIV provider ready.";
        temperatureMetricTemplate_ =
            CreateRequestedBoardMetrics(settings_.requestedTemperatureNames, ScalarMetricUnit::Celsius);
        fanMetricTemplate_ = CreateRequestedBoardMetrics(settings_.requestedFanNames, ScalarMetricUnit::Rpm);
        requestedTemperatureIndexBySourceName_.clear();
        requestedFanIndexBySourceName_.clear();
        for (size_t i = 0; i < temperatureMetricTemplate_.size(); ++i) {
            AppendRequestedMetricIndex(requestedTemperatureIndexBySourceName_,
                ResolveTemperatureSensorName(temperatureMetricTemplate_[i].name),
                i);
        }
        for (size_t i = 0; i < fanMetricTemplate_.size(); ++i) {
            AppendRequestedMetricIndex(
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
        sample.providerName = "Gigabyte";
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

        if (!initialized_ || !sivDirectory_.has_value()) {
            return sample;
        }

        GigabyteSivCapture capture(trace());
        const bool captured = runtime_.Capture(sivDirectory_->c_str(), capture);
        GigabyteSivSnapshot snapshot = captured ? capture.FinishSuccess() : capture.FinishFailure();
        if (!captured) {
            diagnostics_ = snapshot.diagnostics;
            sample.diagnostics = diagnostics_ + requestedDiagnosticsSuffix_;
            return sample;
        }

        diagnostics_ = snapshot.diagnostics;
        availableFanNames_ = ExtractSensorNames(snapshot.fans);
        availableTemperatureNames_ = ExtractSensorNames(snapshot.temperatures);
        sample.availableFanNames = availableFanNames_;
        sample.availableTemperatureNames = availableTemperatureNames_;

        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        ResetMetricValues(sample.temperatures);
        ResetMetricValues(sample.fans);
        for (const auto& reading : snapshot.temperatures) {
            const auto it = requestedTemperatureIndexBySourceName_.find(reading.title);
            if (it != requestedTemperatureIndexBySourceName_.end()) {
                for (const size_t index : it->second) {
                    sample.temperatures[index].metric.value = reading.celsius;
                }
            }
        }
        for (const auto& reading : snapshot.fans) {
            const auto it = requestedFanIndexBySourceName_.find(reading.title);
            if (it != requestedFanIndexBySourceName_.end()) {
                for (const size_t index : it->second) {
                    sample.fans[index].metric.value = reading.rpm;
                }
            }
        }
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = diagnostics_ + requestedDiagnosticsSuffix_;
        return sample;
    }

private:
    std::string ResolveTemperatureSensorName(const std::string& logicalName) const {
        return ResolveMappedSensorName(settings_.temperatureSensorNames, logicalName);
    }

    std::string ResolveFanSensorName(const std::string& logicalName) const {
        return ResolveMappedSensorName(settings_.fanSensorNames, logicalName);
    }

    Trace& trace() {
        return trace_;
    }

    Trace& trace_;
    BoardTelemetrySettings settings_{};
    GigabyteSivRuntime runtime_;
    std::optional<std::wstring> sivDirectory_;
    std::string boardManufacturer_;
    std::string boardProduct_;
    std::string loadedLibrary_;
    std::string diagnostics_ = "Gigabyte provider not initialized.";
    std::string requestedDiagnosticsSuffix_;
    std::vector<std::string> availableFanNames_;
    std::vector<std::string> availableTemperatureNames_;
    std::vector<NamedScalarMetric> fanMetricTemplate_;
    std::vector<NamedScalarMetric> temperatureMetricTemplate_;
    std::unordered_map<std::string, std::vector<size_t>> requestedFanIndexBySourceName_;
    std::unordered_map<std::string, std::vector<size_t>> requestedTemperatureIndexBySourceName_;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<BoardVendorTelemetryProvider> CreateGigabyteBoardTelemetryProvider(Trace& trace) {
    return std::make_unique<GigabyteSivBoardTelemetryProvider>(trace);
}
