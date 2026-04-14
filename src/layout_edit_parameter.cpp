#include "layout_edit_parameter.h"

#include <cmath>
#include <type_traits>

namespace {

using Parameter = LayoutEditParameter;

template <typename Meta> bool ApplyFieldEdit(AppConfig& config, double value) {
    using PolicyTag = typename Meta::traits_type::policy_tag;
    if constexpr (std::is_same_v<PolicyTag, configschema::FontSizePolicy>) {
        UiFontConfig font = Meta::RawGet(config);
        font.size = static_cast<int>(std::lround(value));
        Meta::Get(config) = std::move(font);
        return true;
    } else {
        if constexpr (std::is_same_v<typename Meta::value_type, int>) {
            Meta::Get(config) = static_cast<int>(std::lround(value));
        } else {
            Meta::Get(config) = static_cast<typename Meta::value_type>(value);
        }
        return true;
    }
}

template <typename Meta> std::optional<const UiFontConfig*> FindFontFieldValue(const AppConfig& config) {
    if constexpr (std::is_same_v<typename Meta::value_type, UiFontConfig>) {
        return &Meta::RawGet(config);
    } else {
        return std::nullopt;
    }
}

template <typename Meta> bool ApplyFontFieldEdit(AppConfig& config, const UiFontConfig& value) {
    if constexpr (std::is_same_v<typename Meta::value_type, UiFontConfig>) {
        Meta::Set(config, value);
        return true;
    } else {
        (void)config;
        (void)value;
        return false;
    }
}

std::string HumanizeSnakeCase(std::string_view value) {
    std::string text;
    text.reserve(value.size());
    for (const char ch : value) {
        if (ch == '_') {
            if (!text.empty()) {
                text.push_back(' ');
            }
            continue;
        }
        text.push_back(ch);
    }
    return text;
}

template <typename Meta> const LayoutEditConfigFieldMetadata& GetFieldMetadata() {
    static const LayoutEditConfigFieldMetadata metadata{
        Meta::section_name,
        Meta::parameter_name,
        Meta::traits_type::value_format,
        std::is_same_v<typename Meta::value_type, UiFontConfig>,
        &ApplyFieldEdit<Meta>,
        &ApplyFontFieldEdit<Meta>,
        &FindFontFieldValue<Meta>,
    };
    return metadata;
}

const LayoutEditParameterInfo kParameterInfo[] = {
    {Parameter::FontTitle, &GetFieldMetadata<UiFontSetConfig::titleMeta>(), false, true},
    {Parameter::FontBig, &GetFieldMetadata<UiFontSetConfig::bigMeta>(), false, true},
    {Parameter::FontValue, &GetFieldMetadata<UiFontSetConfig::valueMeta>(), false, true},
    {Parameter::FontLabel, &GetFieldMetadata<UiFontSetConfig::labelMeta>(), false, true},
    {Parameter::FontText, &GetFieldMetadata<UiFontSetConfig::textMeta>(), false, true},
    {Parameter::FontSmall, &GetFieldMetadata<UiFontSetConfig::smallTextMeta>(), false, true},
    {Parameter::FontFooter, &GetFieldMetadata<UiFontSetConfig::footerMeta>(), false, true},
    {Parameter::FontClockTime, &GetFieldMetadata<UiFontSetConfig::clockTimeMeta>(), false, true},
    {Parameter::FontClockDate, &GetFieldMetadata<UiFontSetConfig::clockDateMeta>(), false, true},

    {Parameter::CardRadius, &GetFieldMetadata<CardStyleConfig::cardRadiusMeta>(), false, true},
    {Parameter::CardBorder, &GetFieldMetadata<CardStyleConfig::cardBorderWidthMeta>(), false, true},
    {Parameter::CardHeaderIconSize, &GetFieldMetadata<CardStyleConfig::headerIconSizeMeta>(), false, true},

    {Parameter::CardPadding, &GetFieldMetadata<CardStyleConfig::cardPaddingMeta>(), true, false},
    {Parameter::CardHeaderIconGap, &GetFieldMetadata<CardStyleConfig::headerIconGapMeta>(), true, false},
    {Parameter::CardHeaderContentGap, &GetFieldMetadata<CardStyleConfig::headerContentGapMeta>(), true, false},
    {Parameter::CardRowGap, &GetFieldMetadata<CardStyleConfig::rowGapMeta>(), true, false},
    {Parameter::CardColumnGap, &GetFieldMetadata<CardStyleConfig::columnGapMeta>(), true, false},

    {Parameter::DashboardRowGap, &GetFieldMetadata<DashboardSectionConfig::rowGapMeta>(), true, false},
    {Parameter::DashboardColumnGap, &GetFieldMetadata<DashboardSectionConfig::columnGapMeta>(), true, false},

    {Parameter::TextBottomGap, &GetFieldMetadata<TextWidgetConfig::bottomGapMeta>(), true, false},
    {Parameter::NetworkFooterBottomGap, &GetFieldMetadata<NetworkFooterWidgetConfig::bottomGapMeta>(), true, false},

    {Parameter::MetricListBarHeight, &GetFieldMetadata<MetricListWidgetConfig::barHeightMeta>(), false, true},
    {Parameter::MetricListLabelWidth, &GetFieldMetadata<MetricListWidgetConfig::labelWidthMeta>(), true, false},
    {Parameter::MetricListRowGap, &GetFieldMetadata<MetricListWidgetConfig::rowGapMeta>(), true, false},

    {Parameter::DriveUsageActivitySegments,
        &GetFieldMetadata<DriveUsageListWidgetConfig::activitySegmentsMeta>(),
        false,
        true},
    {Parameter::DriveUsageBarHeight, &GetFieldMetadata<DriveUsageListWidgetConfig::barHeightMeta>(), false, true},
    {Parameter::DriveUsageLabelGap, &GetFieldMetadata<DriveUsageListWidgetConfig::labelGapMeta>(), true, false},
    {Parameter::DriveUsageBarGap, &GetFieldMetadata<DriveUsageListWidgetConfig::barGapMeta>(), true, false},
    {Parameter::DriveUsageRwGap, &GetFieldMetadata<DriveUsageListWidgetConfig::rwGapMeta>(), true, false},
    {Parameter::DriveUsagePercentGap, &GetFieldMetadata<DriveUsageListWidgetConfig::percentGapMeta>(), true, false},
    {Parameter::DriveUsageActivityWidth,
        &GetFieldMetadata<DriveUsageListWidgetConfig::activityWidthMeta>(),
        true,
        false},
    {Parameter::DriveUsageFreeWidth, &GetFieldMetadata<DriveUsageListWidgetConfig::freeWidthMeta>(), true, false},
    {Parameter::DriveUsageActivitySegmentGap,
        &GetFieldMetadata<DriveUsageListWidgetConfig::activitySegmentGapMeta>(),
        true,
        false},
    {Parameter::DriveUsageHeaderGap, &GetFieldMetadata<DriveUsageListWidgetConfig::headerGapMeta>(), true, false},
    {Parameter::DriveUsageRowGap, &GetFieldMetadata<DriveUsageListWidgetConfig::rowGapMeta>(), true, false},

    {Parameter::ThroughputGuideStrokeWidth,
        &GetFieldMetadata<ThroughputWidgetConfig::guideStrokeWidthMeta>(),
        true,
        true},
    {Parameter::ThroughputPlotStrokeWidth,
        &GetFieldMetadata<ThroughputWidgetConfig::plotStrokeWidthMeta>(),
        true,
        true},
    {Parameter::ThroughputLeaderDiameter, &GetFieldMetadata<ThroughputWidgetConfig::leaderDiameterMeta>(), true, true},
    {Parameter::ThroughputAxisPadding, &GetFieldMetadata<ThroughputWidgetConfig::axisPaddingMeta>(), true, false},
    {Parameter::ThroughputHeaderGap, &GetFieldMetadata<ThroughputWidgetConfig::headerGapMeta>(), true, false},

    {Parameter::GaugeSegmentCount, &GetFieldMetadata<GaugeWidgetConfig::segmentCountMeta>(), false, true},
    {Parameter::GaugeValueBottom, &GetFieldMetadata<GaugeWidgetConfig::valueBottomMeta>(), true, false},
    {Parameter::GaugeLabelBottom, &GetFieldMetadata<GaugeWidgetConfig::labelBottomMeta>(), true, false},
    {Parameter::GaugeSweepDegrees, &GetFieldMetadata<GaugeWidgetConfig::sweepDegreesMeta>(), true, false},
    {Parameter::GaugeSegmentGapDegrees, &GetFieldMetadata<GaugeWidgetConfig::segmentGapDegreesMeta>(), true, false},
    {Parameter::GaugeOuterPadding, &GetFieldMetadata<GaugeWidgetConfig::outerPaddingMeta>(), true, true},
    {Parameter::GaugeRingThickness, &GetFieldMetadata<GaugeWidgetConfig::ringThicknessMeta>(), true, true},
};

constexpr size_t kParameterInfoCount = sizeof(kParameterInfo) / sizeof(kParameterInfo[0]);
static_assert(kParameterInfoCount == static_cast<size_t>(Parameter::Count));

}  // namespace

const LayoutEditParameterInfo& GetLayoutEditParameterInfo(LayoutEditParameter parameter) {
    return kParameterInfo[static_cast<size_t>(parameter)];
}

const LayoutEditConfigFieldMetadata& GetLayoutEditConfigFieldMetadata(LayoutEditParameter parameter) {
    return *GetLayoutEditParameterInfo(parameter).field;
}

int GetLayoutEditParameterHitPriority(LayoutEditParameter parameter) {
    return static_cast<int>(parameter);
}

bool IsFontLayoutEditParameter(LayoutEditParameter parameter) {
    return GetLayoutEditConfigFieldMetadata(parameter).isFont;
}

std::string GetLayoutEditParameterDisplayName(LayoutEditParameter parameter) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    std::string label = HumanizeSnakeCase(field.parameterName);
    if (field.isFont) {
        label += " font";
    }
    return label;
}

std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(LayoutEditParameter parameter) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    LayoutEditTooltipDescriptor descriptor;
    descriptor.sectionName = std::string(field.sectionName);
    descriptor.memberName = std::string(field.parameterName);
    descriptor.configKey = "config." + descriptor.sectionName + "." + descriptor.memberName;
    descriptor.valueFormat = field.valueFormat;
    return descriptor;
}

std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, LayoutEditParameter parameter) {
    return GetLayoutEditConfigFieldMetadata(parameter).fontValue(config);
}

bool ApplyLayoutEditParameterValue(AppConfig& config, LayoutEditParameter parameter, double value) {
    return GetLayoutEditConfigFieldMetadata(parameter).applyValue(config, value);
}

bool ApplyLayoutEditParameterFontValue(AppConfig& config, LayoutEditParameter parameter, const UiFontConfig& value) {
    const auto& field = GetLayoutEditConfigFieldMetadata(parameter);
    return field.applyFontValue != nullptr ? field.applyFontValue(config, value) : false;
}
