#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/config.h"
#include "widget/card_chrome_layout.h"
#include "widget/layout_edit_types.h"
#include "widget/render_types.h"
#include "widget/widget.h"

class DashboardRenderer;
class DashboardLayoutEditOverlayRenderer;

class DashboardLayoutResolver {
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

private:
    friend class DashboardRenderer;
    friend class DashboardLayoutEditOverlayRenderer;

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
