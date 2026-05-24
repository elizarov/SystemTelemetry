#include "telemetry/impl/hdi_board_discovery.h"

#include <memory>

#include "telemetry/impl/system_info_support.h"

namespace {

constexpr char kBiosKey[] = "HARDWARE\\DESCRIPTION\\System\\BIOS";

class ProductionBoardDiscoveryHdi final : public BoardDiscoveryHdi {
public:
    BoardVendorInfo ReadBoardVendorInfo() override {
        return BoardVendorInfo{
            ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, "BaseBoardManufacturer").value_or(""),
            ReadRegistryString(HKEY_LOCAL_MACHINE, kBiosKey, "BaseBoardProduct").value_or(""),
        };
    }
};

}  // namespace

std::unique_ptr<BoardDiscoveryHdi> CreateProductionBoardDiscoveryHdi() {
    return std::make_unique<ProductionBoardDiscoveryHdi>();
}
