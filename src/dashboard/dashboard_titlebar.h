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

enum class DashboardTitlebarTooltipControl {
    None,
    AppMenu,
    Layout,
    Theme,
    EditLayout,
    Display,
    Close,
};

struct DashboardTitlebarTooltipTarget {
    DashboardTitlebarTooltipControl control = DashboardTitlebarTooltipControl::None;
    RECT rect{};
    const char* localizationKey = "";
};

DashboardTitlebarFrameMargins DashboardTitlebarFrameMarginsFromAdjustedRect(
    const RECT& adjustedRect, int clientWidth, int clientHeight);
DashboardTitlebarGeometry ResolveDashboardTitlebarFrameGeometry(
    const RECT& dashboardClientRect, DashboardTitlebarFrameMargins margins);
DashboardTitlebarGeometry ResolveDashboardTitlebarGeometry(
    const RECT& dashboardClientRect, const RECT& monitorRect, DashboardTitlebarFrameMargins margins);
const char* DashboardTitlebarTooltipLocalizationKey(DashboardTitlebarTooltipControl control);
DashboardTitlebarTooltipTarget ResolveDashboardTitlebarTooltipTarget(POINT clientPoint,
    const RECT& appMenuRect,
    const RECT& layoutComboRect,
    const RECT& themeComboRect,
    const RECT& editLayoutRect,
    const RECT& displayRect,
    const RECT& closeRect);
