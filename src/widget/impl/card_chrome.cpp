#include "widget/impl/card_chrome.h"

#include <algorithm>

#include "widget/widget_host.h"

namespace {

RenderRect MakeSquareAnchorRect(int centerX, int centerY, int size) {
    const int clampedSize = (std::max)(4, size);
    const int radius = clampedSize / 2;
    return RenderRect{
        centerX - radius, centerY - radius, centerX - radius + clampedSize, centerY - radius + clampedSize};
}

RenderRect MakeCircleAnchorRect(int centerX, int centerY, int representedDiameter, int extraDiameter) {
    const int diameter = (std::max)(4, representedDiameter + extraDiameter);
    const int radius = diameter / 2;
    return RenderRect{centerX - radius, centerY - radius, centerX - radius + diameter, centerY - radius + diameter};
}

}  // namespace

CardChromeWidget::CardChromeWidget(const LayoutCardConfig& card) : title_(card.title), iconName_(card.icon) {}

WidgetClass CardChromeWidget::Class() const {
    return WidgetClass::Unknown;
}

std::unique_ptr<Widget> CardChromeWidget::Clone() const {
    return std::make_unique<CardChromeWidget>(*this);
}

void CardChromeWidget::Initialize(const LayoutNodeConfig&) {}

int CardChromeWidget::PreferredHeight(const WidgetHost&) const {
    return 0;
}

bool CardChromeWidget::IsHoverable() const {
    return false;
}

void CardChromeWidget::ResolveLayoutState(const WidgetHost& renderer, const RenderRect& rect) {
    layoutState_ = ResolveCardChromeLayout(title_, iconName_, rect, ResolveCardChromeLayoutMetrics(renderer));
}

void CardChromeWidget::Draw(WidgetHost& renderer, const WidgetLayout& widget, const MetricSource&) const {
    const int radius = (std::max)(0, renderer.Renderer().ScaleLogical(renderer.Config().layout.cardStyle.cardRadius));
    const float borderWidth = static_cast<float>(
        (std::max)(1, renderer.Renderer().ScaleLogical(renderer.Config().layout.cardStyle.cardBorderWidth)));

    renderer.Renderer().FillSolidRoundedRect(widget.rect, radius, RenderColorId::PanelFill);
    renderer.Renderer().DrawSolidRoundedRect(
        widget.rect, radius, RenderStroke::Solid(RenderColorId::PanelBorder, borderWidth));

    if (!iconName_.empty()) {
        renderer.Renderer().DrawIcon(iconName_, layoutState_.iconRect);
        renderer.EditArtifacts().RegisterDynamicColorEditRegion(
            WidgetHost::LayoutEditParameter::ColorIcon, layoutState_.iconRect);
    }
    if (!title_.empty()) {
        const WidgetHost::TextLayoutResult titleLayout = renderer.Renderer().DrawTextBlock(layoutState_.titleRect,
            title_,
            TextStyleId::Title,
            RenderColorId::Foreground,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center));
        renderer.EditArtifacts().RegisterDynamicColorEditRegion(
            WidgetHost::LayoutEditParameter::ColorForeground, titleLayout.textRect);
    }
}

void CardChromeWidget::BuildEditGuides(WidgetHost& renderer, const WidgetLayout& widget) const {
    const auto addGuide = [&](LayoutGuideAxis axis,
                              int guideId,
                              WidgetHost::LayoutEditParameter parameter,
                              int value,
                              int dragDirection,
                              RenderPoint start,
                              RenderPoint end) {
        const int hitInset = (std::max)(3, renderer.Renderer().ScaleLogical(4));
        LayoutEditWidgetGuide guide;
        guide.axis = axis;
        guide.widget = CardIdentity(widget);
        guide.parameter = parameter;
        guide.guideId = guideId;
        guide.widgetRect = widget.rect;
        guide.drawStart = start;
        guide.drawEnd = end;
        if (axis == LayoutGuideAxis::Vertical) {
            guide.hitRect = RenderRect{
                start.x - hitInset, (std::min)(start.y, end.y), start.x + hitInset + 1, (std::max)(start.y, end.y)};
        } else {
            guide.hitRect = RenderRect{
                (std::min)(start.x, end.x), start.y - hitInset, (std::max)(start.x, end.x), start.y + hitInset + 1};
        }
        guide.value = value;
        guide.dragDirection = dragDirection;
        renderer.EditArtifacts().RegisterWidgetEditGuide(std::move(guide));
    };

    const int contentLeft = std::clamp(
        layoutState_.contentRect.left, static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
    const int contentRight =
        std::clamp(layoutState_.contentRect.right, contentLeft, static_cast<int>(widget.rect.right));
    const int paddingY =
        std::clamp(widget.rect.top + renderer.Renderer().ScaleLogical(renderer.Config().layout.cardStyle.cardPadding),
            static_cast<int>(widget.rect.top),
            static_cast<int>(widget.rect.bottom));
    addGuide(LayoutGuideAxis::Horizontal,
        0,
        WidgetHost::LayoutEditParameter::CardPadding,
        renderer.Config().layout.cardStyle.cardPadding,
        1,
        RenderPoint{contentLeft, paddingY},
        RenderPoint{contentRight, paddingY});

    if (!iconName_.empty() && !title_.empty()) {
        const int guideX = std::clamp(
            layoutState_.titleRect.left, static_cast<int>(widget.rect.left), static_cast<int>(widget.rect.right));
        addGuide(LayoutGuideAxis::Vertical,
            1,
            WidgetHost::LayoutEditParameter::CardHeaderIconGap,
            renderer.Config().layout.cardStyle.headerIconGap,
            1,
            RenderPoint{guideX, layoutState_.titleRect.top},
            RenderPoint{guideX, layoutState_.titleRect.bottom});
    }

    if (layoutState_.hasHeader) {
        const int guideY = std::clamp(
            layoutState_.contentRect.top, static_cast<int>(widget.rect.top), static_cast<int>(widget.rect.bottom));
        addGuide(LayoutGuideAxis::Horizontal,
            2,
            WidgetHost::LayoutEditParameter::CardHeaderContentGap,
            renderer.Config().layout.cardStyle.headerContentGap,
            1,
            RenderPoint{contentLeft, guideY},
            RenderPoint{contentRight, guideY});
    }
}

void CardChromeWidget::BuildStaticAnchors(WidgetHost& renderer, const WidgetLayout& widget) const {
    const LayoutEditWidgetIdentity cardIdentity = CardIdentity(widget);
    const int squareAnchorSize = (std::max)(4, renderer.Renderer().ScaleLogical(6));
    const int radiusLogical = renderer.Config().layout.cardStyle.cardRadius;
    const int radiusScaled = renderer.Renderer().ScaleLogical(radiusLogical);
    renderer.EditArtifacts().RegisterStaticEditableAnchorRegion(
        LayoutEditAnchorKey{cardIdentity, WidgetHost::LayoutEditParameter::CardRadius, 0},
        {},
        MakeSquareAnchorRect(widget.rect.left + radiusScaled, widget.rect.top, squareAnchorSize),
        AnchorShape::Square,
        AnchorDragAxis::Vertical,
        AnchorDragMode::AxisDelta,
        RenderPoint{widget.rect.left + radiusScaled, widget.rect.top},
        1.0,
        true,
        true,
        false,
        radiusLogical);

    const int borderScaled =
        (std::max)(1, renderer.Renderer().ScaleLogical(renderer.Config().layout.cardStyle.cardBorderWidth));
    const int borderAnchorPadding = (std::max)(1, renderer.Renderer().ScaleLogical(1));
    const int borderCenterX = widget.rect.left + (std::max)(0, (widget.rect.right - widget.rect.left) / 2);
    const int borderCenterY = widget.rect.top;
    renderer.EditArtifacts().RegisterStaticEditableAnchorRegion(
        LayoutEditAnchorKey{cardIdentity, WidgetHost::LayoutEditParameter::CardBorder, 0},
        {},
        MakeCircleAnchorRect(borderCenterX, borderCenterY, borderScaled, borderAnchorPadding),
        AnchorShape::Circle,
        AnchorDragAxis::Both,
        AnchorDragMode::RadialDistance,
        RenderPoint{borderCenterX, borderCenterY},
        2.0,
        true,
        true,
        false,
        renderer.Config().layout.cardStyle.cardBorderWidth);

    if (!iconName_.empty()) {
        const int anchorCenterX = layoutState_.iconRect.right;
        const int anchorCenterY = layoutState_.iconRect.top;
        renderer.EditArtifacts().RegisterStaticEditableAnchorRegion(
            LayoutEditAnchorKey{cardIdentity, WidgetHost::LayoutEditParameter::CardHeaderIconSize, 0},
            layoutState_.iconRect,
            MakeSquareAnchorRect(anchorCenterX, anchorCenterY, squareAnchorSize),
            AnchorShape::Square,
            AnchorDragAxis::Vertical,
            AnchorDragMode::AxisDelta,
            RenderPoint{anchorCenterX, anchorCenterY},
            1.0,
            true,
            false,
            true,
            renderer.Config().layout.cardStyle.headerIconSize);
    }

    if (!title_.empty()) {
        renderer.EditArtifacts().RegisterStaticTextAnchor(layoutState_.titleRect,
            title_,
            TextStyleId::Title,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
            LayoutEditAnchorBinding{LayoutEditAnchorKey{cardIdentity, WidgetHost::LayoutEditParameter::FontTitle, 0},
                renderer.Config().layout.fonts.title.size,
                AnchorShape::Circle,
                AnchorDragAxis::Vertical,
                AnchorDragMode::AxisDelta});
        renderer.EditArtifacts().RegisterStaticTextAnchor(layoutState_.titleRect,
            title_,
            TextStyleId::Title,
            TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center),
            LayoutEditAnchorBinding{LayoutEditAnchorKey{cardIdentity, LayoutCardTitleEditKey{widget.editCardId}, 1},
                0,
                AnchorShape::Wedge,
                AnchorDragAxis::Vertical,
                AnchorDragMode::AxisDelta,
                false});
    }
}

LayoutEditWidgetIdentity CardChromeWidget::CardIdentity(const WidgetLayout& widget) {
    return LayoutEditWidgetIdentity{
        widget.cardId, widget.editCardId, widget.nodePath, LayoutEditWidgetIdentity::Kind::CardChrome};
}
