#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/config.h"
#include "layout_edit_types.h"
#include "render_types.h"
#include "widget/widget.h"

class DashboardRenderer;

class DashboardLayoutResolver {
public:
    struct ResolvedCardLayout {
        std::string id;
        std::string title;
        std::string iconName;
        bool hasHeader = true;
        std::vector<size_t> nodePath;
        RenderRect rect{};
        RenderRect titleRect{};
        RenderRect iconRect{};
        RenderRect contentRect{};
        std::vector<DashboardWidgetLayout> widgets;
    };

    struct ResolvedDashboardLayout {
        int windowWidth = 800;
        int windowHeight = 480;
        std::vector<ResolvedCardLayout> cards;
    };

    struct ParsedWidgetInfo {
        std::unique_ptr<DashboardWidget> widgetPrototype;
        int preferredHeight = 0;
        bool fixedPreferredHeightInRows = false;
        bool verticalSpring = false;
    };

    void Clear();
    void ClearDynamicEditArtifacts();
    void ResolveNodeWidgets(DashboardRenderer& renderer,
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<DashboardWidgetLayout>& widgets,
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
        std::vector<DashboardWidgetLayout>& widgets,
        std::vector<std::string>& cardReferenceStack,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath,
        bool instantiateWidgets);
    bool ResolveLayout(DashboardRenderer& renderer, bool includeWidgetState);
    int PreferredNodeHeight(const DashboardRenderer& renderer, const LayoutNodeConfig& node, int width) const;
    const ParsedWidgetInfo* FindParsedWidgetInfo(const DashboardRenderer& renderer, const LayoutNodeConfig& node) const;
    DashboardWidgetLayout ResolveWidgetLayout(const DashboardRenderer& renderer,
        const LayoutNodeConfig& node,
        const RenderRect& rect,
        bool instantiateWidget) const;

private:
    friend class DashboardRenderer;

    ResolvedDashboardLayout resolvedLayout_{};
    std::vector<LayoutEditGuide> layoutEditGuides_;
    std::vector<LayoutEditWidgetGuide> widgetEditGuides_;
    std::vector<LayoutEditGapAnchor> gapEditAnchors_;
    std::vector<LayoutEditAnchorRegion> staticEditableAnchorRegions_;
    std::vector<LayoutEditAnchorRegion> dynamicEditableAnchorRegions_;
    std::vector<LayoutEditColorRegion> staticColorEditRegions_;
    std::vector<LayoutEditColorRegion> dynamicColorEditRegions_;
    bool dynamicAnchorRegistrationEnabled_ = false;
    mutable std::unordered_map<const LayoutNodeConfig*, ParsedWidgetInfo> parsedWidgetInfoCache_;
};
