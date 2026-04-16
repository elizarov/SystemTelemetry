#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app_monitor.h"
#include "dashboard_menu_types.h"
#include "layout_edit_controller.h"
#include "layout_edit_parameter.h"

class DashboardApp;
class DashboardShellUiDialogHost;
class LayoutEditConfigDialog;

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
    void HandleExitRequest();
    void BeginLayoutEditModalUi();
    void EndLayoutEditModalUi();
    bool HandleDialogMessage(MSG* msg) const;
    bool HandleEditLayoutToggle();
    bool ShouldDashboardIgnoreMouse(POINT screenPoint) const;
    void SetLayoutEditTreeSelectionHighlightVisible(bool visible);
    void SyncLayoutEditDialogSelection(
        const std::optional<LayoutEditController::TooltipTarget>& target, bool bringToFront);

private:
    friend class DashboardShellUiDialogHost;

    enum class UnsavedLayoutEditAction {
        Save,
        Discard,
        Cancel,
    };

    enum class UnsavedLayoutEditPrompt {
        StopEditing,
        ExitApplication,
        ReloadConfig,
    };

    std::optional<UnsavedLayoutEditAction> PromptForUnsavedLayoutEditChanges(UnsavedLayoutEditPrompt prompt) const;
    bool HandleReloadConfig();
    bool HandleConfigureDisplay(const DisplayMenuOption& option);
    bool StopLayoutEditSession(UnsavedLayoutEditPrompt prompt);
    bool EnsureLayoutEditDialog(
        const std::optional<LayoutEditFocusKey>& focusKey = std::nullopt, bool bringToFront = false);
    void RefreshLayoutEditDialog(const std::optional<LayoutEditFocusKey>& preferredFocus = std::nullopt);
    void RefreshLayoutEditDialogSelection();
    void DestroyLayoutEditDialogWindow();
    HINSTANCE DialogInstance() const;
    AppConfig BuildLayoutEditOriginalConfigSnapshot() const;
    const AppConfig& CurrentConfig() const;
    void RestoreConfigSnapshot(const AppConfig& config);
    bool ApplyParameterPreview(LayoutEditParameter parameter, double value);
    bool ApplyFontPreview(LayoutEditParameter parameter, const UiFontConfig& value);
    bool ApplyColorPreview(LayoutEditParameter parameter, unsigned int value);
    bool ApplyMetricPreview(const LayoutMetricEditKey& key,
        const std::optional<double>& scale,
        const std::string& unit,
        const std::string& label,
        const std::optional<std::string>& binding);
    bool ApplyCardTitlePreview(const LayoutCardTitleEditKey& key, const std::string& title);
    bool ApplyWeightPreview(const LayoutWeightEditKey& key, int firstWeight, int secondWeight);
    void UpdateLayoutEditSelectionHighlight(const std::optional<LayoutEditSelectionHighlight>& highlight);
    void TraceLayoutEditDialogEvent(const std::string& event, const std::string& details = {}) const;
    std::vector<std::string> AvailableBoardMetricSensorBindings(const LayoutMetricEditKey& key) const;
    UINT ResolveDefaultCommand(
        MenuSource source, const std::optional<LayoutEditController::TooltipTarget>& layoutEditTarget) const;
    void ExecuteCommand(UINT selected,
        const std::optional<LayoutEditController::TooltipTarget>& layoutEditTarget,
        std::optional<POINT> cursorAnchorClientPoint = std::nullopt);
    std::optional<double> PromptCustomScale();
    bool PromptAndApplyLayoutEditTarget(const LayoutEditController::TooltipTarget& target);

    DashboardApp& app_;
    std::unique_ptr<DashboardShellUiDialogHost> layoutEditDialogHost_;
    std::unique_ptr<LayoutEditConfigDialog> layoutEditDialog_;
};
