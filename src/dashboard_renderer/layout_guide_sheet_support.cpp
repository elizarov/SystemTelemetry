#include "dashboard_renderer/layout_guide_sheet_support.h"

#include <algorithm>
#include <utility>

#include "dashboard_renderer/impl/layout_edit_overlay_renderer.h"

std::vector<LayoutGuideSheetCardSummary> CollectLayoutGuideSheetCardSummaries(const DashboardRenderer& renderer) {
    std::vector<LayoutGuideSheetCardSummary> summaries;
    summaries.reserve(renderer.layoutResolver_->resolvedLayout_.cards.size());
    for (const auto& card : renderer.layoutResolver_->resolvedLayout_.cards) {
        LayoutGuideSheetCardSummary summary;
        summary.id = card.id;
        summary.title = card.title;
        summary.iconName = card.iconName;
        summary.rect = card.rect;
        summary.chromeLayout = card.chromeLayout;
        for (const auto& widget : card.widgets) {
            if (widget.widget != nullptr) {
                summary.widgetClasses.push_back(widget.widgetClass);
            }
        }
        summaries.push_back(std::move(summary));
    }
    return summaries;
}

bool SaveLayoutGuideSheetSurfacePng(
    DashboardRenderer& renderer, const FilePath& imagePath, int width, int height, Renderer::DrawCallback draw) {
    return renderer.renderer_->SavePng(imagePath, width, height, draw);
}

bool RenderLayoutGuideSheetSurfaceOffscreen(
    DashboardRenderer& renderer, int width, int height, Renderer::DrawCallback draw) {
    return renderer.renderer_->DrawOffscreen(width, height, draw);
}

void BeginLayoutGuideSheetDynamicArtifacts(DashboardRenderer& renderer, const DashboardOverlayState& overlayState) {
    renderer.activeOverlayState_ = &overlayState;
    renderer.layoutResolver_->ClearDynamicEditArtifacts();
    renderer.layoutResolver_->dynamicAnchorRegistrationEnabled_ = overlayState.ShouldRegisterDynamicEditArtifacts();
}

void ResolveLayoutGuideSheetDynamicArtifactCollisions(DashboardRenderer& renderer) {
    renderer.layoutResolver_->ResolveDynamicEditArtifactCollisions();
}

void EndLayoutGuideSheetDynamicArtifacts(DashboardRenderer& renderer) {
    renderer.layoutResolver_->dynamicAnchorRegistrationEnabled_ = false;
    renderer.activeOverlayState_ = nullptr;
}

void DrawLayoutGuideSheetCard(DashboardRenderer& renderer,
    const std::string& cardId,
    const RenderRect& sourceRect,
    const RenderRect& destRect,
    const MetricSource& metrics) {
    renderer.Renderer().PushClipRect(destRect.Inflate(renderer.ScaleLogical(4), renderer.ScaleLogical(4)));
    renderer.Renderer().PushTranslation(RenderPoint{destRect.left - sourceRect.left, destRect.top - sourceRect.top});
    const auto cardIt = std::find_if(renderer.layoutResolver_->resolvedLayout_.cards.begin(),
        renderer.layoutResolver_->resolvedLayout_.cards.end(),
        [&](const auto& card) { return card.id == cardId; });
    if (cardIt != renderer.layoutResolver_->resolvedLayout_.cards.end()) {
        renderer.DrawResolvedWidget(cardIt->chrome, metrics);
        for (const auto& widget : cardIt->widgets) {
            renderer.DrawResolvedWidget(widget, metrics);
        }
    }
    renderer.Renderer().PopTranslation();
    renderer.Renderer().PopClipRect();
}

void DrawLayoutGuideSheetOverlay(DashboardRenderer& renderer,
    const DashboardOverlayState& overlayState,
    const RenderRect& sourceRect,
    const RenderRect& destRect,
    const MetricSource& metrics) {
    renderer.Renderer().PushClipRect(destRect.Inflate(renderer.ScaleLogical(4), renderer.ScaleLogical(4)));
    renderer.Renderer().PushTranslation(RenderPoint{destRect.left - sourceRect.left, destRect.top - sourceRect.top});
    renderer.layoutEditOverlayRenderer_->Draw(overlayState, metrics);
    renderer.Renderer().PopTranslation();
    renderer.Renderer().PopClipRect();
}

LayoutGuideSheetCardChromeArtifacts BuildLayoutGuideSheetCardChromeArtifacts(DashboardRenderer& renderer,
    const std::string& cardId,
    const RenderRect& rect,
    const MetricSource* metrics,
    bool suppressTitle) {
    LayoutGuideSheetCardChromeArtifacts artifacts;
    const LayoutCardConfig* card = renderer.FindCardConfigById(cardId);
    if (card == nullptr) {
        return artifacts;
    }
    LayoutCardConfig displayCard = *card;
    if (suppressTitle) {
        displayCard.title.clear();
        displayCard.icon.clear();
    }

    WidgetLayout widget;
    widget.rect = rect;
    widget.cardId = cardId;
    widget.editCardId = cardId;
    widget.widget = CreateCardChromeWidget(displayCard);
    widget.widget->ResolveLayoutState(renderer, rect);

    auto savedWidgetGuides = std::move(renderer.layoutResolver_->widgetEditGuides_);
    auto savedStaticAnchors = std::move(renderer.layoutResolver_->staticEditableAnchorRegions_);
    auto savedDynamicAnchors = std::move(renderer.layoutResolver_->dynamicEditableAnchorRegions_);
    auto savedDynamicColors = std::move(renderer.layoutResolver_->dynamicColorEditRegions_);
    renderer.layoutResolver_->widgetEditGuides_.clear();
    renderer.layoutResolver_->staticEditableAnchorRegions_.clear();
    renderer.layoutResolver_->dynamicEditableAnchorRegions_.clear();
    renderer.layoutResolver_->dynamicColorEditRegions_.clear();

    widget.widget->BuildEditGuides(renderer, widget);
    widget.widget->BuildStaticAnchors(renderer, widget);
    if (metrics != nullptr) {
        widget.widget->Draw(renderer, widget, *metrics);
    }

    artifacts.chromeLayout = ResolveCardChromeLayout(displayCard, rect, ResolveCardChromeLayoutMetrics(renderer));
    artifacts.widgetGuides = renderer.layoutResolver_->widgetEditGuides_;
    artifacts.anchorRegions = renderer.layoutResolver_->staticEditableAnchorRegions_;
    artifacts.colorRegions = renderer.layoutResolver_->dynamicColorEditRegions_;
    if (!displayCard.icon.empty() && !artifacts.chromeLayout.iconRect.IsEmpty() &&
        std::none_of(artifacts.colorRegions.begin(), artifacts.colorRegions.end(), [](const auto& region) {
            return region.parameter == LayoutEditParameter::ColorIcon;
        })) {
        artifacts.colorRegions.push_back(
            LayoutEditColorRegion{LayoutEditParameter::ColorIcon, artifacts.chromeLayout.iconRect});
    }
    if (!displayCard.title.empty() && !artifacts.chromeLayout.titleRect.IsEmpty() &&
        std::none_of(artifacts.colorRegions.begin(), artifacts.colorRegions.end(), [](const auto& region) {
            return region.parameter == LayoutEditParameter::ColorForeground;
        })) {
        const RenderRect titleTextRect =
            renderer.Renderer()
                .MeasureTextBlock(artifacts.chromeLayout.titleRect,
                    displayCard.title,
                    TextStyleId::Title,
                    TextLayoutOptions::SingleLine(TextHorizontalAlign::Leading, TextVerticalAlign::Center))
                .textRect;
        artifacts.colorRegions.push_back(LayoutEditColorRegion{LayoutEditParameter::ColorForeground, titleTextRect});
    }

    renderer.layoutResolver_->widgetEditGuides_ = std::move(savedWidgetGuides);
    renderer.layoutResolver_->staticEditableAnchorRegions_ = std::move(savedStaticAnchors);
    renderer.layoutResolver_->dynamicEditableAnchorRegions_ = std::move(savedDynamicAnchors);
    renderer.layoutResolver_->dynamicColorEditRegions_ = std::move(savedDynamicColors);
    return artifacts;
}
