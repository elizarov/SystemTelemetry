#include <gtest/gtest.h>

#include "config/widget_class.h"

#include <array>
#include <set>
#include <string>

TEST(DashboardWidgetClass, EnumStringMappingsStayUniqueAndRoundTrip) {
    constexpr std::array<DashboardWidgetClass, 10> kKnownClasses{
        DashboardWidgetClass::Text,
        DashboardWidgetClass::Gauge,
        DashboardWidgetClass::MetricList,
        DashboardWidgetClass::Throughput,
        DashboardWidgetClass::NetworkFooter,
        DashboardWidgetClass::VerticalSpacer,
        DashboardWidgetClass::VerticalSpring,
        DashboardWidgetClass::DriveUsageList,
        DashboardWidgetClass::ClockTime,
        DashboardWidgetClass::ClockDate,
    };

    std::set<std::string> seenNames;
    for (const auto widgetClass : kKnownClasses) {
        const std::string_view name = EnumToString(widgetClass);
        EXPECT_FALSE(name.empty());

        EXPECT_TRUE(seenNames.insert(std::string(name)).second);

        const auto resolvedClass = EnumFromString<DashboardWidgetClass>(name);
        ASSERT_TRUE(resolvedClass.has_value());
        EXPECT_EQ(*resolvedClass, widgetClass);
    }

    EXPECT_EQ(seenNames.size(), kKnownClasses.size());
    EXPECT_EQ(EnumToString(DashboardWidgetClass::Unknown), "");

    const auto unknownClass = EnumFromString<DashboardWidgetClass>("");
    ASSERT_TRUE(unknownClass.has_value());
    EXPECT_EQ(*unknownClass, DashboardWidgetClass::Unknown);

    EXPECT_FALSE(EnumFromString<DashboardWidgetClass>("unknown_widget").has_value());
}
