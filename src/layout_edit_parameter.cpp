#include "layout_edit_parameter.h"

#include <algorithm>
#include <cmath>
#include <utility>

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

std::optional<LayoutEditTooltipDescriptor> MakeDescriptor(std::string sectionName,
    std::string memberName,
    LayoutEditTooltipValueFormat valueFormat = LayoutEditTooltipValueFormat::Integer) {
    LayoutEditTooltipDescriptor descriptor;
    descriptor.configKey = "config." + sectionName + "." + memberName;
    descriptor.sectionName = std::move(sectionName);
    descriptor.memberName = std::move(memberName);
    descriptor.valueFormat = valueFormat;
    return descriptor;
}

std::optional<LayoutEditTooltipDescriptor> MakeFontDescriptor(std::string memberName) {
    return MakeDescriptor("fonts", std::move(memberName), LayoutEditTooltipValueFormat::FontSpec);
}

template <typename Section>
bool ApplyPositiveInt(Section LayoutConfig::* section, int Section::* member, AppConfig& config, double value) {
    (config.layout.*section).*member = ClampPositiveInt(value);
    return true;
}

bool ApplyFontSize(UiFontConfig UiFontSetConfig::* font, AppConfig& config, double value) {
    (config.layout.fonts.*font).size = ClampPositiveInt(value);
    return true;
}

bool ApplyMetricListLabelWidth(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::metricList, &MetricListWidgetConfig::labelWidth, config, value);
}

bool ApplyMetricListVerticalGap(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::metricList, &MetricListWidgetConfig::verticalGap, config, value);
}

bool ApplyMetricListBarHeight(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::metricList, &MetricListWidgetConfig::barHeight, config, value);
}

bool ApplyDriveUsageLabelGap(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::labelGap, config, value);
}

bool ApplyDriveUsageBarGap(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::barGap, config, value);
}

bool ApplyDriveUsageRwGap(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::rwGap, config, value);
}

bool ApplyDriveUsagePercentGap(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::percentGap, config, value);
}

bool ApplyDriveUsageActivityWidth(AppConfig& config, double value) {
    return ApplyPositiveInt(
        &LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::activityWidth, config, value);
}

bool ApplyDriveUsageFreeWidth(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::freeWidth, config, value);
}

bool ApplyDriveUsageActivitySegmentGap(AppConfig& config, double value) {
    config.layout.driveUsageList.activitySegmentGap = ClampDriveUsageActivitySegmentGap(config, value);
    return true;
}

bool ApplyDriveUsageHeaderGap(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::headerGap, config, value);
}

bool ApplyDriveUsageRowGap(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::rowGap, config, value);
}

bool ApplyDriveUsageBarHeight(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::barHeight, config, value);
}

bool ApplyDriveUsageActivitySegments(AppConfig& config, double value) {
    return ApplyPositiveInt(
        &LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::activitySegments, config, value);
}

bool ApplyThroughputAxisPadding(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::throughput, &ThroughputWidgetConfig::axisPadding, config, value);
}

bool ApplyThroughputHeaderGap(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::throughput, &ThroughputWidgetConfig::headerGap, config, value);
}

bool ApplyThroughputGuideStrokeWidth(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::throughput, &ThroughputWidgetConfig::guideStrokeWidth, config, value);
}

bool ApplyThroughputPlotStrokeWidth(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::throughput, &ThroughputWidgetConfig::plotStrokeWidth, config, value);
}

bool ApplyThroughputLeaderDiameter(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::throughput, &ThroughputWidgetConfig::leaderDiameter, config, value);
}

bool ApplyGaugeOuterPadding(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::gauge, &GaugeWidgetConfig::outerPadding, config, value);
}

bool ApplyGaugeRingThickness(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::gauge, &GaugeWidgetConfig::ringThickness, config, value);
}

bool ApplyGaugeValueBottom(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::gauge, &GaugeWidgetConfig::valueBottom, config, value);
}

bool ApplyGaugeLabelBottom(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::gauge, &GaugeWidgetConfig::labelBottom, config, value);
}

bool ApplyGaugeSweep(AppConfig& config, double value) {
    config.layout.gauge.sweepDegrees = ClampGaugeSweepDegrees(value);
    return true;
}

bool ApplyGaugeSegmentGap(AppConfig& config, double value) {
    config.layout.gauge.segmentGapDegrees = ClampGaugeSegmentGapDegrees(config, value);
    return true;
}

bool ApplyGaugeSegmentCount(AppConfig& config, double value) {
    return ApplyPositiveInt(&LayoutConfig::gauge, &GaugeWidgetConfig::segmentCount, config, value);
}

const UiFontConfig* FontTitle(const AppConfig& config) {
    return &config.layout.fonts.title;
}

const UiFontConfig* FontBig(const AppConfig& config) {
    return &config.layout.fonts.big;
}

const UiFontConfig* FontValue(const AppConfig& config) {
    return &config.layout.fonts.value;
}

const UiFontConfig* FontLabel(const AppConfig& config) {
    return &config.layout.fonts.label;
}

const UiFontConfig* FontText(const AppConfig& config) {
    return &config.layout.fonts.text;
}

const UiFontConfig* FontSmall(const AppConfig& config) {
    return &config.layout.fonts.smallText;
}

const UiFontConfig* FontFooter(const AppConfig& config) {
    return &config.layout.fonts.footer;
}

const UiFontConfig* FontClockTime(const AppConfig& config) {
    return &config.layout.fonts.clockTime;
}

const UiFontConfig* FontClockDate(const AppConfig& config) {
    return &config.layout.fonts.clockDate;
}

bool ApplyFontTitle(AppConfig& config, double value) {
    return ApplyFontSize(&UiFontSetConfig::title, config, value);
}

bool ApplyFontBig(AppConfig& config, double value) {
    return ApplyFontSize(&UiFontSetConfig::big, config, value);
}

bool ApplyFontValue(AppConfig& config, double value) {
    return ApplyFontSize(&UiFontSetConfig::value, config, value);
}

bool ApplyFontLabel(AppConfig& config, double value) {
    return ApplyFontSize(&UiFontSetConfig::label, config, value);
}

bool ApplyFontText(AppConfig& config, double value) {
    return ApplyFontSize(&UiFontSetConfig::text, config, value);
}

bool ApplyFontSmall(AppConfig& config, double value) {
    return ApplyFontSize(&UiFontSetConfig::smallText, config, value);
}

bool ApplyFontFooter(AppConfig& config, double value) {
    return ApplyFontSize(&UiFontSetConfig::footer, config, value);
}

bool ApplyFontClockTime(AppConfig& config, double value) {
    return ApplyFontSize(&UiFontSetConfig::clockTime, config, value);
}

bool ApplyFontClockDate(AppConfig& config, double value) {
    return ApplyFontSize(&UiFontSetConfig::clockDate, config, value);
}

const LayoutEditParameterInfo kParameterInfo[] = {
    {Parameter::FontTitle, *MakeFontDescriptor("title"), false, true, true, LayoutEditWidgetDragMode::Linear, &ApplyFontTitle, &FontTitle},
    {Parameter::FontBig, *MakeFontDescriptor("big"), false, true, true, LayoutEditWidgetDragMode::Linear, &ApplyFontBig, &FontBig},
    {Parameter::FontValue, *MakeFontDescriptor("value"), false, true, true, LayoutEditWidgetDragMode::Linear, &ApplyFontValue, &FontValue},
    {Parameter::FontLabel, *MakeFontDescriptor("label"), false, true, true, LayoutEditWidgetDragMode::Linear, &ApplyFontLabel, &FontLabel},
    {Parameter::FontText, *MakeFontDescriptor("text"), false, true, true, LayoutEditWidgetDragMode::Linear, &ApplyFontText, &FontText},
    {Parameter::FontSmall, *MakeFontDescriptor("small"), false, true, true, LayoutEditWidgetDragMode::Linear, &ApplyFontSmall, &FontSmall},
    {Parameter::FontFooter, *MakeFontDescriptor("footer"), false, true, true, LayoutEditWidgetDragMode::Linear, &ApplyFontFooter, &FontFooter},
    {Parameter::FontClockTime, *MakeFontDescriptor("clock_time"), false, true, true, LayoutEditWidgetDragMode::Linear, &ApplyFontClockTime, &FontClockTime},
    {Parameter::FontClockDate, *MakeFontDescriptor("clock_date"), false, true, true, LayoutEditWidgetDragMode::Linear, &ApplyFontClockDate, &FontClockDate},

    {Parameter::MetricListBarHeight, *MakeDescriptor("metric_list", "bar_height"), false, true, false, LayoutEditWidgetDragMode::Linear, &ApplyMetricListBarHeight, nullptr},
    {Parameter::MetricListLabelWidth, *MakeDescriptor("metric_list", "label_width"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyMetricListLabelWidth, nullptr},
    {Parameter::MetricListVerticalGap, *MakeDescriptor("metric_list", "vertical_gap"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyMetricListVerticalGap, nullptr},

    {Parameter::DriveUsageActivitySegments, *MakeDescriptor("drive_usage_list", "activity_segments"), false, true, false, LayoutEditWidgetDragMode::Linear, &ApplyDriveUsageActivitySegments, nullptr},
    {Parameter::DriveUsageBarHeight, *MakeDescriptor("drive_usage_list", "bar_height"), false, true, false, LayoutEditWidgetDragMode::Linear, &ApplyDriveUsageBarHeight, nullptr},
    {Parameter::DriveUsageLabelGap, *MakeDescriptor("drive_usage_list", "label_gap"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyDriveUsageLabelGap, nullptr},
    {Parameter::DriveUsageBarGap, *MakeDescriptor("drive_usage_list", "bar_gap"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyDriveUsageBarGap, nullptr},
    {Parameter::DriveUsageRwGap, *MakeDescriptor("drive_usage_list", "rw_gap"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyDriveUsageRwGap, nullptr},
    {Parameter::DriveUsagePercentGap, *MakeDescriptor("drive_usage_list", "percent_gap"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyDriveUsagePercentGap, nullptr},
    {Parameter::DriveUsageActivityWidth, *MakeDescriptor("drive_usage_list", "activity_width"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyDriveUsageActivityWidth, nullptr},
    {Parameter::DriveUsageFreeWidth, *MakeDescriptor("drive_usage_list", "free_width"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyDriveUsageFreeWidth, nullptr},
    {Parameter::DriveUsageActivitySegmentGap, *MakeDescriptor("drive_usage_list", "activity_segment_gap"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyDriveUsageActivitySegmentGap, nullptr},
    {Parameter::DriveUsageHeaderGap, *MakeDescriptor("drive_usage_list", "header_gap"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyDriveUsageHeaderGap, nullptr},
    {Parameter::DriveUsageRowGap, *MakeDescriptor("drive_usage_list", "row_gap"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyDriveUsageRowGap, nullptr},

    {Parameter::ThroughputGuideStrokeWidth, *MakeDescriptor("throughput", "guide_stroke_width"), true, true, false, LayoutEditWidgetDragMode::Linear, &ApplyThroughputGuideStrokeWidth, nullptr},
    {Parameter::ThroughputPlotStrokeWidth, *MakeDescriptor("throughput", "plot_stroke_width"), true, true, false, LayoutEditWidgetDragMode::Linear, &ApplyThroughputPlotStrokeWidth, nullptr},
    {Parameter::ThroughputLeaderDiameter, *MakeDescriptor("throughput", "leader_diameter"), true, true, false, LayoutEditWidgetDragMode::Linear, &ApplyThroughputLeaderDiameter, nullptr},
    {Parameter::ThroughputAxisPadding, *MakeDescriptor("throughput", "axis_padding"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyThroughputAxisPadding, nullptr},
    {Parameter::ThroughputHeaderGap, *MakeDescriptor("throughput", "header_gap"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyThroughputHeaderGap, nullptr},

    {Parameter::GaugeSegmentCount, *MakeDescriptor("gauge", "segment_count"), false, true, false, LayoutEditWidgetDragMode::Linear, &ApplyGaugeSegmentCount, nullptr},
    {Parameter::GaugeValueBottom, *MakeDescriptor("gauge", "value_bottom"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyGaugeValueBottom, nullptr},
    {Parameter::GaugeLabelBottom, *MakeDescriptor("gauge", "label_bottom"), true, false, false, LayoutEditWidgetDragMode::Linear, &ApplyGaugeLabelBottom, nullptr},
    {Parameter::GaugeSweepDegrees, *MakeDescriptor("gauge", "sweep_degrees", LayoutEditTooltipValueFormat::FloatingPoint), true, false, false, LayoutEditWidgetDragMode::GaugeSweepDegrees, &ApplyGaugeSweep, nullptr},
    {Parameter::GaugeSegmentGapDegrees, *MakeDescriptor("gauge", "segment_gap_degrees", LayoutEditTooltipValueFormat::FloatingPoint), true, false, false, LayoutEditWidgetDragMode::GaugeSegmentGapDegrees, &ApplyGaugeSegmentGap, nullptr},
    {Parameter::GaugeOuterPadding, *MakeDescriptor("gauge", "outer_padding"), true, true, false, LayoutEditWidgetDragMode::Linear, &ApplyGaugeOuterPadding, nullptr},
    {Parameter::GaugeRingThickness, *MakeDescriptor("gauge", "ring_thickness"), true, true, false, LayoutEditWidgetDragMode::Linear, &ApplyGaugeRingThickness, nullptr},
};

constexpr size_t kParameterInfoCount = sizeof(kParameterInfo) / sizeof(kParameterInfo[0]);
static_assert(kParameterInfoCount == static_cast<size_t>(Parameter::Count));

}  // namespace

const LayoutEditParameterInfo& GetLayoutEditParameterInfo(LayoutEditParameter parameter) {
    return kParameterInfo[static_cast<size_t>(parameter)];
}

int GetLayoutEditParameterHitPriority(LayoutEditParameter parameter) {
    return static_cast<int>(parameter);
}

bool IsFontLayoutEditParameter(LayoutEditParameter parameter) {
    return GetLayoutEditParameterInfo(parameter).isFont;
}

std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(LayoutEditParameter parameter) {
    return GetLayoutEditParameterInfo(parameter).tooltip;
}

std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, LayoutEditParameter parameter) {
    const auto& info = GetLayoutEditParameterInfo(parameter);
    if (info.fontValue == nullptr) {
        return std::nullopt;
    }
    return info.fontValue(config);
}

bool ApplyLayoutEditParameterValue(AppConfig& config, LayoutEditParameter parameter, double value) {
    const auto& info = GetLayoutEditParameterInfo(parameter);
    return info.applyValue != nullptr ? info.applyValue(config, value) : false;
}
