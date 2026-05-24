#include "telemetry/board/board_vendor.h"

#include <utility>

#include "telemetry/board/asus/board_asus_armoury_crate.h"
#include "telemetry/board/gigabyte/board_gigabyte_siv.h"
#include "telemetry/board/lenovo/board_lenovo_vantage.h"
#include "telemetry/board/msi/board_msi_center.h"
#include "telemetry/impl/hdi.h"
#include "telemetry/impl/hdi_board_discovery.h"
#include "telemetry/impl/system_info_support.h"
#include "util/resource_strings.h"
#include "util/trace.h"

namespace {

class UnsupportedBoardTelemetryProvider final : public BoardVendorTelemetryProvider {
public:
    UnsupportedBoardTelemetryProvider(Trace& trace, BoardVendorInfo info) : trace_(trace), info_(std::move(info)) {}

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

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardProviderForVendor(Trace& trace,
    BoardVendor vendor,
    BoardVendorInfo info,
    const BoardVendorTelemetryProviderOptions& options,
    const HardwareDependencyInjection* injection) {
    if (vendor == BoardVendor::Asus) {
        return CreateAsusBoardTelemetryProvider(trace, std::move(info));
    }
    if (vendor == BoardVendor::Msi) {
        return CreateMsiBoardTelemetryProvider(trace, std::move(info), injection);
    }
    if (vendor == BoardVendor::Gigabyte) {
        return CreateGigabyteBoardTelemetryProvider(trace, std::move(info));
    }
    if (vendor == BoardVendor::Lenovo) {
        return CreateLenovoBoardTelemetryProvider(trace, std::move(info), options);
    }

    return std::make_unique<UnsupportedBoardTelemetryProvider>(trace, std::move(info));
}

}  // namespace

BoardVendorInfo ExtractBoardVendorInfo() {
    return ExtractBoardVendorInfo(nullptr);
}

BoardVendorInfo ExtractBoardVendorInfo(const HardwareDependencyInjection* injection) {
    std::unique_ptr<BoardDiscoveryHdi> discovery = ResolveHdiFactory(injection).CreateBoardDiscoveryHdi();
    return discovery != nullptr ? discovery->ReadBoardVendorInfo() : BoardVendorInfo{};
}

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(
    Trace& trace, BoardVendorInfo info, const BoardVendorTelemetryProviderOptions& options) {
    return CreateBoardVendorTelemetryProvider(trace, std::move(info), options, nullptr);
}

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(Trace& trace,
    BoardVendorInfo info,
    const BoardVendorTelemetryProviderOptions& options,
    const HardwareDependencyInjection* injection) {
    const BoardVendor vendor = SelectBoardVendor(info);
    trace.WriteFmt(TracePrefix::BoardVendor,
        RES_STR("create vendor=%s manufacturer=\"%s\" product=\"%s\""),
        BoardVendorName(vendor),
        info.manufacturer.c_str(),
        info.product.c_str());
    return CreateBoardProviderForVendor(trace, vendor, std::move(info), options, injection);
}

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(
    Trace& trace, const BoardVendorTelemetryProviderOptions& options) {
    return CreateBoardVendorTelemetryProvider(trace, options, nullptr);
}

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(
    Trace& trace, const BoardVendorTelemetryProviderOptions& options, const HardwareDependencyInjection* injection) {
    return CreateBoardVendorTelemetryProvider(trace, ExtractBoardVendorInfo(injection), options, injection);
}
