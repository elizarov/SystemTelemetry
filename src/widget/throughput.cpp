#include "throughput.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

namespace {

COLORREF ToColorRef(unsigned int color) {
    return RGB((color >> 16) & 0xFFu, (color >> 8) & 0xFFu, color & 0xFFu);
}

void FillCircle(HDC hdc, int centerX, int centerY, int diameter, COLORREF color, BYTE alpha) {
    const int clampedDiameter = std::max(1, diameter);
    const int radius = clampedDiameter / 2;
    HBRUSH brush = CreateSolidBrush(
        RGB((GetRValue(color) * alpha) / 255, (GetGValue(color) * alpha) / 255, (GetBValue(color) * alpha) / 255));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc,
        centerX - radius,
        centerY - radius,
        centerX - radius + clampedDiameter,
        centerY - radius + clampedDiameter);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(brush);
}

void DrawGraph(DashboardRenderer& renderer,
    HDC hdc,
    const RECT& rect,
    const std::vector<double>& history,
    double maxValue,
    double guideStepMbps,
    double timeMarkerOffsetSamples,
    double timeMarkerIntervalSamples,
    const std::optional<DashboardRenderer::EditableAnchorBinding>& maxLabelEditable) {
    HBRUSH bg = CreateSolidBrush(ToColorRef(renderer.Config().layout.colors.graphBackgroundColor));
    FillRect(hdc, &rect, bg);
    DeleteObject(bg);

    const int axisWidth = std::max(1, renderer.MeasuredTextWidths().throughputAxis);
    const int labelBandHeight =
        renderer.FontMetrics().smallText +
        std::max(0, renderer.ScaleLogical(renderer.Config().layout.throughput.scaleLabelPadding));
    const int graphTop = std::min(rect.bottom - 1, rect.top + labelBandHeight);
    const int graphLeft = rect.left + axisWidth;
    const int leaderDiameter = std::max(0, renderer.ScaleLogical(renderer.Config().layout.throughput.leaderDiameter));
    const int leaderRadius = leaderDiameter / 2;
    const int width = std::max<int>(1, rect.right - graphLeft - 1 - leaderRadius);
    const int graphRight = graphLeft + width;
    const int graphBottom = rect.bottom - 1;
    const int plotStrokeWidth = std::max(1, renderer.ScaleLogical(renderer.Config().layout.throughput.plotStrokeWidth));
    const int plotTop = std::min(graphBottom, static_cast<int>(rect.top) + plotStrokeWidth);
    const int plotHeight = std::max(1, graphBottom - plotTop);

    const int strokeWidth = std::max(1, renderer.ScaleLogical(renderer.Config().layout.throughput.guideStrokeWidth));
    const double guideStep = guideStepMbps > 0.0 ? guideStepMbps : 5.0;
    HBRUSH markerBrush = CreateSolidBrush(ToColorRef(renderer.Config().layout.colors.graphMarkerColor));
    for (double tick = guideStep; tick < maxValue; tick += guideStep) {
        const double ratio = tick / maxValue;
        const int y = graphBottom - static_cast<int>(std::round(ratio * plotHeight));
        RECT lineRect{graphLeft, std::max(plotTop, y), graphRight, std::min(graphBottom + 1, y + strokeWidth)};
        FillRect(hdc, &lineRect, markerBrush);
    }

    if (!history.empty()) {
        const double markerInterval = timeMarkerIntervalSamples > 0.0 ? timeMarkerIntervalSamples : 20.0;
        for (double sampleOffset = timeMarkerOffsetSamples;
            sampleOffset <= static_cast<double>(history.size() - 1) + markerInterval;
            sampleOffset += markerInterval) {
            const double clampedOffset = std::clamp(sampleOffset, 0.0, static_cast<double>(history.size() - 1));
            const int x = graphRight -
                          static_cast<int>(std::round(clampedOffset * width / std::max<size_t>(1, history.size() - 1)));
            RECT lineRect{x, rect.top, std::min(graphRight + 1, x + strokeWidth), rect.bottom};
            FillRect(hdc, &lineRect, markerBrush);
        }
    }

    DeleteObject(markerBrush);

    HBRUSH axisBrush = CreateSolidBrush(ToColorRef(renderer.Config().layout.colors.graphAxisColor));
    RECT verticalAxisRect{rect.left + axisWidth, rect.top, rect.left + axisWidth + strokeWidth, rect.bottom};
    RECT horizontalAxisRect{rect.left + axisWidth, rect.bottom - strokeWidth, rect.right, rect.bottom};
    FillRect(hdc, &verticalAxisRect, axisBrush);
    FillRect(hdc, &horizontalAxisRect, axisBrush);
    DeleteObject(axisBrush);

    char maxLabel[32];
    sprintf_s(maxLabel, "%.0f", maxValue);
    RECT maxRect{rect.left, rect.top, rect.left + axisWidth, graphTop};
    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
        renderer.DrawTextBlock(hdc,
            maxRect,
            maxLabel,
            renderer.WidgetFonts().smallFont,
            renderer.MutedTextColor(),
            DT_CENTER | DT_SINGLELINE | DT_VCENTER,
            maxLabelEditable);
    }

    if (renderer.CurrentRenderMode() == DashboardRenderer::RenderMode::Blank) {
        return;
    }

    const COLORREF plotColor = renderer.AccentColor();
    HPEN pen = CreatePen(PS_SOLID, plotStrokeWidth, plotColor);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    POINT lastPoint{graphLeft, graphBottom};
    bool hasLastPoint = false;
    if (!history.empty()) {
        const size_t historyDenominator = std::max<size_t>(1, history.size() - 1);
        for (size_t i = 0; i < history.size(); ++i) {
            const double valueRatio = std::clamp(history[i] / maxValue, 0.0, 1.0);
            const int x = graphLeft + static_cast<int>(i * width / historyDenominator);
            const int y = graphBottom - static_cast<int>(std::round(valueRatio * plotHeight));
            lastPoint = POINT{x, y};
            hasLastPoint = true;
        }
    }
    for (size_t i = 1; i < history.size(); ++i) {
        const double v1 = std::clamp(history[i - 1] / maxValue, 0.0, 1.0);
        const double v2 = std::clamp(history[i] / maxValue, 0.0, 1.0);
        const int x1 = graphLeft + static_cast<int>((i - 1) * width / std::max<size_t>(1, history.size() - 1));
        const int x2 = graphLeft + static_cast<int>(i * width / std::max<size_t>(1, history.size() - 1));
        const int y1 = graphBottom - static_cast<int>(std::round(v1 * plotHeight));
        const int y2 = graphBottom - static_cast<int>(std::round(v2 * plotHeight));
        MoveToEx(hdc, x1, y1, nullptr);
        LineTo(hdc, x2, y2);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    if (hasLastPoint && leaderDiameter > 0) {
        FillCircle(hdc, lastPoint.x, lastPoint.y, leaderDiameter, plotColor, 255);
    }
}

int EffectiveThroughputPreferredHeight(const DashboardRenderer& renderer) {
    const int headerHeight = renderer.FontMetrics().smallText;
    const int graphLabelHeight =
        renderer.FontMetrics().smallText +
        std::max(0, renderer.ScaleLogical(renderer.Config().layout.throughput.scaleLabelPadding));
    return headerHeight + std::max(0, renderer.ScaleLogical(renderer.Config().layout.throughput.headerGap)) +
           graphLabelHeight;
}

}  // namespace

DashboardWidgetClass ThroughputWidget::Class() const {
    return DashboardWidgetClass::Throughput;
}

std::unique_ptr<DashboardWidget> ThroughputWidget::Clone() const {
    return std::make_unique<ThroughputWidget>(*this);
}

void ThroughputWidget::Initialize(const LayoutNodeConfig& node) {
    metric_ = node.parameter;
}

int ThroughputWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return EffectiveThroughputPreferredHeight(renderer);
}

void ThroughputWidget::Draw(DashboardRenderer& renderer,
    HDC hdc,
    const DashboardWidgetLayout& widget,
    const DashboardMetricSource& metrics) const {
    const DashboardThroughputMetric metric = metrics.ResolveThroughput(metric_);
    const int lineHeight = renderer.FontMetrics().smallText;
    RECT valueRect{widget.rect.left,
        widget.rect.top,
        widget.rect.right,
        (std::min)(widget.rect.bottom, widget.rect.top + lineHeight)};
    RECT graphRect{widget.rect.left,
        (std::min)(widget.rect.bottom,
            valueRect.bottom + (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.throughput.headerGap))),
        widget.rect.right,
        widget.rect.bottom};
    char buffer[64];
    if (metric.valueMbps >= 100.0) {
        sprintf_s(buffer, "%.0f MB/s", metric.valueMbps);
    } else {
        sprintf_s(buffer, "%.1f MB/s", metric.valueMbps);
    }
    const DashboardRenderer::TextLayoutResult labelLayout = renderer.DrawTextBlock(hdc,
        valueRect,
        metric.label,
        renderer.WidgetFonts().smallFont,
        renderer.MutedTextColor(),
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::AnchorEditParameter::FontSmall,
            0,
            renderer.Config().layout.fonts.smallText.size));
    RECT numberRect{(std::min)(valueRect.right,
                        labelLayout.textRect.right +
                            (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.throughput.headerGap))),
        valueRect.top,
        valueRect.right,
        valueRect.bottom};
    if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
        renderer.DrawTextBlock(hdc,
            numberRect,
            buffer,
            renderer.WidgetFonts().smallFont,
            renderer.ForegroundColor(),
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
            renderer.MakeEditableTextBinding(widget,
                DashboardRenderer::AnchorEditParameter::FontSmall,
                1,
                renderer.Config().layout.fonts.smallText.size));
    }
    DrawGraph(renderer,
        hdc,
        graphRect,
        metric.history,
        metric.maxGraph,
        metric.guideStepMbps,
        metric.timeMarkerOffsetSamples,
        metric.timeMarkerIntervalSamples,
        renderer.MakeEditableTextBinding(widget,
            DashboardRenderer::AnchorEditParameter::FontSmall,
            2,
            renderer.Config().layout.fonts.smallText.size));
}

void ThroughputWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int lineHeight = renderer.FontMetrics().smallText;
    RECT valueRect{widget.rect.left,
        widget.rect.top,
        widget.rect.right,
        (std::min)(widget.rect.bottom, widget.rect.top + lineHeight)};
    RECT graphRect{widget.rect.left,
        (std::min)(widget.rect.bottom,
            valueRect.bottom + (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.throughput.headerGap))),
        widget.rect.right,
        widget.rect.bottom};
    const int axisWidth = (std::max)(1, renderer.MeasuredTextWidths().throughputAxis);
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));

    auto& guides = renderer.WidgetEditGuidesMutable();
    DashboardRenderer::WidgetEditGuide guide;
    const int x = std::clamp(static_cast<int>(graphRect.left) + axisWidth,
        static_cast<int>(widget.rect.left),
        static_cast<int>(widget.rect.right));
    guide.axis = DashboardRenderer::LayoutGuideAxis::Vertical;
    guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = DashboardRenderer::WidgetEditParameter::ThroughputAxisPadding;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = POINT{x, graphRect.top};
    guide.drawEnd = POINT{x, graphRect.bottom};
    guide.hitRect = RECT{x - hitInset, graphRect.top, x + hitInset + 1, graphRect.bottom};
    guide.value = renderer.Config().layout.throughput.axisPadding;
    guide.dragDirection = 1;
    guides.push_back(guide);

    const int y = std::clamp(
        static_cast<int>(graphRect.top), static_cast<int>(widget.rect.top), static_cast<int>(widget.rect.bottom));
    guide = {};
    guide.axis = DashboardRenderer::LayoutGuideAxis::Horizontal;
    guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = DashboardRenderer::WidgetEditParameter::ThroughputHeaderGap;
    guide.guideId = 1;
    guide.widgetRect = widget.rect;
    guide.drawStart = POINT{widget.rect.left, y};
    guide.drawEnd = POINT{widget.rect.right, y};
    guide.hitRect = RECT{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
    guide.value = renderer.Config().layout.throughput.headerGap;
    guide.dragDirection = 1;
    guides.push_back(std::move(guide));
}
