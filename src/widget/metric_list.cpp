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

void MetricListWidget::ResolveLayoutState(const DashboardRenderer& renderer, const RECT& rect) {
    layoutState_ = {};
    layoutState_.rowHeight = EffectiveMetricRowHeight(renderer);
    layoutState_.labelWidth = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.metricList.labelWidth));
    layoutState_.metricBarHeight = (std::max)(1, renderer.ScaleLogical(renderer.Config().layout.metricList.barHeight));
    layoutState_.anchorSize = (std::max)(4, renderer.ScaleLogical(6));
    layoutState_.visibleRows =
        layoutState_.rowHeight > 0
            ? std::clamp(((std::max)(0, static_cast<int>(rect.bottom - rect.top)) + layoutState_.rowHeight - 1) /
                             layoutState_.rowHeight,
                  0,
                  static_cast<int>(entries_.size()))
            : 0;
    layoutState_.rowRects.clear();
    layoutState_.labelRects.clear();
    layoutState_.valueRects.clear();
    layoutState_.barRects.clear();
    layoutState_.barAnchorRects.clear();
    RECT rowRect{rect.left, rect.top, rect.right, rect.top + layoutState_.rowHeight};
    for (int rowIndex = 0; rowIndex < layoutState_.visibleRows; ++rowIndex) {
        layoutState_.rowRects.push_back(rowRect);
        RECT labelRect{rowRect.left,
            rowRect.top,
            (std::min)(rowRect.right, rowRect.left + layoutState_.labelWidth),
            rowRect.bottom};
        RECT valueRect{labelRect.right, rowRect.top, rowRect.right, rowRect.bottom};
        const int barBottom =
            (std::min)(static_cast<int>(rowRect.bottom), static_cast<int>(rowRect.top) + layoutState_.rowHeight);
        const int barTop = (std::max)(static_cast<int>(rowRect.top), barBottom - layoutState_.metricBarHeight);
        layoutState_.labelRects.push_back(labelRect);
        layoutState_.valueRects.push_back(valueRect);
        layoutState_.barRects.push_back(RECT{valueRect.left, barTop, rowRect.right, barBottom});
        const int anchorCenterX =
            static_cast<int>(valueRect.left) + ((std::max)(0, static_cast<int>(rowRect.right - valueRect.left) / 2));
        const int anchorCenterY = barBottom;
        layoutState_.barAnchorRects.push_back(RECT{anchorCenterX - (layoutState_.anchorSize / 2),
            anchorCenterY - (layoutState_.anchorSize / 2),
            anchorCenterX - (layoutState_.anchorSize / 2) + layoutState_.anchorSize,
            anchorCenterY - (layoutState_.anchorSize / 2) + layoutState_.anchorSize});
        OffsetRect(&rowRect, 0, layoutState_.rowHeight);
    }
}

void MetricListWidget::Draw(DashboardRenderer& renderer,
    HDC hdc,
    const DashboardWidgetLayout& widget,
    const DashboardMetricSource& metrics) const {
    const int savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, widget.rect.left, widget.rect.top, widget.rect.right, widget.rect.bottom);
    int rowIndex = 0;
    std::vector<DashboardMetricListEntry> entries;
    entries.reserve(entries_.size());
    for (const auto& entry : entries_) {
        entries.push_back(DashboardMetricListEntry{entry.metricRef, entry.labelOverride});
    }
    for (const auto& row : metrics.ResolveMetricList(entries)) {
        if (rowIndex >= static_cast<int>(layoutState_.rowRects.size())) {
            break;
        }
        const RECT& labelRect = layoutState_.labelRects[rowIndex];
        const RECT& valueRect = layoutState_.valueRects[rowIndex];
        renderer.DrawTextBlock(hdc,
            labelRect,
            row.label,
            renderer.WidgetFonts().label,
            renderer.MutedTextColor(),
            DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        if (renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank) {
            renderer.DrawTextBlock(hdc,
                valueRect,
                row.valueText,
                renderer.WidgetFonts().value,
                renderer.ForegroundColor(),
                DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            renderer.RegisterDynamicTextAnchor(valueRect,
                row.valueText,
                renderer.WidgetFonts().value,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
                renderer.MakeEditableTextBinding(
                    widget, DashboardRenderer::AnchorEditParameter::FontValue, rowIndex * 2 + 1, renderer.Config().layout.fonts.value.size));
        }
        const RECT& barRect = layoutState_.barRects[rowIndex];
        renderer.DrawPillBar(hdc,
            barRect,
            row.ratio,
            row.peakRatio,
            renderer.CurrentRenderMode() != DashboardRenderer::RenderMode::Blank);

        ++rowIndex;
    }
    RestoreDC(hdc, savedDc);
}

void MetricListWidget::BuildStaticAnchors(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const auto& config = renderer.Config().layout.metricList;
    for (int rowIndex = 0; rowIndex < layoutState_.visibleRows && rowIndex < static_cast<int>(layoutState_.barRects.size()) &&
                           rowIndex < static_cast<int>(layoutState_.barAnchorRects.size()) &&
                           rowIndex < static_cast<int>(layoutState_.labelRects.size()) && rowIndex < static_cast<int>(entries_.size());
         ++rowIndex) {
        const RECT& barRect = layoutState_.barRects[rowIndex];
        const RECT& anchorRect = layoutState_.barAnchorRects[rowIndex];
        const int anchorCenterX = anchorRect.left + ((std::max)(0L, anchorRect.right - anchorRect.left) / 2);
        const int anchorCenterY = anchorRect.top + ((std::max)(0L, anchorRect.bottom - anchorRect.top) / 2);
        renderer.RegisterStaticEditableAnchorRegion(
            DashboardRenderer::EditableAnchorKey{
                DashboardRenderer::LayoutWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                DashboardRenderer::AnchorEditParameter::MetricListBarHeight,
                rowIndex,
            },
            barRect,
            anchorRect,
            DashboardRenderer::AnchorShape::Circle,
            DashboardRenderer::AnchorDragAxis::Horizontal,
            DashboardRenderer::AnchorDragMode::AxisDelta,
            POINT{anchorCenterX, anchorCenterY},
            1.0,
            false,
            true,
            config.barHeight);
        const std::string label = ResolveMetricListLabel(entries_[rowIndex]);
        if (!label.empty()) {
            renderer.RegisterStaticTextAnchor(layoutState_.labelRects[rowIndex],
                label,
                renderer.WidgetFonts().label,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
                renderer.MakeEditableTextBinding(
                    widget, DashboardRenderer::AnchorEditParameter::FontLabel, rowIndex * 2, renderer.Config().layout.fonts.label.size));
        }
    }
}

void MetricListWidget::BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const {
    const int hitInset = (std::max)(3, renderer.ScaleLogical(4));
    const int x = std::clamp(static_cast<int>(widget.rect.left) + layoutState_.labelWidth,
        static_cast<int>(widget.rect.left),
        static_cast<int>(widget.rect.right));

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

    for (int rowIndex = 0; rowIndex < layoutState_.visibleRows; ++rowIndex) {
        const int y = layoutState_.rowRects[rowIndex].bottom;
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
