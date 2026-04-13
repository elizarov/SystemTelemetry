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

template <typename Lens>
bool ApplyFieldEdit(AppConfig& config, double value) {
    using PolicyTag = typename Lens::traits_type::policy_tag;
    if constexpr (std::is_same_v<PolicyTag, configschema::PositiveIntLayoutEditPolicyTag>) {
        Lens::Get(config) = ClampPositiveInt(value);
        return true;
    } else if constexpr (std::is_same_v<PolicyTag, configschema::FontSizeLayoutEditPolicyTag>) {
        Lens::Get(config).size = ClampPositiveInt(value);
        return true;
    } else if constexpr (std::is_same_v<PolicyTag, configschema::GaugeSweepDegreesLayoutEditPolicyTag>) {
        Lens::Get(config) = ClampGaugeSweepDegrees(value);
        return true;
    } else if constexpr (std::is_same_v<PolicyTag, configschema::GaugeSegmentGapDegreesLayoutEditPolicyTag>) {
        Lens::Get(config) = ClampGaugeSegmentGapDegrees(config, value);
        return true;
    } else if constexpr (std::is_same_v<PolicyTag, configschema::DriveUsageActivitySegmentGapLayoutEditPolicyTag>) {
        Lens::Get(config) = ClampDriveUsageActivitySegmentGap(config, value);
        return true;
    } else {
        return false;
    }
}

template <typename Lens>
std::optional<const UiFontConfig*> FindFontFieldValue(const AppConfig& config) {
    if constexpr (std::is_same_v<typename Lens::value_type, UiFontConfig>) {
        return &Lens::Get(config);
    } else {
        return std::nullopt;
    }
}

template <typename Lens>
const LayoutEditConfigFieldMetadata& GetFieldMetadata() {
    static const LayoutEditConfigFieldMetadata metadata{
        Lens::section_name,
        Lens::parameter_name,
        Lens::traits_type::value_format,
        std::is_same_v<typename Lens::value_type, UiFontConfig>,
        &ApplyFieldEdit<Lens>,
        &FindFontFieldValue<Lens>,
    };
    return metadata;
}

const LayoutEditParameterInfo kParameterInfo[] = {
    {Parameter::FontTitle, &GetFieldMetadata<UiFontSetConfig::titleLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontBig, &GetFieldMetadata<UiFontSetConfig::bigLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontValue, &GetFieldMetadata<UiFontSetConfig::valueLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontLabel, &GetFieldMetadata<UiFontSetConfig::labelLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontText, &GetFieldMetadata<UiFontSetConfig::textLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontSmall, &GetFieldMetadata<UiFontSetConfig::smallTextLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontFooter, &GetFieldMetadata<UiFontSetConfig::footerLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontClockTime, &GetFieldMetadata<UiFontSetConfig::clockTimeLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontClockDate, &GetFieldMetadata<UiFontSetConfig::clockDateLens>(), false, true, LayoutEditWidgetDragMode::Linear},

    {Parameter::MetricListBarHeight, &GetFieldMetadata<MetricListWidgetConfig::barHeightLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::MetricListLabelWidth, &GetFieldMetadata<MetricListWidgetConfig::labelWidthLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::MetricListVerticalGap, &GetFieldMetadata<MetricListWidgetConfig::verticalGapLens>(), true, false, LayoutEditWidgetDragMode::Linear},

    {Parameter::DriveUsageActivitySegments, &GetFieldMetadata<DriveUsageListWidgetConfig::activitySegmentsLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageBarHeight, &GetFieldMetadata<DriveUsageListWidgetConfig::barHeightLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageLabelGap, &GetFieldMetadata<DriveUsageListWidgetConfig::labelGapLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageBarGap, &GetFieldMetadata<DriveUsageListWidgetConfig::barGapLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageRwGap, &GetFieldMetadata<DriveUsageListWidgetConfig::rwGapLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsagePercentGap, &GetFieldMetadata<DriveUsageListWidgetConfig::percentGapLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageActivityWidth, &GetFieldMetadata<DriveUsageListWidgetConfig::activityWidthLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageFreeWidth, &GetFieldMetadata<DriveUsageListWidgetConfig::freeWidthLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageActivitySegmentGap, &GetFieldMetadata<DriveUsageListWidgetConfig::activitySegmentGapLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageHeaderGap, &GetFieldMetadata<DriveUsageListWidgetConfig::headerGapLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageRowGap, &GetFieldMetadata<DriveUsageListWidgetConfig::rowGapLens>(), true, false, LayoutEditWidgetDragMode::Linear},

    {Parameter::ThroughputGuideStrokeWidth, &GetFieldMetadata<ThroughputWidgetConfig::guideStrokeWidthLens>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputPlotStrokeWidth, &GetFieldMetadata<ThroughputWidgetConfig::plotStrokeWidthLens>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputLeaderDiameter, &GetFieldMetadata<ThroughputWidgetConfig::leaderDiameterLens>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputAxisPadding, &GetFieldMetadata<ThroughputWidgetConfig::axisPaddingLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputHeaderGap, &GetFieldMetadata<ThroughputWidgetConfig::headerGapLens>(), true, false, LayoutEditWidgetDragMode::Linear},

    {Parameter::GaugeSegmentCount, &GetFieldMetadata<GaugeWidgetConfig::segmentCountLens>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeValueBottom, &GetFieldMetadata<GaugeWidgetConfig::valueBottomLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeLabelBottom, &GetFieldMetadata<GaugeWidgetConfig::labelBottomLens>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeSweepDegrees, &GetFieldMetadata<GaugeWidgetConfig::sweepDegreesLens>(), true, false, LayoutEditWidgetDragMode::GaugeSweepDegrees},
    {Parameter::GaugeSegmentGapDegrees, &GetFieldMetadata<GaugeWidgetConfig::segmentGapDegreesLens>(), true, false, LayoutEditWidgetDragMode::GaugeSegmentGapDegrees},
    {Parameter::GaugeOuterPadding, &GetFieldMetadata<GaugeWidgetConfig::outerPaddingLens>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeRingThickness, &GetFieldMetadata<GaugeWidgetConfig::ringThicknessLens>(), true, true, LayoutEditWidgetDragMode::Linear},
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
