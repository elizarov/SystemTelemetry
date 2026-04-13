#include "layout_edit_parameter.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace {

using Parameter = LayoutEditParameter;

int ClampPositiveInt(double value) {
    return (std::max)(1, static_cast<int>(std::lround(value)));
}

int ClampNonNegativeInt(double value) {
    return (std::max)(0, static_cast<int>(std::lround(value)));
}

double ClampGaugeSweepDegrees(double value) {
    return std::clamp(value, 0.0, 360.0);
}

double ClampGaugeSegmentGapDegrees(const AppConfig& config, double value) {
    const double totalSweep = ClampGaugeSweepDegrees(config.layout.gauge.sweepDegrees);
    const int segmentCount = (std::max)(1, config.layout.gauge.segmentCount);
    const double maxSegmentGap = segmentCount <= 1 ? 0.0 : totalSweep / static_cast<double>(segmentCount - 1);
    return std::clamp(value, 0.0, maxSegmentGap);
}

int ClampDriveUsageActivitySegmentGap(const AppConfig& config, double value) {
    const int segmentCount = (std::max)(1, config.layout.driveUsageList.activitySegments);
    if (segmentCount <= 1) {
        return 0;
    }

    const int rowContentHeight = (std::max)(config.layout.fonts.label.size,
        (std::max)(config.layout.fonts.smallText.size, config.layout.driveUsageList.barHeight));
    const int maxGap = (std::max)(0, (rowContentHeight - segmentCount) / (segmentCount - 1));
    return std::clamp(ClampNonNegativeInt(value), 0, maxGap);
}

template <typename Meta>
bool ApplyFieldEdit(AppConfig& config, double value) {
    using PolicyTag = typename Meta::traits_type::policy_tag;
    if constexpr (std::is_same_v<PolicyTag, configschema::PositiveIntPolicy>) {
        Meta::Get(config) = ClampPositiveInt(value);
        return true;
    } else if constexpr (std::is_same_v<PolicyTag, configschema::NonNegativeIntPolicy>) {
        Meta::Get(config) = ClampNonNegativeInt(value);
        return true;
    } else if constexpr (std::is_same_v<PolicyTag, configschema::FontSizePolicy>) {
        Meta::Get(config).size = ClampPositiveInt(value);
        return true;
    } else if constexpr (std::is_same_v<PolicyTag, configschema::GaugeSweepDegreesPolicy>) {
        Meta::Get(config) = ClampGaugeSweepDegrees(value);
        return true;
    } else if constexpr (std::is_same_v<PolicyTag, configschema::GaugeSegmentGapDegreesPolicy>) {
        Meta::Get(config) = ClampGaugeSegmentGapDegrees(config, value);
        return true;
    } else if constexpr (std::is_same_v<PolicyTag, configschema::DriveUsageActivitySegmentGapPolicy>) {
        Meta::Get(config) = ClampDriveUsageActivitySegmentGap(config, value);
        return true;
    } else {
        return false;
    }
}

template <typename Meta>
std::optional<const UiFontConfig*> FindFontFieldValue(const AppConfig& config) {
    if constexpr (std::is_same_v<typename Meta::value_type, UiFontConfig>) {
        return &Meta::Get(config);
    } else {
        return std::nullopt;
    }
}

template <typename Meta>
const LayoutEditConfigFieldMetadata& GetFieldMetadata() {
    static const LayoutEditConfigFieldMetadata metadata{
        Meta::section_name,
        Meta::parameter_name,
        Meta::traits_type::value_format,
        std::is_same_v<typename Meta::value_type, UiFontConfig>,
        &ApplyFieldEdit<Meta>,
        &FindFontFieldValue<Meta>,
    };
    return metadata;
}

const LayoutEditParameterInfo kParameterInfo[] = {
    {Parameter::FontTitle, &GetFieldMetadata<UiFontSetConfig::titleMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontBig, &GetFieldMetadata<UiFontSetConfig::bigMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontValue, &GetFieldMetadata<UiFontSetConfig::valueMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontLabel, &GetFieldMetadata<UiFontSetConfig::labelMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontText, &GetFieldMetadata<UiFontSetConfig::textMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontSmall, &GetFieldMetadata<UiFontSetConfig::smallTextMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontFooter, &GetFieldMetadata<UiFontSetConfig::footerMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontClockTime, &GetFieldMetadata<UiFontSetConfig::clockTimeMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontClockDate, &GetFieldMetadata<UiFontSetConfig::clockDateMeta>(), false, true, LayoutEditWidgetDragMode::Linear},

    {Parameter::TextBottomPadding, &GetFieldMetadata<TextWidgetConfig::bottomPaddingMeta>(), true, false, LayoutEditWidgetDragMode::Linear},

    {Parameter::MetricListBarHeight, &GetFieldMetadata<MetricListWidgetConfig::barHeightMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::MetricListLabelWidth, &GetFieldMetadata<MetricListWidgetConfig::labelWidthMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::MetricListVerticalGap, &GetFieldMetadata<MetricListWidgetConfig::verticalGapMeta>(), true, false, LayoutEditWidgetDragMode::Linear},

    {Parameter::DriveUsageActivitySegments, &GetFieldMetadata<DriveUsageListWidgetConfig::activitySegmentsMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageBarHeight, &GetFieldMetadata<DriveUsageListWidgetConfig::barHeightMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageLabelGap, &GetFieldMetadata<DriveUsageListWidgetConfig::labelGapMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageBarGap, &GetFieldMetadata<DriveUsageListWidgetConfig::barGapMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageRwGap, &GetFieldMetadata<DriveUsageListWidgetConfig::rwGapMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsagePercentGap, &GetFieldMetadata<DriveUsageListWidgetConfig::percentGapMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageActivityWidth, &GetFieldMetadata<DriveUsageListWidgetConfig::activityWidthMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageFreeWidth, &GetFieldMetadata<DriveUsageListWidgetConfig::freeWidthMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageActivitySegmentGap, &GetFieldMetadata<DriveUsageListWidgetConfig::activitySegmentGapMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageHeaderGap, &GetFieldMetadata<DriveUsageListWidgetConfig::headerGapMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageRowGap, &GetFieldMetadata<DriveUsageListWidgetConfig::rowGapMeta>(), true, false, LayoutEditWidgetDragMode::Linear},

    {Parameter::ThroughputGuideStrokeWidth, &GetFieldMetadata<ThroughputWidgetConfig::guideStrokeWidthMeta>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputPlotStrokeWidth, &GetFieldMetadata<ThroughputWidgetConfig::plotStrokeWidthMeta>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputLeaderDiameter, &GetFieldMetadata<ThroughputWidgetConfig::leaderDiameterMeta>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputAxisPadding, &GetFieldMetadata<ThroughputWidgetConfig::axisPaddingMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputHeaderGap, &GetFieldMetadata<ThroughputWidgetConfig::headerGapMeta>(), true, false, LayoutEditWidgetDragMode::Linear},

    {Parameter::GaugeSegmentCount, &GetFieldMetadata<GaugeWidgetConfig::segmentCountMeta>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeValueBottom, &GetFieldMetadata<GaugeWidgetConfig::valueBottomMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeLabelBottom, &GetFieldMetadata<GaugeWidgetConfig::labelBottomMeta>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeSweepDegrees, &GetFieldMetadata<GaugeWidgetConfig::sweepDegreesMeta>(), true, false, LayoutEditWidgetDragMode::GaugeSweepDegrees},
    {Parameter::GaugeSegmentGapDegrees, &GetFieldMetadata<GaugeWidgetConfig::segmentGapDegreesMeta>(), true, false, LayoutEditWidgetDragMode::GaugeSegmentGapDegrees},
    {Parameter::GaugeOuterPadding, &GetFieldMetadata<GaugeWidgetConfig::outerPaddingMeta>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeRingThickness, &GetFieldMetadata<GaugeWidgetConfig::ringThicknessMeta>(), true, true, LayoutEditWidgetDragMode::Linear},
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
