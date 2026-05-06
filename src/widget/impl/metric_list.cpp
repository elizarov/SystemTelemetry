#include "widget/impl/metric_list.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "telemetry/metrics.h"
#include "util/strings.h"
#include "util/utf8.h"
#include "widget/impl/pill_bar.h"
#include "widget/widget_host.h"

namespace {

int EffectiveMetricRowHeight(const WidgetHost& renderer) {
    const int valueHeight = renderer.Renderer().TextMetrics().value;
    const int barHeight = std::max(1, renderer.Renderer().ScaleLogical(renderer.Config().layout.metricList.barHeight));
    const int rowGap = std::max(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.metricList.rowGap));
    return valueHeight + rowGap + barHeight;
}

RenderRect OffsetRect(RenderRect rect, int dy) {
    rect.top += dy;
    rect.bottom += dy;
    return rect;
}

std::string FitMiddleEllipsis(const Renderer& renderer, TextStyleId style, std::string_view text, int maxWidth) {
    if (text.empty() || maxWidth <= 0) {
        return {};
    }

    const std::string original(text);
    if (renderer.MeasureTextWidth(style, original) <= maxWidth) {
        return original;
    }

    constexpr std::string_view kEllipsis = "...";
    if (renderer.MeasureTextWidth(style, kEllipsis) > maxWidth) {
        return {};
    }
    const std::wstring wide = WideFromUtf8(original);
    if (wide.empty()) {
        return std::string(kEllipsis);
    }

    if (wide.size() <= kEllipsis.size() + 2) {
        return std::string(kEllipsis);
    }

    const std::wstring lastLetter = wide.substr(wide.size() - 1);
    for (size_t prefixLength = wide.size() - 2; prefixLength > 0; --prefixLength) {
        std::string candidate = Utf8FromWide(wide.substr(0, prefixLength) + L"..." + lastLetter);
        if (renderer.MeasureTextWidth(style, candidate) <= maxWidth) {
            return candidate;
        }
    }

    return std::string(kEllipsis);
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
        const RenderColorId valueColor =
            row.state == MetricValueState::PermissionRequired ? RenderColorId::Warning : RenderColorId::Foreground;
        RenderRect metricValueRect = valueRect;
        std::string annotationText;
        if (!row.annotationText.empty()) {
            const int annotationGap = renderer.Renderer().ScaleLogical(6);
            const int valueWidth = renderer.Renderer().MeasureTextWidth(TextStyleId::Value, row.valueText);
            const int annotationMaxWidth = valueRect.Width() - valueWidth - annotationGap;
            annotationText =
                FitMiddleEllipsis(renderer.Renderer(), TextStyleId::Label, row.annotationText, annotationMaxWidth);
            if (!annotationText.empty()) {
                const int annotationWidth = renderer.Renderer().MeasureTextWidth(TextStyleId::Label, annotationText);
                metricValueRect.right =
                    std::max(metricValueRect.left, valueRect.right - annotationWidth - annotationGap);
            }
        }
        const WidgetHost::TextLayoutResult valueLayout = renderer.Renderer().DrawTextBlock(metricValueRect,
            row.valueText,
            TextStyleId::Value,
            valueColor,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
        if (registerEditRegions) {
            renderer.EditArtifacts().RegisterDynamicTextAnchor(valueLayout,
                renderer.MakeEditableTextBinding(widget,
                    WidgetHost::LayoutEditParameter::FontValue,
                    rowIndex * 2 + 1,
                    renderer.Config().layout.fonts.value.size),
                row.state == MetricValueState::PermissionRequired ? WidgetHost::LayoutEditParameter::ColorWarning
                                                                  : WidgetHost::LayoutEditParameter::ColorForeground);
            if (rowIndex < static_cast<int>(metricRefs.size()) && !IsRuntimePlaceholderMetricId(metricRefs[rowIndex])) {
                renderer.EditArtifacts().RegisterDynamicTextAnchor(
                    valueLayout, renderer.MakeMetricTextBinding(widget, metricRefs[rowIndex], rowIndex * 2 + 101));
            }
        }
        if (!annotationText.empty()) {
            const RenderColorId annotationColor =
                row.warningAnnotation ? RenderColorId::Warning : RenderColorId::MutedText;
            renderer.Renderer().DrawText(valueRect,
                annotationText,
                TextStyleId::Label,
                annotationColor,
                TextLayoutOptions::SingleLine(TextHorizontalAlign::Trailing, TextVerticalAlign::Center));
        }
    }

    const RenderRect barRect = OffsetRect(layout.barRects[rowIndex], yOffset);
    const bool drawValue =
        row.state == MetricValueState::Available && renderer.CurrentRenderMode() != WidgetHost::RenderMode::Blank;
    const std::optional<RenderRect> peakMarkerRect =
        DrawWidgetPillBar(renderer, barRect, row.ratio, row.peakRatio, drawValue);
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
        const bool hoverEquivalent = renderer.CurrentRenderMode() == WidgetHost::RenderMode::LayoutGuideSheet;
        const RenderColorId outlineColor = hoverEquivalent ? RenderColorId::LayoutGuide : RenderColorId::ActiveEdit;
        const int outlineWidth = hoverEquivalent ? (std::max)(1, renderer.Renderer().ScaleLogical(1))
                                                 : (std::max)(2, renderer.Renderer().ScaleLogical(2));
        renderer.Renderer().DrawSolidRect(
            outlineRect, RenderStroke::Dotted(outlineColor, static_cast<float>(outlineWidth)));
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
        renderer.EditArtifacts().RegisterStaticEditAnchor(LayoutEditAnchorRegistration{
            .key = LayoutEditAnchorKey{LayoutEditWidgetIdentity{widget.cardId, widget.editCardId, widget.nodePath},
                WidgetHost::LayoutEditParameter::MetricListBarHeight,
                rowIndex},
            .targetRect = barRect,
            .anchorRect = anchorRect,
            .shape = AnchorShape::Circle,
            .value = config.barHeight,
            .drag = LayoutEditAnchorDrag::AxisDelta(
                AnchorDragAxis::Horizontal, RenderPoint{anchorCenterX, anchorCenterY})});
        renderer.EditArtifacts().RegisterStaticEditAnchor(LayoutEditAnchorRegistration{
            .key = MakeLayoutNodeFieldEditAnchorKey(widget, WidgetClass::MetricList, rowIndex),
            .targetRect = layoutState_.rowRects[rowIndex],
            .anchorRect = layoutState_.reorderAnchorRects[rowIndex],
            .shape = AnchorShape::VerticalReorder,
            .drag = LayoutEditAnchorDrag::AxisDelta(
                AnchorDragAxis::Horizontal, layoutState_.reorderAnchorRects[rowIndex].Center()),
            .visibility = LayoutEditAnchorVisibility::WhenWidgetHovered,
            .targetOutline = LayoutEditTargetOutline::Hidden});
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
        renderer.EditArtifacts().RegisterStaticEditAnchor(
            LayoutEditAnchorRegistration{.key = MakeLayoutNodeFieldEditAnchorKey(
                                             widget, WidgetClass::MetricList, static_cast<int>(metricRefs_.size())),
                .targetRect = layoutState_.addRowRect,
                .anchorRect = layoutState_.addRowAnchorRect,
                .shape = AnchorShape::Plus,
                .visibility = LayoutEditAnchorVisibility::WhenWidgetHovered,
                .targetOutline = LayoutEditTargetOutline::Hidden});
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
