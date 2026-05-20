#pragma once

#include <windows.h>

struct DashboardTitlebarFrameMargins {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct DashboardTitlebarGeometry {
    bool canShow = false;
    RECT windowRect{};
    RECT virtualHoverRect{};
};

DashboardTitlebarFrameMargins DashboardTitlebarFrameMarginsFromAdjustedRect(
    const RECT& adjustedRect, int clientWidth, int clientHeight);
DashboardTitlebarGeometry ResolveDashboardTitlebarGeometry(
    const RECT& dashboardClientRect, const RECT& monitorRect, DashboardTitlebarFrameMargins margins);
