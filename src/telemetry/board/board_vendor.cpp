#include "telemetry/board/board_vendor.h"

#include "telemetry/board/gigabyte/board_gigabyte_siv.h"
#include "telemetry/board/msi/board_msi_center.h"
#include "telemetry/impl/system_info_support.h"
#include "util/strings.h"
#include "util/trace.h"

namespace {

constexpr wchar_t kBiosKey[] = L"HARDWARE\\DESCRIPTION\\System\\BIOS";

class UnsupportedBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    explicit UnsupportedBoardTelemetryProvider(Trace& trace) : trace_(trace) {}

    bool Initialize(const BoardTelemetrySettings& settings) override {
        boardManufacturer_ = ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardManufacturer").value_or("");
        boardProduct_ = ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardProduct").value_or("");
        sample_.providerName = "Unsupported";
        sample_.boardManufacturer = boardManufacturer_;
        sample_.boardProduct = boardProduct_;
        sample_.requestedFanNames = settings.requestedFanNames;
        sample_.requestedTemperatureNames = settings.requestedTemperatureNames;
        sample_.fans = CreateRequestedBoardMetrics(settings.requestedFanNames, ScalarMetricUnit::Rpm);
        sample_.temperatures =
            CreateRequestedBoardMetrics(settings.requestedTemperatureNames, ScalarMetricUnit::Celsius);
        sample_.available = false;
        sample_.diagnostics = "No supported board telemetry provider matches the baseboard manufacturer.";
        trace_.Write("unsupported_board:initialize manufacturer=\"" + boardManufacturer_ + "\" product=\"" +
                     boardProduct_ + "\"");
        return true;
    }

    BoardVendorTelemetrySample Sample() override {
        return sample_;
    }

private:
    Trace& trace_;
    std::string boardManufacturer_;
    std::string boardProduct_;
    BoardVendorTelemetrySample sample_;
};

}  // namespace

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(Trace& trace) {
    const std::string manufacturer =
        ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, L"BaseBoardManufacturer").value_or("");
    if (ContainsInsensitive(manufacturer, "micro-star") || ContainsInsensitive(manufacturer, "msi")) {
        return CreateMsiBoardTelemetryProvider(trace);
    }
    if (ContainsInsensitive(manufacturer, "gigabyte")) {
        return CreateGigabyteBoardTelemetryProvider(trace);
    }

    return std::make_unique<UnsupportedBoardTelemetryProvider>(trace);
}
