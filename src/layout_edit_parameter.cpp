#include "layout_edit_parameter.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace {

using Parameter = LayoutEditParameter;

#define APP_LAYOUT_FIELD_LENS(owner_type, field_member, section_member)                                                \
    configschema::RootFieldLens<                                                                                       \
        AppConfig, owner_type::field_member##Field, AppConfig::layoutBinding, LayoutConfig::section_member##Binding>

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
    {Parameter::FontTitle, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(UiFontSetConfig, title, fonts)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontBig, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(UiFontSetConfig, big, fonts)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontValue, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(UiFontSetConfig, value, fonts)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontLabel, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(UiFontSetConfig, label, fonts)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontText, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(UiFontSetConfig, text, fonts)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontSmall, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(UiFontSetConfig, smallText, fonts)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontFooter, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(UiFontSetConfig, footer, fonts)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontClockTime, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(UiFontSetConfig, clockTime, fonts)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::FontClockDate, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(UiFontSetConfig, clockDate, fonts)>(), false, true, LayoutEditWidgetDragMode::Linear},

    {Parameter::MetricListBarHeight, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(MetricListWidgetConfig, barHeight, metricList)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::MetricListLabelWidth, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(MetricListWidgetConfig, labelWidth, metricList)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::MetricListVerticalGap, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(MetricListWidgetConfig, verticalGap, metricList)>(), true, false, LayoutEditWidgetDragMode::Linear},

    {Parameter::DriveUsageActivitySegments, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(DriveUsageListWidgetConfig, activitySegments, driveUsageList)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageBarHeight, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(DriveUsageListWidgetConfig, barHeight, driveUsageList)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageLabelGap, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(DriveUsageListWidgetConfig, labelGap, driveUsageList)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageBarGap, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(DriveUsageListWidgetConfig, barGap, driveUsageList)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageRwGap, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(DriveUsageListWidgetConfig, rwGap, driveUsageList)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsagePercentGap, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(DriveUsageListWidgetConfig, percentGap, driveUsageList)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageActivityWidth, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(DriveUsageListWidgetConfig, activityWidth, driveUsageList)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageFreeWidth, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(DriveUsageListWidgetConfig, freeWidth, driveUsageList)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageActivitySegmentGap, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(DriveUsageListWidgetConfig, activitySegmentGap, driveUsageList)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageHeaderGap, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(DriveUsageListWidgetConfig, headerGap, driveUsageList)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::DriveUsageRowGap, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(DriveUsageListWidgetConfig, rowGap, driveUsageList)>(), true, false, LayoutEditWidgetDragMode::Linear},

    {Parameter::ThroughputGuideStrokeWidth, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(ThroughputWidgetConfig, guideStrokeWidth, throughput)>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputPlotStrokeWidth, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(ThroughputWidgetConfig, plotStrokeWidth, throughput)>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputLeaderDiameter, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(ThroughputWidgetConfig, leaderDiameter, throughput)>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputAxisPadding, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(ThroughputWidgetConfig, axisPadding, throughput)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::ThroughputHeaderGap, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(ThroughputWidgetConfig, headerGap, throughput)>(), true, false, LayoutEditWidgetDragMode::Linear},

    {Parameter::GaugeSegmentCount, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(GaugeWidgetConfig, segmentCount, gauge)>(), false, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeValueBottom, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(GaugeWidgetConfig, valueBottom, gauge)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeLabelBottom, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(GaugeWidgetConfig, labelBottom, gauge)>(), true, false, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeSweepDegrees, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(GaugeWidgetConfig, sweepDegrees, gauge)>(), true, false, LayoutEditWidgetDragMode::GaugeSweepDegrees},
    {Parameter::GaugeSegmentGapDegrees, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(GaugeWidgetConfig, segmentGapDegrees, gauge)>(), true, false, LayoutEditWidgetDragMode::GaugeSegmentGapDegrees},
    {Parameter::GaugeOuterPadding, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(GaugeWidgetConfig, outerPadding, gauge)>(), true, true, LayoutEditWidgetDragMode::Linear},
    {Parameter::GaugeRingThickness, &GetFieldMetadata<APP_LAYOUT_FIELD_LENS(GaugeWidgetConfig, ringThickness, gauge)>(), true, true, LayoutEditWidgetDragMode::Linear},
};

constexpr size_t kParameterInfoCount = sizeof(kParameterInfo) / sizeof(kParameterInfo[0]);
static_assert(kParameterInfoCount == static_cast<size_t>(Parameter::Count));

}  // namespace

#undef APP_LAYOUT_FIELD_LENS

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
