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

struct DashboardTitlebarControlMetrics {
    int buttonWidth = 0;
    int gap = 0;
    int padding = 0;
    int comboHeight = 0;
    int layoutComboMinWidth = 0;
    int layoutComboDesiredWidth = 0;
    int themeComboMinWidth = 0;
    int themeComboDesiredWidth = 0;
};

struct DashboardTitlebarControlLayout {
    RECT appMenuRect{};
    RECT themeComboRect{};
    RECT layoutComboRect{};
    RECT editLayoutRect{};
    RECT displayRect{};
    RECT closeRect{};
    RECT titleTextRect{};
};

DashboardTitlebarFrameMargins DashboardTitlebarFrameMarginsFromAdjustedRect(
    const RECT& adjustedRect, int clientWidth, int clientHeight);
DashboardTitlebarGeometry ResolveDashboardTitlebarFrameGeometry(
    const RECT& dashboardClientRect, DashboardTitlebarFrameMargins margins);
DashboardTitlebarGeometry ResolveDashboardTitlebarGeometry(
    const RECT& dashboardClientRect, const RECT& monitorRect, DashboardTitlebarFrameMargins margins);
DashboardTitlebarControlLayout ResolveDashboardTitlebarControlLayout(
    const RECT& clientRect, const DashboardTitlebarControlMetrics& metrics);
const char* DashboardTitlebarTooltipLocalizationKey(DashboardTitlebarTooltipControl control);
DashboardTitlebarTooltipTarget ResolveDashboardTitlebarTooltipTarget(POINT clientPoint,
    const RECT& appMenuRect,
    const RECT& layoutComboRect,
    const RECT& themeComboRect,
    const RECT& editLayoutRect,
    const RECT& displayRect,
    const RECT& closeRect);
