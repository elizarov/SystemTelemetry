#include <gtest/gtest.h>

#include "dashboard_widget_class.h"

#include <array>
#include <set>
#include <string>

TEST(DashboardWidgetClass, PublicLookupMethodsStayUniqueAndRoundTrip) {
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
        const std::string_view name = DashboardWidgetClassName(widgetClass);
        EXPECT_FALSE(name.empty());

        EXPECT_TRUE(seenNames.insert(std::string(name)).second);

        const auto resolvedClass = FindDashboardWidgetClass(name);
        ASSERT_TRUE(resolvedClass.has_value());
        EXPECT_EQ(*resolvedClass, widgetClass);
    }

    EXPECT_EQ(seenNames.size(), kKnownClasses.size());
    EXPECT_EQ(DashboardWidgetClassName(DashboardWidgetClass::Unknown), "");
    EXPECT_FALSE(FindDashboardWidgetClass("").has_value());
    EXPECT_FALSE(FindDashboardWidgetClass("unknown_widget").has_value());
}
