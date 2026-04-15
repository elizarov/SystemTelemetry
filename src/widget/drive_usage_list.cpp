#include "drive_usage_list.h"

#include <cmath>
#include <limits>

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

namespace {

std::string ResolveDriveMetricLabel(
    const DashboardRenderer& renderer, std::string_view metricRef, std::string_view fallback) {
    const MetricDefinitionConfig* definition = FindMetricDefinition(renderer.Config().metrics, metricRef);
    if (definition != nullptr && !definition->label.empty()) {
        return definition->label;
    }
    return std::string(fallback);
}

std::string ResolveDriveMetricSampleValue(
    const DashboardRenderer& renderer, std::string_view metricRef, std::string_view fallback) {
    const std::string sample = ResolveMetricSampleValueText(renderer.Config().metrics, std::string(metricRef));
    return sample.empty() ? std::string(fallback) : sample;
}

DriveUsageListWidget::MeasuredColumnWidths MeasureColumnWidths(const DashboardRenderer& renderer) {
    const std::string writeLabel = ResolveDriveMetricLabel(renderer, "drive.activity.write", "W");
    const std::string usageSample = ResolveDriveMetricSampleValue(renderer, "drive.usage", "100%");
    return DriveUsageListWidget::MeasuredColumnWidths{
        std::max(1, renderer.MeasureTextWidth(TextStyleId::Label, writeLabel + ":")),
        std::max(1, renderer.MeasureTextWidth(TextStyleId::Label, usageSample)),
    };
}

DriveUsageListWidget::ColumnRects ResolveColumns(const RenderRect& band,
    int labelWidth,
    int labelGap,
    int activityWidth,
    int rwGap,
    int barGap,
    int percentWidth,
    int percentGap,
    int freeWidth) {
    DriveUsageListWidget::ColumnRects columns;
    columns.label = {band.left, band.top, (std::min)(band.right, band.left + labelWidth), band.bottom};
    columns.read = {(std::min)(band.right, columns.label.right + labelGap),
        band.top,
        (std::min)(band.right, columns.label.right + labelGap + activityWidth),
        band.bottom};
    columns.write = {(std::min)(band.right, columns.read.right + rwGap),
        band.top,
        (std::min)(band.right, columns.read.right + rwGap + activityWidth),
        band.bottom};
    columns.free = {(std::max)(band.left, band.right - freeWidth), band.top, band.right, band.bottom};
    columns.percent = {
        (std::max)(band.left, columns.free.left - percentWidth), band.top, columns.free.left, band.bottom};
    columns.bar = {(std::min)(band.right, columns.write.right + barGap),
        band.top,
        (std::max)((std::min)(band.right, columns.write.right + barGap), columns.percent.left - percentGap),
        band.bottom};
    return columns;
}

int ClampStackedSegmentGap(int height, int segmentCount, int gap) {
    if (segmentCount <= 1) {
        return 0;
    }
    const int maxGap = ((std::max))(0, (height - segmentCount) / (segmentCount - 1));
    return std::clamp(((std::max))(0, gap), 0, maxGap);
}

int ComputeLowestStackedSegmentTop(int top, int height, int width, int segmentCount, int segmentGap) {
    if (segmentCount <= 0 || height <= 0 || width <= 0) {
        return top;
    }

    const int clampedGap = ClampStackedSegmentGap(height, segmentCount, segmentGap);
    const int totalGap = clampedGap * (segmentCount - 1);
    const int availableHeight = ((std::max))(segmentCount, height - totalGap);
    const int baseSegmentHeight = ((std::max))(1, availableHeight / segmentCount);
    const int remainder = ((std::max))(0, availableHeight - (baseSegmentHeight * segmentCount));
    const int maxVisualHeight = ((std::max))(2, width / 2);

    int currentTop = top;
    int lastSegmentTop = top;
    for (int index = segmentCount - 1; index >= 0; --index) {
        const int extra = (segmentCount - 1 - index) < remainder ? 1 : 0;
        const int segmentHeight = baseSegmentHeight + extra;
        const int visualHeight = ((std::min))(segmentHeight, maxVisualHeight);
        lastSegmentTop = currentTop + (((std::max))(0, (segmentHeight - visualHeight) / 2));
        currentTop = lastSegmentTop + visualHeight + clampedGap;
    }
    return lastSegmentTop;
}

void DrawSegmentIndicator(DashboardRenderer& renderer,
    const RenderRect& rect,
    int segmentCount,
    int segmentGap,
    double ratio,
    RenderColor trackColor,
    RenderColor accentColor) {
    const int width = (std::max)(0, rect.right - rect.left);
    const int height = (std::max)(0, rect.bottom - rect.top);
    if (width <= 0 || height <= 0 || segmentCount <= 0) {
        return;
    }

    const int maxGap = segmentCount <= 1 ? 0 : (std::max)(0, (height - segmentCount) / (segmentCount - 1));
    const int clampedGap = std::clamp((std::max)(0, segmentGap), 0, maxGap);
    const int totalGap = clampedGap * (std::max)(0, segmentCount - 1);
    const int availableHeight = (std::max)(segmentCount, height - totalGap);
    const int baseSegmentHeight = (std::max)(1, availableHeight / segmentCount);
    const int remainder = (std::max)(0, availableHeight - (baseSegmentHeight * segmentCount));
    const double clampedRatio = std::clamp(ratio, 0.0, 1.0);
    const int filledSegments =
        clampedRatio > 0.0
            ? std::clamp(static_cast<int>(std::ceil(clampedRatio * static_cast<double>(segmentCount))), 1, segmentCount)
            : 0;
    int top = rect.top;
    for (int index = segmentCount - 1; index >= 0; --index) {
        const int extra = (segmentCount - 1 - index) < remainder ? 1 : 0;
        const int segmentHeight = baseSegmentHeight + extra;
        const int visualHeight = (std::min)(segmentHeight, (std::max)(2, width / 2));
        const int segmentTop = top + (std::max)(0, (segmentHeight - visualHeight) / 2);
        RenderRect segmentRect{rect.left, segmentTop, rect.right, (std::min)(rect.bottom, segmentTop + visualHeight)};
        renderer.FillSolidRect(segmentRect, trackColor);

        if (index < filledSegments) {
            renderer.FillSolidRect(segmentRect, accentColor);
        }

        top = segmentRect.bottom + clampedGap;
    }
}

int EffectiveDriveHeaderHeight(const DashboardRenderer& renderer) {
    const int headerGap = std::max(0, renderer.ScaleLogical(renderer.Config().layout.driveUsageList.headerGap));
    return renderer.TextMetrics().smallText + headerGap;
}

int EffectiveDriveRowHeight(const DashboardRenderer& renderer) {
    const int textHeight = std::max(renderer.TextMetrics().label, renderer.TextMetrics().smallText);
    const int barHeight = std::max(1, renderer.ScaleLogical(renderer.Config().layout.driveUsageList.barHeight));
    const int rowGap = std::max(0, renderer.ScaleLogical(renderer.Config().layout.driveUsageList.rowGap));
    return std::max(textHeight, barHeight) + rowGap;
}

}  // namespace

DashboardWidgetClass DriveUsageListWidget::Class() const {
    return DashboardWidgetClass::DriveUsageList;
}

std::unique_ptr<DashboardWidget> DriveUsageListWidget::Clone() const {
    return std::make_unique<DriveUsageListWidget>(*this);
}

void DriveUsageListWidget::Initialize(const LayoutNodeConfig&) {}

int DriveUsageListWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    const int count = static_cast<int>(renderer.Config().storage.drives.size());
    return (count > 0 ? EffectiveDriveHeaderHeight(renderer) : 0) + (count * EffectiveDriveRowHeight(renderer));
}

void DriveUsageListWidget::ResolveLayoutState(const DashboardRenderer& renderer, const RenderRect& rect) {
    const auto& config = renderer.Config().layout.driveUsageList;
    layoutState_ = {};
    layoutState_.measuredColumnWidths = MeasureColumnWidths(renderer);
    layoutState_.headerHeight = EffectiveDriveHeaderHeight(renderer);
    layoutState_.rowHeight = EffectiveDriveRowHeight(renderer);
    layoutState_.labelGap = (std::max)(0, renderer.ScaleLogical(config.labelGap));
    layoutState_.activityWidth = (std::max)(1, renderer.ScaleLogical(config.activityWidth));
    layoutState_.rwGap = (std::max)(0, renderer.ScaleLogical(config.rwGap));
    layoutState_.barGap = (std::max)(0, renderer.ScaleLogical(config.barGap));
    layoutState_.percentGap = (std::max)(0, renderer.ScaleLogical(config.percentGap));
    layoutState_.freeWidth = (std::max)(1, renderer.ScaleLogical(config.freeWidth));
    layoutState_.driveBarHeight = (std::max)(1, renderer.ScaleLogical(config.barHeight));
    layoutState_.activitySegments = (std::max)(1, config.activitySegments);
    layoutState_.activitySegmentGap = (std::max)(0, renderer.ScaleLogical(config.activitySegmentGap));
    layoutState_.rowContentHeight = (std::max)(
        renderer.TextMetrics().label, (std::max)(renderer.TextMetrics().smallText, layoutState_.driveBarHeight));
    layoutState_.activityAnchorSize = (std::max)(8, renderer.ScaleLogical(10));
    layoutState_.headerRect = RenderRect{rect.left, rect.top, rect.right, rect.top + layoutState_.headerHeight};
    layoutState_.headerColumns = ResolveColumns(layoutState_.headerRect,
        layoutState_.measuredColumnWidths.label,
        layoutState_.labelGap,
        layoutState_.activityWidth,
        layoutState_.rwGap,
        layoutState_.barGap,
        layoutState_.measuredColumnWidths.percent,
        layoutState_.percentGap,
        layoutState_.freeWidth);
    layoutState_.usageHeaderRect = RenderRect{layoutState_.headerColumns.bar.left,
        layoutState_.headerRect.top,
        layoutState_.headerColumns.percent.right,
        layoutState_.headerRect.bottom};
    layoutState_.headerReadLabelRect = RenderRect{layoutState_.headerColumns.read.left - layoutState_.rwGap,
        layoutState_.headerColumns.read.top,
        layoutState_.headerColumns.read.right + layoutState_.rwGap,
        layoutState_.headerColumns.read.bottom};
    layoutState_.headerWriteLabelRect = RenderRect{layoutState_.headerColumns.write.left - layoutState_.rwGap,
        layoutState_.headerColumns.write.top,
        layoutState_.headerColumns.write.right + layoutState_.rwGap,
        layoutState_.headerColumns.write.bottom};
    layoutState_.activityTargetRect =
        RenderRect{layoutState_.headerColumns.read.left, rect.top, layoutState_.headerColumns.write.right, rect.bottom};
    layoutState_.rowBands.clear();
    layoutState_.rowColumns.clear();
    layoutState_.rowReadIndicatorRects.clear();
    layoutState_.rowWriteIndicatorRects.clear();
    layoutState_.rowBarRects.clear();
    layoutState_.rowBarAnchorRects.clear();
    const int totalRows = static_cast<int>(renderer.Config().storage.drives.size());
    RenderRect rowRect{
        rect.left, layoutState_.headerRect.bottom, rect.right, layoutState_.headerRect.bottom + layoutState_.rowHeight};
    for (int rowIndex = 0; rowIndex < totalRows && rowRect.top < rect.bottom; ++rowIndex) {
        layoutState_.rowBands.push_back(rowRect);
        layoutState_.rowColumns.push_back(ResolveColumns(rowRect,
            layoutState_.measuredColumnWidths.label,
            layoutState_.labelGap,
            layoutState_.activityWidth,
            layoutState_.rwGap,
            layoutState_.barGap,
            layoutState_.measuredColumnWidths.percent,
            layoutState_.percentGap,
            layoutState_.freeWidth));
        const int rowPixelHeight = static_cast<int>(rowRect.bottom - rowRect.top);
        const int contentTop =
            static_cast<int>(rowRect.top) + (std::max)(0, (rowPixelHeight - layoutState_.rowContentHeight) / 2);
        const ColumnRects& columns = layoutState_.rowColumns.back();
        layoutState_.rowReadIndicatorRects.push_back(
            RenderRect{columns.read.left, contentTop, columns.read.right, contentTop + layoutState_.rowContentHeight});
        layoutState_.rowWriteIndicatorRects.push_back(RenderRect{
            columns.write.left, contentTop, columns.write.right, contentTop + layoutState_.rowContentHeight});
        const int barTop =
            static_cast<int>(rowRect.top) + (std::max)(0, (rowPixelHeight - layoutState_.driveBarHeight) / 2);
        layoutState_.rowBarRects.push_back(
            RenderRect{columns.bar.left, barTop, columns.bar.right, barTop + layoutState_.driveBarHeight});
        const int anchorCenterX = static_cast<int>(columns.bar.left) +
                                  ((std::max)(0, static_cast<int>(columns.bar.right - columns.bar.left) / 2));
        const int anchorCenterY = barTop + layoutState_.driveBarHeight;
        const int anchorSize = (std::max)(4, renderer.ScaleLogical(6));
        layoutState_.rowBarAnchorRects.push_back(RenderRect{anchorCenterX - (anchorSize / 2),
            anchorCenterY - (anchorSize / 2),
            anchorCenterX - (anchorSize / 2) + anchorSize,
            anchorCenterY - (anchorSize / 2) + anchorSize});
        rowRect = RenderRect{
            rowRect.left,
            rowRect.top + layoutState_.rowHeight,
            rowRect.right,
            rowRect.bottom + layoutState_.rowHeight,
        };
    }
    layoutState_.visibleRows = static_cast<int>(layoutState_.rowBands.size());
    layoutState_.activityAnchorCenterX =
        layoutState_.headerColumns.read.left +
        ((std::max)(0, layoutState_.headerColumns.write.right - layoutState_.headerColumns.read.left) / 2);
    const int firstRowTop = layoutState_.visibleRows > 0 ? static_cast<int>(layoutState_.rowBands.front().top)
                                                         : static_cast<int>(layoutState_.headerRect.bottom);
    const int firstRowBottom = layoutState_.visibleRows > 0 ? static_cast<int>(layoutState_.rowBands.front().bottom)
                                                            : static_cast<int>(layoutState_.headerRect.bottom);
    layoutState_.firstRowContentTop =
        firstRowTop + (std::max)(0, ((firstRowBottom - firstRowTop) - layoutState_.rowContentHeight) / 2);
    layoutState_.activityAnchorRect = RenderRect{
        layoutState_.activityAnchorCenterX - (layoutState_.activityAnchorSize / 2),
        layoutState_.firstRowContentTop - (layoutState_.activityAnchorSize / 2),
        layoutState_.activityAnchorCenterX - (layoutState_.activityAnchorSize / 2) + layoutState_.activityAnchorSize,
        layoutState_.firstRowContentTop - (layoutState_.activityAnchorSize / 2) + layoutState_.activityAnchorSize};
    if (layoutState_.visibleRows > 0 && config.activitySegments > 1) {
        layoutState_.clampedActivitySegmentGap = ClampStackedSegmentGap(
            layoutState_.rowContentHeight, config.activitySegments, layoutState_.activitySegmentGap);
        layoutState_.lowestSegmentTop = ComputeLowestStackedSegmentTop(layoutState_.firstRowContentTop,
            layoutState_.rowContentHeight,
            layoutState_.activityWidth,
            config.activitySegments,
            layoutState_.clampedActivitySegmentGap);
    }
}

void DriveUsageListWidget::Draw(
    DashboardRenderer& renderer, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const {
    const std::string readLabel = ResolveDriveMetricLabel(renderer, "drive.activity.read", "R");
    const std::string writeLabel = ResolveDriveMetricLabel(renderer, "drive.activity.write", "W");
    const std::string usageLabel = ResolveDriveMetricLabel(renderer, "drive.usage", "Usage");
    const std::string freeLabel = ResolveDriveMetricLabel(renderer, "drive.free", "Free");

    renderer.PushClipRect(widget.rect);
    renderer.DrawText(layoutState_.headerReadLabelRect,
        readLabel,
        TextStyleId::Small,
        renderer.MutedTextColor(),
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center, false));
    renderer.DrawText(layoutState_.headerWriteLabelRect,
        writeLabel,
        TextStyleId::Small,
        renderer.MutedTextColor(),
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center, false));
    renderer.DrawText(layoutState_.usageHeaderRect,
        usageLabel,
        TextStyleId::Small,
        renderer.MutedTextColor(),
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center));
    renderer.DrawText(layoutState_.headerColumns.free,
        freeLabel,
        TextStyleId::Small,
        renderer.MutedTextColor(),
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Trailing, TextVerticalAlign::Center));

    const auto rows = metrics.ResolveDriveRows();
    for (size_t rowIndex = 0; rowIndex < rows.size() && rowIndex < layoutState_.rowBands.size(); ++rowIndex) {
        const auto& drive = rows[rowIndex];
        const int textBaseId = 100 + static_cast<int>(rowIndex) * 3;
        const ColumnRects& columns = layoutState_.rowColumns[rowIndex];
        const RenderRect& readIndicatorRect = layoutState_.rowReadIndicatorRects[rowIndex];
        const RenderRect& writeIndicatorRect = layoutState_.rowWriteIndicatorRects[rowIndex];
        const RenderRect& barRect = layoutState_.rowBarRects[rowIndex];

        const DashboardRenderer::TextLayoutResult labelLayout = renderer.DrawTextBlock(columns.label,
            drive.label,
            TextStyleId::Label,
            renderer.ForegroundColor(),
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
        renderer.RegisterDynamicTextAnchor(labelLayout,
            renderer.MakeEditableTextBinding(widget,
                DashboardRenderer::LayoutEditParameter::FontLabel,
                textBaseId,
                renderer.Config().layout.fonts.label.size),
            DashboardRenderer::LayoutEditParameter::ColorForeground);
        DrawSegmentIndicator(renderer,
            readIndicatorRect,
            layoutState_.activitySegments,
            layoutState_.activitySegmentGap,
            renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank ? 0.0 : drive.readActivity,
            renderer.TrackColor(),
            renderer.AccentColor());
        DrawSegmentIndicator(renderer,
            writeIndicatorRect,
            layoutState_.activitySegments,
            layoutState_.activitySegmentGap,
            renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank ? 0.0 : drive.writeActivity,
            renderer.TrackColor(),
            renderer.AccentColor());
        renderer.DrawPillBar(barRect,
            drive.usedPercent / 100.0,
            std::nullopt,
            renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank);
        const int splitX = barRect.left + ((std::max)(0, barRect.right - barRect.left) / 2);
        renderer.RegisterDynamicColorEditRegion(DashboardRenderer::LayoutEditParameter::ColorAccent,
            RenderRect{barRect.left, barRect.top, splitX, barRect.bottom});
        renderer.RegisterDynamicColorEditRegion(DashboardRenderer::LayoutEditParameter::ColorTrack,
            RenderRect{splitX, barRect.top, barRect.right, barRect.bottom});
        if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
            const DashboardRenderer::TextLayoutResult percentLayout = renderer.DrawTextBlock(columns.percent,
                drive.usedText,
                TextStyleId::Label,
                renderer.ForegroundColor(),
                TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
            renderer.RegisterDynamicTextAnchor(percentLayout,
                renderer.MakeEditableTextBinding(widget,
                    DashboardRenderer::LayoutEditParameter::FontLabel,
                    textBaseId + 1,
                    renderer.Config().layout.fonts.label.size),
                DashboardRenderer::LayoutEditParameter::ColorForeground);
            renderer.RegisterDynamicTextAnchor(
                percentLayout, renderer.MakeMetricTextBinding(widget, "drive.usage", textBaseId + 101));
            const DashboardRenderer::TextLayoutResult freeLayout = renderer.DrawTextBlock(columns.free,
                drive.freeText,
                TextStyleId::Small,
                renderer.MutedTextColor(),
                TextLayoutOptions::SingleLine(TextHorizontalAlign::Trailing, TextVerticalAlign::Center));
            renderer.RegisterDynamicTextAnchor(freeLayout,
                renderer.MakeEditableTextBinding(widget,
                    DashboardRenderer::LayoutEditParameter::FontSmall,
                    textBaseId + 2,
                    renderer.Config().layout.fonts.smallText.size),
                DashboardRenderer::LayoutEditParameter::ColorMutedText);
            renderer.RegisterDynamicTextAnchor(
                freeLayout, renderer.MakeMetricTextBinding(widget, "drive.free", textBaseId + 102));
        }
    }

    renderer.PopClipRect();
}

void DriveUsageListWidget::BuildStaticAnchors(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const auto& config = renderer.Config().layout.driveUsageList;
    renderer.RegisterStaticEditableAnchorRegion(
        LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::LayoutEditParameter::DriveUsageActivitySegments,
            0},
        layoutState_.activityTargetRect,
        layoutState_.activityAnchorRect,
        AnchorShape::Diamond,
        AnchorDragAxis::Both,
        AnchorDragMode::AxisDelta,
        RenderPoint{layoutState_.activityAnchorCenterX, layoutState_.firstRowContentTop},
        1.0,
        true,
        true,
        true,
        config.activitySegments);
    renderer.RegisterStaticTextAnchor(layoutState_.headerReadLabelRect,
        ResolveDriveMetricLabel(renderer, "drive.activity.read", "R"),
        TextStyleId::Small,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center, false),
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::LayoutEditParameter::FontSmall,
            0,
            renderer.Config().layout.fonts.smallText.size),
        DashboardRenderer::LayoutEditParameter::ColorMutedText);
    renderer.RegisterStaticTextAnchor(layoutState_.headerReadLabelRect,
        ResolveDriveMetricLabel(renderer, "drive.activity.read", "R"),
        TextStyleId::Small,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center, false),
        renderer.MakeMetricTextBinding(widget, "drive.activity.read", 100));
    renderer.RegisterStaticTextAnchor(layoutState_.headerWriteLabelRect,
        ResolveDriveMetricLabel(renderer, "drive.activity.write", "W"),
        TextStyleId::Small,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center, false),
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::LayoutEditParameter::FontSmall,
            1,
            renderer.Config().layout.fonts.smallText.size),
        DashboardRenderer::LayoutEditParameter::ColorMutedText);
    renderer.RegisterStaticTextAnchor(layoutState_.headerWriteLabelRect,
        ResolveDriveMetricLabel(renderer, "drive.activity.write", "W"),
        TextStyleId::Small,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center, false),
        renderer.MakeMetricTextBinding(widget, "drive.activity.write", 101));
    renderer.RegisterStaticTextAnchor(layoutState_.usageHeaderRect,
        ResolveDriveMetricLabel(renderer, "drive.usage", "Usage"),
        TextStyleId::Small,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center),
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::LayoutEditParameter::FontSmall,
            2,
            renderer.Config().layout.fonts.smallText.size),
        DashboardRenderer::LayoutEditParameter::ColorMutedText);
    renderer.RegisterStaticTextAnchor(layoutState_.usageHeaderRect,
        ResolveDriveMetricLabel(renderer, "drive.usage", "Usage"),
        TextStyleId::Small,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Center, TextVerticalAlign::Center),
        renderer.MakeMetricTextBinding(widget, "drive.usage", 102));
    renderer.RegisterStaticTextAnchor(layoutState_.headerColumns.free,
        ResolveDriveMetricLabel(renderer, "drive.free", "Free"),
        TextStyleId::Small,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Trailing, TextVerticalAlign::Center),
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::LayoutEditParameter::FontSmall,
            3,
            renderer.Config().layout.fonts.smallText.size),
        DashboardRenderer::LayoutEditParameter::ColorMutedText);
    renderer.RegisterStaticTextAnchor(layoutState_.headerColumns.free,
        ResolveDriveMetricLabel(renderer, "drive.free", "Free"),
        TextStyleId::Small,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Trailing, TextVerticalAlign::Center),
        renderer.MakeMetricTextBinding(widget, "drive.free", 103));
    for (int rowIndex = 0;
        rowIndex < layoutState_.visibleRows && rowIndex < static_cast<int>(layoutState_.rowBarRects.size()) &&
        rowIndex < static_cast<int>(layoutState_.rowBarAnchorRects.size());
        ++rowIndex) {
        const RenderRect& barRect = layoutState_.rowBarRects[rowIndex];
        const RenderRect& anchorRect = layoutState_.rowBarAnchorRects[rowIndex];
        const int anchorCenterX = anchorRect.left + ((std::max)(0, anchorRect.right - anchorRect.left) / 2);
        const int anchorCenterY = anchorRect.top + ((std::max)(0, anchorRect.bottom - anchorRect.top) / 2);
        renderer.RegisterStaticEditableAnchorRegion(
            LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                DashboardRenderer::LayoutEditParameter::DriveUsageBarHeight,
                rowIndex},
            barRect,
            anchorRect,
            AnchorShape::Circle,
            AnchorDragAxis::Horizontal,
            AnchorDragMode::AxisDelta,
            RenderPoint{anchorCenterX, anchorCenterY},
            1.0,
            true,
            false,
            true,
            config.barHeight);
    }
}

void DriveUsageListWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const auto& config = renderer.Config().layout.driveUsageList;
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
    const ColumnRects& columns = layoutState_.headerColumns;
    if (columns.percent.left < columns.write.right) {
        return;
    }

    auto& guides = renderer.WidgetEditGuidesMutable();
    const auto addVerticalGuide =
        [&](int guideId, int x, DashboardRenderer::LayoutEditParameter parameter, int value, int dragDirection) {
            const int clampedX = std::clamp(x, static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
            LayoutEditWidgetGuide guide;
            guide.axis = LayoutGuideAxis::Vertical;
            guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
            guide.parameter = parameter;
            guide.guideId = guideId;
            guide.widgetRect = widget.rect;
            guide.drawStart = RenderPoint{clampedX, widget.rect.top};
            guide.drawEnd = RenderPoint{clampedX, widget.rect.bottom};
            guide.hitRect =
                RenderRect{clampedX - hitInset, widget.rect.top, clampedX + hitInset + 1, widget.rect.bottom};
            guide.value = value;
            guide.dragDirection = dragDirection;
            guides.push_back(std::move(guide));
        };
    const auto addHorizontalGuide = [&](int guideId,
                                        int y,
                                        DashboardRenderer::LayoutEditParameter parameter,
                                        int value,
                                        int dragDirection,
                                        int left = (std::numeric_limits<int>::min)(),
                                        int right = (std::numeric_limits<int>::max)()) {
        const int clampedY = std::clamp(y, static_cast<int>(widget.rect.top), static_cast<int>(widget.rect.bottom));
        const int guideLeft = std::clamp(left, static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
        const int guideRight = std::clamp(right, guideLeft, static_cast<int>(widget.rect.right));
        LayoutEditWidgetGuide guide;
        guide.axis = LayoutGuideAxis::Horizontal;
        guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = parameter;
        guide.guideId = guideId;
        guide.widgetRect = widget.rect;
        guide.drawStart = RenderPoint{guideLeft, clampedY};
        guide.drawEnd = RenderPoint{guideRight, clampedY};
        guide.hitRect = RenderRect{guideLeft, clampedY - hitInset, guideRight, clampedY + hitInset + 1};
        guide.value = value;
        guide.dragDirection = dragDirection;
        guides.push_back(std::move(guide));
    };

    addVerticalGuide(
        0, columns.read.left, DashboardRenderer::LayoutEditParameter::DriveUsageLabelGap, config.labelGap, 1);
    addVerticalGuide(1, columns.write.left, DashboardRenderer::LayoutEditParameter::DriveUsageRwGap, config.rwGap, 1);
    addVerticalGuide(2, columns.bar.left, DashboardRenderer::LayoutEditParameter::DriveUsageBarGap, config.barGap, 1);
    addVerticalGuide(
        3, columns.bar.right, DashboardRenderer::LayoutEditParameter::DriveUsagePercentGap, config.percentGap, -1);
    addVerticalGuide(4,
        columns.write.right,
        DashboardRenderer::LayoutEditParameter::DriveUsageActivityWidth,
        config.activityWidth,
        1);
    addVerticalGuide(
        5, columns.free.left, DashboardRenderer::LayoutEditParameter::DriveUsageFreeWidth, config.freeWidth, -1);
    addHorizontalGuide(6,
        widget.rect.top + layoutState_.headerHeight,
        DashboardRenderer::LayoutEditParameter::DriveUsageHeaderGap,
        config.headerGap,
        1);
    if (layoutState_.visibleRows > 0 && config.activitySegments > 1) {
        const RenderRect activityBandRect{columns.read.left,
            layoutState_.firstRowContentTop,
            columns.write.right,
            layoutState_.firstRowContentTop + layoutState_.rowContentHeight};
        addHorizontalGuide(7,
            layoutState_.lowestSegmentTop,
            DashboardRenderer::LayoutEditParameter::DriveUsageActivitySegmentGap,
            config.activitySegmentGap,
            1,
            activityBandRect.left,
            activityBandRect.right);
    }
    for (int rowIndex = 0; rowIndex < layoutState_.visibleRows; ++rowIndex) {
        const int y = layoutState_.rowBands[rowIndex].bottom;
        addHorizontalGuide(8 + rowIndex, y, DashboardRenderer::LayoutEditParameter::DriveUsageRowGap, config.rowGap, 1);
    }
}
