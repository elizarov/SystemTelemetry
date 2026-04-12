#include "layout_edit_tooltip.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

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

}  // namespace

std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(
    DashboardRenderer::WidgetEditParameter parameter) {
    using Parameter = DashboardRenderer::WidgetEditParameter;
    switch (parameter) {
        case Parameter::MetricListLabelWidth:
            return MakeDescriptor("metric_list", "label_width");
        case Parameter::MetricListVerticalGap:
            return MakeDescriptor("metric_list", "vertical_gap");
        case Parameter::DriveUsageLabelGap:
            return MakeDescriptor("drive_usage_list", "label_gap");
        case Parameter::DriveUsageBarGap:
            return MakeDescriptor("drive_usage_list", "bar_gap");
        case Parameter::DriveUsageRwGap:
            return MakeDescriptor("drive_usage_list", "rw_gap");
        case Parameter::DriveUsagePercentGap:
            return MakeDescriptor("drive_usage_list", "percent_gap");
        case Parameter::DriveUsageActivityWidth:
            return MakeDescriptor("drive_usage_list", "activity_width");
        case Parameter::DriveUsageFreeWidth:
            return MakeDescriptor("drive_usage_list", "free_width");
        case Parameter::DriveUsageActivitySegmentGap:
            return MakeDescriptor("drive_usage_list", "activity_segment_gap");
        case Parameter::DriveUsageHeaderGap:
            return MakeDescriptor("drive_usage_list", "header_gap");
        case Parameter::DriveUsageRowGap:
            return MakeDescriptor("drive_usage_list", "row_gap");
        case Parameter::ThroughputAxisPadding:
            return MakeDescriptor("throughput", "axis_padding");
        case Parameter::ThroughputHeaderGap:
            return MakeDescriptor("throughput", "header_gap");
        case Parameter::ThroughputGuideStrokeWidth:
            return MakeDescriptor("throughput", "guide_stroke_width");
        case Parameter::ThroughputPlotStrokeWidth:
            return MakeDescriptor("throughput", "plot_stroke_width");
        case Parameter::ThroughputLeaderDiameter:
            return MakeDescriptor("throughput", "leader_diameter");
        case Parameter::GaugeOuterPadding:
            return MakeDescriptor("gauge", "outer_padding");
        case Parameter::GaugeRingThickness:
            return MakeDescriptor("gauge", "ring_thickness");
        case Parameter::GaugeValueBottom:
            return MakeDescriptor("gauge", "value_bottom");
        case Parameter::GaugeLabelBottom:
            return MakeDescriptor("gauge", "label_bottom");
        case Parameter::GaugeSweepDegrees:
            return MakeDescriptor("gauge", "sweep_degrees", LayoutEditTooltipValueFormat::FloatingPoint);
        case Parameter::GaugeSegmentGapDegrees:
            return MakeDescriptor("gauge", "segment_gap_degrees", LayoutEditTooltipValueFormat::FloatingPoint);
    }
    return std::nullopt;
}

std::optional<LayoutEditTooltipDescriptor> FindLayoutEditTooltipDescriptor(
    DashboardRenderer::AnchorEditParameter parameter) {
    using Parameter = DashboardRenderer::AnchorEditParameter;
    switch (parameter) {
        case Parameter::MetricListBarHeight:
            return MakeDescriptor("metric_list", "bar_height");
        case Parameter::DriveUsageBarHeight:
            return MakeDescriptor("drive_usage_list", "bar_height");
        case Parameter::SegmentCount:
            return MakeDescriptor("gauge", "segment_count");
        case Parameter::DriveUsageActivitySegments:
            return MakeDescriptor("drive_usage_list", "activity_segments");
        case Parameter::ThroughputGuideStrokeWidth:
            return MakeDescriptor("throughput", "guide_stroke_width");
        case Parameter::ThroughputPlotStrokeWidth:
            return MakeDescriptor("throughput", "plot_stroke_width");
        case Parameter::ThroughputLeaderDiameter:
            return MakeDescriptor("throughput", "leader_diameter");
        case Parameter::GaugeOuterPadding:
            return MakeDescriptor("gauge", "outer_padding");
        case Parameter::GaugeRingThickness:
            return MakeDescriptor("gauge", "ring_thickness");
        case Parameter::FontTitle:
            return MakeFontDescriptor("title");
        case Parameter::FontBig:
            return MakeFontDescriptor("big");
        case Parameter::FontValue:
            return MakeFontDescriptor("value");
        case Parameter::FontLabel:
            return MakeFontDescriptor("label");
        case Parameter::FontText:
            return MakeFontDescriptor("text");
        case Parameter::FontSmall:
            return MakeFontDescriptor("small");
        case Parameter::FontFooter:
            return MakeFontDescriptor("footer");
        case Parameter::FontClockTime:
            return MakeFontDescriptor("clock_time");
        case Parameter::FontClockDate:
            return MakeFontDescriptor("clock_date");
    }
    return std::nullopt;
}

std::string FormatLayoutEditTooltipValue(double value, LayoutEditTooltipValueFormat format) {
    if (format == LayoutEditTooltipValueFormat::FontSpec) {
        return {};
    }
    if (format == LayoutEditTooltipValueFormat::Integer) {
        return std::to_string(static_cast<int>(std::lround(value)));
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    std::string text = stream.str();
    const size_t dot = text.find('.');
    if (dot != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

std::string FormatLayoutEditTooltipValue(const UiFontConfig& value) {
    return value.face + "," + std::to_string(value.size) + "," + std::to_string(value.weight);
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, double value) {
    return "[" + descriptor.sectionName + "] " + descriptor.memberName + " = " +
           FormatLayoutEditTooltipValue(value, descriptor.valueFormat);
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, const UiFontConfig& value) {
    return "[" + descriptor.sectionName + "] " + descriptor.memberName + " = " +
           FormatLayoutEditTooltipValue(value);
}

std::optional<const UiFontConfig*> FindLayoutEditTooltipFontValue(
    const AppConfig& config, DashboardRenderer::AnchorEditParameter parameter) {
    using Parameter = DashboardRenderer::AnchorEditParameter;
    switch (parameter) {
        case Parameter::FontTitle:
            return &config.layout.fonts.title;
        case Parameter::FontBig:
            return &config.layout.fonts.big;
        case Parameter::FontValue:
            return &config.layout.fonts.value;
        case Parameter::FontLabel:
            return &config.layout.fonts.label;
        case Parameter::FontText:
            return &config.layout.fonts.text;
        case Parameter::FontSmall:
            return &config.layout.fonts.smallText;
        case Parameter::FontFooter:
            return &config.layout.fonts.footer;
        case Parameter::FontClockTime:
            return &config.layout.fonts.clockTime;
        case Parameter::FontClockDate:
            return &config.layout.fonts.clockDate;
        default:
            return std::nullopt;
    }
}
