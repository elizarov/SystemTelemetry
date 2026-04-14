#include <gtest/gtest.h>

#include "config_parser.h"
#include "widget.h"

#include <fstream>

namespace {

std::filesystem::path WriteTestConfig(const std::string& text) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "systemtelemetry_config_parser_test.ini";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    return path;
}

}  // namespace

std::optional<DashboardWidgetClass> FindDashboardWidgetClass(std::string_view) {
    return std::nullopt;
}

TEST(ConfigParser, ClampsParsedEditableValuesThroughSchemaPolicies) {
    const std::filesystem::path path = WriteTestConfig("[fonts]\n"
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

TEST(ConfigParser, ParsesRenamedCardStyleKeys) {
    const std::filesystem::path path = WriteTestConfig("[card_style]\n"
                                                       "header_icon_size = 21\n"
                                                       "header_icon_gap = 7\n"
                                                       "header_content_gap = 5\n"
                                                       "row_gap = 4\n");

    const AppConfig config = LoadConfig(path, true);

    EXPECT_EQ(config.layout.cardStyle.headerIconSize, 21);
    EXPECT_EQ(config.layout.cardStyle.headerIconGap, 7);
    EXPECT_EQ(config.layout.cardStyle.headerContentGap, 5);
    EXPECT_EQ(config.layout.cardStyle.rowGap, 4);

    std::filesystem::remove(path);
}

TEST(ConfigParser, ParsesRenamedDashboardColumnGapKey) {
    const std::filesystem::path path = WriteTestConfig("[dashboard]\n"
                                                       "outer_margin = 8\n"
                                                       "row_gap = 9\n"
                                                       "column_gap = 11\n");

    const AppConfig config = LoadConfig(path, true);

    EXPECT_EQ(config.layout.dashboard.outerMargin, 8);
    EXPECT_EQ(config.layout.dashboard.rowGap, 9);
    EXPECT_EQ(config.layout.dashboard.columnGap, 11);

    std::filesystem::remove(path);
}
