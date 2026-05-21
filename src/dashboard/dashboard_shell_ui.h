#pragma once

#include <windows.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "display/monitor.h"
#include "layout_edit/layout_edit_controller.h"
#include "layout_edit_dialog/layout_edit_dialog.h"
#include "widget/layout_edit_types.h"

class DashboardApp;

class DashboardShellUi final : public LayoutEditDialogHost {
public:
    enum class MenuSource {
        AppWindow,
        TrayIcon,
    };

    explicit DashboardShellUi(DashboardApp& app);
    ~DashboardShellUi();

    bool IsLayoutEditModalUiActive() const;
    void ShowContextMenu(
        MenuSource source, POINT screenPoint, const LayoutEditController::TooltipTarget* layoutEditTarget);
    void ApplyTitlebarLayoutSelection(size_t index);
    void ApplyTitlebarThemeSelection(size_t index);
    void ShowTitlebarConfigureDisplayMenu(POINT screenPoint);
    void InvokeDefaultAction(MenuSource source,
        const LayoutEditController::TooltipTarget* layoutEditTarget,
        const POINT* cursorAnchorClientPoint = nullptr);
    bool HandleMenuMeasureItem(MEASUREITEMSTRUCT* item);
    bool HandleMenuDrawItem(const DRAWITEMSTRUCT* item);
    void HandleExitRequest();
    void BeginLayoutEditModalUi();
    void EndLayoutEditModalUi();
    bool HandleDialogMessage(MSG* msg) const;
    bool HandleEditLayoutToggle();
    bool ShouldDashboardIgnoreMouse(POINT screenPoint) const;
    void SetLayoutEditTreeSelectionHighlightVisible(bool visible);
    void RefreshDialogIcons();
    void SyncLayoutEditDialogSelection(const LayoutEditController::TooltipTarget* target, bool bringToFront);

private:
    enum class UnsavedLayoutEditAction {
        Save,
        Discard,
        Cancel,
    };

    enum class UnsavedLayoutEditPrompt {
        StopEditing,
        ExitApplication,
        ReloadConfig,
        RunAsAdministrator,
    };

    struct ConfigureDisplayMenuDrawItem {
        UINT commandId = 0;
        DisplayMenuOption option;
    };

    std::optional<UnsavedLayoutEditAction> PromptForUnsavedLayoutEditChanges(UnsavedLayoutEditPrompt prompt) const;
    bool HandleRunAsAdministrator();
    bool HandleReloadConfig();
    bool HandleConfigureDisplay(const DisplayMenuOption& option);
    void ShowAboutDialog() const;
    bool StopLayoutEditSession(UnsavedLayoutEditPrompt prompt);
    bool OpenLayoutEditDialog();
    bool EnsureLayoutEditDialog(
        const std::optional<LayoutEditFocusKey>& focusKey = std::nullopt, bool bringToFront = false);
    void RefreshLayoutEditDialog(const std::optional<LayoutEditFocusKey>& preferredFocus = std::nullopt);
    void RefreshLayoutEditDialogSelection();
    void DestroyLayoutEditDialogWindow();
    HINSTANCE DialogInstance() const;
    HINSTANCE LayoutEditDialogInstance() const override;
    HWND LayoutEditDialogAnchorWindow() const override;
    UINT LayoutEditDialogAnchorDpi() const override;
    AppConfig BuildLayoutEditOriginalConfigSnapshot() const;
    AppConfig BuildLayoutEditOriginalConfig() const override;
    const AppConfig& CurrentConfig() const override;
    void RestoreConfigSnapshot(const AppConfig& config);
    bool ApplyParameterPreview(LayoutEditParameter parameter, double value) override;
    bool ApplyFontPreview(LayoutEditParameter parameter, const UiFontConfig& value) override;
    bool ApplyFontFamilyPreview(const std::string& family) override;
    bool ApplyFontSetPreview(const FontsConfig& fonts) override;
    bool ApplyLayoutPreview(const std::string& layoutName) override;
    bool ApplyThemePreview(const std::string& themeName) override;
    bool ApplyColorPreview(LayoutEditParameter parameter, unsigned int value) override;
    bool ApplyColorExpressionPreview(LayoutEditParameter parameter, const std::string& expression) override;
    bool ApplyThemeColorPreview(const ThemeColorEditKey& key, unsigned int value) override;
    bool ApplyMetricPreview(const LayoutMetricEditKey& key,
        const std::optional<double>& scale,
        const std::string& unit,
        const std::string& label,
        const std::optional<std::string>& binding) override;
    bool ApplyCardTitlePreview(const LayoutCardTitleEditKey& key, const std::string& title) override;
    bool ApplyLayoutEditPreview(const LayoutEditFocusKey& key, const LayoutEditValue& value) override;
    bool ApplyMetricListAddRowPreview(const LayoutEditController::TooltipTarget& target);
    bool ApplyWeightPreview(const LayoutWeightEditKey& key, int firstWeight, int secondWeight) override;
    bool ShouldShowMetricBoardBinding(const LayoutMetricEditKey& key) const override;
    void UpdateLayoutEditSelectionHighlight(const std::optional<LayoutEditSelectionHighlight>& highlight) override;
    void ApplyLayoutEditDialogIcons(HWND dialogHwnd) const override;
    void RestackLayoutEditDialogAnchor(HWND dialogHwnd) override;
    void TraceLayoutEditDialogEvent(const char* event, const std::string& details = {}) const override;
    void OnLayoutEditDialogCloseRequested() override;
    std::vector<std::string> AvailableBoardMetricSensorBindings(const LayoutMetricEditKey& key) const override;
    UINT ResolveDefaultCommand(MenuSource source, const LayoutEditController::TooltipTarget* layoutEditTarget) const;
    size_t BuildConfigureDisplayMenu(HMENU menu, DisplayMenuOption* options, size_t capacity);
    const ConfigureDisplayMenuDrawItem* FindConfigureDisplayMenuDrawItem(UINT commandId, ULONG_PTR itemData) const;
    void ExecuteCommand(UINT selected,
        const LayoutEditController::TooltipTarget* layoutEditTarget,
        const POINT* cursorAnchorClientPoint = nullptr);
    std::optional<double> PromptCustomScale();
    bool PromptAndApplyLayoutEditTarget(const LayoutEditController::TooltipTarget& target);

    DashboardApp& app_;
    std::unique_ptr<LayoutEditDialog> layoutEditDialog_;
    std::vector<ConfigureDisplayMenuDrawItem> configureDisplayMenuDrawItems_;
};
