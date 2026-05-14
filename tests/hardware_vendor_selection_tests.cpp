#include <gtest/gtest.h>

#include "telemetry/board/board_vendor_selection.h"
#include "telemetry/gpu/gpu_vendor_selection.h"

namespace {

struct KnownTestGpu {
    GpuVendorInfo primaryGpu;
    GpuVendor expectedGpuVendor;
};

struct KnownTestBoard {
    BoardVendorInfo board;
    BoardVendor expectedBoardVendor;
};

const KnownTestGpu kKnownTestGpus[] = {
    {GpuVendorInfo{0x1002u, "AMD Radeon RX 6800"}, GpuVendor::Amd},
};

const KnownTestBoard kKnownTestBoards[] = {
    {BoardVendorInfo{"Gigabyte Technology Co., Ltd.", "X570 I AORUS PRO WIFI"}, BoardVendor::Gigabyte},
};

}  // namespace

TEST(HardwareVendorSelection, SelectsExpectedGpuProvidersForKnownTestMachines) {
    for (const KnownTestGpu& gpu : kKnownTestGpus) {
        EXPECT_EQ(SelectGpuVendor(gpu.primaryGpu), gpu.expectedGpuVendor);
    }
}

TEST(HardwareVendorSelection, SelectsExpectedBoardProvidersForKnownTestMachines) {
    for (const KnownTestBoard& board : kKnownTestBoards) {
        EXPECT_EQ(SelectBoardVendor(board.board), board.expectedBoardVendor);
    }
}
