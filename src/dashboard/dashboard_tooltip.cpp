#include "dashboard/dashboard_tooltip.h"

#include <commctrl.h>

namespace {

const UINT kTooltipToolInfoSize = TTTOOLINFOA_V2_SIZE;
constexpr UINT kDashboardTooltipFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
constexpr UINT_PTR kDashboardTooltipToolId = 1;
constexpr int kDashboardTooltipAutoPopMs = 30000;

bool RectsEqual(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

bool PointsEqual(POINT left, POINT right) {
    return left.x == right.x && left.y == right.y;
}

}  // namespace

bool DashboardTooltip::Create(HWND owner, HINSTANCE instance, int maxTipWidth) {
    owner_ = owner;
    hwnd_ = CreateWindowExA(WS_EX_TOPMOST,
        TOOLTIPS_CLASSA,
        nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        owner_,
        nullptr,
        instance,
        nullptr);
    if (hwnd_ == nullptr) {
        owner_ = nullptr;
        return false;
    }

    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    RECT clientRect{};
    GetClientRect(owner_, &clientRect);
    targetRect_ = clientRect;
    targetRectValid_ = true;
    text_.clear();

    TOOLINFOA toolInfo = ToolInfo();
    toolInfo.rect = targetRect_;
    toolInfo.lpszText = text_.data();
    const LRESULT addToolResult = SendMessageA(hwnd_, TTM_ADDTOOLA, 0, reinterpret_cast<LPARAM>(&toolInfo));
    const LRESULT activateResult = SendMessageA(hwnd_, TTM_ACTIVATE, TRUE, 0);
    SendMessageA(hwnd_, TTM_SETDELAYTIME, TTDT_INITIAL, 0);
    SendMessageA(hwnd_, TTM_SETDELAYTIME, TTDT_RESHOW, 0);
    SendMessageA(hwnd_, TTM_SETDELAYTIME, TTDT_AUTOPOP, kDashboardTooltipAutoPopMs);
    SetMaxTipWidth(maxTipWidth);
    (void)addToolResult;
    (void)activateResult;
    return true;
}

void DashboardTooltip::Destroy() {
    Hide();
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    owner_ = nullptr;
    text_.clear();
    targetRect_ = {};
    screenPoint_ = {};
    visible_ = false;
    targetRectValid_ = false;
    screenPointValid_ = false;
    maxTipWidth_ = 0;
}

void DashboardTooltip::Hide() {
    if (hwnd_ == nullptr || !visible_) {
        return;
    }

    TOOLINFOA toolInfo = ToolInfo();
    SendMessageA(hwnd_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&toolInfo));
    visible_ = false;
    targetRectValid_ = false;
    screenPointValid_ = false;
}

void DashboardTooltip::SetMaxTipWidth(int maxTipWidth) {
    if (hwnd_ == nullptr || maxTipWidth_ == maxTipWidth) {
        return;
    }
    maxTipWidth_ = maxTipWidth;
    SendMessageA(hwnd_, TTM_SETMAXTIPWIDTH, 0, maxTipWidth_);
}

void DashboardTooltip::ShowOrUpdate(const RECT& targetRect, POINT screenPoint, std::string_view text, int maxTipWidth) {
    if (hwnd_ == nullptr || owner_ == nullptr || text.empty()) {
        Hide();
        return;
    }

    const bool wasVisible = visible_;
    const bool textChanged = text_ != text;
    const bool rectChanged = !targetRectValid_ || !RectsEqual(targetRect_, targetRect);
    const bool pointChanged = !screenPointValid_ || !PointsEqual(screenPoint_, screenPoint);

    if (textChanged) {
        text_ = std::string(text);
    }
    if (rectChanged) {
        targetRect_ = targetRect;
        targetRectValid_ = true;
    }
    if (pointChanged) {
        screenPoint_ = screenPoint;
        screenPointValid_ = true;
    }

    TOOLINFOA toolInfo = ToolInfo();
    toolInfo.rect = targetRect_;
    toolInfo.lpszText = text_.data();
    if (textChanged) {
        SendMessageA(hwnd_, TTM_UPDATETIPTEXTA, 0, reinterpret_cast<LPARAM>(&toolInfo));
    }
    if (rectChanged) {
        SendMessageA(hwnd_, TTM_NEWTOOLRECTA, 0, reinterpret_cast<LPARAM>(&toolInfo));
    }
    SetMaxTipWidth(maxTipWidth);
    if (pointChanged) {
        SendMessageA(hwnd_, TTM_TRACKPOSITION, 0, MAKELPARAM(screenPoint_.x, screenPoint_.y));
    }
    if (!wasVisible) {
        SendMessageA(hwnd_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&toolInfo));
    }
    if (!wasVisible || textChanged || rectChanged || pointChanged) {
        SendMessageA(hwnd_, TTM_UPDATE, 0, 0);
    }
    if (!wasVisible && !IsWindowVisible(hwnd_)) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
    visible_ = true;
}

void DashboardTooltip::RelayMouseMessage(UINT message, WPARAM wParam, LPARAM lParam) const {
    if (hwnd_ == nullptr || owner_ == nullptr || !visible_ || !targetRectValid_) {
        return;
    }

    MSG msg{};
    msg.hwnd = owner_;
    msg.message = message;
    msg.wParam = wParam;
    msg.lParam = lParam;
    SendMessageA(hwnd_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&msg));
}

bool DashboardTooltip::Visible() const {
    return visible_;
}

bool DashboardTooltip::TargetRectValid() const {
    return targetRectValid_;
}

const RECT& DashboardTooltip::TargetRect() const {
    return targetRect_;
}

const std::string& DashboardTooltip::Text() const {
    return text_;
}

TOOLINFOA DashboardTooltip::ToolInfo() const {
    TOOLINFOA toolInfo{};
    toolInfo.cbSize = kTooltipToolInfoSize;
    toolInfo.hwnd = owner_;
    toolInfo.uFlags = kDashboardTooltipFlags;
    toolInfo.uId = kDashboardTooltipToolId;
    return toolInfo;
}
