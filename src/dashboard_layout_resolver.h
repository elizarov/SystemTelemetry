#pragma once

#include <cstddef>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class DashboardRenderer;

struct DashboardLayoutResolver {
    static void ResolveNodeWidgets(DashboardRenderer& renderer,
        const struct LayoutNodeConfig& node,
        const RECT& rect,
        std::vector<struct DashboardWidgetLayout>& widgets);
    static void BuildWidgetEditGuides(DashboardRenderer& renderer);
    static void AddLayoutEditGuide(DashboardRenderer& renderer,
        const struct LayoutNodeConfig& node,
        const RECT& rect,
        const std::vector<RECT>& childRects,
        int gap,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath);
    static void ResolveNodeWidgetsInternal(DashboardRenderer& renderer,
        const struct LayoutNodeConfig& node,
        const RECT& rect,
        std::vector<struct DashboardWidgetLayout>& widgets,
        std::vector<std::string>& cardReferenceStack,
        const std::string& renderCardId,
        const std::string& editCardId,
        const std::vector<size_t>& nodePath);
    static bool ResolveLayout(DashboardRenderer& renderer);
};
