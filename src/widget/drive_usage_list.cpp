#include "drive_usage_list.h"

#include <cmath>
#include <limits>

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

namespace {

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

void DrawSegmentIndicator(HDC hdc,
    const RECT& rect,
    int segmentCount,
    int segmentGap,
    double ratio,
    COLORREF trackColor,
    COLORREF accentColor) {
    const int width = (std::max)(0, static_cast<int>(rect.right - rect.left));
    const int height = (std::max)(0, static_cast<int>(rect.bottom - rect.top));
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
        RECT segmentRect{
            rect.left, segmentTop, rect.right, (std::min)(rect.bottom, static_cast<LONG>(segmentTop + visualHeight))};
        HBRUSH trackBrush = CreateSolidBrush(trackColor);
        FillRect(hdc, &segmentRect, trackBrush);
        DeleteObject(trackBrush);

        if (index < filledSegments) {
            HBRUSH fillBrush = CreateSolidBrush(accentColor);
            FillRect(hdc, &segmentRect, fillBrush);
            DeleteObject(fillBrush);
        }

        top = segmentRect.bottom + clampedGap;
    }
}

}  // namespace

const char* DriveUsageListWidget::TypeName() const {
    return "drive_usage_list";
}

void DriveUsageListWidget::Initialize(const LayoutNodeConfig&) {}

int DriveUsageListWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    const int count = static_cast<int>(renderer.Config().storage.drives.size());
    return (count > 0 ? renderer.EffectiveDriveHeaderHeight() : 0) + (count * renderer.EffectiveDriveRowHeight());
}

void DriveUsageListWidget::Draw(
    DashboardRenderer& renderer, HDC hdc, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const {
    const auto& config = renderer.Config().layout.driveUsageList;
    const int headerHeight = renderer.EffectiveDriveHeaderHeight();
    const int rowHeight = renderer.EffectiveDriveRowHeight();
    const int labelWidth = (std::max)(1, renderer.MeasuredTextWidths().driveLabel);
    const int percentWidth = (std::max)(1, renderer.MeasuredTextWidths().drivePercent);
    const int labelGap = (std::max)(0, renderer.ScaleLogical(config.labelGap));
    const int activityWidth = (std::max)(1, renderer.ScaleLogical(config.activityWidth));
    const int rwGap = (std::max)(0, renderer.ScaleLogical(config.rwGap));
    const int barGap = (std::max)(0, renderer.ScaleLogical(config.barGap));
    const int percentGap = (std::max)(0, renderer.ScaleLogical(config.percentGap));
    const int freeWidth = (std::max)(1, renderer.ScaleLogical(config.freeWidth));
    const int driveBarHeight = (std::max)(1, renderer.ScaleLogical(config.barHeight));
    const int activitySegments = (std::max)(1, config.activitySegments);
    const int activitySegmentGap = (std::max)(0, renderer.ScaleLogical(config.activitySegmentGap));
    const int rowContentHeight =
        (std::max)(renderer.FontMetrics().label, (std::max)(renderer.FontMetrics().smallText, driveBarHeight));

    const int savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, widget.rect.left, widget.rect.top, widget.rect.right, widget.rect.bottom);

    RECT header{widget.rect.left, widget.rect.top, widget.rect.right, widget.rect.top + headerHeight};
    RECT row{widget.rect.left, header.bottom, widget.rect.right, header.bottom + rowHeight};

    const auto resolveColumns = [&](const RECT& band,
                                    RECT& labelRect,
                                    RECT& readRect,
                                    RECT& writeRect,
                                    RECT& barRect,
                                    RECT& pctRect,
                                    RECT& freeRect) {
        labelRect = {band.left, band.top, (std::min)(band.right, static_cast<LONG>(band.left + labelWidth)), band.bottom};
        readRect = {(std::min)(band.right, static_cast<LONG>(labelRect.right + labelGap)),
            band.top,
            (std::min)(band.right, static_cast<LONG>(labelRect.right + labelGap + activityWidth)),
            band.bottom};
        writeRect = {(std::min)(band.right, static_cast<LONG>(readRect.right + rwGap)),
            band.top,
            (std::min)(band.right, static_cast<LONG>(readRect.right + rwGap + activityWidth)),
            band.bottom};
        freeRect = {(std::max)(band.left, static_cast<LONG>(band.right - freeWidth)), band.top, band.right, band.bottom};
        pctRect = {
            (std::max)(band.left, static_cast<LONG>(freeRect.left - percentWidth)), band.top, freeRect.left, band.bottom};
        barRect = {(std::min)(band.right, static_cast<LONG>(writeRect.right + barGap)),
            band.top,
            (std::max)((std::min)(band.right, static_cast<LONG>(writeRect.right + barGap)),
                static_cast<LONG>(pctRect.left - percentGap)),
            band.bottom};
    };

    RECT headerLabelRect{}, headerReadRect{}, headerWriteRect{}, headerBarRect{}, headerPctRect{}, headerFreeRect{};
    resolveColumns(header, headerLabelRect, headerReadRect, headerWriteRect, headerBarRect, headerPctRect, headerFreeRect);
    const int activityAnchorSize = (std::max)(8, renderer.ScaleLogical(10));
    const int activityAnchorCenterX =
        headerReadRect.left + ((std::max)(0L, headerWriteRect.right - headerReadRect.left) / 2);
    const int firstRowTop = (std::min)(static_cast<int>(widget.rect.bottom), static_cast<int>(header.bottom));
    const int firstRowBottom = (std::min)(static_cast<int>(widget.rect.bottom), static_cast<int>(header.bottom + rowHeight));
    const int firstRowContentTop = firstRowTop + (std::max)(0, ((firstRowBottom - firstRowTop) - rowContentHeight) / 2);
    renderer.RegisterEditableAnchorRegion(
        DashboardRenderer::EditableAnchorKey{
            DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
            DashboardRenderer::AnchorEditParameter::DriveUsageActivitySegments,
            0,
        },
        RECT{headerReadRect.left, widget.rect.top, headerWriteRect.right, widget.rect.bottom},
        RECT{activityAnchorCenterX - (activityAnchorSize / 2),
            firstRowContentTop - (activityAnchorSize / 2),
            activityAnchorCenterX - (activityAnchorSize / 2) + activityAnchorSize,
            firstRowContentTop - (activityAnchorSize / 2) + activityAnchorSize},
        DashboardRenderer::AnchorShape::Diamond,
        DashboardRenderer::AnchorDragAxis::Both,
        config.activitySegments);

    RECT usageHeaderRect{headerBarRect.left, header.top, headerPctRect.right, header.bottom};
    RECT headerReadLabelRect{
        headerReadRect.left - rwGap, headerReadRect.top, headerReadRect.right + rwGap, headerReadRect.bottom};
    RECT headerWriteLabelRect{
        headerWriteRect.left - rwGap, headerWriteRect.top, headerWriteRect.right + rwGap, headerWriteRect.bottom};
    renderer.DrawTextBlock(hdc,
        headerReadLabelRect,
        "R",
        renderer.WidgetFonts().smallFont,
        renderer.MutedTextColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP,
        renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontSmall, 0,
            renderer.Config().layout.fonts.smallText.size));
    renderer.DrawTextBlock(hdc,
        headerWriteLabelRect,
        "W",
        renderer.WidgetFonts().smallFont,
        renderer.MutedTextColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP,
        renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontSmall, 1,
            renderer.Config().layout.fonts.smallText.size));
    renderer.DrawTextBlock(hdc,
        usageHeaderRect,
        "Usage",
        renderer.WidgetFonts().smallFont,
        renderer.MutedTextColor(),
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
        renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontSmall, 2,
            renderer.Config().layout.fonts.smallText.size));
    renderer.DrawTextBlock(hdc,
        headerFreeRect,
        "Free",
        renderer.WidgetFonts().smallFont,
        renderer.MutedTextColor(),
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
        renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontSmall, 3,
            renderer.Config().layout.fonts.smallText.size));

    const auto rows = metrics.ResolveDriveRows();
    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const auto& drive = rows[rowIndex];
        const int textBaseId = 100 + static_cast<int>(rowIndex) * 3;
        RECT labelRect{}, readRect{}, writeRect{}, pctRect{}, freeRect{}, barBandRect{};
        resolveColumns(row, labelRect, readRect, writeRect, barBandRect, pctRect, freeRect);
        const int rowPixelHeight = static_cast<int>(row.bottom - row.top);
        const int contentTop = static_cast<int>(row.top) + (std::max)(0, (rowPixelHeight - rowContentHeight) / 2);
        RECT readIndicatorRect{readRect.left, contentTop, readRect.right, contentTop + rowContentHeight};
        RECT writeIndicatorRect{writeRect.left, contentTop, writeRect.right, contentTop + rowContentHeight};
        const int barTop = static_cast<int>(row.top) + (std::max)(0, (rowPixelHeight - driveBarHeight) / 2);
        RECT barRect{barBandRect.left, barTop, barBandRect.right, barTop + driveBarHeight};

        renderer.DrawTextBlock(hdc,
            labelRect,
            drive.label,
            renderer.WidgetFonts().label,
            renderer.ForegroundColor(),
            DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontLabel, textBaseId,
                renderer.Config().layout.fonts.label.size));
        DrawSegmentIndicator(hdc,
            readIndicatorRect,
            activitySegments,
            activitySegmentGap,
            renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank ? 0.0 : drive.readActivity,
            renderer.TrackColor(),
            renderer.AccentColor());
        DrawSegmentIndicator(hdc,
            writeIndicatorRect,
            activitySegments,
            activitySegmentGap,
            renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank ? 0.0 : drive.writeActivity,
            renderer.TrackColor(),
            renderer.AccentColor());
        renderer.DrawPillBar(
            hdc, barRect, drive.usedPercent / 100.0, std::nullopt, renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank);
        const int anchorSize = (std::max)(4, renderer.ScaleLogical(6));
        const int anchorCenterX =
            static_cast<int>(barRect.left) + ((std::max)(0, static_cast<int>(barRect.right - barRect.left) / 2));
        const int anchorCenterY = static_cast<int>(barRect.bottom);
        renderer.RegisterEditableAnchorRegion(
            DashboardRenderer::EditableAnchorKey{
                DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                DashboardRenderer::AnchorEditParameter::DriveUsageBarHeight,
                static_cast<int>(rowIndex),
            },
            barRect,
            RECT{anchorCenterX - (anchorSize / 2),
                anchorCenterY - (anchorSize / 2),
                anchorCenterX - (anchorSize / 2) + anchorSize,
                anchorCenterY - (anchorSize / 2) + anchorSize},
            DashboardRenderer::AnchorShape::Circle,
            DashboardRenderer::AnchorDragAxis::Horizontal,
            config.barHeight);

        if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
            char percent[16];
            sprintf_s(percent, "%.0f%%", drive.usedPercent);
            renderer.DrawTextBlock(hdc,
                pctRect,
                percent,
                renderer.WidgetFonts().label,
                renderer.ForegroundColor(),
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
                renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontLabel, textBaseId + 1,
                    renderer.Config().layout.fonts.label.size));
            renderer.DrawTextBlock(hdc,
                freeRect,
                drive.freeText,
                renderer.WidgetFonts().smallFont,
                renderer.MutedTextColor(),
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
                renderer.MakeEditableTextBinding(widget, DashboardRenderer::AnchorEditParameter::FontSmall, textBaseId + 2,
                    renderer.Config().layout.fonts.smallText.size));
        }

        OffsetRect(&row, 0, rowHeight);
        if (row.top >= widget.rect.bottom) {
            break;
        }
    }

    RestoreDC(hdc, savedDc);
}

void DriveUsageListWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const auto& config = renderer.Config().layout.driveUsageList;
    const int headerHeight = renderer.EffectiveDriveHeaderHeight();
    const int rowHeight = renderer.EffectiveDriveRowHeight();
    const int labelWidth = (std::max)(1, renderer.MeasuredTextWidths().driveLabel);
    const int percentWidth = (std::max)(1, renderer.MeasuredTextWidths().drivePercent);
    const int labelGap = (std::max)(0, renderer.ScaleLogical(config.labelGap));
    const int activityWidth = (std::max)(1, renderer.ScaleLogical(config.activityWidth));
    const int rwGap = (std::max)(0, renderer.ScaleLogical(config.rwGap));
    const int barGap = (std::max)(0, renderer.ScaleLogical(config.barGap));
    const int freeWidth = (std::max)(1, renderer.ScaleLogical(config.freeWidth));
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
    const int totalRows = static_cast<int>(renderer.Config().storage.drives.size());
    const int availableRowPixels = (std::max)(0, static_cast<int>(widget.rect.bottom - widget.rect.top) - headerHeight);
    const int visibleRows = rowHeight > 0 ? std::clamp((availableRowPixels + rowHeight - 1) / rowHeight, 0, totalRows) : 0;

    RECT labelRect{widget.rect.left,
        widget.rect.top,
        (std::min)(widget.rect.right, static_cast<LONG>(widget.rect.left + labelWidth)),
        widget.rect.bottom};
    RECT readRect{(std::min)(widget.rect.right, static_cast<LONG>(labelRect.right + labelGap)),
        widget.rect.top,
        (std::min)(widget.rect.right, static_cast<LONG>(labelRect.right + labelGap + activityWidth)),
        widget.rect.bottom};
    RECT writeRect{(std::min)(widget.rect.right, static_cast<LONG>(readRect.right + rwGap)),
        widget.rect.top,
        (std::min)(widget.rect.right, static_cast<LONG>(readRect.right + rwGap + activityWidth)),
        widget.rect.bottom};
    RECT freeRect{(std::max)(widget.rect.left, static_cast<LONG>(widget.rect.right - freeWidth)),
        widget.rect.top,
        widget.rect.right,
        widget.rect.bottom};
    RECT pctRect{(std::max)(widget.rect.left, static_cast<LONG>(freeRect.left - percentWidth)),
        widget.rect.top,
        freeRect.left,
        widget.rect.bottom};
    RECT barRect{(std::min)(widget.rect.right, static_cast<LONG>(writeRect.right + barGap)),
        widget.rect.top,
        (std::max)((std::min)(widget.rect.right, static_cast<LONG>(writeRect.right + barGap)),
            static_cast<LONG>(pctRect.left - renderer.ScaleLogical(config.percentGap))),
        widget.rect.bottom};
    if (pctRect.left < writeRect.right) {
        return;
    }

    auto& guides = renderer.WidgetEditGuidesMutable();
    const auto addVerticalGuide = [&](int guideId, int x, DashboardRenderer::WidgetEditParameter parameter, int value, int dragDirection) {
        const int clampedX = std::clamp(x, static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
        DashboardRenderer::WidgetEditGuide guide;
        guide.axis = DashboardRenderer::LayoutGuideAxis::Vertical;
        guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = parameter;
        guide.guideId = guideId;
        guide.widgetRect = widget.rect;
        guide.drawStart = POINT{clampedX, widget.rect.top};
        guide.drawEnd = POINT{clampedX, widget.rect.bottom};
        guide.hitRect = RECT{clampedX - hitInset, widget.rect.top, clampedX + hitInset + 1, widget.rect.bottom};
        guide.value = value;
        guide.dragDirection = dragDirection;
        guides.push_back(std::move(guide));
    };
    const auto addHorizontalGuide = [&](int guideId,
                                        int y,
                                        DashboardRenderer::WidgetEditParameter parameter,
                                        int value,
                                        int dragDirection,
                                        int left = (std::numeric_limits<int>::min)(),
                                        int right = (std::numeric_limits<int>::max)()) {
        const int clampedY = std::clamp(y, static_cast<int>(widget.rect.top), static_cast<int>(widget.rect.bottom));
        const int guideLeft = std::clamp(left, static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
        const int guideRight = std::clamp(right, guideLeft, static_cast<int>(widget.rect.right));
        DashboardRenderer::WidgetEditGuide guide;
        guide.axis = DashboardRenderer::LayoutGuideAxis::Horizontal;
        guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = parameter;
        guide.guideId = guideId;
        guide.widgetRect = widget.rect;
        guide.drawStart = POINT{guideLeft, clampedY};
        guide.drawEnd = POINT{guideRight, clampedY};
        guide.hitRect = RECT{guideLeft, clampedY - hitInset, guideRight, clampedY + hitInset + 1};
        guide.value = value;
        guide.dragDirection = dragDirection;
        guides.push_back(std::move(guide));
    };

    addVerticalGuide(0, readRect.left, DashboardRenderer::WidgetEditParameter::DriveUsageLabelGap, config.labelGap, 1);
    addVerticalGuide(1, writeRect.left, DashboardRenderer::WidgetEditParameter::DriveUsageRwGap, config.rwGap, 1);
    addVerticalGuide(2, barRect.left, DashboardRenderer::WidgetEditParameter::DriveUsageBarGap, config.barGap, 1);
    addVerticalGuide(3, barRect.right, DashboardRenderer::WidgetEditParameter::DriveUsagePercentGap, config.percentGap, -1);
    addVerticalGuide(4, writeRect.right, DashboardRenderer::WidgetEditParameter::DriveUsageActivityWidth, config.activityWidth, 1);
    addVerticalGuide(5, freeRect.left, DashboardRenderer::WidgetEditParameter::DriveUsageFreeWidth, config.freeWidth, -1);
    addHorizontalGuide(6,
        widget.rect.top + headerHeight,
        DashboardRenderer::WidgetEditParameter::DriveUsageHeaderGap,
        config.headerGap,
        1);
    if (visibleRows > 0 && config.activitySegments > 1) {
        const int rowContentHeight =
            (std::max)(renderer.FontMetrics().label,
                (std::max)(renderer.FontMetrics().smallText, renderer.ScaleLogical(config.barHeight)));
        const int activitySegmentGap =
            ClampStackedSegmentGap(rowContentHeight, config.activitySegments, renderer.ScaleLogical(config.activitySegmentGap));
        const int contentTop = widget.rect.top + headerHeight + (std::max)(0, (rowHeight - rowContentHeight) / 2);
        const RECT activityBandRect{readRect.left, contentTop, writeRect.right, contentTop + rowContentHeight};
        const int lowestSegmentTop = ComputeLowestStackedSegmentTop(
            activityBandRect.top, rowContentHeight, activityWidth, config.activitySegments, activitySegmentGap);
        addHorizontalGuide(7,
            lowestSegmentTop,
            DashboardRenderer::WidgetEditParameter::DriveUsageActivitySegmentGap,
            config.activitySegmentGap,
            1,
            activityBandRect.left,
            activityBandRect.right);
    }
    for (int rowIndex = 0; rowIndex < visibleRows; ++rowIndex) {
        const int y = widget.rect.top + headerHeight + ((rowIndex + 1) * rowHeight);
        addHorizontalGuide(8 + rowIndex,
            y,
            DashboardRenderer::WidgetEditParameter::DriveUsageRowGap,
            config.rowGap,
            1);
    }
}
