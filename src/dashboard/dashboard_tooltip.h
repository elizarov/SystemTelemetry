#pragma once

#include <windows.h>

#include <commctrl.h>
#include <string>
#include <string_view>

class DashboardTooltip {
public:
    bool Create(HWND owner, HINSTANCE instance, int maxTipWidth);
    void Destroy();
    void Hide();
    void SetMaxTipWidth(int maxTipWidth);
    void ShowOrUpdate(const RECT& targetRect, POINT screenPoint, std::string_view text, int maxTipWidth);
    void RelayMouseMessage(UINT message, WPARAM wParam, LPARAM lParam) const;

    bool Visible() const;
    bool TargetRectValid() const;
    const RECT& TargetRect() const;
    const std::string& Text() const;

private:
    TOOLINFOA ToolInfo() const;

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    std::string text_;
    RECT targetRect_{};
    POINT screenPoint_{};
    bool visible_ = false;
    bool targetRectValid_ = false;
    bool screenPointValid_ = false;
    int maxTipWidth_ = 0;
};
