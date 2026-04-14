#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "render_types.h"

class DashboardRenderer;

struct DashboardLayoutResolver {
    static void ResolveNodeWidgets(DashboardRenderer& renderer,
        const struct LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<struct DashboardWidgetLayout>& widgets,
        bool instantiateWidgets);
    static void BuildWidgetEditGuides(DashboardRenderer& renderer);
    static void BuildStaticEditableAnchors(DashboardRenderer& renderer);
    static void AddLayoutEditGuide(DashboardRenderer& renderer,
        const struct LayoutNodeConfig& node,
        const RenderRect& rect,
        const std::vector<RenderRect>& childRects,
        int gap,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath);
    static void ResolveNodeWidgetsInternal(DashboardRenderer& renderer,
        const struct LayoutNodeConfig& node,
        const RenderRect& rect,
        std::vector<struct DashboardWidgetLayout>& widgets,
        std::vector<std::string>& cardReferenceStack,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath,
        bool instantiateWidgets);
    static bool ResolveLayout(DashboardRenderer& renderer, bool includeWidgetState);
};
