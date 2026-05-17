#include "telemetry/board/asus/board_asus_armoury_crate.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "telemetry/board/board_vendor.h"
#include "telemetry/impl/system_info_support.h"
#include "util/strings.h"
#include "util/text_format.h"
#include "util/trace.h"

namespace {

constexpr DWORD kAsusAtkIoControl = 0x22240c;
constexpr std::uint32_t kAsusAtkMethodDsts = 0x53545344;
constexpr std::uint32_t kAsusUnsupportedStatus = 0xfffffffe;
constexpr std::uint32_t kAsusDstsPresenceBit = 0x00010000;
constexpr std::uint32_t kAsusAtkCpuTemperature = 0x00120094;
constexpr std::uint32_t kAsusAtkCpuFan = 0x00110013;
constexpr std::uint32_t kAsusAtkGpuFan = 0x00110014;
constexpr char kAsusGpuFanName[] = "GPU Fan";
constexpr wchar_t kAsusAtkDevicePath[] = L"\\\\.\\ATKACPI";  // CreateFileW requires a UTF-16 device path.

struct AsusArmouryCrateSnapshot {
    bool success = false;
    std::string diagnostics;
    std::vector<BoardSensorReading> fans;
    std::vector<BoardSensorReading> temperatures;
};

struct AsusAtkDstsInput {
    std::uint32_t method = kAsusAtkMethodDsts;
    std::uint32_t inputSize = sizeof(std::uint32_t);
    std::uint32_t deviceId = 0;
};

class UniqueHandle final {
public:
    UniqueHandle() = default;

    explicit UniqueHandle(HANDLE value) : value_(value) {}

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    ~UniqueHandle() {
        if (Valid()) {
            CloseHandle(value_);
        }
    }

    bool Valid() const {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }

    HANDLE Get() const {
        return value_;
    }

private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
};

bool HasDstsPresence(std::uint32_t status) {
    return status != kAsusUnsupportedStatus && (status & kAsusDstsPresenceBit) != 0;
}

bool IsSaneCelsius(double value) {
    return value >= -20.0 && value <= 125.0;
}

UniqueHandle OpenAsusAtkDevice() {
    return UniqueHandle(CreateFileW(kAsusAtkDevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
}

bool QueryAsusAtkDsts(
    HANDLE device, std::uint32_t deviceId, std::uint32_t& status, DWORD& error, DWORD& bytesReturned) {
    AsusAtkDstsInput input;
    input.deviceId = deviceId;
    status = 0;
    bytesReturned = 0;
    SetLastError(ERROR_SUCCESS);
    const BOOL result = DeviceIoControl(
        device, kAsusAtkIoControl, &input, sizeof(input), &status, sizeof(status), &bytesReturned, nullptr);
    error = result != FALSE ? ERROR_SUCCESS : GetLastError();
    return result != FALSE && bytesReturned >= sizeof(status);
}

void TraceAtkDriverResult(Trace& trace,
    const char* kind,
    std::uint32_t deviceId,
    const char* name,
    bool ok,
    DWORD error,
    DWORD bytesReturned,
    std::uint32_t status) {
    std::string text = FormatText("atk_driver_%s id=0x%08x", kind, deviceId);
    if (name != nullptr && name[0] != '\0') {
        AppendFormat(text, " name=\"%s\"", name);
    }
    AppendFormat(text,
        " ok=%d error=%lu bytes=%lu status=0x%08x",
        ok ? 1 : 0,
        static_cast<unsigned long>(error),
        static_cast<unsigned long>(bytesReturned),
        status);
    trace.Write(TracePrefix::AsusArmouryCrate, text);
}

bool CaptureAtkDriverStatus(
    Trace& trace, HANDLE device, const char* kind, std::uint32_t deviceId, const char* name, std::uint32_t& status) {
    DWORD error = ERROR_SUCCESS;
    DWORD bytesReturned = 0;
    const bool ok = QueryAsusAtkDsts(device, deviceId, status, error, bytesReturned);
    TraceAtkDriverResult(trace, kind, deviceId, name, ok, error, bytesReturned, status);
    return ok;
}

void CaptureAtkDriverFan(
    Trace& trace, HANDLE device, std::uint32_t deviceId, const char* name, std::vector<BoardSensorReading>& fans) {
    std::uint32_t status = 0;
    if (!CaptureAtkDriverStatus(trace, device, "fan", deviceId, name, status) || !HasDstsPresence(status)) {
        return;
    }

    const std::uint32_t rpm = (status & 0xffff) * 100;
    if (rpm > 0 && rpm < 30000) {
        fans.push_back(BoardSensorReading{name, static_cast<double>(rpm)});
    }
}

void CaptureAtkDriverTemperature(Trace& trace, HANDLE device, std::vector<BoardSensorReading>& temperatures) {
    std::uint32_t status = 0;
    if (!CaptureAtkDriverStatus(trace, device, "temp", kAsusAtkCpuTemperature, "cpu", status) ||
        !HasDstsPresence(status)) {
        return;
    }

    const double celsius = static_cast<double>(status & 0xff);
    if (IsSaneCelsius(celsius)) {
        temperatures.push_back(BoardSensorReading{"CPU Temperature", celsius});
    }
}

AsusArmouryCrateSnapshot CaptureAsusAtkDriverSensors(Trace& trace) {
    AsusArmouryCrateSnapshot snapshot;
    UniqueHandle device = OpenAsusAtkDevice();
    if (!device.Valid()) {
        const DWORD error = GetLastError();
        snapshot.diagnostics = FormatText("ASUS ATKACPI device open failed: %lu", static_cast<unsigned long>(error));
        trace.WriteFmt(
            TracePrefix::AsusArmouryCrate, "atk_driver_open_failed error=%lu", static_cast<unsigned long>(error));
        return snapshot;
    }

    CaptureAtkDriverTemperature(trace, device.Get(), snapshot.temperatures);
    CaptureAtkDriverFan(trace, device.Get(), kAsusAtkCpuFan, "CPU Fan", snapshot.fans);
    CaptureAtkDriverFan(trace, device.Get(), kAsusAtkGpuFan, kAsusGpuFanName, snapshot.fans);

    snapshot.success = true;
    snapshot.diagnostics = FormatText("ASUS Armoury Crate ATKACPI query completed. fan_count=%zu temp_count=%zu",
        snapshot.fans.size(),
        snapshot.temperatures.size());
    trace.WriteFmt(TracePrefix::AsusArmouryCrate,
        "atk_driver_snapshot_done fan_count=%zu temp_count=%zu",
        snapshot.fans.size(),
        snapshot.temperatures.size());
    return snapshot;
}

class AsusArmouryCrateBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    AsusArmouryCrateBoardTelemetryProvider(Trace& trace, BoardVendorInfo info)
        : trace_(trace), info_(std::move(info)) {}

    bool Initialize(const BoardTelemetrySettings& settings) override {
        settings_ = settings;
        trace_.Write(TracePrefix::AsusArmouryCrate, "initialize_begin");

        boardManufacturer_ = info_.manufacturer;
        boardProduct_ = info_.product;
        trace_.WriteFmt(TracePrefix::AsusArmouryCrate,
            "board manufacturer=\"%s\" product=\"%s\"",
            boardManufacturer_.c_str(),
            boardProduct_.c_str());

        if (SelectBoardVendor(info_) != BoardVendor::Asus) {
            diagnostics_ = "Baseboard manufacturer is not ASUS.";
            return false;
        }

        driverLibrary_ = "ASUS Armoury Crate ATKACPI DSTS";
        diagnostics_ = "ASUS Armoury Crate ATKACPI provider ready.";
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
                " requested_temps=%s",
                JoinNames(settings_.requestedTemperatureNames).c_str());
        }
        if (!settings_.requestedFanNames.empty()) {
            AppendFormat(
                requestedDiagnosticsSuffix_, " requested_fans=%s", JoinNames(settings_.requestedFanNames).c_str());
        }
        initialized_ = true;
        return true;
    }

    BoardVendorTelemetrySample Sample() override {
        BoardVendorTelemetrySample sample;
        sample.providerName = "ASUS";
        sample.requestedFanNames = settings_.requestedFanNames;
        sample.requestedTemperatureNames = settings_.requestedTemperatureNames;
        sample.availableFanNames = availableFanNames_;
        sample.availableTemperatureNames = availableTemperatureNames_;
        sample.boardManufacturer = boardManufacturer_;
        sample.boardProduct = boardProduct_;
        sample.driverLibrary = driverLibrary_;
        sample.temperatures = temperatureMetricTemplate_;
        sample.fans = fanMetricTemplate_;
        sample.available = HasAvailableMetricValue(sample.temperatures) || HasAvailableMetricValue(sample.fans);
        sample.diagnostics = FormatText("%s%s", diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());

        if (!initialized_) {
            return sample;
        }

        AsusArmouryCrateSnapshot snapshot = CaptureAsusAtkDriverSensors(trace_);
        if (!snapshot.success) {
            diagnostics_ = snapshot.diagnostics;
            sample.diagnostics = FormatText("%s%s", diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
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
        sample.diagnostics = FormatText("%s%s", diagnostics_.c_str(), requestedDiagnosticsSuffix_.c_str());
        return sample;
    }

private:
    std::string ResolveTemperatureSensorName(const std::string& logicalName) const {
        return ResolveMappedBoardSensorName(settings_.temperatureSensorNames, logicalName);
    }

    std::string ResolveFanSensorName(const std::string& logicalName) const {
        return ResolveMappedBoardSensorName(settings_.fanSensorNames, logicalName);
    }

    Trace& trace_;
    BoardVendorInfo info_;
    BoardTelemetrySettings settings_{};
    std::string boardManufacturer_;
    std::string boardProduct_;
    std::string driverLibrary_;
    std::string diagnostics_ = "ASUS Armoury Crate provider not initialized.";
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

std::unique_ptr<BoardVendorTelemetryProvider> CreateAsusBoardTelemetryProvider(Trace& trace, BoardVendorInfo info) {
    return std::make_unique<AsusArmouryCrateBoardTelemetryProvider>(trace, std::move(info));
}
