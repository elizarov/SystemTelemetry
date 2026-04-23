#include <array>
#include <gtest/gtest.h>
#include <set>
#include <string>

#include "config/widget_class.h"

TEST(WidgetClass, EnumStringMappingsStayUniqueAndRoundTrip) {
    constexpr std::array<WidgetClass, 10> kKnownClasses{
        WidgetClass::Text,
        WidgetClass::Gauge,
        WidgetClass::MetricList,
        WidgetClass::Throughput,
        WidgetClass::NetworkFooter,
        WidgetClass::VerticalSpacer,
        WidgetClass::VerticalSpring,
        WidgetClass::DriveUsageList,
        WidgetClass::ClockTime,
        WidgetClass::ClockDate,
    };

    std::set<std::string> seenNames;
    for (const auto widgetClass : kKnownClasses) {
        const std::string_view name = EnumToString(widgetClass);
        EXPECT_FALSE(name.empty());

        EXPECT_TRUE(seenNames.insert(std::string(name)).second);

        const auto resolvedClass = EnumFromString<WidgetClass>(name);
        ASSERT_TRUE(resolvedClass.has_value());
        EXPECT_EQ(*resolvedClass, widgetClass);
    }

    EXPECT_EQ(seenNames.size(), kKnownClasses.size());
    EXPECT_EQ(EnumToString(WidgetClass::Unknown), "");

    const auto unknownClass = EnumFromString<WidgetClass>("");
    ASSERT_TRUE(unknownClass.has_value());
    EXPECT_EQ(*unknownClass, WidgetClass::Unknown);

    EXPECT_FALSE(EnumFromString<WidgetClass>("unknown_widget").has_value());
}
