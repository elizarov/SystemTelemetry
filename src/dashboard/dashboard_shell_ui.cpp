#include "dashboard/dashboard_shell_ui.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>

#include "build_version.h"
#include "config/config_telemetry.h"
#include "config/metric_board_binding.h"
#include "dashboard/constants.h"
#include "dashboard/dashboard_app.h"
#include "dashboard/dashboard_menu_format.h"
#include "diagnostics/diagnostics.h"
#include "display/constants.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_target_descriptor.h"
#include "layout_edit/layout_edit_tooltip_payload.h"
#include "layout_edit/layout_edit_tree.h"
#include "layout_edit_dialog/layout_edit_dialog.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "resource.h"
#include "telemetry/metrics.h"
#include "util/command_line.h"
#include "util/elevated_process.h"
#include "util/localization_catalog.h"
#include "util/message_box.h"
#include "util/numeric_format.h"
#include "util/paths.h"
#include "util/text_format.h"
#include "util/trace.h"

namespace {

constexpr std::string_view kMetricListPlaceholderId = "nothing";

class DashboardShellUiModalScope {
public:
    explicit DashboardShellUiModalScope(DashboardShellUi& shellUi) : shellUi_(shellUi) {
        shellUi_.BeginLayoutEditModalUi();
    }

    ~DashboardShellUiModalScope() {
        shellUi_.EndLayoutEditModalUi();
    }

private:
    DashboardShellUi& shellUi_;
};

class DialogRedrawScope {
public:
    explicit DialogRedrawScope(HWND hwnd) : hwnd_(hwnd) {
        if (hwnd_ != nullptr) {
            SendMessageA(hwnd_, WM_SETREDRAW, FALSE, 0);
        }
    }

    ~DialogRedrawScope() {
        if (hwnd_ != nullptr) {
            SendMessageA(hwnd_, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
    }

    DialogRedrawScope(const DialogRedrawScope&) = delete;
    DialogRedrawScope& operator=(const DialogRedrawScope&) = delete;

private:
    HWND hwnd_ = nullptr;
};

constexpr double kPredefinedDisplayScales[] = {1.0, 1.5, 2.0, 2.5, 3.0};
constexpr double kScaleEpsilon = 0.0001;
constexpr size_t kConfigureDisplayMenuCapacity = kCommandConfigureDisplayMax - kCommandConfigureDisplayBase + 1;

struct UnsavedLayoutEditDialogState {
    const DashboardApp* app = nullptr;
    const char* mainInstruction = "";
    const char* content = "";
    int selectedButton = IDCANCEL;
};

struct AboutDialogState {
    const DashboardApp* app = nullptr;
    std::string text;
    HICON icon = nullptr;
};

BOOL AppendMenuText(HMENU menu, UINT flags, UINT_PTR id, std::string_view text) {
    return AppendMenuA(menu, flags, id, std::string(text).c_str());
}

int MakeAboutIconSlotSquare(HWND hwnd) {
    HWND iconHwnd = GetDlgItem(hwnd, IDC_ABOUT_ICON);
    if (iconHwnd == nullptr) {
        return 0;
    }

    RECT iconRect{};
    if (!GetWindowRect(iconHwnd, &iconRect)) {
        return 0;
    }

    POINT topLeft{iconRect.left, iconRect.top};
    ScreenToClient(hwnd, &topLeft);

    const int width = iconRect.right - iconRect.left;
    const int height = iconRect.bottom - iconRect.top;
    const int size = std::min(width, height);
    if (size <= 0) {
        return 0;
    }

    SetWindowPos(iconHwnd, nullptr, topLeft.x, topLeft.y, size, size, SWP_NOACTIVATE | SWP_NOZORDER);
    return size;
}

AppConfig BuildLayoutEditOriginalConfig(const DashboardSessionState& sessionState) {
    AppConfig config = sessionState.config;
    if (sessionState.layoutEditSessionSavedLayout != nullptr) {
        config.layout = *sessionState.layoutEditSessionSavedLayout;
    }
    return config;
}

bool AreScalesEqual(double left, double right) {
    return std::abs(left - right) < kScaleEpsilon;
}

INT_PTR CALLBACK UnsavedLayoutEditDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<UnsavedLayoutEditDialogState*>(GetWindowLongPtrA(hwnd, DWLP_USER));
    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<UnsavedLayoutEditDialogState*>(lParam);
            SetWindowLongPtrA(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            if (state->app != nullptr) {
                state->app->ApplyThemedIconsToWindow(hwnd);
            }
            SetDlgItemTextA(hwnd, IDC_UNSAVED_LAYOUT_EDIT_MAIN, state->mainInstruction);
            SetDlgItemTextA(hwnd, IDC_UNSAVED_LAYOUT_EDIT_CONTENT, state->content);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_UNSAVED_LAYOUT_EDIT_SAVE:
                    state->selectedButton = IDC_UNSAVED_LAYOUT_EDIT_SAVE;
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                case IDC_UNSAVED_LAYOUT_EDIT_DISCARD:
                    state->selectedButton = IDC_UNSAVED_LAYOUT_EDIT_DISCARD;
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                case IDCANCEL:
                    state->selectedButton = IDCANCEL;
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
        case WM_CLOSE:
            if (state != nullptr) {
                state->selectedButton = IDCANCEL;
            }
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK AboutDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AboutDialogState*>(GetWindowLongPtrA(hwnd, DWLP_USER));
    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<AboutDialogState*>(lParam);
            SetWindowLongPtrA(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            if (state != nullptr && state->app != nullptr) {
                state->app->ApplyThemedIconsToWindow(hwnd);
            }
            SetDlgItemTextA(hwnd, IDC_ABOUT_TEXT, state != nullptr ? state->text.c_str() : "");
            if (state != nullptr && state->app != nullptr) {
                const int iconSize = MakeAboutIconSlotSquare(hwnd);
                state->icon = state->app->CreateThemedAppIconForSize(iconSize);
            }
            if (state != nullptr && state->icon != nullptr) {
                SendDlgItemMessageA(
                    hwnd, IDC_ABOUT_ICON, STM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(state->icon));
            }
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, LOWORD(wParam));
                return TRUE;
            }
            break;
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

bool IsPredefinedDisplayScale(double scale) {
    for (double predefinedScale : kPredefinedDisplayScales) {
        if (AreScalesEqual(scale, predefinedScale)) {
            return true;
        }
    }
    return false;
}

std::string FormatScaleLabel(double scale) {
    return FormatText("%s%%", FormatDoubleGeneral(scale * 100.0, 12).c_str());
}

std::string FormatScalePercentageValue(double scale) {
    return FormatDoubleGeneral(scale * 100.0, 12);
}

std::string FormatNamedMenuLabel(std::string_view name, std::string_view description) {
    return description.empty() ? std::string(name)
                               : FormatText("%.*s - %.*s",
                                     static_cast<int>(name.size()),
                                     name.data(),
                                     static_cast<int>(description.size()),
                                     description.data());
}

size_t BuildScaleMenuEntries(double currentScale, double* entries, size_t capacity) {
    size_t count = 0;
    for (double predefinedScale : kPredefinedDisplayScales) {
        if (count < capacity) {
            entries[count++] = predefinedScale;
        }
    }
    if (!HasExplicitDisplayScale(currentScale) || IsPredefinedDisplayScale(currentScale) || count >= capacity) {
        return count;
    }
    size_t insertAt = 0;
    while (insertAt < count && entries[insertAt] < currentScale) {
        ++insertAt;
    }
    for (size_t i = count; i > insertAt; --i) {
        entries[i] = entries[i - 1];
    }
    entries[insertAt] = currentScale;
    return count + 1;
}

void SetMenuItemRadioStyle(HMENU menu, UINT commandId) {
    MENUITEMINFOA info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE;
    info.fType = MFT_RADIOCHECK;
    SetMenuItemInfoA(menu, commandId, FALSE, &info);
}

std::string BuildLayoutEditMenuLabel(std::string_view subject) {
    return FormatText("Edit %.*s ...", static_cast<int>(subject.size()), subject.data());
}

std::string BuildAboutText() {
    std::string text = FormatText("CaseDash %s\n%s",
        casedash::version::kVersion,
        casedash::version::kOfficialRelease ? "Official release" : "Development build");
    if (std::string_view(casedash::version::kGitCommitShort) != "unknown") {
        AppendFormat(text, "\nCommit %s", casedash::version::kGitCommitShort);
        if (casedash::version::kGitDirty) {
            AppendFormat(text, " (dirty)");
        }
    }
    AppendFormat(text,
        "\n\nA compact dashboard for dedicated PC telemetry screens."
        "\nCopyright (c) Roman Elizarov."
        "\nLicensed under the Apache License 2.0.");
    return text;
}

bool IsMetricListAddRowTarget(const LayoutEditController::TooltipTarget& target) {
    const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&target.payload);
    const auto nodeFieldKey = anchor != nullptr ? LayoutEditAnchorNodeFieldKey(anchor->key) : std::nullopt;
    return anchor != nullptr && anchor->shape == AnchorShape::Plus && nodeFieldKey.has_value() &&
           nodeFieldKey->widgetClass == WidgetClass::MetricList;
}

std::string BuildLayoutGuideEditLabel(const LayoutEditGuide& guide) {
    return guide.editCardId.empty() ? "cards weights" : "layout weights";
}

void AppendQuotedCommandLineArgument(std::string& parameters, std::string_view argument) {
    if (!parameters.empty()) {
        AppendFormat(parameters, " ");
    }
    AppendFormat(parameters, "%s", QuoteCommandLineArgument(argument).c_str());
}

std::string BuildElevatedRestartParameters() {
    const CommandLineArguments arguments = GetCommandLineArguments();
    std::string parameters = BuildCommandLineExcludingSwitch(arguments, "/elevate");
    if (!HasSwitch(arguments, "/bring-to-front")) {
        AppendQuotedCommandLineArgument(parameters, "/bring-to-front");
    }
    return parameters;
}

struct CustomScaleDialogState {
    const DashboardApp* app = nullptr;
    double initialScale = 1.0;
    std::optional<double> result;
};

struct LayoutEditTreeItemBinding {
    const LayoutEditTreeNode* node = nullptr;
    HTREEITEM item = nullptr;
};

INT_PTR CALLBACK CustomScaleDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<CustomScaleDialogState*>(GetWindowLongPtrA(hwnd, DWLP_USER));
    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<CustomScaleDialogState*>(lParam);
            SetWindowLongPtrA(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            if (state->app != nullptr) {
                state->app->ApplyThemedIconsToWindow(hwnd);
            }
            const std::string initialText = FormatScalePercentageValue(state->initialScale);
            SetDlgItemTextA(hwnd, IDC_CUSTOM_SCALE_EDIT, initialText.c_str());
            SendDlgItemMessageA(hwnd, IDC_CUSTOM_SCALE_EDIT, EM_SETSEL, 0, -1);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    char buffer[64] = {};
                    GetDlgItemTextA(hwnd, IDC_CUSTOM_SCALE_EDIT, buffer, ARRAYSIZE(buffer));
                    const std::optional<double> percentage = TryParseScaleValue(buffer);
                    if (!percentage.has_value()) {
                        ShowAppMessageBox(
                            hwnd, FindLocalizedText(RES_STR("dashboard.message.scale_positive_percent")), MB_ICONERROR);
                        SetFocus(GetDlgItem(hwnd, IDC_CUSTOM_SCALE_EDIT));
                        SendDlgItemMessageA(hwnd, IDC_CUSTOM_SCALE_EDIT, EM_SETSEL, 0, -1);
                        return TRUE;
                    }
                    state->result = *percentage / 100.0;
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

}  // namespace

DashboardShellUi::DashboardShellUi(DashboardApp& app)
    : app_(app), layoutEditDialog_(std::make_unique<LayoutEditDialog>(*this)) {}

DashboardShellUi::~DashboardShellUi() {
    DestroyLayoutEditDialogWindow();
}

bool DashboardShellUi::HandleDialogMessage(MSG* msg) const {
    return layoutEditDialog_ != nullptr && layoutEditDialog_->HandleDialogMessage(msg);
}

bool DashboardShellUi::ShouldDashboardIgnoreMouse(POINT screenPoint) const {
    return layoutEditDialog_ != nullptr && layoutEditDialog_->ShouldDashboardIgnoreMouse(screenPoint);
}

void DashboardShellUi::SetLayoutEditTreeSelectionHighlightVisible(bool visible) {
    if (layoutEditDialog_ != nullptr) {
        layoutEditDialog_->SetSelectionHighlightVisible(visible);
    }
}

void DashboardShellUi::RefreshDialogIcons() {
    if (layoutEditDialog_ != nullptr) {
        layoutEditDialog_->RefreshIcons();
    }
}

void DashboardShellUi::DestroyLayoutEditDialogWindow() {
    if (layoutEditDialog_ != nullptr) {
        layoutEditDialog_->Close();
    }
}

bool DashboardShellUi::EnsureLayoutEditDialog(const std::optional<LayoutEditFocusKey>& focusKey, bool bringToFront) {
    return layoutEditDialog_ != nullptr && layoutEditDialog_->Ensure(focusKey, bringToFront);
}

void DashboardShellUi::RefreshLayoutEditDialog(const std::optional<LayoutEditFocusKey>& preferredFocus) {
    if (layoutEditDialog_ != nullptr) {
        app_.TraceLayoutEditUiEventFmt(TracePrefix::LayoutEditDialog,
            "refresh_begin",
            "preferred_focus=\"%s\"",
            preferredFocus.has_value() ? "set" : "none");
        layoutEditDialog_->Refresh(preferredFocus);
        layoutEditDialog_->SetSelectionHighlightVisible(true);
        layoutEditDialog_->RestackAnchor();
        app_.TraceLayoutEditUiEvent(TracePrefix::LayoutEditDialog, "refresh_done");
    }
}

void DashboardShellUi::RefreshLayoutEditDialogSelection() {
    if (layoutEditDialog_ != nullptr) {
        layoutEditDialog_->RefreshSelection();
    }
}

void DashboardShellUi::SyncLayoutEditDialogSelection(
    const LayoutEditController::TooltipTarget* target, bool bringToFront) {
    if (layoutEditDialog_ != nullptr && !layoutEditDialog_->SyncSelection(target, bringToFront)) {
        ShowAppMessageBox(
            app_.hwnd_, FindLocalizedText(RES_STR("dashboard.message.layout_edit_dialog_open_failed")), MB_ICONERROR);
    }
}

std::optional<DashboardShellUi::UnsavedLayoutEditAction> DashboardShellUi::PromptForUnsavedLayoutEditChanges(
    UnsavedLayoutEditPrompt prompt) const {
    DashboardShellUiModalScope scopedModalUi(const_cast<DashboardShellUi&>(*this));
    UnsavedLayoutEditDialogState state;
    state.app = &app_;
    switch (prompt) {
        case UnsavedLayoutEditPrompt::StopEditing:
            state.mainInstruction = FindLocalizedText(RES_STR("dashboard.message.layout_edit_stop_prompt_title"));
            state.content = FindLocalizedText(RES_STR("dashboard.message.layout_edit_unsaved_content"));
            break;
        case UnsavedLayoutEditPrompt::ExitApplication:
            state.mainInstruction = FindLocalizedText(RES_STR("dashboard.message.layout_edit_exit_prompt_title"));
            state.content = FindLocalizedText(RES_STR("dashboard.message.layout_edit_exit_prompt_content"));
            break;
        case UnsavedLayoutEditPrompt::ReloadConfig:
            state.mainInstruction = FindLocalizedText(RES_STR("dashboard.message.layout_edit_reload_prompt_title"));
            state.content = FindLocalizedText(RES_STR("dashboard.message.layout_edit_reload_prompt_content"));
            break;
        case UnsavedLayoutEditPrompt::RunAsAdministrator:
            state.mainInstruction = FindLocalizedText(RES_STR("dashboard.message.layout_edit_elevate_prompt_title"));
            state.content = FindLocalizedText(RES_STR("dashboard.message.layout_edit_elevate_prompt_content"));
            break;
    }

    DialogBoxParamA(app_.instance_,
        MAKEINTRESOURCEA(IDD_UNSAVED_LAYOUT_EDIT),
        app_.hwnd_,
        UnsavedLayoutEditDialogProc,
        reinterpret_cast<LPARAM>(&state));

    switch (state.selectedButton) {
        case IDC_UNSAVED_LAYOUT_EDIT_SAVE:
            return UnsavedLayoutEditAction::Save;
        case IDC_UNSAVED_LAYOUT_EDIT_DISCARD:
            return UnsavedLayoutEditAction::Discard;
        default:
            return UnsavedLayoutEditAction::Cancel;
    }
}

bool DashboardShellUi::StopLayoutEditSession(UnsavedLayoutEditPrompt prompt) {
    DashboardSessionState& state = app_.controller_.State();
    if (!state.isEditingLayout) {
        DestroyLayoutEditDialogWindow();
        return true;
    }

    if (app_.controller_.HasUnsavedLayoutEditChanges()) {
        const auto action = PromptForUnsavedLayoutEditChanges(prompt);
        if (!action.has_value() || *action == UnsavedLayoutEditAction::Cancel) {
            return false;
        }
        if (*action == UnsavedLayoutEditAction::Save) {
            if (!app_.controller_.UpdateConfigFromCurrentPlacement(app_)) {
                return false;
            }
        } else if (!app_.controller_.RestoreLayoutEditSessionSavedLayout(app_)) {
            ShowAppMessageBox(
                app_.hwnd_, FindLocalizedText(RES_STR("dashboard.message.layout_edit_restore_failed")), MB_ICONERROR);
            return false;
        }
    }

    app_.controller_.StopLayoutEditMode(app_, app_.layoutEditController_, app_.diagnosticsOptions_.editLayout);
    app_.HideLayoutEditTooltip();
    DestroyLayoutEditDialogWindow();
    app_.InvalidateNativeTitlebar();
    return true;
}

bool DashboardShellUi::HandleEditLayoutToggle() {
    DashboardSessionState& state = app_.controller_.State();
    if (!state.isEditingLayout) {
        app_.controller_.StartLayoutEditMode(app_, app_.layoutEditController_);
        app_.InvalidateNativeTitlebar();
        return true;
    }

    return StopLayoutEditSession(UnsavedLayoutEditPrompt::StopEditing);
}

bool DashboardShellUi::HandleRunAsAdministrator() {
    if (!StopLayoutEditSession(UnsavedLayoutEditPrompt::RunAsAdministrator)) {
        return false;
    }

    if (!RunElevatedSelf(app_.hwnd_, BuildElevatedRestartParameters(), GetWorkingDirectory(), SW_SHOWNORMAL)) {
        ShowAppMessageBox(
            app_.hwnd_, FindLocalizedText(RES_STR("dashboard.message.run_as_administrator_failed")), MB_ICONERROR);
        return false;
    }

    DestroyWindow(app_.hwnd_);
    return true;
}

bool DashboardShellUi::OpenLayoutEditDialog() {
    DashboardSessionState& state = app_.controller_.State();
    const bool startedLayoutEdit = !state.isEditingLayout;
    if (startedLayoutEdit) {
        app_.controller_.StartLayoutEditMode(app_, app_.layoutEditController_);
        app_.InvalidateNativeTitlebar();
    }

    if (!EnsureLayoutEditDialog(std::nullopt, true)) {
        if (startedLayoutEdit) {
            app_.controller_.StopLayoutEditMode(app_, app_.layoutEditController_, app_.diagnosticsOptions_.editLayout);
            app_.InvalidateNativeTitlebar();
        }
        ShowAppMessageBox(
            app_.hwnd_, FindLocalizedText(RES_STR("dashboard.message.layout_edit_dialog_open_failed")), MB_ICONERROR);
        return false;
    }
    return true;
}

bool DashboardShellUi::HandleReloadConfig() {
    if (app_.controller_.State().isEditingLayout && app_.controller_.HasUnsavedLayoutEditChanges()) {
        const auto action = PromptForUnsavedLayoutEditChanges(UnsavedLayoutEditPrompt::ReloadConfig);
        if (!action.has_value() || *action == UnsavedLayoutEditAction::Cancel) {
            return false;
        }
        if (*action == UnsavedLayoutEditAction::Save && !app_.controller_.UpdateConfigFromCurrentPlacement(app_)) {
            return false;
        }
    }

    if (!app_.controller_.ReloadConfigFromDisk(app_, app_.diagnosticsOptions_)) {
        ShowAppMessageBox(
            app_.hwnd_, FindLocalizedText(RES_STR("dashboard.message.reload_config_failed")), MB_ICONERROR);
        return false;
    }
    RefreshLayoutEditDialog();
    app_.InvalidateNativeTitlebar();
    return true;
}

bool DashboardShellUi::HandleConfigureDisplay(const DisplayMenuOption& option) {
    const bool wasEditingLayout = app_.controller_.State().isEditingLayout;
    if (!app_.controller_.ConfigureDisplay(app_, option)) {
        return false;
    }
    if (wasEditingLayout) {
        app_.controller_.StopLayoutEditMode(app_, app_.layoutEditController_, app_.diagnosticsOptions_.editLayout);
        app_.HideLayoutEditTooltip();
        DestroyLayoutEditDialogWindow();
        app_.InvalidateNativeTitlebar();
    }
    return true;
}

void DashboardShellUi::ApplyTitlebarLayoutSelection(size_t index) {
    if (index >= app_.controller_.State().config.layout.layouts.size() ||
        (kCommandLayoutBase + index) > kCommandLayoutMax) {
        return;
    }
    ExecuteCommand(kCommandLayoutBase + static_cast<UINT>(index), nullptr);
}

void DashboardShellUi::ApplyTitlebarThemeSelection(size_t index) {
    if (index >= app_.controller_.State().config.layout.themes.size() ||
        (kCommandThemeBase + index) > kCommandThemeMax) {
        return;
    }
    ExecuteCommand(kCommandThemeBase + static_cast<UINT>(index), nullptr);
}

void DashboardShellUi::ShowTitlebarConfigureDisplayMenu(POINT screenPoint) {
    app_.HideLayoutEditTooltip();
    DashboardShellUiModalScope scopedModalUi(*this);
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    DisplayMenuOption configDisplayOptions[kConfigureDisplayMenuCapacity];
    const size_t configDisplayOptionCount =
        BuildConfigureDisplayMenu(menu, configDisplayOptions, kConfigureDisplayMenuCapacity);
    SetForegroundWindow(app_.hwnd_);
    const UINT selected = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x,
        screenPoint.y,
        0,
        app_.hwnd_,
        nullptr);
    DestroyMenu(menu);

    if (selected >= kCommandConfigureDisplayBase && selected <= kCommandConfigureDisplayMax) {
        const size_t index = selected - kCommandConfigureDisplayBase;
        if (index < configDisplayOptionCount) {
            HandleConfigureDisplay(configDisplayOptions[index]);
        }
    }
}

void DashboardShellUi::HandleExitRequest() {
    if (app_.controller_.State().isEditingLayout && app_.controller_.HasUnsavedLayoutEditChanges()) {
        const auto action = PromptForUnsavedLayoutEditChanges(UnsavedLayoutEditPrompt::ExitApplication);
        if (!action.has_value() || *action == UnsavedLayoutEditAction::Cancel) {
            return;
        }
        if (*action == UnsavedLayoutEditAction::Save && !app_.controller_.UpdateConfigFromCurrentPlacement(app_)) {
            return;
        }
    }
    DestroyLayoutEditDialogWindow();
    DestroyWindow(app_.hwnd_);
}

void DashboardShellUi::TraceLayoutEditDialogEvent(const char* event, const std::string& details) const {
    const auto& state = app_.controller_.State();
    if (state.diagnostics == nullptr) {
        return;
    }

    if (details.empty()) {
        state.diagnostics->WriteTraceMarker(TracePrefix::LayoutEditDialog, event);
    } else {
        std::string text = FormatText("%s %s", event, details.c_str());
        state.diagnostics->WriteTraceMarker(TracePrefix::LayoutEditDialog, text);
    }
}

bool DashboardShellUi::IsLayoutEditModalUiActive() const {
    return app_.layoutEditModalUiDepth_ > 0;
}

void DashboardShellUi::ShowAboutDialog() const {
    AboutDialogState state;
    state.app = &app_;
    state.text = BuildAboutText();
    DialogBoxParamA(
        app_.instance_, MAKEINTRESOURCEA(IDD_ABOUT), app_.hwnd_, AboutDialogProc, reinterpret_cast<LPARAM>(&state));
    if (state.icon != nullptr) {
        DestroyIcon(state.icon);
    }
}

void DashboardShellUi::BeginLayoutEditModalUi() {
    app_.TraceLayoutEditUiEventFmt(
        TracePrefix::LayoutEditModal, "begin_request", "depth_before=\"%d\"", app_.layoutEditModalUiDepth_);
    ++app_.layoutEditModalUiDepth_;
    if (app_.layoutEditModalUiDepth_ == 1 && app_.controller_.State().isEditingLayout) {
        app_.layoutEditController_.CancelInteraction();
    }
    app_.HideLayoutEditTooltip();
    app_.layoutEditMouseTracking_ = false;
    app_.UpdateNativeTitlebarProbe();
    SetCursor(LoadCursorA(nullptr, IDC_ARROW));
    app_.TraceLayoutEditUiEventFmt(
        TracePrefix::LayoutEditModal, "begin_done", "depth_after=\"%d\"", app_.layoutEditModalUiDepth_);
}

void DashboardShellUi::EndLayoutEditModalUi() {
    if (app_.layoutEditModalUiDepth_ <= 0) {
        app_.layoutEditModalUiDepth_ = 0;
        return;
    }
    app_.TraceLayoutEditUiEventFmt(
        TracePrefix::LayoutEditModal, "end_request", "depth_before=\"%d\"", app_.layoutEditModalUiDepth_);
    --app_.layoutEditModalUiDepth_;
    if (app_.layoutEditModalUiDepth_ == 0) {
        ReleaseCapture();
        app_.layoutEditMouseTracking_ = false;
        app_.TraceLayoutEditUiEvent(TracePrefix::LayoutEditModal, "end_released_capture");
        app_.RefreshLayoutEditHoverFromCursor();
        app_.UpdateNativeTitlebarHoverFromCursor();
    }
    app_.TraceLayoutEditUiEventFmt(
        TracePrefix::LayoutEditModal, "end_done", "depth_after=\"%d\"", app_.layoutEditModalUiDepth_);
}

HINSTANCE DashboardShellUi::DialogInstance() const {
    return app_.instance_;
}

HINSTANCE DashboardShellUi::LayoutEditDialogInstance() const {
    return DialogInstance();
}

HWND DashboardShellUi::LayoutEditDialogAnchorWindow() const {
    return app_.WindowHandle();
}

UINT DashboardShellUi::LayoutEditDialogAnchorDpi() const {
    return app_.CurrentWindowDpi();
}

AppConfig DashboardShellUi::BuildLayoutEditOriginalConfigSnapshot() const {
    return ::BuildLayoutEditOriginalConfig(app_.controller_.State());
}

AppConfig DashboardShellUi::BuildLayoutEditOriginalConfig() const {
    return BuildLayoutEditOriginalConfigSnapshot();
}

const AppConfig& DashboardShellUi::CurrentConfig() const {
    return app_.controller_.State().config;
}

bool DashboardShellUi::ShouldShowMetricBoardBinding(const LayoutMetricEditKey& key) const {
    const auto& state = app_.controller_.State();
    return ShouldExposeMetricBoardBinding(key.metricId, state.telemetryUpdate.dump.activeMetricBoardBindings);
}

std::vector<std::string> DashboardShellUi::AvailableBoardMetricSensorBindings(const LayoutMetricEditKey& key) const {
    if (!ShouldShowMetricBoardBinding(key)) {
        return {};
    }
    const auto target = ResolveMetricBoardBindingTarget(key.metricId);
    if (!target.has_value()) {
        return {};
    }

    const auto& state = app_.controller_.State();
    if (state.telemetry == nullptr) {
        return {};
    }

    const BoardVendorTelemetrySample sample = state.telemetryUpdate.dump.boardProvider;
    return target->kind == BoardMetricBindingKind::Temperature ? sample.availableTemperatureNames
                                                               : sample.availableFanNames;
}

void DashboardShellUi::RestoreConfigSnapshot(const AppConfig& config) {
    app_.controller_.ApplyConfigSnapshot(app_, config);
}

bool DashboardShellUi::ApplyParameterPreview(LayoutEditParameter parameter, double value) {
    return app_.ApplyLayoutEditValue(parameter, value);
}

bool DashboardShellUi::ApplyFontPreview(LayoutEditParameter parameter, const UiFontConfig& value) {
    return app_.controller_.ApplyLayoutEditFont(app_, parameter, value);
}

bool DashboardShellUi::ApplyFontFamilyPreview(const std::string& family) {
    // Size: font previews mutate layout.fonts directly; copying full AppConfig measured larger.
    return app_.controller_.ApplyLayoutEditFontFamily(app_, family);
}

bool DashboardShellUi::ApplyFontSetPreview(const FontsConfig& fonts) {
    return app_.controller_.ApplyLayoutEditFontSet(app_, fonts);
}

bool DashboardShellUi::ApplyLayoutPreview(const std::string& layoutName) {
    return app_.controller_.SwitchLayout(app_, layoutName, app_.diagnosticsOptions_.editLayout);
}

bool DashboardShellUi::ApplyThemePreview(const std::string& themeName) {
    // Size: pure layout previews mutate config in-place; copying full AppConfig in the dialog host measured larger.
    return app_.controller_.ApplyLayoutEditTheme(app_, themeName);
}

bool DashboardShellUi::ApplyColorPreview(LayoutEditParameter parameter, unsigned int value) {
    return app_.controller_.ApplyLayoutEditColor(app_, parameter, value);
}

bool DashboardShellUi::ApplyColorExpressionPreview(LayoutEditParameter parameter, const std::string& expression) {
    return app_.controller_.ApplyLayoutEditColorExpression(app_, parameter, expression);
}

bool DashboardShellUi::ApplyThemeColorPreview(const ThemeColorEditKey& key, unsigned int value) {
    return app_.controller_.ApplyLayoutEditThemeColor(app_, key, value);
}

bool DashboardShellUi::ApplyMetricPreview(const LayoutMetricEditKey& key,
    const std::optional<double>& scale,
    const std::string& unit,
    const std::string& label,
    const std::optional<std::string>& binding) {
    AppConfig updatedConfig = CurrentConfig();
    MetricDefinitionConfig* definition = FindMetricDefinition(updatedConfig.layout.metrics, key.metricId);
    if (definition == nullptr) {
        return false;
    }

    if (!definition->telemetryScale && definition->style != MetricDisplayStyle::LabelOnly && scale.has_value()) {
        definition->scale = *scale;
    }
    if (definition->style != MetricDisplayStyle::LabelOnly) {
        definition->unit = unit;
    }
    definition->label = label;
    if (const auto target = ResolveMetricBoardBindingTarget(key.metricId); target.has_value() && binding.has_value()) {
        auto& bindings = target->kind == BoardMetricBindingKind::Temperature
                             ? updatedConfig.layout.board.temperatureSensorNames
                             : updatedConfig.layout.board.fanSensorNames;
        if (binding->empty()) {
            bindings.erase(target->logicalName);
        } else {
            bindings[target->logicalName] = *binding;
        }
    }
    RestoreConfigSnapshot(updatedConfig);
    return true;
}

bool DashboardShellUi::ApplyCardTitlePreview(const LayoutCardTitleEditKey& key, const std::string& title) {
    return app_.controller_.ApplyLayoutEditCardTitle(app_, key, title);
}

bool DashboardShellUi::ApplyLayoutEditPreview(const LayoutEditFocusKey& key, const LayoutEditValue& value) {
    AppConfig updatedConfig = CurrentConfig();
    if (!::ApplyLayoutEditValue(updatedConfig, key, value)) {
        return false;
    }
    RestoreConfigSnapshot(updatedConfig);
    return true;
}

bool DashboardShellUi::ApplyMetricListAddRowPreview(const LayoutEditController::TooltipTarget& target) {
    const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&target.payload);
    if (anchor == nullptr || anchor->shape != AnchorShape::Plus) {
        return false;
    }

    const auto metricListKey = LayoutEditAnchorNodeFieldKey(anchor->key);
    if (!metricListKey.has_value() || metricListKey->widgetClass != WidgetClass::MetricList) {
        return false;
    }

    if (!FindMetricDisplayStyle(kMetricListPlaceholderId).has_value()) {
        return false;
    }

    AppConfig updatedConfig = CurrentConfig();
    if (!AppendMetricListRow(updatedConfig, anchor->key.widget, kMetricListPlaceholderId)) {
        return false;
    }
    RestoreConfigSnapshot(updatedConfig);
    RefreshLayoutEditDialog(LayoutEditFocusKey{*metricListKey});
    return true;
}

bool DashboardShellUi::ApplyWeightPreview(const LayoutWeightEditKey& key, int firstWeight, int secondWeight) {
    LayoutEditLayoutTarget target;
    target.editCardId = key.editCardId;
    target.nodePath = key.nodePath;
    // Size: dialog edits touch one separator; update adjacent weights without building a vector copy.
    return app_.ApplyLayoutGuideAdjacentWeights(target, key.separatorIndex, firstWeight, secondWeight);
}

void DashboardShellUi::UpdateLayoutEditSelectionHighlight(
    const std::optional<LayoutEditSelectionHighlight>& highlight) {
    app_.rendererDashboardOverlayState_.selectedTreeHighlight.reset();
    if (highlight.has_value()) {
        app_.rendererDashboardOverlayState_.selectedTreeHighlight.emplace(*highlight);
    }
    InvalidateRect(app_.hwnd_, nullptr, FALSE);
}

void DashboardShellUi::ApplyLayoutEditDialogIcons(HWND dialogHwnd) const {
    app_.ApplyThemedIconsToWindow(dialogHwnd);
}

void DashboardShellUi::RestackLayoutEditDialogAnchor(HWND dialogHwnd) {
    const HWND anchorHwnd = app_.WindowHandle();
    if (dialogHwnd == nullptr || anchorHwnd == nullptr || !IsWindow(dialogHwnd) || !IsWindow(anchorHwnd) ||
        dialogHwnd == anchorHwnd) {
        return;
    }

    ShowWindow(anchorHwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(anchorHwnd, dialogHwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void DashboardShellUi::OnLayoutEditDialogCloseRequested() {
    DestroyLayoutEditDialogWindow();
}

bool DashboardShellUi::PromptAndApplyLayoutEditTarget(const LayoutEditController::TooltipTarget& target) {
    const auto focusKey = TooltipPayloadFocusKey(target.payload);
    if (!focusKey.has_value()) {
        return false;
    }

    bool startedLayoutEdit = false;
    if (!app_.controller_.State().isEditingLayout) {
        app_.controller_.StartLayoutEditMode(app_, app_.layoutEditController_);
        app_.InvalidateNativeTitlebar();
        startedLayoutEdit = true;
    }
    if (!EnsureLayoutEditDialog(focusKey, true)) {
        if (startedLayoutEdit) {
            app_.controller_.StopLayoutEditMode(app_, app_.layoutEditController_, app_.diagnosticsOptions_.editLayout);
            app_.InvalidateNativeTitlebar();
        }
        ShowAppMessageBox(
            app_.hwnd_, FindLocalizedText(RES_STR("dashboard.message.layout_edit_dialog_open_failed")), MB_ICONERROR);
        return false;
    }
    if (IsMetricListAddRowTarget(target) && !ApplyMetricListAddRowPreview(target)) {
        ShowAppMessageBox(
            app_.hwnd_, FindLocalizedText(RES_STR("dashboard.message.metric_list_add_row_failed")), MB_ICONERROR);
        return false;
    }
    return true;
}

std::optional<double> DashboardShellUi::PromptCustomScale() {
    CustomScaleDialogState state;
    state.app = &app_;
    state.initialScale = HasExplicitDisplayScale(app_.controller_.State().config.display.scale)
                             ? app_.controller_.State().config.display.scale
                             : app_.ResolveCurrentDisplayScale(app_.CurrentWindowDpi());
    DashboardShellUiModalScope scopedModalUi(*this);
    if (DialogBoxParamA(app_.instance_,
            MAKEINTRESOURCEA(IDD_CUSTOM_SCALE),
            app_.hwnd_,
            CustomScaleDialogProc,
            reinterpret_cast<LPARAM>(&state)) == IDOK) {
        return state.result;
    }
    return std::nullopt;
}

UINT DashboardShellUi::ResolveDefaultCommand(
    MenuSource source, const LayoutEditController::TooltipTarget* layoutEditTarget) const {
    if (source == MenuSource::TrayIcon) {
        return kCommandBringOnTop;
    }
    return layoutEditTarget != nullptr ? kCommandEditLayoutTarget : kCommandMove;
}

size_t DashboardShellUi::BuildConfigureDisplayMenu(HMENU menu, DisplayMenuOption* options, size_t capacity) const {
    const DashboardSessionState& state = app_.controller_.State();
    const size_t optionCount = EnumerateDisplayMenuOptions(state.config, options, capacity);
    if (optionCount == 0) {
        AppendMenuText(menu, MF_STRING | MF_GRAYED, kCommandConfigureDisplayBase, "No displays found");
        return 0;
    }

    for (size_t i = 0; i < optionCount; ++i) {
        const DisplayMenuOption& option = options[i];
        const UINT commandId = kCommandConfigureDisplayBase + static_cast<UINT>(i);
        const UINT flags = MF_STRING | (option.layoutFits ? MF_ENABLED : MF_GRAYED) |
                           (option.matchesCurrentConfig ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuText(menu, flags, commandId, option.displayName);
    }
    return optionCount;
}

void DashboardShellUi::ExecuteCommand(
    UINT selected, const LayoutEditController::TooltipTarget* layoutEditTarget, const POINT* cursorAnchorClientPoint) {
    DashboardSessionState& state = app_.controller_.State();
    switch (selected) {
        case kCommandMove:
            if (cursorAnchorClientPoint != nullptr) {
                app_.StartMoveModeAt(*cursorAnchorClientPoint);
            } else {
                app_.StartMoveMode();
            }
            break;
        case kCommandEditLayout:
            HandleEditLayoutToggle();
            app_.UpdateLayoutEditTooltip();
            break;
        case kCommandEditLayoutDialog:
            OpenLayoutEditDialog();
            app_.UpdateLayoutEditTooltip();
            break;
        case kCommandEditLayoutTarget:
            if (layoutEditTarget != nullptr) {
                PromptAndApplyLayoutEditTarget(*layoutEditTarget);
            }
            app_.UpdateLayoutEditTooltip();
            break;
        case kCommandBringOnTop:
            app_.BringOnTop();
            break;
        case kCommandRunAsAdministrator:
            HandleRunAsAdministrator();
            break;
        case kCommandReloadConfig:
            HandleReloadConfig();
            break;
        case kCommandSaveConfig:
            if (app_.controller_.UpdateConfigFromCurrentPlacement(app_)) {
                if (state.isEditingLayout) {
                    StopLayoutEditSession(UnsavedLayoutEditPrompt::StopEditing);
                } else {
                    RefreshLayoutEditDialogSelection();
                }
            }
            break;
        case kCommandAutoStart:
            app_.controller_.ToggleAutoStart(app_);
            break;
        case kCommandSaveDumpAs:
            app_.controller_.SaveDumpAs(app_);
            break;
        case kCommandSaveScreenshotAs:
            app_.controller_.SaveScreenshotAs(app_, app_.diagnosticsOptions_);
            break;
        case kCommandSaveLayoutGuideSheetAs:
            app_.controller_.SaveLayoutGuideSheetAs(app_);
            break;
        case kCommandSaveFullConfigAs:
            app_.controller_.SaveFullConfigAs(app_);
            break;
        case kCommandCustomScale:
            if (const auto scale = PromptCustomScale(); scale.has_value()) {
                app_.controller_.SetDisplayScale(app_, *scale);
            }
            break;
        case kCommandAbout:
            ShowAboutDialog();
            break;
        case kCommandExit:
            HandleExitRequest();
            break;
        default:
            if (selected >= kCommandLayoutBase && selected <= kCommandLayoutMax) {
                const size_t index = selected - kCommandLayoutBase;
                if (index < state.config.layout.layouts.size()) {
                    const std::string& layoutName = state.config.layout.layouts[index].name;
                    app_.TraceLayoutEditUiEventFmt(
                        TracePrefix::LayoutSwitch, "menu_command", "selected_layout=\"%s\"", layoutName.c_str());
                    const bool suppressTooltipRefresh = app_.controller_.State().isEditingLayout;
                    if (suppressTooltipRefresh) {
                        app_.SetLayoutEditTooltipRefreshSuppressed(true);
                        app_.layoutEditController_.HandleMouseLeave();
                        app_.HideLayoutEditTooltip();
                        app_.TraceLayoutEditUiEvent(
                            TracePrefix::LayoutSwitch, "menu_prepare", "tooltip_suppressed=\"true\"");
                    }
                    if (!app_.controller_.SwitchLayout(app_, layoutName, app_.diagnosticsOptions_.editLayout)) {
                        if (suppressTooltipRefresh) {
                            app_.SetLayoutEditTooltipRefreshSuppressed(false);
                        }
                        app_.TraceLayoutEditUiEventFmt(
                            TracePrefix::LayoutSwitch, "menu_failed", "selected_layout=\"%s\"", layoutName.c_str());
                        ShowAppMessageBox(app_.hwnd_,
                            FindLocalizedText(RES_STR("dashboard.message.switch_layout_failed")),
                            MB_ICONERROR);
                    } else {
                        RefreshLayoutEditDialog();
                        if (suppressTooltipRefresh) {
                            app_.SetLayoutEditTooltipRefreshSuppressed(false);
                        }
                        app_.TraceLayoutEditUiEventFmt(
                            TracePrefix::LayoutSwitch, "menu_done", "selected_layout=\"%s\"", layoutName.c_str());
                    }
                }
                break;
            }
            if (selected >= kCommandNetworkAdapterBase && selected <= kCommandNetworkAdapterMax) {
                const size_t index = selected - kCommandNetworkAdapterBase;
                const auto& candidates = state.telemetryUpdate.networkAdapterCandidates;
                if (index < candidates.size()) {
                    app_.controller_.SelectNetworkAdapter(app_, candidates[index].adapterName);
                }
                break;
            }
            if (selected >= kCommandGpuAdapterBase && selected <= kCommandGpuAdapterMax) {
                const size_t index = selected - kCommandGpuAdapterBase;
                const auto& candidates = state.telemetryUpdate.gpuAdapterCandidates;
                if (index < candidates.size()) {
                    app_.controller_.SelectGpuAdapter(app_, candidates[index].adapterName);
                }
                break;
            }
            if (selected >= kCommandThemeBase && selected <= kCommandThemeMax) {
                const size_t index = selected - kCommandThemeBase;
                if (index < state.config.layout.themes.size()) {
                    if (!app_.controller_.SwitchTheme(
                            app_, state.config.layout.themes[index].name, app_.diagnosticsOptions_.editLayout)) {
                        ShowAppMessageBox(app_.hwnd_,
                            FindLocalizedText(RES_STR("dashboard.message.switch_theme_failed")),
                            MB_ICONERROR);
                    } else {
                        RefreshLayoutEditDialog();
                    }
                }
                break;
            }
            if (selected >= kCommandStorageDriveBase && selected <= kCommandStorageDriveMax) {
                const size_t index = selected - kCommandStorageDriveBase;
                const auto& candidates = state.telemetryUpdate.storageDriveCandidates;
                if (index < candidates.size()) {
                    app_.controller_.ToggleStorageDrive(app_, candidates[index].letter);
                }
                break;
            }
            if (selected >= kCommandScaleBase && selected <= kCommandScaleMax) {
                if (selected == kCommandScaleBase) {
                    app_.controller_.SetDisplayScale(app_, 0.0);
                } else {
                    double scaleEntries[std::size(kPredefinedDisplayScales) + 1] = {};
                    const size_t scaleEntryCount =
                        BuildScaleMenuEntries(state.config.display.scale, scaleEntries, std::size(scaleEntries));
                    const size_t index = selected - kCommandScaleBase - 1;
                    if (index < scaleEntryCount) {
                        app_.controller_.SetDisplayScale(app_, scaleEntries[index]);
                    }
                }
            }
            break;
    }
}

void DashboardShellUi::InvokeDefaultAction(MenuSource source,
    const LayoutEditController::TooltipTarget* layoutEditTarget,
    const POINT* cursorAnchorClientPoint) {
    if (source == MenuSource::AppWindow && app_.controller_.State().isEditingLayout) {
        app_.layoutEditController_.CancelInteraction();
        app_.UpdateLayoutEditTooltip();
    }
    ExecuteCommand(ResolveDefaultCommand(source, layoutEditTarget), layoutEditTarget, cursorAnchorClientPoint);
}

void DashboardShellUi::ShowContextMenu(
    MenuSource source, POINT screenPoint, const LayoutEditController::TooltipTarget* layoutEditTarget) {
    app_.HideLayoutEditTooltip();
    DashboardShellUiModalScope scopedModalUi(*this);
    DashboardSessionState& state = app_.controller_.State();
    HMENU menu = CreatePopupMenu();
    HMENU layoutMenu = CreatePopupMenu();
    HMENU themeMenu = CreatePopupMenu();
    HMENU gpuMenu = CreatePopupMenu();
    HMENU networkMenu = CreatePopupMenu();
    HMENU scaleMenu = CreatePopupMenu();
    HMENU storageDrivesMenu = CreatePopupMenu();
    HMENU configureDisplayMenu = CreatePopupMenu();
    HMENU displayMenu = CreatePopupMenu();
    HMENU devicesMenu = CreatePopupMenu();
    HMENU editLayoutMenu = CreatePopupMenu();
    const bool showAdvancedMenu = (::GetKeyState(VK_MENU) & 0x8000) != 0;
    HMENU advancedMenu = showAdvancedMenu ? CreatePopupMenu() : nullptr;
    const UINT autoStartFlags = MF_STRING | (app_.controller_.IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED);
    if (state.config.layout.layouts.empty()) {
        AppendMenuText(layoutMenu, MF_STRING | MF_GRAYED, kCommandLayoutBase, "No layouts found");
    } else {
        for (size_t i = 0; i < state.config.layout.layouts.size() && (kCommandLayoutBase + i) <= kCommandLayoutMax;
            ++i) {
            const LayoutSectionConfig& layout = state.config.layout.layouts[i];
            const UINT commandId = kCommandLayoutBase + static_cast<UINT>(i);
            const std::string label = FormatNamedMenuLabel(layout.name, layout.description);
            const UINT flags = MF_STRING | (state.config.display.layout == layout.name ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuText(layoutMenu, flags, commandId, label);
            SetMenuItemRadioStyle(layoutMenu, commandId);
        }
    }
    if (state.config.layout.themes.empty()) {
        AppendMenuText(themeMenu, MF_STRING | MF_GRAYED, kCommandThemeBase, "No themes found");
    } else {
        for (size_t i = 0; i < state.config.layout.themes.size() && (kCommandThemeBase + i) <= kCommandThemeMax; ++i) {
            const ThemeConfig& theme = state.config.layout.themes[i];
            const UINT commandId = kCommandThemeBase + static_cast<UINT>(i);
            const UINT flags = MF_STRING | (theme.name == state.config.display.theme ? MF_CHECKED : MF_UNCHECKED);
            const std::string label = FormatNamedMenuLabel(theme.name, theme.description);
            AppendMenuText(themeMenu, flags, commandId, label);
            SetMenuItemRadioStyle(themeMenu, commandId);
        }
    }
    const auto& gpuCandidates = state.telemetryUpdate.gpuAdapterCandidates;
    if (gpuCandidates.empty()) {
        AppendMenuText(gpuMenu, MF_STRING | MF_GRAYED, kCommandGpuAdapterBase, "No adapters found");
    } else {
        for (size_t i = 0; i < gpuCandidates.size() && (kCommandGpuAdapterBase + i) <= kCommandGpuAdapterMax; ++i) {
            const UINT commandId = kCommandGpuAdapterBase + static_cast<UINT>(i);
            const UINT flags = MF_STRING | (gpuCandidates[i].selected ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuText(gpuMenu, flags, commandId, gpuCandidates[i].adapterName);
            SetMenuItemRadioStyle(gpuMenu, commandId);
        }
    }
    const auto& networkCandidates = state.telemetryUpdate.networkAdapterCandidates;
    if (networkCandidates.empty()) {
        AppendMenuText(networkMenu, MF_STRING | MF_GRAYED, kCommandNetworkAdapterBase, "No adapters found");
    } else {
        for (size_t i = 0;
            i < networkCandidates.size() && (kCommandNetworkAdapterBase + i) <= kCommandNetworkAdapterMax;
            ++i) {
            const UINT commandId = kCommandNetworkAdapterBase + static_cast<UINT>(i);
            const std::string label =
                FormatNetworkMenuText(networkCandidates[i].adapterName, networkCandidates[i].ipAddress);
            const UINT flags = MF_STRING | (networkCandidates[i].selected ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuText(networkMenu, flags, commandId, label);
            SetMenuItemRadioStyle(networkMenu, commandId);
        }
    }
    const auto& storageDriveCandidates = state.telemetryUpdate.storageDriveCandidates;
    if (storageDriveCandidates.empty()) {
        AppendMenuText(storageDrivesMenu, MF_STRING | MF_GRAYED, kCommandStorageDriveBase, "No drives found");
    } else {
        for (size_t i = 0;
            i < storageDriveCandidates.size() && (kCommandStorageDriveBase + i) <= kCommandStorageDriveMax;
            ++i) {
            const UINT commandId = kCommandStorageDriveBase + static_cast<UINT>(i);
            const std::string label = FormatStorageDriveMenuText(storageDriveCandidates[i].letter,
                storageDriveCandidates[i].volumeLabel,
                storageDriveCandidates[i].totalGb);
            const UINT flags = MF_STRING | (storageDriveCandidates[i].selected ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuText(storageDrivesMenu, flags, commandId, label);
        }
    }
    double scaleEntries[std::size(kPredefinedDisplayScales) + 1] = {};
    const size_t scaleEntryCount =
        BuildScaleMenuEntries(state.config.display.scale, scaleEntries, std::size(scaleEntries));
    AppendMenuText(scaleMenu,
        MF_STRING | (!HasExplicitDisplayScale(state.config.display.scale) ? MF_CHECKED : MF_UNCHECKED),
        kCommandScaleBase,
        "Default");
    SetMenuItemRadioStyle(scaleMenu, kCommandScaleBase);
    for (size_t i = 0; i < scaleEntryCount && (kCommandScaleBase + 1 + i) <= kCommandScaleMax; ++i) {
        const UINT commandId = kCommandScaleBase + 1 + static_cast<UINT>(i);
        const UINT flags = MF_STRING | (HasExplicitDisplayScale(state.config.display.scale) &&
                                                   AreScalesEqual(state.config.display.scale, scaleEntries[i])
                                               ? MF_CHECKED
                                               : MF_UNCHECKED);
        const std::string label = FormatScaleLabel(scaleEntries[i]);
        AppendMenuText(scaleMenu, flags, commandId, label);
        SetMenuItemRadioStyle(scaleMenu, commandId);
    }
    AppendMenuA(scaleMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuText(scaleMenu, MF_STRING, kCommandCustomScale, "Custom...");
    DisplayMenuOption configDisplayOptions[kConfigureDisplayMenuCapacity];
    const size_t configDisplayOptionCount =
        BuildConfigureDisplayMenu(configureDisplayMenu, configDisplayOptions, kConfigureDisplayMenuCapacity);
    AppendMenuText(displayMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(configureDisplayMenu), "Configure Display");
    AppendMenuText(displayMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(scaleMenu), "Scale");
    AppendMenuText(devicesMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(gpuMenu), "GPU");
    AppendMenuText(devicesMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(networkMenu), "Network");
    AppendMenuText(devicesMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(storageDrivesMenu), "Storage Drives");
    AppendMenuText(editLayoutMenu,
        MF_STRING | (state.isEditingLayout ? MF_CHECKED : MF_UNCHECKED),
        kCommandEditLayout,
        "Edit Layout");
    AppendMenuText(editLayoutMenu, MF_STRING, kCommandEditLayoutDialog, "Layout Editor...");
    AppendMenuText(editLayoutMenu, MF_STRING, kCommandSaveConfig, "Save Config");
    if (advancedMenu != nullptr) {
        const UINT runAsAdministratorFlags =
            MF_STRING | (IsCurrentProcessElevated() ? (MF_CHECKED | MF_GRAYED) : MF_UNCHECKED);
        AppendMenuText(advancedMenu, runAsAdministratorFlags, kCommandRunAsAdministrator, "Run as administrator");
        AppendMenuA(advancedMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuText(advancedMenu, MF_STRING, kCommandReloadConfig, "Reload Config");
        AppendMenuText(advancedMenu, MF_STRING, kCommandSaveConfig, "Save Config");
        AppendMenuText(advancedMenu, MF_STRING, kCommandSaveFullConfigAs, "Export Full Config...");
        AppendMenuText(advancedMenu, MF_STRING, kCommandSaveDumpAs, "Export Snapshot Dump...");
        AppendMenuText(advancedMenu, MF_STRING, kCommandSaveScreenshotAs, "Save Screenshot...");
        AppendMenuText(advancedMenu, MF_STRING, kCommandSaveLayoutGuideSheetAs, "Save Layout Guide Sheet...");
    }
    if (layoutEditTarget != nullptr) {
        std::string label;
        if (const auto* guide = std::get_if<LayoutEditGuide>(&layoutEditTarget->payload)) {
            label = BuildLayoutEditMenuLabel(BuildLayoutGuideEditLabel(*guide));
        } else {
            if (IsMetricListAddRowTarget(*layoutEditTarget)) {
                label = "Add metric list row...";
            }
            const auto focusKey = TooltipPayloadFocusKey(layoutEditTarget->payload);
            if (label.empty() && focusKey.has_value()) {
                if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&*focusKey); metricKey != nullptr) {
                    label = BuildLayoutEditMenuLabel(FormatText("%s metric", metricKey->metricId.c_str()));
                }
            }
            if (label.empty() && focusKey.has_value() && std::holds_alternative<LayoutCardTitleEditKey>(*focusKey)) {
                label = BuildLayoutEditMenuLabel("card title");
            } else if (label.empty() && focusKey.has_value() &&
                       std::get_if<LayoutNodeFieldEditKey>(&*focusKey) != nullptr) {
                const auto& nodeFieldKey = *std::get_if<LayoutNodeFieldEditKey>(&*focusKey);
                const std::string_view subject = LayoutNodeFieldEditMenuSubject(nodeFieldKey);
                if (!subject.empty()) {
                    label = BuildLayoutEditMenuLabel(subject);
                }
            } else if (label.empty() && focusKey.has_value() &&
                       std::holds_alternative<LayoutContainerEditKey>(*focusKey)) {
                label = BuildLayoutEditMenuLabel("layout container");
            } else if (label.empty()) {
                const auto parameter = TooltipPayloadParameter(layoutEditTarget->payload);
                if (parameter.has_value()) {
                    label = BuildLayoutEditMenuLabel(GetLayoutEditParameterDisplayName(*parameter));
                }
            }
        }
        AppendMenuText(menu, MF_STRING, kCommandEditLayoutTarget, label);
        AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuText(menu, MF_STRING, kCommandMove, "Move");
    AppendMenuText(menu, MF_STRING, kCommandBringOnTop, "Bring to Front");
    AppendMenuText(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(themeMenu), "Theme");
    AppendMenuText(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(layoutMenu), "Layout");
    AppendMenuText(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(displayMenu), "Display");
    AppendMenuText(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(devicesMenu), "Devices");
    AppendMenuText(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(editLayoutMenu), "Edit Layout");
    AppendMenuText(menu, autoStartFlags, kCommandAutoStart, "Start with Windows");
    if (advancedMenu != nullptr) {
        AppendMenuText(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(advancedMenu), "Advanced");
    }
    AppendMenuText(menu, MF_STRING, kCommandAbout, "About CaseDash");
    AppendMenuText(menu, MF_STRING, kCommandExit, "Exit");
    const UINT defaultCommand = ResolveDefaultCommand(source, layoutEditTarget);
    SetMenuDefaultItem(menu, defaultCommand, FALSE);
    SetForegroundWindow(app_.hwnd_);
    const UINT selected = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x,
        screenPoint.y,
        0,
        app_.hwnd_,
        nullptr);
    DestroyMenu(menu);
    if (selected != 0) {
        if (selected >= kCommandConfigureDisplayBase && selected <= kCommandConfigureDisplayMax) {
            const size_t index = selected - kCommandConfigureDisplayBase;
            if (index < configDisplayOptionCount) {
                HandleConfigureDisplay(configDisplayOptions[index]);
            }
        } else {
            ExecuteCommand(selected, layoutEditTarget);
        }
    }
}
