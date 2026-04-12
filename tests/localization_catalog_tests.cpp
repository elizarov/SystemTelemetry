#include <gtest/gtest.h>

#include "localization_catalog.h"

TEST(LocalizationCatalog, ParsesKeyValueLines) {
    const LocalizationCatalogMap catalog = ParseLocalizationCatalog(
        "# comment\n"
        "config.metric_list.label_width = Label width text\n"
        "\n"
        "config.gauge.segment_count= Segment count text\n");

    ASSERT_EQ(catalog.size(), 2u);
    EXPECT_EQ(catalog.at("config.metric_list.label_width"), "Label width text");
    EXPECT_EQ(catalog.at("config.gauge.segment_count"), "Segment count text");
}
