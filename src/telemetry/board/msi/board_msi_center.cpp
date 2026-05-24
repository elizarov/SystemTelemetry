#include "telemetry/board/msi/board_msi_center.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "telemetry/board/board_vendor.h"
#include "telemetry/board/msi/impl/hdi_msi_center.h"
#include "telemetry/impl/hdi.h"
#include "telemetry/impl/system_info_support.h"
#include "util/file_path.h"
#include "util/resource_strings.h"
#include "util/strings.h"
#include "util/text_encoding.h"
#include "util/text_format.h"
#include "util/trace.h"

namespace {

std::string TextFromNullableWide(const wchar_t* text) {
    return text != nullptr ? TextFromWide(text) : std::string();
}

struct MsiCenterSnapshot {
    bool success = false;
    std::string diagnostics;
    std::vector<BoardSensorReading> fans;
    std::vector<BoardSensorReading> temperatures;
};

class MsiCenterCapture final : public MsiCenterCaptureSink {
public:
    explicit MsiCenterCapture(Trace& trace) : trace_(trace) {}

    void AddFanReading(const wchar_t* title, double rpm) override {
        snapshot_.fans.push_back(BoardSensorReading{TextFromNullableWide(title), rpm});
    }

    void AddTemperatureReading(const wchar_t* title, double celsius) override {
        snapshot_.temperatures.push_back(BoardSensorReading{TextFromNullableWide(title), celsius});
    }

    void SetDiagnostics(const wchar_t* diagnostics) override {
        snapshot_.diagnostics = TextFromNullableWide(diagnostics);
    }

    void TraceAssemblyLoaded(const wchar_t* path) override {
        if (trace_.Enabled(TracePrefix::MsiCenter)) {
            const std::string pathText = TextFromNullableWide(path);
            trace_.WriteFmt(TracePrefix::MsiCenter, RES_STR("assembly_loaded path=\"%s\""), pathText.c_str());
        }
    }

    void TraceQuerySuccess(int fanCount, int temperatureCount) override {
        trace_.WriteFmt(
            TracePrefix::MsiCenter, RES_STR("snapshot_done fan_count=%d temp_count=%d"), fanCount, temperatureCount);
    }

    void TraceInitializeException(const wchar_t* diagnostics) override {
        if (trace_.Enabled(TracePrefix::MsiCenter)) {
            const std::string diagnosticsText = TextFromNullableWide(diagnostics);
            trace_.WriteFmt(TracePrefix::MsiCenter, RES_STR("initialize_exception %s"), diagnosticsText.c_str());
        }
    }

    void TraceSnapshotException(const wchar_t* diagnostics) override {
        if (trace_.Enabled(TracePrefix::MsiCenter)) {
            const std::string diagnosticsText = TextFromNullableWide(diagnostics);
            trace_.WriteFmt(TracePrefix::MsiCenter, RES_STR("snapshot_exception %s"), diagnosticsText.c_str());
        }
    }

    MsiCenterSnapshot FinishSuccess() {
        snapshot_.success = true;
        snapshot_.diagnostics =
            FormatText(RES_STR("MSI Center hardware-monitor query completed. fan_count=%zu temp_count=%zu"),
                snapshot_.fans.size(),
                snapshot_.temperatures.size());
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
    MsiCenterBoardTelemetryProvider(Trace& trace, BoardVendorInfo info, const HardwareDependencyInjection* injection)
        : trace_(trace), hdi_(ResolveHdiFactory(injection).CreateMsiCenterHdi(trace)), info_(std::move(info)) {}

    bool Initialize(const BoardTelemetrySettings& settings) override {
        settings_ = settings;
        trace().Write(TracePrefix::MsiCenter, RES_STR("initialize_begin"));

        boardManufacturer_ = info_.manufacturer;
        boardProduct_ = info_.product;
        trace().WriteFmt(TracePrefix::MsiCenter,
            RES_STR("board manufacturer=\"%s\" product=\"%s\""),
            boardManufacturer_.c_str(),
            boardProduct_.c_str());

        if (SelectBoardVendor(info_) != BoardVendor::Msi) {
            diagnostics_ = ResourceStringText(RES_STR("Baseboard manufacturer is not MSI."));
            return false;
        }

        msiCenterDirectory_ = hdi_ != nullptr ? hdi_->FindInstalledDirectory() : std::nullopt;
        if (!msiCenterDirectory_.has_value()) {
            diagnostics_ = ResourceStringText(RES_STR("MSI Center SDK directory was not found in the registry."));
            return false;
        }

        loadedLibrary_ = (*msiCenterDirectory_ / "CS_CommonAPI.dll").string();
        diagnostics_ = ResourceStringText(RES_STR("MSI Center provider ready."));
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
            AppendFormat(requestedDiagnosticsSuffix_,
                RES_STR(" requested_temps=%s"),
                JoinNames(settings_.requestedTemperatureNames).c_str());
        }
        if (!settings_.requestedFanNames.empty()) {
            AppendFormat(requestedDiagnosticsSuffix_,
                RES_STR(" requested_fans=%s"),
                JoinNames(settings_.requestedFanNames).c_str());
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
        sample.diagnostics = FormatText(RES_STR("%s%s"), diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());

        const std::optional<FilePath> msiCenterDirectory = msiCenterDirectory_;
        if (!initialized_ || !msiCenterDirectory.has_value()) {
            return sample;
        }

        MsiCenterCapture capture(trace());
        const bool captured = hdi_ != nullptr && hdi_->Capture(msiCenterDirectory->string().c_str(), capture);
        MsiCenterSnapshot snapshot = captured ? capture.FinishSuccess() : capture.FinishFailure();
        if (!captured) {
            diagnostics_ = snapshot.diagnostics;
            sample.diagnostics = FormatText(RES_STR("%s%s"), diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
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

    Trace& trace_;
    std::unique_ptr<MsiCenterHdi> hdi_;
    BoardVendorInfo info_;
    BoardTelemetrySettings settings_{};
    std::optional<FilePath> msiCenterDirectory_;
    std::string boardManufacturer_;
    std::string boardProduct_;
    std::string loadedLibrary_;
    std::string diagnostics_ = ResourceStringText(RES_STR("MSI Center provider not initialized."));
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

std::unique_ptr<BoardVendorTelemetryProvider> CreateMsiBoardTelemetryProvider(Trace& trace, BoardVendorInfo info) {
    return CreateMsiBoardTelemetryProvider(trace, std::move(info), nullptr);
}

std::unique_ptr<BoardVendorTelemetryProvider> CreateMsiBoardTelemetryProvider(
    Trace& trace, BoardVendorInfo info, const HardwareDependencyInjection* injection) {
    return std::make_unique<MsiCenterBoardTelemetryProvider>(trace, std::move(info), injection);
}
