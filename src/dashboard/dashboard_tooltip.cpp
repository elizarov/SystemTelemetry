#include "dashboard/dashboard_tooltip.h"

#include <commctrl.h>

#include "util/resource_strings.h"
#include "util/text_format.h"

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

std::string TraceQuotedValue(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
            case '\\':
                AppendFormat(result, "\\\\");
                break;
            case '"':
                AppendFormat(result, "\\\"");
                break;
            case '\r':
                AppendFormat(result, "\\r");
                break;
            case '\n':
                AppendFormat(result, "\\n");
                break;
            case '\t':
                AppendFormat(result, "\\t");
                break;
            default:
                result.push_back(ch);
                break;
        }
    }
    return result;
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
    Hide("destroy");
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    owner_ = nullptr;
    text_.clear();
    surface_.clear();
    target_.clear();
    targetRect_ = {};
    screenPoint_ = {};
    visible_ = false;
    targetRectValid_ = false;
    screenPointValid_ = false;
    maxTipWidth_ = 0;
}

void DashboardTooltip::Hide(std::string_view reason) {
    if (hwnd_ == nullptr || !visible_) {
        return;
    }

    const bool wasVisible = visible_;
    TOOLINFOA toolInfo = ToolInfo();
    SendMessageA(hwnd_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&toolInfo));
    visible_ = false;
    targetRectValid_ = false;
    screenPointValid_ = false;
    TraceLifecycle(RES_STR("hide"), reason, wasVisible, false, false, false, false, false, false);
    surface_.clear();
    target_.clear();
}

void DashboardTooltip::SetTrace(Trace* trace, TracePrefix prefix) {
    trace_ = trace;
    tracePrefix_ = prefix;
}

void DashboardTooltip::SetMaxTipWidth(int maxTipWidth) {
    if (hwnd_ == nullptr || maxTipWidth_ == maxTipWidth) {
        return;
    }
    maxTipWidth_ = maxTipWidth;
    SendMessageA(hwnd_, TTM_SETMAXTIPWIDTH, 0, maxTipWidth_);
}

void DashboardTooltip::ShowOrUpdate(const RECT& targetRect,
    POINT screenPoint,
    std::string_view text,
    int maxTipWidth,
    std::string_view surface,
    std::string_view target) {
    if (hwnd_ == nullptr || owner_ == nullptr || text.empty()) {
        Hide(text.empty() ? "empty_text" : "inactive");
        return;
    }

    const bool wasVisible = visible_;
    const bool textChanged = text_ != text;
    const bool rectChanged = !targetRectValid_ || !RectsEqual(targetRect_, targetRect);
    const bool pointChanged = !screenPointValid_ || !PointsEqual(screenPoint_, screenPoint);
    const bool maxWidthChanged = maxTipWidth_ != maxTipWidth;
    const bool surfaceChanged = surface_ != surface;
    const bool targetChanged = target_ != target;

    if (textChanged) {
        text_ = std::string(text);
    }
    if (surfaceChanged) {
        surface_ = std::string(surface);
    }
    if (targetChanged) {
        target_ = std::string(target);
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
    const ResourceStringId event = wasVisible && !textChanged && !rectChanged && !pointChanged && !maxWidthChanged &&
                                           !surfaceChanged && !targetChanged
                                       ? RES_STR("update_noop")
                                       : (wasVisible ? RES_STR("update") : RES_STR("show"));
    TraceLifecycle(
        event, {}, wasVisible, textChanged, surfaceChanged, targetChanged, rectChanged, pointChanged, maxWidthChanged);
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

void DashboardTooltip::TraceLifecycle(ResourceStringId event,
    std::string_view reason,
    bool wasVisible,
    bool textChanged,
    bool surfaceChanged,
    bool targetChanged,
    bool rectChanged,
    bool pointChanged,
    bool maxWidthChanged) const {
    if (trace_ == nullptr || !trace_->Enabled(tracePrefix_)) {
        return;
    }

    const std::string escapedText = TraceQuotedValue(text_);
    const std::string escapedReason = TraceQuotedValue(reason);
    const std::string escapedSurface = TraceQuotedValue(surface_);
    const std::string escapedTarget = TraceQuotedValue(target_);
    trace_->WriteFmt(tracePrefix_,
        RES_STR("event=\"%s\" surface=\"%s\" target=\"%s\" reason=\"%s\" visible_before=%s visible_after=%s "
                "text_changed=%s surface_changed=%s target_changed=%s rect_changed=%s point_changed=%s "
                "max_width_changed=%s rect=(%ld,%ld,%ld,%ld) screen=(%ld,%ld) max_width=%d text=\"%s\""),
        ResourceStringText(event),
        escapedSurface.c_str(),
        escapedTarget.c_str(),
        escapedReason.c_str(),
        Trace::BoolText(wasVisible),
        Trace::BoolText(visible_),
        Trace::BoolText(textChanged),
        Trace::BoolText(surfaceChanged),
        Trace::BoolText(targetChanged),
        Trace::BoolText(rectChanged),
        Trace::BoolText(pointChanged),
        Trace::BoolText(maxWidthChanged),
        targetRect_.left,
        targetRect_.top,
        targetRect_.right,
        targetRect_.bottom,
        screenPoint_.x,
        screenPoint_.y,
        maxTipWidth_,
        escapedText.c_str());
}
