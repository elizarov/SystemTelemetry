#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/config.h"
#include "renderer/render_types.h"
#include "widget/card_chrome_layout.h"
#include "widget/layout_edit_types.h"
#include "widget/widget.h"
#include "widget/widget_host.h"

class DashboardRenderer;
class DashboardLayoutEditOverlayRenderer;

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
        std::unique_ptr<Widget> widgetPrototype;
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
    void ResolveNodeWidgets(DashboardRenderer& renderer,
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<WidgetLayout>& widgets,
        bool instantiateWidgets);
    void BuildWidgetEditGuides(DashboardRenderer& renderer);
    void BuildStaticEditableAnchors(DashboardRenderer& renderer);
    void AddLayoutEditGuide(DashboardRenderer& renderer,
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        const std::vector<RenderRect>& childRects,
        int gap,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath);
    void ResolveNodeWidgetsInternal(DashboardRenderer& renderer,
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<WidgetLayout>& widgets,
        std::vector<std::string>& cardReferenceStack,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath,
        bool instantiateWidgets);
    bool ResolveLayout(DashboardRenderer& renderer, bool includeWidgetState);
    int PreferredNodeHeight(const DashboardRenderer& renderer, const LayoutNodeConfig& node, int width) const;
    const ParsedWidgetInfo* FindParsedWidgetInfo(const DashboardRenderer& renderer, const LayoutNodeConfig& node) const;
    WidgetLayout ResolveWidgetLayout(const DashboardRenderer& renderer,
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        bool instantiateWidget) const;
    void RegisterStaticEditAnchor(LayoutEditAnchorRegistration registration) override;
    void RegisterDynamicEditAnchor(LayoutEditAnchorRegistration registration) override;
    void RegisterStaticCornerEditAnchor(const LayoutEditAnchorKey& key, const RenderRect& targetRect) override;
    void RegisterDynamicCornerEditAnchor(const LayoutEditAnchorKey& key, const RenderRect& targetRect) override;
    void RegisterStaticTextAnchor(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt,
        LayoutEditTargetOutline targetOutline = LayoutEditTargetOutline::Visible) override;
    void RegisterDynamicTextAnchor(const TextLayoutResult& layoutResult,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt,
        LayoutEditTargetOutline targetOutline = LayoutEditTargetOutline::Visible) override;
    void RegisterDynamicTextAnchor(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt,
        LayoutEditTargetOutline targetOutline = LayoutEditTargetOutline::Visible) override;
    void RegisterStaticColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) override;
    void RegisterDynamicColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) override;
    void RegisterWidgetEditGuide(LayoutEditWidgetGuide guide) override;

private:
    friend class DashboardRenderer;
    friend class DashboardLayoutEditOverlayRenderer;

    void RegisterEditableAnchorRegion(
        std::vector<LayoutEditAnchorRegion>& regions, const LayoutEditAnchorRegistration& registration);
    void RegisterTextAnchor(std::vector<LayoutEditAnchorRegion>& regions,
        const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable,
        LayoutEditTargetOutline targetOutline);
    void RegisterTextAnchor(std::vector<LayoutEditAnchorRegion>& regions,
        const TextLayoutResult& layoutResult,
        const LayoutEditAnchorBinding& editable,
        LayoutEditTargetOutline targetOutline);

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
    mutable std::unordered_map<const LayoutNodeConfig*, ParsedWidgetInfo> parsedWidgetInfoCache_;
};
