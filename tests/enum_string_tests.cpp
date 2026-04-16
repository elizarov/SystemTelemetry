#include <gtest/gtest.h>

#include "enum_string.h"

#define SYSTEM_TELEMETRY_TEST_ENUM_ITEMS(X) \
    X(Zero, "zero") \
    X(One, "one") \
    X(Two, "two")

enum class TestEnumStringValue {
#define SYSTEM_TELEMETRY_ENUM_ITEM(name, text) name,
    SYSTEM_TELEMETRY_TEST_ENUM_ITEMS(SYSTEM_TELEMETRY_ENUM_ITEM)
#undef SYSTEM_TELEMETRY_ENUM_ITEM
};

template <> struct EnumStringTraits<TestEnumStringValue> {
    static constexpr auto values = std::to_array<TestEnumStringValue>({
#define SYSTEM_TELEMETRY_ENUM_ITEM(name, text) TestEnumStringValue::name,
        SYSTEM_TELEMETRY_TEST_ENUM_ITEMS(SYSTEM_TELEMETRY_ENUM_ITEM)
#undef SYSTEM_TELEMETRY_ENUM_ITEM
    });

    static constexpr auto names = std::to_array<std::string_view>({
#define SYSTEM_TELEMETRY_ENUM_ITEM(name, text) text,
        SYSTEM_TELEMETRY_TEST_ENUM_ITEMS(SYSTEM_TELEMETRY_ENUM_ITEM)
#undef SYSTEM_TELEMETRY_ENUM_ITEM
    });

    static_assert(enum_string_detail::ValidateCanonicalMappings(values, names));
};

#undef SYSTEM_TELEMETRY_TEST_ENUM_ITEMS

TEST(EnumString, CanonicalRoundTripsInBothDirections) {
    EXPECT_EQ(EnumToString(TestEnumStringValue::Zero), "zero");
    EXPECT_EQ(EnumToString(TestEnumStringValue::One), "one");
    EXPECT_EQ(EnumToString(TestEnumStringValue::Two), "two");

    const auto parsed = EnumFromString<TestEnumStringValue>("one");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, TestEnumStringValue::One);

    TestEnumStringValue value = TestEnumStringValue::Zero;
    EXPECT_TRUE(TryEnumFromString("two", value));
    EXPECT_EQ(value, TestEnumStringValue::Two);
}

TEST(EnumString, RejectsUnknownToken) {
    EXPECT_FALSE(EnumFromString<TestEnumStringValue>("missing").has_value());

    TestEnumStringValue value = TestEnumStringValue::Zero;
    EXPECT_FALSE(TryEnumFromString("missing", value));
    EXPECT_EQ(value, TestEnumStringValue::Zero);
}

TEST(EnumString, InvalidEnumValueReturnsEmptyString) {
    EXPECT_EQ(EnumToString(static_cast<TestEnumStringValue>(99)), "");
}
