#include "layout_edit_commands.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace layout_edit {

namespace {

class EditCommand {
public:
    virtual ~EditCommand() = default;
    virtual void Apply(AppConfig& config) const = 0;
};

class EditCommandValidator {
public:
    static int ClampPositiveInt(double value) {
        return (std::max)(1, static_cast<int>(std::lround(value)));
    }

    static double ClampGaugeSweepDegrees(double value) {
        return std::clamp(value, 0.0, 360.0);
    }

    static double ClampGaugeSegmentGapDegrees(const AppConfig& config, double value) {
        const double totalSweep = ClampGaugeSweepDegrees(config.layout.gauge.sweepDegrees);
        const int segmentCount = (std::max)(1, config.layout.gauge.segmentCount);
        const double maxSegmentGap = segmentCount <= 1 ? 0.0 : totalSweep / static_cast<double>(segmentCount - 1);
        return std::clamp(value, 0.0, maxSegmentGap);
    }
};

template <typename Section>
class SetPositiveIntCommand final : public EditCommand {
public:
    SetPositiveIntCommand(Section LayoutConfig::* section, int Section::* member, int value)
        : section_(section), member_(member), value_(value) {}

    void Apply(AppConfig& config) const override {
        (config.layout.*section_).*member_ = value_;
    }

private:
    Section LayoutConfig::* section_;
    int Section::* member_;
    int value_;
};

class SetFontSizeCommand final : public EditCommand {
public:
    SetFontSizeCommand(UiFontConfig UiFontSetConfig::* font, int value)
        : font_(font), value_(value) {}

    void Apply(AppConfig& config) const override {
        (config.layout.fonts.*font_).size = value_;
    }

private:
    UiFontConfig UiFontSetConfig::* font_;
    int value_;
};

class SetGaugeSweepDegreesCommand final : public EditCommand {
public:
    explicit SetGaugeSweepDegreesCommand(double value) : value_(value) {}

    void Apply(AppConfig& config) const override {
        config.layout.gauge.sweepDegrees = value_;
    }

private:
    double value_;
};

class SetGaugeSegmentGapDegreesCommand final : public EditCommand {
public:
    explicit SetGaugeSegmentGapDegreesCommand(double value) : value_(value) {}

    void Apply(AppConfig& config) const override {
        config.layout.gauge.segmentGapDegrees = value_;
    }

private:
    double value_;
};

std::unique_ptr<EditCommand> CreateEditCommand(const AppConfig& config, const LayoutEditHost::ValueTarget& target, double value) {
    using Field = LayoutEditHost::ValueTarget::Field;
    switch (target.field) {
    case Field::MetricListLabelWidth:
        return std::make_unique<SetPositiveIntCommand<MetricListWidgetConfig>>(
            &LayoutConfig::metricList, &MetricListWidgetConfig::labelWidth, EditCommandValidator::ClampPositiveInt(value));
    case Field::MetricListVerticalGap:
        return std::make_unique<SetPositiveIntCommand<MetricListWidgetConfig>>(
            &LayoutConfig::metricList, &MetricListWidgetConfig::verticalGap, EditCommandValidator::ClampPositiveInt(value));
    case Field::DriveUsageActivityWidth:
        return std::make_unique<SetPositiveIntCommand<DriveUsageListWidgetConfig>>(
            &LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::activityWidth, EditCommandValidator::ClampPositiveInt(value));
    case Field::DriveUsageFreeWidth:
        return std::make_unique<SetPositiveIntCommand<DriveUsageListWidgetConfig>>(
            &LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::freeWidth, EditCommandValidator::ClampPositiveInt(value));
    case Field::DriveUsageHeaderGap:
        return std::make_unique<SetPositiveIntCommand<DriveUsageListWidgetConfig>>(
            &LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::headerGap, EditCommandValidator::ClampPositiveInt(value));
    case Field::DriveUsageRowGap:
        return std::make_unique<SetPositiveIntCommand<DriveUsageListWidgetConfig>>(
            &LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::rowGap, EditCommandValidator::ClampPositiveInt(value));
    case Field::ThroughputAxisPadding:
        return std::make_unique<SetPositiveIntCommand<ThroughputWidgetConfig>>(
            &LayoutConfig::throughput, &ThroughputWidgetConfig::axisPadding, EditCommandValidator::ClampPositiveInt(value));
    case Field::ThroughputHeaderGap:
        return std::make_unique<SetPositiveIntCommand<ThroughputWidgetConfig>>(
            &LayoutConfig::throughput, &ThroughputWidgetConfig::headerGap, EditCommandValidator::ClampPositiveInt(value));
    case Field::GaugeSweepDegrees:
        return std::make_unique<SetGaugeSweepDegreesCommand>(EditCommandValidator::ClampGaugeSweepDegrees(value));
    case Field::GaugeSegmentGapDegrees:
        return std::make_unique<SetGaugeSegmentGapDegreesCommand>(
            EditCommandValidator::ClampGaugeSegmentGapDegrees(config, value));
    case Field::FontTitle:
        return std::make_unique<SetFontSizeCommand>(&UiFontSetConfig::title, EditCommandValidator::ClampPositiveInt(value));
    case Field::FontBig:
        return std::make_unique<SetFontSizeCommand>(&UiFontSetConfig::big, EditCommandValidator::ClampPositiveInt(value));
    case Field::FontValue:
        return std::make_unique<SetFontSizeCommand>(&UiFontSetConfig::value, EditCommandValidator::ClampPositiveInt(value));
    case Field::FontLabel:
        return std::make_unique<SetFontSizeCommand>(&UiFontSetConfig::label, EditCommandValidator::ClampPositiveInt(value));
    case Field::FontText:
        return std::make_unique<SetFontSizeCommand>(&UiFontSetConfig::text, EditCommandValidator::ClampPositiveInt(value));
    case Field::FontSmall:
        return std::make_unique<SetFontSizeCommand>(&UiFontSetConfig::smallText, EditCommandValidator::ClampPositiveInt(value));
    case Field::FontFooter:
        return std::make_unique<SetFontSizeCommand>(&UiFontSetConfig::footer, EditCommandValidator::ClampPositiveInt(value));
    case Field::FontClockTime:
        return std::make_unique<SetFontSizeCommand>(&UiFontSetConfig::clockTime, EditCommandValidator::ClampPositiveInt(value));
    case Field::FontClockDate:
        return std::make_unique<SetFontSizeCommand>(&UiFontSetConfig::clockDate, EditCommandValidator::ClampPositiveInt(value));
    case Field::MetricListBarHeight:
        return std::make_unique<SetPositiveIntCommand<MetricListWidgetConfig>>(
            &LayoutConfig::metricList, &MetricListWidgetConfig::barHeight, EditCommandValidator::ClampPositiveInt(value));
    case Field::DriveUsageBarHeight:
        return std::make_unique<SetPositiveIntCommand<DriveUsageListWidgetConfig>>(
            &LayoutConfig::driveUsageList, &DriveUsageListWidgetConfig::barHeight, EditCommandValidator::ClampPositiveInt(value));
    case Field::GaugeSegmentCount:
        return std::make_unique<SetPositiveIntCommand<GaugeWidgetConfig>>(
            &LayoutConfig::gauge, &GaugeWidgetConfig::segmentCount, EditCommandValidator::ClampPositiveInt(value));
    default:
        return nullptr;
    }
}

}  // namespace

bool ApplyValue(AppConfig& config, const LayoutEditHost::ValueTarget& target, double value) {
    std::unique_ptr<EditCommand> command = CreateEditCommand(config, target, value);
    if (command == nullptr) {
        return false;
    }
    command->Apply(config);
    return true;
}

}  // namespace layout_edit
