#include <gtest/gtest.h>

#include "diagnostics_options.h"

TEST(DiagnosticsOptions, ParsesBackgroundOnlyModeWithoutSeed) {
    std::string error;
    const auto spec = ParseDiagnosticsBackgroundOnlySpec("floral-voronoi", &error);
    ASSERT_TRUE(spec.has_value()) << error;
    EXPECT_EQ(spec->mode, DiagnosticsBackgroundOnlyMode::FloralVoronoi);
    EXPECT_FALSE(spec->seed.has_value());
}

TEST(DiagnosticsOptions, ParsesBackgroundOnlyModeWithSeed) {
    std::string error;
    const auto spec = ParseDiagnosticsBackgroundOnlySpec(" floral-voronoi , 42 ", &error);
    ASSERT_TRUE(spec.has_value()) << error;
    ASSERT_TRUE(spec->seed.has_value());
    EXPECT_EQ(spec->mode, DiagnosticsBackgroundOnlyMode::FloralVoronoi);
    EXPECT_EQ(*spec->seed, 42u);
}

TEST(DiagnosticsOptions, RejectsUnknownBackgroundOnlyMode) {
    std::string error;
    const auto spec = ParseDiagnosticsBackgroundOnlySpec("voronoi", &error);
    EXPECT_FALSE(spec.has_value());
    EXPECT_FALSE(error.empty());
}

TEST(DiagnosticsOptions, RejectsMalformedBackgroundOnlySeed) {
    std::string error;
    const auto spec = ParseDiagnosticsBackgroundOnlySpec("floral-voronoi,abc", &error);
    EXPECT_FALSE(spec.has_value());
    EXPECT_FALSE(error.empty());
}
