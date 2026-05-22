#include "telemetry/board/board_vendor.h"

#include <utility>

#include "telemetry/board/asus/board_asus_armoury_crate.h"
#include "telemetry/board/gigabyte/board_gigabyte_siv.h"
#include "telemetry/board/msi/board_msi_center.h"
#include "telemetry/impl/system_info_support.h"
#include "util/resource_strings.h"
#include "util/trace.h"

namespace {

constexpr char kBiosKey[] = "HARDWARE\\DESCRIPTION\\System\\BIOS";

class UnsupportedBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    UnsupportedBoardTelemetryProvider(Trace& trace, BoardVendorInfo info) :
        trace_(trace),
        info_(std::move(info)) {}

    bool Initialize(const BoardTelemetrySettings& settings) override {
        sample_.providerName = "Unsupported";
        sample_.boardManufacturer = info_.manufacturer;
        sample_.boardProduct = info_.product;
        sample_.requestedFanNames = settings.requestedFanNames;
        sample_.requestedTemperatureNames = settings.requestedTemperatureNames;
        sample_.fans = CreateRequestedBoardMetrics(settings.requestedFanNames, ScalarMetricUnit::Rpm);
        sample_.temperatures =
            CreateRequestedBoardMetrics(settings.requestedTemperatureNames, ScalarMetricUnit::Celsius);
        sample_.available = false;
        sample_.diagnostics =
            ResourceStringText(RES_STR("No supported board telemetry provider matches the baseboard manufacturer."));
        trace_.WriteFmt(TracePrefix::UnsupportedBoard,
            RES_STR("initialize manufacturer=\"%s\" product=\"%s\""),
            info_.manufacturer.c_str(),
            info_.product.c_str());
        return true;
    }

    BoardVendorTelemetrySample Sample() override {
        return sample_;
    }

private:
    Trace& trace_;
    BoardVendorInfo info_;
    BoardVendorTelemetrySample sample_;
};

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardProviderForVendor(
    Trace& trace, BoardVendor vendor, BoardVendorInfo info) {
    if (vendor == BoardVendor::Asus) {
        return CreateAsusBoardTelemetryProvider(trace, std::move(info));
    }
    if (vendor == BoardVendor::Msi) {
        return CreateMsiBoardTelemetryProvider(trace, std::move(info));
    }
    if (vendor == BoardVendor::Gigabyte) {
        return CreateGigabyteBoardTelemetryProvider(trace, std::move(info));
    }

    return std::make_unique<UnsupportedBoardTelemetryProvider>(trace, std::move(info));
}

}  // namespace

BoardVendorInfo ExtractBoardVendorInfo() {
    return BoardVendorInfo{
        ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, "BaseBoardManufacturer").value_or(""),
        ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, "BaseBoardProduct").value_or(""),
    };
}

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(Trace& trace) {
    BoardVendorInfo info = ExtractBoardVendorInfo();
    const BoardVendor vendor = SelectBoardVendor(info);
    trace.WriteFmt(TracePrefix::BoardVendor,
        RES_STR("create vendor=%s manufacturer=\"%s\" product=\"%s\""),
        BoardVendorName(vendor),
        info.manufacturer.c_str(),
        info.product.c_str());
    return CreateBoardProviderForVendor(trace, vendor, std::move(info));
}
