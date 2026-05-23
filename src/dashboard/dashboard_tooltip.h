#pragma once

#include <windows.h>

#include <commctrl.h>
#include <string>
#include <string_view>

#include "util/trace.h"

class DashboardTooltip {
public:
    bool Create(HWND owner, HINSTANCE instance, int maxTipWidth);
    void Destroy();
    void Hide(std::string_view reason = {});
    void SetTrace(Trace* trace, TracePrefix prefix = TracePrefix::DashboardTooltip);
    void SetMaxTipWidth(int maxTipWidth);
    void ShowOrUpdate(const RECT& targetRect,
        POINT screenPoint,
        std::string_view text,
        int maxTipWidth,
        std::string_view surface,
        std::string_view target);
    void RelayMouseMessage(UINT message, WPARAM wParam, LPARAM lParam) const;

    bool Visible() const;
    bool TargetRectValid() const;
    const RECT& TargetRect() const;
    const std::string& Text() const;

private:
    TOOLINFOA ToolInfo() const;
    void TraceLifecycle(std::string_view event,
        std::string_view reason,
        bool wasVisible,
        bool textChanged,
        bool surfaceChanged,
        bool targetChanged,
        bool rectChanged,
        bool pointChanged,
        bool maxWidthChanged) const;

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    std::string text_;
    std::string surface_;
    std::string target_;
    RECT targetRect_{};
    POINT screenPoint_{};
    Trace* trace_ = nullptr;
    TracePrefix tracePrefix_ = TracePrefix::DashboardTooltip;
    bool visible_ = false;
    bool targetRectValid_ = false;
    bool screenPointValid_ = false;
    int maxTipWidth_ = 0;
};
