#include "telemetry/board/gigabyte/board_gigabyte_siv.h"

#include <windows.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <winreg.h>

#include "telemetry/board/board_vendor.h"
#include "telemetry/board/gigabyte/board_gigabyte_siv_bridge.h"
#include "telemetry/impl/system_info_support.h"
#include "util/file_path.h"
#include "util/resource_strings.h"
#include "util/strings.h"
#include "util/text_encoding.h"
#include "util/text_format.h"
#include "util/trace.h"

namespace {

constexpr char kEngineEnvironmentControlDll[] = "Gigabyte.Engine.EnvironmentControl.dll";
constexpr char kSivUninstallKey[] = "SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

std::string TextFromNullableWide(const wchar_t* text) {
    return text != nullptr ? TextFromWide(text) : std::string();
}

struct GigabyteSivSnapshot {
    bool success = false;
    std::string diagnostics;
    std::vector<BoardSensorReading> fans;
    std::vector<BoardSensorReading> temperatures;
};

std::optional<FilePath> FindInstalledSivDirectory() {
    HKEY uninstallKey = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, kSivUninstallKey, 0, KEY_READ, &uninstallKey) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD index = 0;
    char childName[256];
    DWORD childNameLength = ARRAYSIZE(childName);
    while (RegEnumKeyExA(uninstallKey, index, childName, &childNameLength, nullptr, nullptr, nullptr, nullptr) ==
           ERROR_SUCCESS) {
        HKEY childKey = nullptr;
        if (RegOpenKeyExA(uninstallKey, childName, 0, KEY_READ, &childKey) == ERROR_SUCCESS) {
            const auto displayName = ReadRegistryString(childKey, nullptr, "DisplayName");
            const std::string displayNameText = displayName.value_or("");
            const bool isSiv = !displayNameText.empty() &&
                (EqualsInsensitive(displayNameText, "SIV") ||
                 EqualsInsensitive(displayNameText, "System Information Viewer"));
            if (isSiv) {
                const auto installLocation = ReadRegistryString(childKey, nullptr, "InstallLocation");
                if (installLocation.has_value() && !installLocation->empty()) {
                    const DWORD attributes = GetFileAttributesA(installLocation->c_str());
                    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                        RegCloseKey(childKey);
                        RegCloseKey(uninstallKey);
                        return FilePath(*installLocation);
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

class GigabyteSivCapture final : public GigabyteSivCaptureSink {
public:
    explicit GigabyteSivCapture(Trace& trace) : trace_(trace) {
        snapshot_.fans.reserve(4);
        snapshot_.temperatures.reserve(4);
    }

    void AddFanReading(const wchar_t* title, double rpm) override {
        snapshot_.fans.push_back(BoardSensorReading{TextFromNullableWide(title), rpm});
    }

    void AddTemperatureReading(const wchar_t* title, double celsius) override {
        snapshot_.temperatures.push_back(BoardSensorReading{TextFromNullableWide(title), celsius});
    }

    void SetDiagnostics(const wchar_t* diagnostics) override {
        snapshot_.diagnostics = TextFromNullableWide(diagnostics);
    }

    void TraceAssemblyPreload(const wchar_t* path) override {
        if (trace_.Enabled(TracePrefix::GigabyteSiv)) {
            const std::string pathText = TextFromNullableWide(path);
            trace_.WriteFmt(TracePrefix::GigabyteSiv, RES_STR("assembly_preload path=\"%s\""), pathText.c_str());
        }
    }

    void TraceMonitorCreated(const wchar_t* typeName) override {
        if (trace_.Enabled(TracePrefix::GigabyteSiv)) {
            const std::string typeText = TextFromNullableWide(typeName);
            trace_.WriteFmt(TracePrefix::GigabyteSiv, RES_STR("monitor_created type=\"%s\""), typeText.c_str());
        }
    }

    void TraceInitializeSuccess() override {
        trace_.Write(TracePrefix::GigabyteSiv, RES_STR("initialize_success source=HwRegister"));
    }

    void TraceInitializeException(const wchar_t* diagnostics) override {
        if (trace_.Enabled(TracePrefix::GigabyteSiv)) {
            const std::string diagnosticsText = TextFromNullableWide(diagnostics);
            trace_.WriteFmt(TracePrefix::GigabyteSiv, RES_STR("initialize_exception %s"), diagnosticsText.c_str());
        }
    }

    void TraceSnapshotException(const wchar_t* diagnostics) override {
        if (trace_.Enabled(TracePrefix::GigabyteSiv)) {
            const std::string diagnosticsText = TextFromNullableWide(diagnostics);
            trace_.WriteFmt(TracePrefix::GigabyteSiv, RES_STR("snapshot_exception %s"), diagnosticsText.c_str());
        }
    }

    GigabyteSivSnapshot FinishSuccess() {
        snapshot_.success = true;
        snapshot_.diagnostics = FormatText(
            RES_STR("Gigabyte SIV hardware-monitor query completed. fan_count=%zu temp_count=%zu"),
            snapshot_.fans.size(),
            snapshot_.temperatures.size());
        trace_.WriteFmt(
            TracePrefix::GigabyteSiv,
            RES_STR("snapshot_done fan_count=%zu temp_count=%zu"),
            snapshot_.fans.size(),
            snapshot_.temperatures.size());
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
    GigabyteSivBoardTelemetryProvider(Trace& trace, BoardVendorInfo info) : trace_(trace), info_(std::move(info)) {}

    bool Initialize(const BoardTelemetrySettings& settings) override {
        settings_ = settings;
        trace().Write(TracePrefix::GigabyteSiv, RES_STR("initialize_begin"));

        boardManufacturer_ = info_.manufacturer;
        boardProduct_ = info_.product;
        trace().WriteFmt(
            TracePrefix::GigabyteSiv,
            RES_STR("board manufacturer=\"%s\" product=\"%s\""),
            boardManufacturer_.c_str(),
            boardProduct_.c_str());

        if (SelectBoardVendor(info_) != BoardVendor::Gigabyte) {
            diagnostics_ = ResourceStringText(RES_STR("Baseboard manufacturer is not Gigabyte."));
            return false;
        }

        sivDirectory_ = FindInstalledSivDirectory();

        if (!sivDirectory_.has_value()) {
            diagnostics_ = ResourceStringText(RES_STR("Gigabyte SIV directory was not found in the registry."));
            return false;
        }

        loadedLibrary_ = (*sivDirectory_ / kEngineEnvironmentControlDll).string();
        diagnostics_ = ResourceStringText(RES_STR("Gigabyte SIV provider ready."));
        temperatureMetricTemplate_ =
            CreateRequestedBoardMetrics(settings_.requestedTemperatureNames, ScalarMetricUnit::Celsius);
        fanMetricTemplate_ = CreateRequestedBoardMetrics(settings_.requestedFanNames, ScalarMetricUnit::Rpm);
        requestedTemperatureIndexBySourceName_.clear();
        requestedFanIndexBySourceName_.clear();
        for (size_t i = 0; i < temperatureMetricTemplate_.size(); ++i) {
            AppendRequestedBoardMetricIndex(
                requestedTemperatureIndexBySourceName_,
                ResolveTemperatureSensorName(temperatureMetricTemplate_[i].name),
                i);
        }
        for (size_t i = 0; i < fanMetricTemplate_.size(); ++i) {
            AppendRequestedBoardMetricIndex(
                requestedFanIndexBySourceName_, ResolveFanSensorName(fanMetricTemplate_[i].name), i);
        }
        requestedDiagnosticsSuffix_.clear();
        if (!settings_.requestedTemperatureNames.empty()) {
            AppendFormat(
                requestedDiagnosticsSuffix_,
                RES_STR(" requested_temps=%s"),
                JoinNames(settings_.requestedTemperatureNames).c_str());
        }
        if (!settings_.requestedFanNames.empty()) {
            AppendFormat(
                requestedDiagnosticsSuffix_,
                RES_STR(" requested_fans=%s"),
                JoinNames(settings_.requestedFanNames).c_str());
        }
        initialized_ = true;
        return true;
    }

    BoardVendorTelemetrySample Sample() override {
        BoardVendorTelemetrySample sample;
        sample.providerName = "Gigabyte";
        sample.requestedFanNames = settings_.requestedFanNames;
        sample.requestedTemperatureNames = settings_.requestedTemperatureNames;
        sample.boardManufacturer = boardManufacturer_;
        sample.boardProduct = boardProduct_;
        sample.driverLibrary = loadedLibrary_;

        if (!initialized_ || !sivDirectory_.has_value()) {
            PopulateUnavailableSample(sample);
            return sample;
        }

        GigabyteSivCapture capture(trace());
        const bool captured = runtime_.Capture(sivDirectory_->string().c_str(), capture);
        GigabyteSivSnapshot snapshot = captured ? capture.FinishSuccess() : capture.FinishFailure();
        if (!captured) {
            diagnostics_ = snapshot.diagnostics;
            PopulateUnavailableSample(sample);
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
        sample.diagnostics = FormatText(RES_STR("%s%s"), diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
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

    void PopulateUnavailableSample(BoardVendorTelemetrySample& sample) const {
        sample.availableFanNames = availableFanNames_;
        sample.availableTemperatureNames = availableTemperatureNames_;
        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = FormatText(RES_STR("%s%s"), diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
    }

    Trace& trace_;
    BoardVendorInfo info_;
    BoardTelemetrySettings settings_{};
    GigabyteSivRuntime runtime_;
    std::optional<FilePath> sivDirectory_;
    std::string boardManufacturer_;
    std::string boardProduct_;
    std::string loadedLibrary_;
    std::string diagnostics_ = ResourceStringText(RES_STR("Gigabyte provider not initialized."));
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

std::unique_ptr<BoardVendorTelemetryProvider> CreateGigabyteBoardTelemetryProvider(Trace& trace, BoardVendorInfo info) {
    return std::make_unique<GigabyteSivBoardTelemetryProvider>(trace, std::move(info));
}
