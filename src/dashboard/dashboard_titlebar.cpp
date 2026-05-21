#include "dashboard/dashboard_titlebar.h"

#include <algorithm>

namespace {

int RectWidth(const RECT& rect) {
    return rect.right - rect.left;
}

int RectHeight(const RECT& rect) {
    return rect.bottom - rect.top;
}

bool IsRectUsable(const RECT& rect) {
    return RectWidth(rect) > 0 && RectHeight(rect) > 0;
}

bool TitlebarHorizontalAndTopEdgesFitMonitor(const RECT& monitor, const RECT& window) {
    return window.left >= monitor.left && window.top >= monitor.top && window.right <= monitor.right;
}

bool RectsMatch(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

bool PointInUsableRect(POINT point, const RECT& rect) {
    return IsRectUsable(rect) && PtInRect(&rect, point) != FALSE;
}

int NonNegativeMargin(LONG value) {
    return static_cast<int>(std::max<LONG>(0, value));
}

}  // namespace

DashboardTitlebarFrameMargins DashboardTitlebarFrameMarginsFromAdjustedRect(
    const RECT& adjustedRect, int clientWidth, int clientHeight) {
    return DashboardTitlebarFrameMargins{NonNegativeMargin(-adjustedRect.left),
        NonNegativeMargin(-adjustedRect.top),
        NonNegativeMargin(adjustedRect.right - clientWidth),
        NonNegativeMargin(adjustedRect.bottom - clientHeight)};
}

DashboardTitlebarGeometry ResolveDashboardTitlebarFrameGeometry(
    const RECT& dashboardClientRect, DashboardTitlebarFrameMargins margins) {
    DashboardTitlebarGeometry geometry;
    if (!IsRectUsable(dashboardClientRect) || margins.top <= 0) {
        return geometry;
    }

    geometry.windowRect = RECT{dashboardClientRect.left - margins.left,
        dashboardClientRect.top - margins.top,
        dashboardClientRect.right + margins.right,
        dashboardClientRect.bottom + margins.bottom};
    geometry.virtualHoverRect =
        RECT{dashboardClientRect.left, geometry.windowRect.top, dashboardClientRect.right, dashboardClientRect.top};
    geometry.canShow = IsRectUsable(geometry.windowRect) && IsRectUsable(geometry.virtualHoverRect);
    if (!geometry.canShow) {
        geometry.windowRect = {};
        geometry.virtualHoverRect = {};
    }
    return geometry;
}

const char* DashboardTitlebarTooltipLocalizationKey(DashboardTitlebarTooltipControl control) {
    switch (control) {
        case DashboardTitlebarTooltipControl::AppMenu:
            return "titlebar.app_menu";
        case DashboardTitlebarTooltipControl::Layout:
            return "titlebar.layout";
        case DashboardTitlebarTooltipControl::Theme:
            return "titlebar.theme";
        case DashboardTitlebarTooltipControl::EditLayout:
            return "titlebar.edit_layout";
        case DashboardTitlebarTooltipControl::Display:
            return "titlebar.display";
        case DashboardTitlebarTooltipControl::Close:
            return "titlebar.close";
        case DashboardTitlebarTooltipControl::None:
            break;
    }
    return "";
}

DashboardTitlebarTooltipTarget ResolveDashboardTitlebarTooltipTarget(POINT clientPoint,
    const RECT& appMenuRect,
    const RECT& layoutComboRect,
    const RECT& themeComboRect,
    const RECT& editLayoutRect,
    const RECT& displayRect,
    const RECT& closeRect) {
    const struct {
        DashboardTitlebarTooltipControl control;
        const RECT& rect;
    } controls[] = {
        {DashboardTitlebarTooltipControl::Close, closeRect},
        {DashboardTitlebarTooltipControl::Display, displayRect},
        {DashboardTitlebarTooltipControl::EditLayout, editLayoutRect},
        {DashboardTitlebarTooltipControl::Layout, layoutComboRect},
        {DashboardTitlebarTooltipControl::Theme, themeComboRect},
        {DashboardTitlebarTooltipControl::AppMenu, appMenuRect},
    };

    for (const auto& control : controls) {
        if (PointInUsableRect(clientPoint, control.rect)) {
            return DashboardTitlebarTooltipTarget{
                control.control, control.rect, DashboardTitlebarTooltipLocalizationKey(control.control)};
        }
    }
    return {};
}

DashboardTitlebarGeometry ResolveDashboardTitlebarGeometry(
    const RECT& dashboardClientRect, const RECT& monitorRect, DashboardTitlebarFrameMargins margins) {
    if (!IsRectUsable(monitorRect) || RectsMatch(dashboardClientRect, monitorRect)) {
        return {};
    }

    DashboardTitlebarGeometry geometry = ResolveDashboardTitlebarFrameGeometry(dashboardClientRect, margins);
    geometry.canShow = IsRectUsable(geometry.windowRect) && IsRectUsable(geometry.virtualHoverRect) &&
                       TitlebarHorizontalAndTopEdgesFitMonitor(monitorRect, geometry.windowRect);
    if (!geometry.canShow) {
        geometry.windowRect = {};
        geometry.virtualHoverRect = {};
    }
    return geometry;
}
