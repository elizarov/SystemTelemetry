#include "widget/impl/metric_list.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "telemetry/metrics.h"
#include "util/numeric_safety.h"
#include "util/strings.h"
#include "widget/widget_host.h"

namespace {

int EffectiveMetricRowHeight(const WidgetHost& renderer) {
    const int valueHeight = renderer.Renderer().TextMetrics().value;
    const int barHeight = std::max(1, renderer.Renderer().ScaleLogical(renderer.Config().layout.metricList.barHeight));
    const int rowGap = std::max(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.metricList.rowGap));
    return valueHeight + rowGap + barHeight;
}

void FillCapsule(WidgetHost& renderer, const RenderRect& rect, RenderColorId color) {
    const int width = rect.Width();
    const int height = rect.Height();
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width <= height) {
        renderer.Renderer().FillSolidEllipse(rect, color);
    } else {
        renderer.Renderer().FillSolidRoundedRect(rect, height / 2, color);
    }
}

std::optional<RenderRect> DrawMetricCapsuleBar(
    WidgetHost& renderer, const RenderRect& rect, double ratio, std::optional<double> peakRatio, bool drawFill) {
    FillCapsule(renderer, rect, RenderColorId::Track);

    const int width = rect.Width();
    const int height = rect.Height();
    if (width <= 0 || height <= 0 || !drawFill) {
        return std::nullopt;
    }

    const double clampedRatio = ClampFinite(ratio, 0.0, 1.0);
    const int straightWidth = std::max(0, width - height);
    const int fillWidth = std::min(width, height + static_cast<int>(std::round(clampedRatio * straightWidth)));
    RenderRect fillRect = rect;
    fillRect.right = fillRect.left + fillWidth;
    FillCapsule(renderer, fillRect, RenderColorId::Accent);

    if (!peakRatio.has_value()) {
        return std::nullopt;
    }

    const double peak = ClampFinite(*peakRatio, 0.0, 1.0);
    const int markerWidth = std::min(width, std::max(1, std::max(renderer.Renderer().ScaleLogical(4), height)));
    const int centerX = rect.left + static_cast<int>(std::round(peak * width));
    const int minLeft = rect.left;
    const int maxLeft = rect.right - markerWidth;
    const int markerLeft = std::clamp(centerX - markerWidth / 2, minLeft, maxLeft);
    RenderRect markerRect{markerLeft, rect.top, markerLeft + markerWidth, rect.bottom};
    FillCapsule(renderer, markerRect, RenderColorId::PeakGhost);
    return markerRect;
}

RenderRect OffsetRect(RenderRect rect, int dy) {
    rect.top += dy;
    rect.bottom += dy;
    return rect;
}

void DrawMetricListRow(WidgetHost& renderer,
    const WidgetLayout& widget,
    const MetricListWidget::LayoutState& layout,
    const std::vector<std::string>& metricRefs,
    int rowIndex,
    const MetricValue& row,
    int yOffset,
    bool registerEditRegions) {
    const RenderRect labelRect = OffsetRect(layout.labelRects[rowIndex], yOffset);
    const RenderRect valueRect = OffsetRect(layout.valueRects[rowIndex], yOffset);
    renderer.Renderer().DrawText(labelRect,
        row.label,
        TextStyleId::Label,
        RenderColorId::MutedText,
        TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
    if (renderer.CurrentRenderMode() != WidgetHost::RenderMode::Blank) {
        const WidgetHost::TextLayoutResult valueLayout = renderer.Renderer().DrawTextBlock(valueRect,
            row.valueText,
            TextStyleId::Value,
            RenderColorId::Foreground,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
        if (registerEditRegions) {
            renderer.EditArtifacts().RegisterDynamicTextAnchor(valueLayout,
                renderer.MakeEditableTextBinding(widget,
                    WidgetHost::LayoutEditParameter::FontValue,
                    rowIndex * 2 + 1,
                    renderer.Config().layout.fonts.value.size),
                WidgetHost::LayoutEditParameter::ColorForeground);
            if (rowIndex < static_cast<int>(metricRefs.size()) && !IsRuntimePlaceholderMetricId(metricRefs[rowIndex])) {
                renderer.EditArtifacts().RegisterDynamicTextAnchor(
                    valueLayout, renderer.MakeMetricTextBinding(widget, metricRefs[rowIndex], rowIndex * 2 + 101));
            }
        }
    }

    const RenderRect barRect = OffsetRect(layout.barRects[rowIndex], yOffset);
    const std::optional<RenderRect> peakMarkerRect = DrawMetricCapsuleBar(
        renderer, barRect, row.ratio, row.peakRatio, renderer.CurrentRenderMode() != WidgetHost::RenderMode::Blank);
    if (!registerEditRegions) {
        return;
    }

    const int splitX = barRect.left + ((std::max)(0, barRect.right - barRect.left) / 2);
    renderer.EditArtifacts().RegisterDynamicColorEditRegion(
        WidgetHost::LayoutEditParameter::ColorAccent, RenderRect{barRect.left, barRect.top, splitX, barRect.bottom});
    renderer.EditArtifacts().RegisterDynamicColorEditRegion(
        WidgetHost::LayoutEditParameter::ColorTrack, RenderRect{splitX, barRect.top, barRect.right, barRect.bottom});
    if (peakMarkerRect.has_value()) {
        renderer.EditArtifacts().RegisterDynamicColorEditRegion(
            WidgetHost::LayoutEditParameter::ColorPeakGhost, *peakMarkerRect);
    }
}

}  // namespace

WidgetClass MetricListWidget::Class() const {
    return WidgetClass::MetricList;
}

std::unique_ptr<Widget> MetricListWidget::Clone() const {
    return std::make_unique<MetricListWidget>(*this);
}

void MetricListWidget::Initialize(const LayoutNodeConfig& node) {
    metricRefs_.clear();
    metricRefs_ = SplitTrimmed(node.parameter, ',');
}

int MetricListWidget::PreferredHeight(const WidgetHost& renderer) const {
    return static_cast<int>(metricRefs_.size()) * EffectiveMetricRowHeight(renderer);
}

void MetricListWidget::ResolveLayoutState(const WidgetHost& renderer, const RenderRect& rect) {
    layoutState_ = {};
    layoutState_.rowHeight = EffectiveMetricRowHeight(renderer);
    layoutState_.labelWidth =
        (std::max)(1, renderer.Renderer().ScaleLogical(renderer.Config().layout.metricList.labelWidth));
    layoutState_.metricBarHeight =
        (std::max)(1, renderer.Renderer().ScaleLogical(renderer.Config().layout.metricList.barHeight));
    layoutState_.anchorSize = (std::max)(4, renderer.Renderer().ScaleLogical(6));
    layoutState_.reorderAnchorWidth = (std::max)(6, renderer.Renderer().ScaleLogical(8));
    layoutState_.reorderAnchorHeight = (std::max)(10, renderer.Renderer().ScaleLogical(12));
    const int valueHeight = renderer.Renderer().TextMetrics().value;
    const int rowContentHeight = valueHeight + layoutState_.metricBarHeight;
    layoutState_.visibleRows =
        layoutState_.rowHeight > 0
            ? std::clamp(((std::max)(0, rect.bottom - rect.top) + layoutState_.rowHeight - 1) / layoutState_.rowHeight,
                  0,
                  static_cast<int>(metricRefs_.size()))
            : 0;
    layoutState_.rowRects.clear();
    layoutState_.labelRects.clear();
    layoutState_.valueRects.clear();
    layoutState_.barRects.clear();
    layoutState_.barAnchorRects.clear();
    layoutState_.reorderAnchorRects.clear();
    layoutState_.showAddRowAnchor = false;
    layoutState_.addRowRect = {};
    layoutState_.addRowAnchorRect = {};
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
        const int reorderCenterX =
            rowRect.right - (std::max)(layoutState_.reorderAnchorWidth, renderer.Renderer().ScaleLogical(10)) / 2;
        const int reorderCenterY = rowRect.top + ((std::max)(0, static_cast<int>(rowRect.bottom - rowRect.top)) / 2);
        layoutState_.reorderAnchorRects.push_back(RenderRect{reorderCenterX - (layoutState_.reorderAnchorWidth / 2),
            reorderCenterY - (layoutState_.reorderAnchorHeight / 2),
            reorderCenterX - (layoutState_.reorderAnchorWidth / 2) + layoutState_.reorderAnchorWidth,
            reorderCenterY - (layoutState_.reorderAnchorHeight / 2) + layoutState_.reorderAnchorHeight});
        rowRect = RenderRect{
            rowRect.left,
            rowRect.top + layoutState_.rowHeight,
            rowRect.right,
            rowRect.bottom + layoutState_.rowHeight,
        };
    }
    if (rowRect.bottom <= rect.bottom) {
        layoutState_.showAddRowAnchor = true;
        layoutState_.addRowRect = rowRect;
        const int addAnchorSize = (std::max)(layoutState_.reorderAnchorWidth, renderer.Renderer().ScaleLogical(10));
        const int addCenterX = rowRect.right - (addAnchorSize / 2);
        const int addCenterY = rowRect.top + ((std::max)(0, static_cast<int>(rowRect.bottom - rowRect.top)) / 2);
        layoutState_.addRowAnchorRect = RenderRect{addCenterX - (addAnchorSize / 2),
            addCenterY - (addAnchorSize / 2),
            addCenterX - (addAnchorSize / 2) + addAnchorSize,
            addCenterY - (addAnchorSize / 2) + addAnchorSize};
    }
}

void MetricListWidget::Draw(WidgetHost& renderer, const WidgetLayout& widget, const MetricSource& metrics) const {
    renderer.Renderer().PushClipRect(widget.rect);
    const auto dragState = renderer.ActiveMetricListReorderDrag(
        LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath});
    const int draggedIndex = dragState.has_value() ? dragState->currentIndex : -1;
    const auto& rows = metrics.ResolveMetricList(metricRefs_);
    int rowIndex = 0;
    for (const auto& row : rows) {
        if (rowIndex >= static_cast<int>(layoutState_.rowRects.size())) {
            break;
        }
        if (rowIndex != draggedIndex) {
            DrawMetricListRow(renderer, widget, layoutState_, metricRefs_, rowIndex, row, 0, true);
        }

        ++rowIndex;
    }

    if (dragState.has_value() && draggedIndex >= 0 && draggedIndex < static_cast<int>(rows.size()) &&
        draggedIndex < static_cast<int>(layoutState_.rowRects.size())) {
        const int draggedTop = dragState->mouseY - dragState->dragOffsetY;
        const int yOffset = draggedTop - layoutState_.rowRects[draggedIndex].top;
        DrawMetricListRow(
            renderer, widget, layoutState_, metricRefs_, draggedIndex, rows[draggedIndex], yOffset, false);
        const RenderRect outlineRect = OffsetRect(layoutState_.rowRects[draggedIndex], yOffset);
        renderer.Renderer().DrawSolidRect(outlineRect,
            RenderStroke::Dotted(
                RenderColorId::ActiveEdit, static_cast<float>((std::max)(2, renderer.Renderer().ScaleLogical(2)))));
    }
    renderer.Renderer().PopClipRect();
}

void MetricListWidget::BuildStaticAnchors(WidgetHost& renderer, const WidgetLayout& widget) const {
    const auto& config = renderer.Config().layout.metricList;
    for (int rowIndex = 0;
        rowIndex < layoutState_.visibleRows && rowIndex < static_cast<int>(layoutState_.barRects.size()) &&
        rowIndex < static_cast<int>(layoutState_.barAnchorRects.size()) &&
        rowIndex < static_cast<int>(layoutState_.labelRects.size()) && rowIndex < static_cast<int>(metricRefs_.size());
        ++rowIndex) {
        const RenderRect& barRect = layoutState_.barRects[rowIndex];
        const RenderRect& anchorRect = layoutState_.barAnchorRects[rowIndex];
        const int anchorCenterX = anchorRect.left + ((std::max)(0, anchorRect.right - anchorRect.left) / 2);
        const int anchorCenterY = anchorRect.top + ((std::max)(0, anchorRect.bottom - anchorRect.top) / 2);
        renderer.EditArtifacts().RegisterStaticEditableAnchorRegion(
            LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                WidgetHost::LayoutEditParameter::MetricListBarHeight,
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
        renderer.EditArtifacts().RegisterStaticEditableAnchorRegion(
            LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                LayoutNodeFieldEditKey{
                    widget.editCardId, widget.nodePath, WidgetClass::MetricList, LayoutNodeField::Parameter},
                rowIndex},
            layoutState_.rowRects[rowIndex],
            layoutState_.reorderAnchorRects[rowIndex],
            AnchorShape::VerticalReorder,
            AnchorDragAxis::Horizontal,
            AnchorDragMode::AxisDelta,
            layoutState_.reorderAnchorRects[rowIndex].Center(),
            1.0,
            true,
            true,
            false,
            0);
        const MetricDefinitionConfig* definition = renderer.FindConfiguredMetricDefinition(metricRefs_[rowIndex]);
        if (definition != nullptr && !definition->label.empty() &&
            !IsRuntimePlaceholderMetricId(metricRefs_[rowIndex])) {
            renderer.EditArtifacts().RegisterStaticTextAnchor(layoutState_.labelRects[rowIndex],
                definition->label,
                TextStyleId::Label,
                TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
                renderer.MakeEditableTextBinding(widget,
                    WidgetHost::LayoutEditParameter::FontLabel,
                    rowIndex * 2,
                    renderer.Config().layout.fonts.label.size),
                WidgetHost::LayoutEditParameter::ColorMutedText);
            renderer.EditArtifacts().RegisterStaticTextAnchor(layoutState_.labelRects[rowIndex],
                definition->label,
                TextStyleId::Label,
                TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
                renderer.MakeMetricTextBinding(widget, metricRefs_[rowIndex], rowIndex * 2 + 100));
        }
    }
    if (layoutState_.showAddRowAnchor && !layoutState_.addRowAnchorRect.IsEmpty()) {
        renderer.EditArtifacts().RegisterStaticEditableAnchorRegion(
            LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                LayoutNodeFieldEditKey{
                    widget.editCardId, widget.nodePath, WidgetClass::MetricList, LayoutNodeField::Parameter},
                static_cast<int>(metricRefs_.size())},
            layoutState_.addRowRect,
            layoutState_.addRowAnchorRect,
            AnchorShape::Plus,
            AnchorDragAxis::Horizontal,
            AnchorDragMode::AxisDelta,
            layoutState_.addRowAnchorRect.Center(),
            1.0,
            false,
            true,
            false,
            0);
    }
}

void MetricListWidget::BuildEditGuides(WidgetHost& renderer, const WidgetLayout& widget) const {
    const int hitInset = (std::max)(3, renderer.Renderer().ScaleLogical(4));
    const int x = std::clamp(static_cast<int>(widget.rect.left) + layoutState_.labelWidth,
        static_cast<int>(widget.rect.left),
        static_cast<int>(widget.rect.right));

    LayoutEditWidgetGuide guide;
    guide.axis = LayoutGuideAxis::Vertical;
    guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
    guide.parameter = WidgetHost::LayoutEditParameter::MetricListLabelWidth;
    guide.guideId = 0;
    guide.widgetRect = widget.rect;
    guide.drawStart = RenderPoint{x, widget.rect.top};
    guide.drawEnd = RenderPoint{x, widget.rect.bottom};
    guide.hitRect = RenderRect{x - hitInset, widget.rect.top, x + hitInset + 1, widget.rect.bottom};
    guide.value = renderer.Config().layout.metricList.labelWidth;
    guide.dragDirection = 1;
    renderer.EditArtifacts().RegisterWidgetEditGuide(guide);

    for (int rowIndex = 0; rowIndex < layoutState_.visibleRows; ++rowIndex) {
        const int y = layoutState_.rowRects[rowIndex].bottom;
        guide = {};
        guide.axis = LayoutGuideAxis::Horizontal;
        guide.widget = LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath};
        guide.parameter = WidgetHost::LayoutEditParameter::MetricListRowGap;
        guide.guideId = 1 + rowIndex;
        guide.widgetRect = widget.rect;
        guide.drawStart = RenderPoint{widget.rect.left, y};
        guide.drawEnd = RenderPoint{widget.rect.right, y};
        guide.hitRect = RenderRect{widget.rect.left, y - hitInset, widget.rect.right, y + hitInset + 1};
        guide.value = renderer.Config().layout.metricList.rowGap;
        guide.dragDirection = 1;
        renderer.EditArtifacts().RegisterWidgetEditGuide(std::move(guide));
    }
}
