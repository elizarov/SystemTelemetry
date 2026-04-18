#include <gtest/gtest.h>

#include "system_info_support.h"

TEST(SystemInfoSupport, KeepsCachedBoardSensorNamesWhenLatestSampleOmitsThem) {
    std::vector<std::string> cachedNames = {"CPU", "System 1"};

    UpdateDiscoveredBoardSensorNames(cachedNames, {});

    const std::vector<std::string> expectedNames = {"CPU", "System 1"};
    EXPECT_EQ(cachedNames, expectedNames);
}

TEST(SystemInfoSupport, ReplacesCachedBoardSensorNamesWhenLatestSampleProvidesThem) {
    std::vector<std::string> cachedNames = {"CPU"};

    UpdateDiscoveredBoardSensorNames(cachedNames, {"VRM MOS", "Chipset"});

    const std::vector<std::string> expectedNames = {"VRM MOS", "Chipset"};
    EXPECT_EQ(cachedNames, expectedNames);
}
