#include "metric_list.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "../dashboard_metrics.h"
#include "../dashboard_renderer.h"

namespace {

std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::string(first, last);
}

MetricListWidget::Entry ParseMetricListEntry(std::string item) {
    MetricListWidget::Entry entry;
    const size_t equals = item.find('=');
    if (equals == std::string::npos) {
        entry.metricRef = Trim(item);
        return entry;
    }
    entry.metricRef = Trim(item.substr(0, equals));
    entry.labelOverride = Trim(item.substr(equals + 1));
    return entry;
}

int EffectiveMetricRowHeight(const DashboardRenderer& renderer) {
    const int textHeight = std::max(renderer.FontMetrics().label, renderer.FontMetrics().value);
    const int barHeight = std::max(1, renderer.ScaleLogical(renderer.Config().layout.metricList.barHeight));
    const int verticalGap = std::max(0, renderer.ScaleLogical(renderer.Config().layout.metricList.verticalGap));
    return textHeight + verticalGap + barHeight;
}

}  // namespace

DashboardWidgetClass MetricListWidget::Class() const {
    return DashboardWidgetClass::MetricList;
}

std::unique_ptr<DashboardWidget> MetricListWidget::Clone() const {
    return std::make_unique<MetricListWidget>(*this);
}

void MetricListWidget::Initialize(const LayoutNodeConfig& node) {
    entries_.clear();
    std::stringstream stream(node.parameter);
    std::string item;
    while (std::getline(stream, item, ',')) {
        Entry entry = ParseMetricListEntry(item);
        if (!entry.metricRef.empty()) {
            entries_.push_back(std::move(entry));
        }
    }
}

int MetricListWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return static_cast<int>(entries_.size()) * EffectiveMetricRowHeight(renderer);
}

void MetricListWidget::Draw(DashboardRenderer& renderer,
    HDC hdc,
    const DashboardWidgetLayout& widget,
    const DashboardMetricSource& metrics) const {
    const int rowHeight = EffectiveMetricRowHeight(renderer);
    const int savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, widget.rect.left, widget.rect.top, widget.rect.right, widget.rect.bottom);
    RECT rowRect{widget.rect.left, widget.rect.top, widget.rect.right, widget.rect.top + rowHeight};
    int rowIndex = 0;
    std::vector<DashboardMetricListEntry> entries;
    entries.reserve(entries_.size());
    for (const auto& entry : entries_) {
        entries.push_back(DashboardMetricListEntry{entry.metricRef, entry.labelOverride});
    }
    for (const auto& row : metrics.ResolveMetricList(entries)) {
        const int labelWidth = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.metricList.labelWidth));
        RECT labelRect{rowRect.left, rowRect.top, (std::min)(rowRect.right, rowRect.left + labelWidth), rowRect.bottom};
        RECT valueRect{labelRect.right, rowRect.top, rowRect.right, rowRect.bottom};
        renderer.DrawTextBlock(hdc,
            labelRect,
            row.label,
            renderer.WidgetFonts().label,
            renderer.MutedTextColor(),
            DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            renderer.MakeEditableTextBinding(widget,
                DashboardRenderer::AnchorEditParameter::FontLabel,
                rowIndex * 2,
                renderer.Config().layout.fonts.label.size));
        if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
            renderer.DrawTextBlock(hdc,
                valueRect,
                row.valueText,
                renderer.WidgetFonts().value,
                renderer.ForegroundColor(),
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
                renderer.MakeEditableTextBinding(widget,
                    DashboardRenderer::AnchorEditParameter::FontValue,
                    rowIndex * 2 + 1,
                    renderer.Config().layout.fonts.value.size));
        }

        const int metricBarHeight = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.metricList.barHeight));
        const int barBottom = (std::min)(static_cast<int>(rowRect.bottom), static_cast<int>(rowRect.top) + rowHeight);
        const int barTop = (std::max)(static_cast<int>(rowRect.top), barBottom - metricBarHeight);
        RECT barRect{valueRect.left, barTop, rowRect.right, barBottom};
        renderer.DrawPillBar(hdc,
            barRect,
            row.ratio,
            row.peakRatio,
            renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank);
        const int anchorSize = (std::max)(4, renderer.ScaleLogical(6));
        const int anchorCenterX =
            static_cast<int>(barRect.left) + ((std::max)(0, static_cast<int>(barRect.right - barRect.left) / 2));
        const int anchorCenterY = static_cast<int>(barRect.bottom);
        renderer.RegisterEditableAnchorRegion(
            DashboardRenderer::EditableAnchorKey{
                DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                DashboardRenderer::AnchorEditParameter::MetricListBarHeight,
                rowIndex,
            },
            barRect,
            RECT{anchorCenterX - (anchorSize / 2),
                anchorCenterY - (anchorSize / 2),
                anchorCenterX - (anchorSize / 2) + anchorSize,
                anchorCenterY - (anchorSize / 2) + anchorSize},
            DashboardRenderer::AnchorShape::Circle,
            DashboardRenderer::AnchorDragAxis::Horizontal,
            renderer.Config().layout.metricList.barHeight);

        ++rowIndex;
        OffsetRect(&rowRect, 0, rowHeight);
        if (rowRect.top >= widget.rect.bottom) {
            break;
        }
    }
    RestoreDC(hdc, savedDc);
}

void MetricListWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int labelWidth = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.metricList.labelWidth));
    const int rowHeight = EffectiveMetricRowHeight(renderer);
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
    const int x = std::clamp(static_cast<int>(widget.rect.left) + labelWidth,
        static_cast<int>(widget.rect.left),
        static_cast<int>(widget.rect.right));
    const int visibleRows =
        rowHeight > 0
            ? std::clamp(
                  ((std::max)(0, static_cast<int>(widget.rect.bottom - widget.rect.top)) + rowHeight - 1) / rowHeight,
                  0,
                  static_cast<int>(entries_.size()))
            : 0;

    auto& guides = renderer.WidgetEditGuidesMutable();
    DashboardRenderer::WidgetEditGuide guide;
    guide.axis = DashboardRenderer::LayoutGuideAxis::Vertical;
    guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = DashboardRenderer::WidgetEditParameter::MetricListLabelWidth;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = POINT{x, widget.rect.top};
    guide.drawEnd = POINT{x, widget.rect.bottom};
    guide.hitRect = RECT{x - hitInset, widget.rect.top, x + hitInset + 1, widget.rect.bottom};
    guide.value = renderer.Config().layout.metricList.labelWidth;
    guide.dragDirection = 1;
    guides.push_back(guide);

    for (int rowIndex = 0; rowIndex < visibleRows; ++rowIndex) {
        const int y = widget.rect.top + ((rowIndex + 1) * rowHeight);
        guide = {};
        guide.axis = DashboardRenderer::LayoutGuideAxis::Horizontal;
        guide.widget = DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = DashboardRenderer::WidgetEditParameter::MetricListVerticalGap;
        guide.guideId = 1 + rowIndex;
        guide.widgetRect = widget.rect;
        guide.drawStart = POINT{widget.rect.left, y};
        guide.drawEnd = POINT{widget.rect.right, y};
        guide.hitRect = RECT{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
        guide.value = renderer.Config().layout.metricList.verticalGap;
        guide.dragDirection = 1;
        guides.push_back(std::move(guide));
    }
}
