#include <gtest/gtest.h>

#include "config_parser.h"
#include "widget.h"

#include <fstream>

namespace {

std::filesystem::path WriteTestConfig(const std::string& text) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "systemtelemetry_config_parser_test.ini";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    return path;
}

}  // namespace

std::optional<DashboardWidgetClass> FindDashboardWidgetClass(std::string_view) {
    return std::nullopt;
}

TEST(ConfigParser, ClampsParsedEditableValuesThroughSchemaPolicies) {
    const std::filesystem::path path = WriteTestConfig(
        "[fonts]\n"
        "label = Segoe UI,-5,600\n"
        "\n"
        "[drive_usage_list]\n"
        "activity_segment_gap = -9\n"
        "\n"
        "[gauge]\n"
        "sweep_degrees = 500\n"
        "segment_gap_degrees = -12\n");

    const AppConfig config = LoadConfig(path, true);

    EXPECT_EQ(config.layout.fonts.label.size, 1);
    EXPECT_EQ(config.layout.driveUsageList.activitySegmentGap, 0);
    EXPECT_EQ(config.layout.gauge.sweepDegrees, 360.0);
    EXPECT_EQ(config.layout.gauge.segmentGapDegrees, 0.0);

    std::filesystem::remove(path);
}
