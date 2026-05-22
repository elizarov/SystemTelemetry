#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "telemetry/board/board_vendor_selection.h"
#include "telemetry/gpu/gpu_vendor_selection.h"

namespace {

struct KnownTestGpu {
    GpuVendorInfo gpu;
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
    {GpuVendorInfo{0x10deu, "NVIDIA GeForce GTX 1650 Ti with Max-Q Design"}, GpuVendor::Nvidia},
};

const KnownTestBoard kKnownTestBoards[] = {
    {BoardVendorInfo{"ASUSTeK COMPUTER INC.", "GU603VI"}, BoardVendor::Asus},
    {BoardVendorInfo{"Gigabyte Technology Co., Ltd.", "X570 I AORUS PRO WIFI"}, BoardVendor::Gigabyte},
    {BoardVendorInfo{"Micro-Star International Co., Ltd.", "MPG Z690 CARBON WIFI (MS-7D30)"}, BoardVendor::Msi},
};

GpuAdapterInfo MakeRadeonRx6800Adapter(unsigned int bus, unsigned int device, unsigned int function) {
    GpuAdapterInfo info;
    info.vendorId = 0x1002u;
    info.adapterName = "AMD Radeon RX 6800";
    info.dedicatedVideoMemoryBytes = 17131868160ull;
    info.deviceId = 0x73bfu;
    info.subSysId = 0x23271458u;
    info.revision = 0xc3u;
    info.hasPciAddress = true;
    info.pciBus = bus;
    info.pciDevice = device;
    info.pciFunction = function;
    return info;
}

}  // namespace

TEST(HardwareVendorSelection, SelectsExpectedGpuProvidersForKnownTestMachines) {
    for (const KnownTestGpu& gpu : kKnownTestGpus) {
        EXPECT_EQ(SelectGpuVendor(gpu.gpu), gpu.expectedGpuVendor) << gpu.gpu.adapterName;
    }
}

TEST(HardwareVendorSelection, SelectsExpectedBoardProvidersForKnownTestMachines) {
    for (const KnownTestBoard& board : kKnownTestBoards) {
        EXPECT_EQ(SelectBoardVendor(board.board), board.expectedBoardVendor);
    }
}

TEST(HardwareVendorSelection, TreatsInvalidGpuPciSentinelAsUnusable) {
    const GpuAdapterInfo adapter = MakeRadeonRx6800Adapter(0x00u, 0xffffu, 0xffffu);

    EXPECT_FALSE(HasUsableGpuPciAddress(adapter));
}

TEST(HardwareVendorSelection, MatchesDuplicateGpuAdapterViewWithInvalidPciAddress) {
    const GpuAdapterInfo physicalAdapter = MakeRadeonRx6800Adapter(0x0bu, 0x00u, 0x00u);
    GpuAdapterInfo duplicateView = MakeRadeonRx6800Adapter(0x00u, 0xffffu, 0xffffu);
    duplicateView.adapterIndex = 1;

    EXPECT_TRUE(HasUsableGpuPciAddress(physicalAdapter));
    EXPECT_FALSE(HasUsableGpuPciAddress(duplicateView));
    EXPECT_TRUE(GpuAdapterViewsReferToSameHardware(physicalAdapter, duplicateView));
}

TEST(HardwareVendorSelection, KeepsDistinctGpuAdaptersWithDifferentUsablePciAddresses) {
    const GpuAdapterInfo firstAdapter = MakeRadeonRx6800Adapter(0x0bu, 0x00u, 0x00u);
    GpuAdapterInfo secondAdapter = MakeRadeonRx6800Adapter(0x0cu, 0x00u, 0x00u);
    secondAdapter.adapterIndex = 1;

    EXPECT_TRUE(HasUsableGpuPciAddress(firstAdapter));
    EXPECT_TRUE(HasUsableGpuPciAddress(secondAdapter));
    EXPECT_FALSE(GpuAdapterViewsReferToSameHardware(firstAdapter, secondAdapter));
}

TEST(HardwareVendorSelection, LeavesUniqueGpuAdapterSelectionNamesUnnumbered) {
    GpuAdapterInfo radeon = MakeRadeonRx6800Adapter(0x0bu, 0x00u, 0x00u);
    GpuAdapterInfo nvidia;
    nvidia.vendorId = 0x10deu;
    nvidia.adapterName = "NVIDIA GeForce RTX 4070";
    nvidia.hasPciAddress = true;
    nvidia.pciBus = 0x0cu;

    const std::vector<std::string> names = BuildGpuAdapterSelectionNames({radeon, nvidia});

    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "AMD Radeon RX 6800");
    EXPECT_EQ(names[1], "NVIDIA GeForce RTX 4070");
}

TEST(HardwareVendorSelection, NumbersDuplicateGpuAdapterSelectionNamesByStablePciOrder) {
    GpuAdapterInfo laterBus = MakeRadeonRx6800Adapter(0x0cu, 0x00u, 0x00u);
    GpuAdapterInfo earlierBus = MakeRadeonRx6800Adapter(0x0bu, 0x00u, 0x00u);
    laterBus.adapterIndex = 0;
    earlierBus.adapterIndex = 1;

    const std::vector<std::string> names = BuildGpuAdapterSelectionNames({laterBus, earlierBus});

    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "AMD Radeon RX 6800 #2");
    EXPECT_EQ(names[1], "AMD Radeon RX 6800 #1");
}

TEST(HardwareVendorSelection, MatchesNumberedGpuAdapterSelectionNameBeforePhysicalName) {
    GpuAdapterInfo firstAdapter = MakeRadeonRx6800Adapter(0x0bu, 0x00u, 0x00u);
    GpuAdapterInfo secondAdapter = MakeRadeonRx6800Adapter(0x0cu, 0x00u, 0x00u);
    const std::vector<std::string> names = BuildGpuAdapterSelectionNames({firstAdapter, secondAdapter});
    firstAdapter.selectionName = names[0];
    secondAdapter.selectionName = names[1];

    EXPECT_LT(GpuAdapterSelectionMatchRank(firstAdapter, "AMD Radeon RX 6800 #2"),
        GpuAdapterSelectionMatchRank(secondAdapter, "AMD Radeon RX 6800 #2"));
    EXPECT_EQ(GpuAdapterSelectionName(secondAdapter), "AMD Radeon RX 6800 #2");
    EXPECT_EQ(secondAdapter.adapterName, "AMD Radeon RX 6800");
}
