#pragma once

class DashboardRenderer;

struct DashboardRendererLayoutEngine {
    static bool ResolveLayout(DashboardRenderer& renderer);
    static void BuildWidgetEditGuides(DashboardRenderer& renderer);
};
