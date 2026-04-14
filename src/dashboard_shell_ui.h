#pragma once

#include <optional>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "layout_edit_controller.h"
#include "layout_edit_parameter.h"

class DashboardApp;

class DashboardShellUi {
public:
    enum class MenuSource {
        AppWindow,
        TrayIcon,
    };

    explicit DashboardShellUi(DashboardApp& app);
    ~DashboardShellUi();

    bool IsLayoutEditModalUiActive() const;
    void ShowContextMenu(MenuSource source,
        POINT screenPoint,
        const std::optional<LayoutEditController::TooltipTarget>& layoutEditTarget);
    void InvokeDefaultAction(MenuSource source,
        const std::optional<LayoutEditController::TooltipTarget>& layoutEditTarget,
        std::optional<POINT> cursorAnchorClientPoint = std::nullopt);
    void BeginLayoutEditModalUi();
    void EndLayoutEditModalUi();

private:
    UINT ResolveDefaultCommand(
        MenuSource source, const std::optional<LayoutEditController::TooltipTarget>& layoutEditTarget) const;
    void ExecuteCommand(UINT selected,
        const std::optional<LayoutEditController::TooltipTarget>& layoutEditTarget,
        std::optional<POINT> cursorAnchorClientPoint = std::nullopt);
    std::optional<double> PromptCustomScale();
    bool PromptAndApplyLayoutEditTarget(const LayoutEditController::TooltipTarget& target);
    std::optional<double> PromptLayoutEditValue(DashboardRenderer::LayoutEditParameter parameter,
        const LayoutEditTooltipDescriptor& descriptor,
        double initialValue,
        const std::wstring& title);
    std::optional<std::vector<int>> PromptLayoutGuideWeights(
        const DashboardRenderer::LayoutEditGuide& guide, const std::wstring& title);
    std::optional<UiFontConfig> PromptLayoutEditFont(DashboardRenderer::LayoutEditParameter parameter,
        const LayoutEditTooltipDescriptor& descriptor,
        const UiFontConfig& initialValue,
        const std::wstring& title);

    DashboardApp& app_;
};
