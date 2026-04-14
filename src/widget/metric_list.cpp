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
    const int valueHeight = renderer.TextMetrics().value;
    const int barHeight = std::max(1, renderer.ScaleLogical(renderer.Config().layout.metricList.barHeight));
    const int rowGap = std::max(0, renderer.ScaleLogical(renderer.Config().layout.metricList.rowGap));
    return valueHeight + rowGap + barHeight;
}

std::string ResolveMetricListLabel(const MetricListWidget::Entry& entry) {
    if (!entry.labelOverride.empty()) {
        return entry.labelOverride;
    }
    if (entry.metricRef == "cpu.clock") {
        return "Clock";
    }
    if (entry.metricRef == "cpu.ram") {
        return "RAM";
    }
    if (entry.metricRef == "gpu.temp") {
        return "Temp";
    }
    if (entry.metricRef == "gpu.clock") {
        return "Clock";
    }
    if (entry.metricRef == "gpu.fan") {
        return "Fan";
    }
    if (entry.metricRef == "gpu.vram") {
        return "VRAM";
    }
    if (entry.metricRef.rfind("board.temp.", 0) == 0) {
        const std::string name = entry.metricRef.substr(std::string("board.temp.").size());
        return name.empty() ? "Temp" : name + " Temp";
    }
    if (entry.metricRef.rfind("board.fan.", 0) == 0) {
        const std::string name = entry.metricRef.substr(std::string("board.fan.").size());
        return name.empty() ? "Fan" : name + " Fan";
    }
    return {};
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
    metricRefs_.clear();
    std::stringstream stream(node.parameter);
    std::string item;
    while (std::getline(stream, item, ',')) {
        Entry entry = ParseMetricListEntry(item);
        if (!entry.metricRef.empty()) {
            metricRefs_.push_back(DashboardMetricListEntry{entry.metricRef, entry.labelOverride});
            entries_.push_back(std::move(entry));
        }
    }
}

int MetricListWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return static_cast<int>(entries_.size()) * EffectiveMetricRowHeight(renderer);
}

void MetricListWidget::ResolveLayoutState(const DashboardRenderer& renderer, const RenderRect& rect) {
    layoutState_ = {};
    layoutState_.rowHeight = EffectiveMetricRowHeight(renderer);
    layoutState_.labelWidth = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.metricList.labelWidth));
    layoutState_.metricBarHeight = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.metricList.barHeight));
    layoutState_.anchorSize = (std::max)(4, renderer.ScaleLogical(6));
    const int valueHeight = renderer.TextMetrics().value;
    const int rowContentHeight = valueHeight + layoutState_.metricBarHeight;
    layoutState_.visibleRows =
        layoutState_.rowHeight > 0
            ? std::clamp(((std::max)(0, rect.bottom - rect.top) + layoutState_.rowHeight - 1) / layoutState_.rowHeight,
                  0,
                  static_cast<int>(entries_.size()))
            : 0;
    layoutState_.rowRects.clear();
    layoutState_.labelRects.clear();
    layoutState_.valueRects.clear();
    layoutState_.barRects.clear();
    layoutState_.barAnchorRects.clear();
    RenderRect rowRect{rect.left, rect.top, rect.right, rect.top + layoutState_.rowHeight};
    for (int rowIndex = 0; rowIndex < layoutState_.visibleRows; ++rowIndex) {
        layoutState_.rowRects.push_back(rowRect);
        RenderRect labelRect{rowRect.left,
            rowRect.top,
            (std::min)(rowRect.right, rowRect.left + layoutState_.labelWidth),
            rowRect.bottom};
        const int contentTop =
            static_cast<int>(rowRect.top) + (std::max)(0, (layoutState_.rowHeight - rowContentHeight) / 2);
        RenderRect valueRect{labelRect.right, contentTop, rowRect.right, contentTop + valueHeight};
        const int barTop = valueRect.bottom;
        const int barBottom = barTop + layoutState_.metricBarHeight;
        layoutState_.labelRects.push_back(labelRect);
        layoutState_.valueRects.push_back(valueRect);
        layoutState_.barRects.push_back(RenderRect{valueRect.left, barTop, rowRect.right, barBottom});
        const int anchorCenterX =
            static_cast<int>(valueRect.left) + ((std::max)(0, static_cast<int>(rowRect.right - valueRect.left) / 2));
        const int anchorCenterY = barBottom;
        layoutState_.barAnchorRects.push_back(RenderRect{anchorCenterX - (layoutState_.anchorSize / 2),
            anchorCenterY - (layoutState_.anchorSize / 2),
            anchorCenterX - (layoutState_.anchorSize / 2) + layoutState_.anchorSize,
            anchorCenterY - (layoutState_.anchorSize / 2) + layoutState_.anchorSize});
        rowRect = RenderRect{
            rowRect.left,
            rowRect.top + layoutState_.rowHeight,
            rowRect.right,
            rowRect.bottom + layoutState_.rowHeight,
        };
    }
}

void MetricListWidget::Draw(
    DashboardRenderer& renderer, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const {
    renderer.PushClipRect(widget.rect);
    int rowIndex = 0;
    for (const auto& row : metrics.ResolveMetricList(metricRefs_)) {
        if (rowIndex >= static_cast<int>(layoutState_.rowRects.size())) {
            break;
        }
        const RenderRect& labelRect = layoutState_.labelRects[rowIndex];
        const RenderRect& valueRect = layoutState_.valueRects[rowIndex];
        renderer.DrawText(labelRect,
            row.label,
            TextStyleId::Label,
            renderer.MutedTextColor(),
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
        if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
            const DashboardRenderer::TextLayoutResult valueLayout = renderer.DrawTextBlock(valueRect,
                row.valueText,
                TextStyleId::Value,
                renderer.ForegroundColor(),
                TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
            renderer.RegisterDynamicTextAnchor(valueLayout,
                renderer.MakeEditableTextBinding(widget,
                    DashboardRenderer::LayoutEditParameter::FontValue,
                    rowIndex * 2 + 1,
                    renderer.Config().layout.fonts.value.size));
        }
        const RenderRect& barRect = layoutState_.barRects[rowIndex];
        renderer.DrawPillBar(
            barRect, row.ratio, row.peakRatio, renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank);

        ++rowIndex;
    }
    renderer.PopClipRect();
}

void MetricListWidget::BuildStaticAnchors(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const auto& config = renderer.Config().layout.metricList;
    for (int rowIndex = 0;
        rowIndex < layoutState_.visibleRows && rowIndex < static_cast<int>(layoutState_.barRects.size()) &&
        rowIndex < static_cast<int>(layoutState_.barAnchorRects.size()) &&
        rowIndex < static_cast<int>(layoutState_.labelRects.size()) && rowIndex < static_cast<int>(entries_.size());
        ++rowIndex) {
        const RenderRect& barRect = layoutState_.barRects[rowIndex];
        const RenderRect& anchorRect = layoutState_.barAnchorRects[rowIndex];
        const int anchorCenterX = anchorRect.left + ((std::max)(0, anchorRect.right - anchorRect.left) / 2);
        const int anchorCenterY = anchorRect.top + ((std::max)(0, anchorRect.bottom - anchorRect.top) / 2);
        renderer.RegisterStaticEditableAnchorRegion(
            LayoutEditAnchorKey{
                LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                DashboardRenderer::LayoutEditParameter::MetricListBarHeight,
                rowIndex},
            barRect,
            anchorRect,
            AnchorShape::Circle,
            AnchorDragAxis::Horizontal,
            AnchorDragMode::AxisDelta,
            RenderPoint{anchorCenterX, anchorCenterY},
            1.0,
            false,
            true,
            config.barHeight);
        const std::string label = ResolveMetricListLabel(entries_[rowIndex]);
        if (!label.empty()) {
            renderer.RegisterStaticTextAnchor(layoutState_.labelRects[rowIndex],
                label,
                TextStyleId::Label,
                TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
                renderer.MakeEditableTextBinding(widget,
                    DashboardRenderer::LayoutEditParameter::FontLabel,
                    rowIndex * 2,
                    renderer.Config().layout.fonts.label.size));
        }
    }
}

void MetricListWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
    const int x = std::clamp(static_cast<int>(widget.rect.left) + layoutState_.labelWidth,
        static_cast<int>(widget.rect.left),
        static_cast<int>(widget.rect.right));

    auto& guides = renderer.WidgetEditGuidesMutable();
    LayoutEditWidgetGuide guide;
    guide.axis = LayoutGuideAxis::Vertical;
    guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = DashboardRenderer::LayoutEditParameter::MetricListLabelWidth;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = RenderPoint{x, widget.rect.top};
    guide.drawEnd = RenderPoint{x, widget.rect.bottom};
    guide.hitRect = RenderRect{x - hitInset, widget.rect.top, x + hitInset + 1, widget.rect.bottom};
    guide.value = renderer.Config().layout.metricList.labelWidth;
    guide.dragDirection = 1;
    guides.push_back(guide);

    for (int rowIndex = 0; rowIndex < layoutState_.visibleRows; ++rowIndex) {
        const int y = layoutState_.rowRects[rowIndex].bottom;
        guide = {};
        guide.axis = LayoutGuideAxis::Horizontal;
        guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = DashboardRenderer::LayoutEditParameter::MetricListRowGap;
        guide.guideId = 1 + rowIndex;
        guide.widgetRect = widget.rect;
        guide.drawStart = RenderPoint{widget.rect.left, y};
        guide.drawEnd = RenderPoint{widget.rect.right, y};
        guide.hitRect = RenderRect{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
        guide.value = renderer.Config().layout.metricList.rowGap;
        guide.dragDirection = 1;
        guides.push_back(std::move(guide));
    }
}
