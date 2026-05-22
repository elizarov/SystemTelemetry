#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "config/config_primitives.h"
#include "renderer/render_types.h"
#include "widget/card_chrome_layout.h"
#include "widget/layout_edit_types.h"
#include "widget/widget.h"
#include "widget/widget_host.h"

class DashboardRenderer;
class DashboardLayoutEditOverlayRenderer;
struct DashboardOverlayState;

class DashboardLayoutResolver : public WidgetEditArtifactRegistrar {
public:
    struct ResolvedCardLayout {
        std::string id;
        std::string title;
        std::string iconName;
        std::vector<size_t> nodePath;
        RenderRect rect{};
        CardChromeLayout chromeLayout{};
        WidgetLayout chrome;
        std::vector<WidgetLayout> widgets;
    };

    struct ResolvedDashboardLayout {
        int windowWidth = 800;
        int windowHeight = 480;
        std::vector<ResolvedCardLayout> cards;
    };

    struct ParsedWidgetInfo {
        WidgetClass widgetClass = WidgetClass::Unknown;
        int preferredHeight = 0;
        bool fixedPreferredHeightInRows = false;
        bool verticalSpring = false;
    };

    struct ContainerChildReorderTarget {
        std::string renderCardId;
        std::string editCardId;
        std::vector<size_t> nodePath;
        bool horizontal = false;
        std::vector<RenderRect> childRects;
    };

    explicit DashboardLayoutResolver(DashboardRenderer& renderer);

    void Clear();
    void ClearDynamicEditArtifacts();
    void ResolveNodeWidgets(
        DashboardRenderer& renderer,
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<WidgetLayout>& widgets,
        bool instantiateWidgets);
    void BuildWidgetEditGuides(DashboardRenderer& renderer);
    void BuildStaticEditableAnchors(DashboardRenderer& renderer);
    void AddLayoutEditGuide(
        DashboardRenderer& renderer,
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        const std::vector<RenderRect>& childRects,
        int gap,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath,
        const std::vector<LayoutEditOverlayOwner>& overlayOwners);
    void ResolveNodeWidgetsInternal(
        DashboardRenderer& renderer,
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<WidgetLayout>& widgets,
        std::vector<std::string>& cardReferenceStack,
        std::vector<LayoutEditOverlayOwner>& overlayOwners,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath,
        bool instantiateWidgets);
    void SetEditArtifactContext(
        const std::vector<LayoutEditOverlayOwner>& overlayOwners, LayoutEditOverlayAffordanceLayer layer);
    void ResetEditArtifactContext();
    void TagOverlayAffordanceLayers(const DashboardOverlayState& overlayState);
    bool ResolveLayout(DashboardRenderer& renderer, bool includeWidgetState);
    int PreferredNodeHeight(const DashboardRenderer& renderer, const LayoutNodeConfig& node, int width) const;
    const ParsedWidgetInfo* FindParsedWidgetInfo(const DashboardRenderer& renderer, const LayoutNodeConfig& node) const;
    WidgetLayout ResolveWidgetLayout(
        const DashboardRenderer& renderer,
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        bool instantiateWidget) const;
    void RegisterStaticEditAnchor(LayoutEditAnchorRegistration registration) override;
    void RegisterDynamicEditAnchor(LayoutEditAnchorRegistration registration) override;
    void RegisterStaticCornerEditAnchor(const LayoutEditAnchorKey& key, const RenderRect& targetRect) override;
    void RegisterDynamicCornerEditAnchor(const LayoutEditAnchorKey& key, const RenderRect& targetRect) override;
    void RegisterStaticTextAnchor(
        const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt,
        LayoutEditTargetOutline targetOutline = LayoutEditTargetOutline::Visible) override;
    void RegisterDynamicTextAnchor(
        const TextLayoutResult& layoutResult,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt,
        LayoutEditTargetOutline targetOutline = LayoutEditTargetOutline::Visible) override;
    void RegisterDynamicTextAnchor(
        const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt,
        LayoutEditTargetOutline targetOutline = LayoutEditTargetOutline::Visible) override;
    void RegisterStaticColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) override;
    void RegisterDynamicColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) override;
    void RegisterWidgetEditGuide(LayoutEditWidgetGuide guide) override;
    void ResolveDynamicEditArtifactCollisions();

private:
    friend class DashboardRenderer;
    friend class DashboardLayoutEditOverlayRenderer;

    void RegisterEditableAnchorRegion(
        std::vector<LayoutEditAnchorRegion>& regions, const LayoutEditAnchorRegistration& registration);
    void RegisterTextAnchor(
        std::vector<LayoutEditAnchorRegion>& regions,
        const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable,
        LayoutEditTargetOutline targetOutline);
    void RegisterTextAnchor(
        std::vector<LayoutEditAnchorRegion>& regions,
        const TextLayoutResult& layoutResult,
        const LayoutEditAnchorBinding& editable,
        LayoutEditTargetOutline targetOutline);
    void ResolveContainerAnchorCollisions();
    bool OverlayOwnerMatchesActiveDrag(
        const std::vector<LayoutEditOverlayOwner>& owners, const DashboardOverlayState& overlayState) const;
    bool AnchorMatchesActiveMetricListDrag(
        const LayoutEditAnchorRegion& region, const DashboardOverlayState& overlayState) const;
    bool AnchorMatchesActiveContainerChildDrag(
        const LayoutEditAnchorRegion& region, const DashboardOverlayState& overlayState) const;

    DashboardRenderer& renderer_;
    ResolvedDashboardLayout resolvedLayout_{};
    std::vector<LayoutEditGuide> layoutEditGuides_;
    std::vector<ContainerChildReorderTarget> containerChildReorderTargets_;
    std::vector<LayoutEditWidgetGuide> widgetEditGuides_;
    std::vector<LayoutEditGapAnchor> gapEditAnchors_;
    std::vector<LayoutEditAnchorRegion> staticEditableAnchorRegions_;
    std::vector<LayoutEditAnchorRegion> dynamicEditableAnchorRegions_;
    std::vector<LayoutEditColorRegion> staticColorEditRegions_;
    std::vector<LayoutEditColorRegion> dynamicColorEditRegions_;
    bool dynamicAnchorRegistrationEnabled_ = false;
    std::vector<LayoutEditOverlayOwner> currentOverlayOwners_;
    LayoutEditOverlayAffordanceLayer currentOverlayAffordanceLayer_ = LayoutEditOverlayAffordanceLayer::Background;
    // Size: this per-layout cache is tiny; a flat scan measured smaller than std::unordered_map.
    // Size: keep only parsed widget facts here; storing cloneable widget prototypes kept larger per-widget virtual code.
    mutable std::vector<std::pair<const LayoutNodeConfig*, ParsedWidgetInfo>> parsedWidgetInfoCache_;
};
