#include "layout_edit_commands.h"

#include <algorithm>
#include <cmath>

namespace layout_edit {

namespace {

int ClampPositiveInt(double value) {
    return (std::max)(1, static_cast<int>(std::lround(value)));
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

template <typename Section>
void ApplyPositiveInt(Section LayoutConfig::* section, int Section::* member, AppConfig& config, double value) {
    (config.layout.*section).*member = ClampPositiveInt(value);
}

void ApplyFontSize(UiFontConfig UiFontSetConfig::* font, AppConfig& config, double value) {
    (config.layout.fonts.*font).size = ClampPositiveInt(value);
}

}  // namespace

bool ApplyValue(AppConfig& config, const LayoutEditHost::ValueTarget& target, double value) {
    using Field = LayoutEditHost::ValueTarget::Field;
    switch (target.field) {
    case Field::MetricListLabelWidth:
        ApplyPositiveInt(&LayoutConfig::metricList, &MetricListWidgetConfig::labelWidth, config, value);
        return true;
    case Field::MetricListVerticalGap:
        ApplyPositiveInt(&LayoutConfig::metricList, &MetricListWidgetConfig::verticalGap, config, value);
        return true;
    case Field::DriveUsageActivityWidth:
        ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::activityWidth, config, value);
        return true;
    case Field::DriveUsageFreeWidth:
        ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::freeWidth, config, value);
        return true;
    case Field::DriveUsageHeaderGap:
        ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::headerGap, config, value);
        return true;
    case Field::DriveUsageRowGap:
        ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::rowGap, config, value);
        return true;
    case Field::ThroughputAxisPadding:
        ApplyPositiveInt(&LayoutConfig::throughput, &ThroughputWidgetConfig::axisPadding, config, value);
        return true;
    case Field::ThroughputHeaderGap:
        ApplyPositiveInt(&LayoutConfig::throughput, &ThroughputWidgetConfig::headerGap, config, value);
        return true;
    case Field::GaugeSweepDegrees:
        config.layout.gauge.sweepDegrees = ClampGaugeSweepDegrees(value);
        return true;
    case Field::GaugeSegmentGapDegrees:
        config.layout.gauge.segmentGapDegrees = ClampGaugeSegmentGapDegrees(config, value);
        return true;
    case Field::FontTitle:
        ApplyFontSize(&UiFontSetConfig::title, config, value);
        return true;
    case Field::FontBig:
        ApplyFontSize(&UiFontSetConfig::big, config, value);
        return true;
    case Field::FontValue:
        ApplyFontSize(&UiFontSetConfig::value, config, value);
        return true;
    case Field::FontLabel:
        ApplyFontSize(&UiFontSetConfig::label, config, value);
        return true;
    case Field::FontText:
        ApplyFontSize(&UiFontSetConfig::text, config, value);
        return true;
    case Field::FontSmall:
        ApplyFontSize(&UiFontSetConfig::smallText, config, value);
        return true;
    case Field::FontFooter:
        ApplyFontSize(&UiFontSetConfig::footer, config, value);
        return true;
    case Field::FontClockTime:
        ApplyFontSize(&UiFontSetConfig::clockTime, config, value);
        return true;
    case Field::FontClockDate:
        ApplyFontSize(&UiFontSetConfig::clockDate, config, value);
        return true;
    case Field::MetricListBarHeight:
        ApplyPositiveInt(&LayoutConfig::metricList, &MetricListWidgetConfig::barHeight, config, value);
        return true;
    case Field::DriveUsageBarHeight:
        ApplyPositiveInt(&LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::barHeight, config, value);
        return true;
    case Field::GaugeSegmentCount:
        ApplyPositiveInt(&LayoutConfig::gauge, &GaugeWidgetConfig::segmentCount, config, value);
        return true;
    default:
        return false;
    }
}

}  // namespace layout_edit
