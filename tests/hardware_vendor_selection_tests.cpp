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
    {GpuVendorInfo{0x8086u, "Intel(R) UHD Graphics"}, GpuVendor::Intel},
    {GpuVendorInfo{0x10deu, "NVIDIA GeForce RTX 3080"}, GpuVendor::Nvidia},
    {GpuVendorInfo{0x10deu, "NVIDIA GeForce RTX 4070 Laptop GPU"}, GpuVendor::Nvidia},
};

const KnownTestBoard kKnownTestBoards[] = {
    {BoardVendorInfo{"ASUSTeK COMPUTER INC.", "GU603VI"}, BoardVendor::Asus},
    {BoardVendorInfo{"Gigabyte Technology Co., Ltd.", "X570 I AORUS PRO WIFI"}, BoardVendor::Gigabyte},
    {BoardVendorInfo{"Micro-Star International Co., Ltd.", "MPG Z690 CARBON WIFI (MS-7D30)"}, BoardVendor::Msi},
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
