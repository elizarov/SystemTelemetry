#include <cstddef>
#include <gtest/gtest.h>
#include <string_view>
#include <vector>

#include "config/config_runtime_fields.h"
#include "layout_model/layout_edit_parameter_metadata.h"

namespace {

std::string_view FieldKey(const RuntimeConfigFieldDescriptor& field) {
    return std::string_view(field.key, field.keyLength);
}

const RuntimeConfigFieldDescriptor* FindField(
    std::span<const RuntimeConfigFieldDescriptor> fields, std::string_view key) {
    for (const RuntimeConfigFieldDescriptor& field : fields) {
        if (FieldKey(field) == key) {
            return &field;
        }
    }
    return nullptr;
}

std::vector<std::string_view> FieldKeys(std::span<const RuntimeConfigFieldDescriptor> fields) {
    std::vector<std::string_view> keys;
    for (const RuntimeConfigFieldDescriptor& field : fields) {
        keys.push_back(FieldKey(field));
    }
    return keys;
}

std::ptrdiff_t AppConfigOffset(const AppConfig& config, const void* field) {
    return reinterpret_cast<const char*>(field) - reinterpret_cast<const char*>(&config);
}

std::span<const RuntimeConfigFieldDescriptor> FieldsForSection(std::string_view sectionName) {
    const RuntimeConfigSectionDescriptor* section = FindRuntimeConfigSection(sectionName);
    EXPECT_NE(section, nullptr);
    return section != nullptr ? RuntimeConfigFields(*section) : std::span<const RuntimeConfigFieldDescriptor>();
}

}  // namespace

TEST(ConfigMeta, GeneratesRepresentativeRuntimeFieldTables) {
    EXPECT_EQ(FieldKeys(FieldsForSection("fonts")),
        (std::vector<std::string_view>{
            "title", "big", "value", "label", "text", "small", "footer", "clock_time", "clock_date"}));
    const RuntimeConfigFieldDescriptor* smallFont = FindField(FieldsForSection("fonts"), "small");
    ASSERT_NE(smallFont, nullptr);
    EXPECT_EQ(smallFont->kind, RuntimeConfigFieldValueKind::FontSpec);
    EXPECT_EQ(smallFont->policy, RuntimeConfigFieldPolicy::FontSize);

    const RuntimeConfigFieldDescriptor* foregroundColor = FindField(FieldsForSection("colors"), "foreground_color");
    ASSERT_NE(foregroundColor, nullptr);
    EXPECT_EQ(foregroundColor->kind, RuntimeConfigFieldValueKind::HexColor);

    EXPECT_EQ(FieldKeys(FieldsForSection("theme.")),
        (std::vector<std::string_view>{"description", "background", "foreground", "accent", "guide"}));
    EXPECT_EQ(
        FieldKeys(FieldsForSection("layout.")), (std::vector<std::string_view>{"description", "window", "cards"}));
    EXPECT_EQ(FieldKeys(FieldsForSection("card.")), (std::vector<std::string_view>{"title", "icon", "layout"}));
}

TEST(ConfigMeta, GeneratesLayoutEditMetadataFromRootOffsets) {
    AppConfig config;

    const LayoutEditConfigFieldMetadata& fontSmall = GetLayoutEditConfigFieldMetadata(LayoutEditParameter::FontSmall);
    EXPECT_EQ(std::string_view(fontSmall.sectionName), "fonts");
    EXPECT_EQ(std::string_view(fontSmall.parameterName), "small");
    EXPECT_EQ(fontSmall.valueKind, RuntimeConfigFieldValueKind::FontSpec);
    EXPECT_EQ(fontSmall.policy, RuntimeConfigFieldPolicy::FontSize);
    EXPECT_EQ(
        static_cast<std::ptrdiff_t>(fontSmall.rootOffset), AppConfigOffset(config, &config.layout.fonts.smallText));

    const LayoutEditConfigFieldMetadata& cardBorder = GetLayoutEditConfigFieldMetadata(LayoutEditParameter::CardBorder);
    EXPECT_EQ(std::string_view(cardBorder.sectionName), "card_style");
    EXPECT_EQ(std::string_view(cardBorder.parameterName), "card_border");
    EXPECT_EQ(cardBorder.valueKind, RuntimeConfigFieldValueKind::Int);
    EXPECT_EQ(static_cast<std::ptrdiff_t>(cardBorder.rootOffset),
        AppConfigOffset(config, &config.layout.cardStyle.cardBorder));

    ASSERT_EQ(FindLayoutEditParameterByConfigField("layout", "cards"), std::nullopt);
    EXPECT_EQ(FindLayoutEditParameterByConfigField("fonts", "small"), LayoutEditParameter::FontSmall);
}
