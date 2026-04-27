#include "dashboard/dashboard_shell_ui.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <sstream>

#include "dashboard/constants.h"
#include "dashboard/dashboard_app.h"
#include "diagnostics/diagnostics.h"
#include "display/constants.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_tooltip_payload.h"
#include "layout_edit/layout_edit_tree.h"
#include "layout_edit_dialog/layout_edit_dialog.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "resource.h"
#include "telemetry/metrics.h"
#include "util/strings.h"
#include "util/utf8.h"

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
            SendMessageW(hwnd_, WM_SETREDRAW, FALSE, 0);
        }
    }

    ~DialogRedrawScope() {
        if (hwnd_ != nullptr) {
            SendMessageW(hwnd_, WM_SETREDRAW, TRUE, 0);
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
constexpr std::string_view kBoardTemperatureMetricPrefix = "board.temp.";
constexpr std::string_view kBoardFanMetricPrefix = "board.fan.";

enum class BoardMetricBindingKind {
    Temperature,
    Fan,
};

struct BoardMetricBindingTarget {
    BoardMetricBindingKind kind = BoardMetricBindingKind::Temperature;
    std::string logicalName;
};

struct UnsavedLayoutEditDialogState {
    const wchar_t* mainInstruction = L"";
    const wchar_t* content = L"";
    int selectedButton = IDCANCEL;
};

std::optional<BoardMetricBindingTarget> ParseBoardMetricBindingTarget(std::string_view metricId) {
    if (metricId.rfind(kBoardTemperatureMetricPrefix, 0) == 0) {
        return BoardMetricBindingTarget{
            BoardMetricBindingKind::Temperature,
            std::string(metricId.substr(kBoardTemperatureMetricPrefix.size())),
        };
    }
    if (metricId.rfind(kBoardFanMetricPrefix, 0) == 0) {
        return BoardMetricBindingTarget{
            BoardMetricBindingKind::Fan,
            std::string(metricId.substr(kBoardFanMetricPrefix.size())),
        };
    }
    return std::nullopt;
}

AppConfig BuildLayoutEditOriginalConfig(const DashboardSessionState& sessionState) {
    AppConfig config = sessionState.config;
    if (sessionState.hasLayoutEditSessionSavedLayout) {
        config.layout = sessionState.layoutEditSessionSavedLayout;
    }
    return config;
}

bool AreScalesEqual(double left, double right) {
    return std::abs(left - right) < kScaleEpsilon;
}

INT_PTR CALLBACK UnsavedLayoutEditDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<UnsavedLayoutEditDialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    switch (message) {
        case WM_INITDIALOG:
            state = reinterpret_cast<UnsavedLayoutEditDialogState*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            SetDlgItemTextW(hwnd, IDC_UNSAVED_LAYOUT_EDIT_MAIN, state->mainInstruction);
            SetDlgItemTextW(hwnd, IDC_UNSAVED_LAYOUT_EDIT_CONTENT, state->content);
            return TRUE;
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

bool IsPredefinedDisplayScale(double scale) {
    for (double predefinedScale : kPredefinedDisplayScales) {
        if (AreScalesEqual(scale, predefinedScale)) {
            return true;
        }
    }
    return false;
}

std::wstring FormatScaleLabel(double scale) {
    std::ostringstream stream;
    stream.precision(12);
    stream << (scale * 100.0);
    std::string value = stream.str();
    if (const size_t dot = value.find('.'); dot != std::string::npos) {
        while (!value.empty() && value.back() == '0') {
            value.pop_back();
        }
        if (!value.empty() && value.back() == '.') {
            value.pop_back();
        }
    }
    return WideFromUtf8(value + "%");
}

std::wstring FormatScalePercentageValue(double scale) {
    std::ostringstream stream;
    stream.precision(12);
    stream << (scale * 100.0);
    std::string value = stream.str();
    if (const size_t dot = value.find('.'); dot != std::string::npos) {
        while (!value.empty() && value.back() == '0') {
            value.pop_back();
        }
        if (!value.empty() && value.back() == '.') {
            value.pop_back();
        }
    }
    return WideFromUtf8(value);
}

std::wstring FormatLayoutMenuLabel(const LayoutMenuOption& option) {
    std::wstring label = WideFromUtf8(option.name);
    if (!option.description.empty()) {
        label += L" - ";
        label += WideFromUtf8(option.description);
    }
    return label;
}

void SetMenuItemRadioStyle(HMENU menu, UINT commandId) {
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE;
    info.fType = MFT_RADIOCHECK;
    SetMenuItemInfoW(menu, commandId, FALSE, &info);
}

std::wstring BuildLayoutEditMenuLabel(const std::wstring& subject) {
    return L"Edit " + subject + L" ...";
}

bool IsMetricListAddRowTarget(const LayoutEditController::TooltipTarget& target) {
    const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&target.payload);
    return anchor != nullptr && anchor->shape == AnchorShape::Plus &&
           LayoutEditAnchorMetricListOrderKey(anchor->key).has_value();
}

std::wstring BuildLayoutGuideEditLabel(const LayoutEditGuide& guide) {
    return guide.editCardId.empty() ? L"cards weights" : L"layout weights";
}

const LayoutNodeConfig* FindWeightEditNode(const AppConfig& config, const LayoutWeightEditKey& key) {
    LayoutEditLayoutTarget target;
    target.editCardId = key.editCardId;
    target.nodePath = key.nodePath;
    return FindGuideNode(config, target);
}

std::string EscapeTraceText(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\n':
                escaped += "\\n";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string QuoteTraceText(std::string_view text) {
    return "\"" + EscapeTraceText(text) + "\"";
}

struct CustomScaleDialogState {
    double initialScale = 1.0;
    std::optional<double> result;
};

struct LayoutEditTreeItemBinding {
    const LayoutEditTreeNode* node = nullptr;
    HTREEITEM item = nullptr;
};

INT_PTR CALLBACK CustomScaleDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<CustomScaleDialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<CustomScaleDialogState*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            const std::wstring initialText = FormatScalePercentageValue(state->initialScale);
            SetDlgItemTextW(hwnd, IDC_CUSTOM_SCALE_EDIT, initialText.c_str());
            SendDlgItemMessageW(hwnd, IDC_CUSTOM_SCALE_EDIT, EM_SETSEL, 0, -1);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    wchar_t buffer[64] = {};
                    GetDlgItemTextW(hwnd, IDC_CUSTOM_SCALE_EDIT, buffer, ARRAYSIZE(buffer));
                    const std::optional<double> percentage = TryParseScaleValue(buffer);
                    if (!percentage.has_value()) {
                        MessageBoxW(hwnd, L"Enter a positive percentage scale.", L"System Telemetry", MB_ICONERROR);
                        SetFocus(GetDlgItem(hwnd, IDC_CUSTOM_SCALE_EDIT));
                        SendDlgItemMessageW(hwnd, IDC_CUSTOM_SCALE_EDIT, EM_SETSEL, 0, -1);
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

class DashboardShellUiDialogHost final : public LayoutEditDialogHost {
public:
    explicit DashboardShellUiDialogHost(DashboardShellUi& shellUi) : shellUi_(shellUi) {}

    HINSTANCE LayoutEditDialogInstance() const override {
        return shellUi_.DialogInstance();
    }

    HWND LayoutEditDialogAnchorWindow() const override {
        return shellUi_.app_.WindowHandle();
    }

    UINT LayoutEditDialogAnchorDpi() const override {
        return shellUi_.app_.CurrentWindowDpi();
    }

    AppConfig BuildLayoutEditOriginalConfig() const override {
        return shellUi_.BuildLayoutEditOriginalConfigSnapshot();
    }

    const AppConfig& CurrentConfig() const override {
        return shellUi_.CurrentConfig();
    }

    bool ApplyParameterPreview(LayoutEditParameter parameter, double value) override {
        return shellUi_.ApplyParameterPreview(parameter, value);
    }

    bool ApplyFontPreview(LayoutEditParameter parameter, const UiFontConfig& value) override {
        return shellUi_.ApplyFontPreview(parameter, value);
    }

    bool ApplyFontFamilyPreview(const std::string& family) override {
        return shellUi_.ApplyFontFamilyPreview(family);
    }

    bool ApplyFontSetPreview(const UiFontSetConfig& fonts) override {
        return shellUi_.ApplyFontSetPreview(fonts);
    }

    bool ApplyColorPreview(LayoutEditParameter parameter, unsigned int value) override {
        return shellUi_.ApplyColorPreview(parameter, value);
    }

    bool ApplyMetricPreview(const LayoutMetricEditKey& key,
        const std::optional<double>& scale,
        const std::string& unit,
        const std::string& label,
        const std::optional<std::string>& binding) override {
        return shellUi_.ApplyMetricPreview(key, scale, unit, label, binding);
    }

    bool ApplyCardTitlePreview(const LayoutCardTitleEditKey& key, const std::string& title) override {
        return shellUi_.ApplyCardTitlePreview(key, title);
    }

    bool ApplyMetricListOrderPreview(
        const LayoutMetricListOrderEditKey& key, const std::vector<std::string>& metricRefs) override {
        return shellUi_.ApplyMetricListOrderPreview(key, metricRefs);
    }

    bool ApplyWeightPreview(const LayoutWeightEditKey& key, int firstWeight, int secondWeight) override {
        return shellUi_.ApplyWeightPreview(key, firstWeight, secondWeight);
    }

    std::vector<std::string> AvailableBoardMetricSensorBindings(const LayoutMetricEditKey& key) const override {
        return shellUi_.AvailableBoardMetricSensorBindings(key);
    }

    void UpdateLayoutEditSelectionHighlight(const std::optional<LayoutEditSelectionHighlight>& highlight) override {
        shellUi_.UpdateLayoutEditSelectionHighlight(highlight);
    }

    void RestackLayoutEditDialogAnchor(HWND dialogHwnd) override {
        const HWND anchorHwnd = shellUi_.app_.WindowHandle();
        if (dialogHwnd == nullptr || anchorHwnd == nullptr || !IsWindow(dialogHwnd) || !IsWindow(anchorHwnd) ||
            dialogHwnd == anchorHwnd) {
            return;
        }

        ShowWindow(anchorHwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(anchorHwnd, dialogHwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void TraceLayoutEditDialogEvent(const std::string& event, const std::string& details = {}) const override {
        shellUi_.TraceLayoutEditDialogEvent(event, details);
    }

    void OnLayoutEditDialogCloseRequested() override {
        shellUi_.DestroyLayoutEditDialogWindow();
    }

private:
    DashboardShellUi& shellUi_;
};

DashboardShellUi::DashboardShellUi(DashboardApp& app)
    : app_(app), layoutEditDialogHost_(std::make_unique<DashboardShellUiDialogHost>(*this)),
      layoutEditDialog_(std::make_unique<LayoutEditDialog>(*layoutEditDialogHost_)) {}

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
        app_.TraceLayoutEditUiEvent("layout_edit_dialog:refresh_begin",
            "preferred_focus=" + QuoteTraceText(preferredFocus.has_value() ? "set" : "none"));
        layoutEditDialog_->Refresh(preferredFocus);
        layoutEditDialog_->SetSelectionHighlightVisible(true);
        layoutEditDialog_->RestackAnchor();
        app_.TraceLayoutEditUiEvent("layout_edit_dialog:refresh_done");
    }
}

void DashboardShellUi::RefreshLayoutEditDialogSelection() {
    if (layoutEditDialog_ != nullptr) {
        layoutEditDialog_->RefreshSelection();
    }
}

void DashboardShellUi::SyncLayoutEditDialogSelection(
    const std::optional<LayoutEditController::TooltipTarget>& target, bool bringToFront) {
    if (layoutEditDialog_ != nullptr && !layoutEditDialog_->SyncSelection(target, bringToFront)) {
        MessageBoxW(app_.hwnd_, L"Failed to open the Edit Configuration window.", L"System Telemetry", MB_ICONERROR);
    }
}

std::optional<DashboardShellUi::UnsavedLayoutEditAction> DashboardShellUi::PromptForUnsavedLayoutEditChanges(
    UnsavedLayoutEditPrompt prompt) const {
    DashboardShellUiModalScope scopedModalUi(const_cast<DashboardShellUi&>(*this));
    UnsavedLayoutEditDialogState state;
    switch (prompt) {
        case UnsavedLayoutEditPrompt::StopEditing:
            state.mainInstruction = L"Save modified changes before turning off layout edit mode?";
            state.content = L"You have unsaved changes made while editing the layout.";
            break;
        case UnsavedLayoutEditPrompt::ExitApplication:
            state.mainInstruction = L"Save modified changes before exiting?";
            state.content =
                L"Unsaved changes made while editing the layout will be discarded if you exit without saving.";
            break;
        case UnsavedLayoutEditPrompt::ReloadConfig:
            state.mainInstruction = L"Save modified changes before reloading the config?";
            state.content =
                L"Unsaved changes made while editing the layout will be discarded if you reload without saving.";
            break;
    }

    DialogBoxParamW(app_.instance_,
        MAKEINTRESOURCEW(IDD_UNSAVED_LAYOUT_EDIT),
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
            MessageBoxW(
                app_.hwnd_, L"Failed to restore the saved layout edit state.", L"System Telemetry", MB_ICONERROR);
            return false;
        }
    }

    app_.controller_.StopLayoutEditMode(app_, app_.layoutEditController_, app_.diagnosticsOptions_.editLayout);
    app_.HideLayoutEditTooltip();
    DestroyLayoutEditDialogWindow();
    return true;
}

bool DashboardShellUi::HandleEditLayoutToggle() {
    DashboardSessionState& state = app_.controller_.State();
    if (!state.isEditingLayout) {
        app_.controller_.StartLayoutEditMode(app_, app_.layoutEditController_);
        return true;
    }

    return StopLayoutEditSession(UnsavedLayoutEditPrompt::StopEditing);
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
        MessageBoxW(app_.hwnd_, L"Failed to reload config.ini.", L"System Telemetry", MB_ICONERROR);
        return false;
    }
    RefreshLayoutEditDialog();
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
    }
    return true;
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

void DashboardShellUi::TraceLayoutEditDialogEvent(const std::string& event, const std::string& details) const {
    const auto& state = app_.controller_.State();
    if (state.diagnostics == nullptr) {
        return;
    }

    if (details.empty()) {
        state.diagnostics->WriteTraceMarker(event);
    } else {
        state.diagnostics->WriteTraceMarker(event + " " + details);
    }
}

bool DashboardShellUi::IsLayoutEditModalUiActive() const {
    return app_.layoutEditModalUiDepth_ > 0;
}

void DashboardShellUi::BeginLayoutEditModalUi() {
    app_.TraceLayoutEditUiEvent("layout_edit_modal:begin_request",
        "depth_before=" + QuoteTraceText(std::to_string(app_.layoutEditModalUiDepth_)));
    ++app_.layoutEditModalUiDepth_;
    if (app_.layoutEditModalUiDepth_ == 1 && app_.controller_.State().isEditingLayout) {
        app_.layoutEditController_.CancelInteraction();
    }
    app_.HideLayoutEditTooltip();
    app_.layoutEditMouseTracking_ = false;
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    app_.TraceLayoutEditUiEvent(
        "layout_edit_modal:begin_done", "depth_after=" + QuoteTraceText(std::to_string(app_.layoutEditModalUiDepth_)));
}

void DashboardShellUi::EndLayoutEditModalUi() {
    if (app_.layoutEditModalUiDepth_ <= 0) {
        app_.layoutEditModalUiDepth_ = 0;
        return;
    }
    app_.TraceLayoutEditUiEvent("layout_edit_modal:end_request",
        "depth_before=" + QuoteTraceText(std::to_string(app_.layoutEditModalUiDepth_)));
    --app_.layoutEditModalUiDepth_;
    if (app_.layoutEditModalUiDepth_ == 0) {
        ReleaseCapture();
        app_.layoutEditMouseTracking_ = false;
        app_.TraceLayoutEditUiEvent("layout_edit_modal:end_released_capture");
        app_.RefreshLayoutEditHoverFromCursor();
    }
    app_.TraceLayoutEditUiEvent(
        "layout_edit_modal:end_done", "depth_after=" + QuoteTraceText(std::to_string(app_.layoutEditModalUiDepth_)));
}

HINSTANCE DashboardShellUi::DialogInstance() const {
    return app_.instance_;
}

AppConfig DashboardShellUi::BuildLayoutEditOriginalConfigSnapshot() const {
    return ::BuildLayoutEditOriginalConfig(app_.controller_.State());
}

const AppConfig& DashboardShellUi::CurrentConfig() const {
    return app_.controller_.State().config;
}

std::vector<std::string> DashboardShellUi::AvailableBoardMetricSensorBindings(const LayoutMetricEditKey& key) const {
    const auto target = ParseBoardMetricBindingTarget(key.metricId);
    if (!target.has_value()) {
        return {};
    }

    const auto& state = app_.controller_.State();
    if (state.telemetry == nullptr) {
        return {};
    }

    const BoardVendorTelemetrySample sample = state.telemetry->Dump().boardProvider;
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
    AppConfig updatedConfig = CurrentConfig();
    updatedConfig.layout.fonts.title.face = family;
    updatedConfig.layout.fonts.big.face = family;
    updatedConfig.layout.fonts.value.face = family;
    updatedConfig.layout.fonts.label.face = family;
    updatedConfig.layout.fonts.text.face = family;
    updatedConfig.layout.fonts.smallText.face = family;
    updatedConfig.layout.fonts.footer.face = family;
    updatedConfig.layout.fonts.clockTime.face = family;
    updatedConfig.layout.fonts.clockDate.face = family;
    RestoreConfigSnapshot(updatedConfig);
    return true;
}

bool DashboardShellUi::ApplyFontSetPreview(const UiFontSetConfig& fonts) {
    AppConfig updatedConfig = CurrentConfig();
    updatedConfig.layout.fonts = fonts;
    RestoreConfigSnapshot(updatedConfig);
    return true;
}

bool DashboardShellUi::ApplyColorPreview(LayoutEditParameter parameter, unsigned int value) {
    return app_.controller_.ApplyLayoutEditColor(app_, parameter, value);
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
    if (const auto target = ParseBoardMetricBindingTarget(key.metricId); target.has_value() && binding.has_value()) {
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
    AppConfig updatedConfig = CurrentConfig();
    const auto it = std::find_if(updatedConfig.layout.cards.begin(),
        updatedConfig.layout.cards.end(),
        [&](const LayoutCardConfig& card) { return card.id == key.cardId; });
    if (it == updatedConfig.layout.cards.end()) {
        return false;
    }
    it->title = title;
    RestoreConfigSnapshot(updatedConfig);
    return true;
}

bool DashboardShellUi::ApplyMetricListOrderPreview(
    const LayoutMetricListOrderEditKey& key, const std::vector<std::string>& metricRefs) {
    AppConfig updatedConfig = CurrentConfig();
    const LayoutEditWidgetIdentity widget{"", key.editCardId, key.nodePath};
    if (!::ApplyMetricListOrder(updatedConfig, widget, metricRefs)) {
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

    const auto metricListKey = LayoutEditAnchorMetricListOrderKey(anchor->key);
    if (!metricListKey.has_value()) {
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
    const LayoutNodeConfig* node = FindWeightEditNode(CurrentConfig(), key);
    if (node == nullptr || key.separatorIndex + 1 >= node->children.size()) {
        return false;
    }

    std::vector<int> weights;
    weights.reserve(node->children.size());
    for (const auto& child : node->children) {
        weights.push_back(std::max(1, child.weight));
    }
    weights[key.separatorIndex] = firstWeight;
    weights[key.separatorIndex + 1] = secondWeight;

    LayoutEditLayoutTarget target;
    target.editCardId = key.editCardId;
    target.nodePath = key.nodePath;
    return app_.ApplyLayoutGuideWeights(target, weights);
}

void DashboardShellUi::UpdateLayoutEditSelectionHighlight(
    const std::optional<LayoutEditSelectionHighlight>& highlight) {
    app_.rendererDashboardOverlayState_.selectedTreeHighlight = highlight;
    InvalidateRect(app_.hwnd_, nullptr, FALSE);
}

bool DashboardShellUi::PromptAndApplyLayoutEditTarget(const LayoutEditController::TooltipTarget& target) {
    const auto focusKey = TooltipPayloadFocusKey(target.payload);
    if (!focusKey.has_value()) {
        return false;
    }

    bool startedLayoutEdit = false;
    if (!app_.controller_.State().isEditingLayout) {
        app_.controller_.StartLayoutEditMode(app_, app_.layoutEditController_);
        startedLayoutEdit = true;
    }
    if (!EnsureLayoutEditDialog(focusKey, true)) {
        if (startedLayoutEdit) {
            app_.controller_.StopLayoutEditMode(app_, app_.layoutEditController_, app_.diagnosticsOptions_.editLayout);
        }
        MessageBoxW(app_.hwnd_, L"Failed to open the Edit Configuration window.", L"System Telemetry", MB_ICONERROR);
        return false;
    }
    if (IsMetricListAddRowTarget(target) && !ApplyMetricListAddRowPreview(target)) {
        MessageBoxW(app_.hwnd_, L"Failed to add a metric list row.", L"System Telemetry", MB_ICONERROR);
        return false;
    }
    return true;
}

std::optional<double> DashboardShellUi::PromptCustomScale() {
    CustomScaleDialogState state;
    state.initialScale = HasExplicitDisplayScale(app_.controller_.State().config.display.scale)
                             ? app_.controller_.State().config.display.scale
                             : app_.ResolveCurrentDisplayScale(app_.CurrentWindowDpi());
    DashboardShellUiModalScope scopedModalUi(*this);
    if (DialogBoxParamW(app_.instance_,
            MAKEINTRESOURCEW(IDD_CUSTOM_SCALE),
            app_.hwnd_,
            CustomScaleDialogProc,
            reinterpret_cast<LPARAM>(&state)) == IDOK) {
        return state.result;
    }
    return std::nullopt;
}

UINT DashboardShellUi::ResolveDefaultCommand(
    MenuSource source, const std::optional<LayoutEditController::TooltipTarget>& layoutEditTarget) const {
    if (source == MenuSource::TrayIcon) {
        return kCommandBringOnTop;
    }
    return layoutEditTarget.has_value() ? kCommandEditLayoutTarget : kCommandMove;
}

void DashboardShellUi::ExecuteCommand(UINT selected,
    const std::optional<LayoutEditController::TooltipTarget>& layoutEditTarget,
    std::optional<POINT> cursorAnchorClientPoint) {
    DashboardSessionState& state = app_.controller_.State();
    switch (selected) {
        case kCommandMove:
            app_.StartMoveMode(cursorAnchorClientPoint);
            break;
        case kCommandEditLayout:
            HandleEditLayoutToggle();
            app_.UpdateLayoutEditTooltip();
            break;
        case kCommandEditLayoutTarget:
            if (layoutEditTarget.has_value()) {
                PromptAndApplyLayoutEditTarget(*layoutEditTarget);
            }
            app_.UpdateLayoutEditTooltip();
            break;
        case kCommandBringOnTop:
            app_.BringOnTop();
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
        case kCommandSaveFullConfigAs:
            app_.controller_.SaveFullConfigAs(app_);
            break;
        case kCommandCustomScale:
            if (const auto scale = PromptCustomScale(); scale.has_value()) {
                app_.controller_.SetDisplayScale(app_, *scale);
            }
            break;
        case kCommandExit:
            HandleExitRequest();
            break;
        default:
            if (selected >= kCommandLayoutBase && selected <= kCommandLayoutMax) {
                const auto it = std::find_if(state.layoutMenuOptions.begin(),
                    state.layoutMenuOptions.end(),
                    [selected](const LayoutMenuOption& option) { return option.commandId == selected; });
                if (it != state.layoutMenuOptions.end()) {
                    app_.TraceLayoutEditUiEvent(
                        "layout_switch:menu_command", "selected_layout=" + QuoteTraceText(it->name));
                    const bool suppressTooltipRefresh = app_.controller_.State().isEditingLayout;
                    if (suppressTooltipRefresh) {
                        app_.SetLayoutEditTooltipRefreshSuppressed(true);
                        app_.layoutEditController_.HandleMouseLeave();
                        app_.HideLayoutEditTooltip();
                        app_.TraceLayoutEditUiEvent(
                            "layout_switch:menu_prepare", "tooltip_suppressed=" + QuoteTraceText("true"));
                    }
                    if (!app_.controller_.SwitchLayout(app_, it->name, app_.diagnosticsOptions_.editLayout)) {
                        if (suppressTooltipRefresh) {
                            app_.SetLayoutEditTooltipRefreshSuppressed(false);
                        }
                        app_.TraceLayoutEditUiEvent(
                            "layout_switch:menu_failed", "selected_layout=" + QuoteTraceText(it->name));
                        MessageBoxW(app_.hwnd_, L"Failed to switch layout.", L"System Telemetry", MB_ICONERROR);
                    } else {
                        RefreshLayoutEditDialog();
                        if (suppressTooltipRefresh) {
                            app_.SetLayoutEditTooltipRefreshSuppressed(false);
                        }
                        app_.TraceLayoutEditUiEvent(
                            "layout_switch:menu_done", "selected_layout=" + QuoteTraceText(it->name));
                    }
                }
                break;
            }
            if (selected >= kCommandNetworkAdapterBase && selected <= kCommandNetworkAdapterMax) {
                const auto it = std::find_if(state.networkMenuOptions.begin(),
                    state.networkMenuOptions.end(),
                    [selected](const NetworkMenuOption& option) { return option.commandId == selected; });
                if (it != state.networkMenuOptions.end()) {
                    app_.controller_.SelectNetworkAdapter(app_, *it);
                }
                break;
            }
            if (selected >= kCommandStorageDriveBase && selected <= kCommandStorageDriveMax) {
                const auto it = std::find_if(state.storageDriveMenuOptions.begin(),
                    state.storageDriveMenuOptions.end(),
                    [selected](const StorageDriveMenuOption& option) { return option.commandId == selected; });
                if (it != state.storageDriveMenuOptions.end()) {
                    app_.controller_.ToggleStorageDrive(app_, *it);
                }
                break;
            }
            if (selected >= kCommandConfigureDisplayBase && selected <= kCommandConfigureDisplayMax) {
                const auto it = std::find_if(state.configDisplayOptions.begin(),
                    state.configDisplayOptions.end(),
                    [selected](const DisplayMenuOption& option) { return option.commandId == selected; });
                if (it != state.configDisplayOptions.end()) {
                    HandleConfigureDisplay(*it);
                }
                break;
            }
            if (selected >= kCommandScaleBase && selected <= kCommandScaleMax) {
                const auto it = std::find_if(state.scaleMenuOptions.begin(),
                    state.scaleMenuOptions.end(),
                    [selected](const ScaleMenuOption& option) { return option.commandId == selected; });
                if (it != state.scaleMenuOptions.end()) {
                    app_.controller_.SetDisplayScale(app_, it->isDefault ? 0.0 : it->scale);
                }
            }
            break;
    }
}

void DashboardShellUi::InvokeDefaultAction(MenuSource source,
    const std::optional<LayoutEditController::TooltipTarget>& layoutEditTarget,
    std::optional<POINT> cursorAnchorClientPoint) {
    if (source == MenuSource::AppWindow && app_.controller_.State().isEditingLayout) {
        app_.layoutEditController_.CancelInteraction();
        app_.UpdateLayoutEditTooltip();
    }
    ExecuteCommand(ResolveDefaultCommand(source, layoutEditTarget), layoutEditTarget, cursorAnchorClientPoint);
}

void DashboardShellUi::ShowContextMenu(
    MenuSource source, POINT screenPoint, const std::optional<LayoutEditController::TooltipTarget>& layoutEditTarget) {
    app_.HideLayoutEditTooltip();
    DashboardShellUiModalScope scopedModalUi(*this);
    DashboardSessionState& state = app_.controller_.State();
    HMENU menu = CreatePopupMenu();
    HMENU diagnosticsMenu = CreatePopupMenu();
    HMENU layoutMenu = CreatePopupMenu();
    HMENU networkMenu = CreatePopupMenu();
    HMENU scaleMenu = CreatePopupMenu();
    HMENU storageDrivesMenu = CreatePopupMenu();
    HMENU configureDisplayMenu = CreatePopupMenu();
    const UINT autoStartFlags = MF_STRING | (app_.controller_.IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED);
    state.layoutMenuOptions.clear();
    for (size_t i = 0; i < state.config.layout.layouts.size() && (kCommandLayoutBase + i) <= kCommandLayoutMax; ++i) {
        LayoutMenuOption option;
        option.commandId = kCommandLayoutBase + static_cast<UINT>(i);
        option.name = state.config.layout.layouts[i].name;
        option.description = state.config.layout.layouts[i].description;
        state.layoutMenuOptions.push_back(option);
    }
    if (state.layoutMenuOptions.empty()) {
        AppendMenuW(layoutMenu, MF_STRING | MF_GRAYED, kCommandLayoutBase, L"No layouts found");
    } else {
        for (const auto& option : state.layoutMenuOptions) {
            const std::wstring label = FormatLayoutMenuLabel(option);
            const UINT flags = MF_STRING | (state.config.display.layout == option.name ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(layoutMenu, flags, option.commandId, label.c_str());
            SetMenuItemRadioStyle(layoutMenu, option.commandId);
        }
    }
    state.networkMenuOptions.clear();
    const auto& networkCandidates = state.telemetry->NetworkAdapterCandidates();
    for (size_t i = 0; i < networkCandidates.size() && (kCommandNetworkAdapterBase + i) <= kCommandNetworkAdapterMax;
        ++i) {
        NetworkMenuOption option;
        option.commandId = kCommandNetworkAdapterBase + static_cast<UINT>(i);
        option.adapterName = networkCandidates[i].adapterName;
        option.ipAddress = networkCandidates[i].ipAddress;
        option.selected = networkCandidates[i].selected;
        state.networkMenuOptions.push_back(std::move(option));
    }
    if (state.networkMenuOptions.empty()) {
        AppendMenuW(networkMenu, MF_STRING | MF_GRAYED, kCommandNetworkAdapterBase, L"No adapters found");
    } else {
        for (const auto& option : state.networkMenuOptions) {
            const std::wstring label = WideFromUtf8(FormatNetworkFooterText(option.adapterName, option.ipAddress));
            const UINT flags = MF_STRING | (option.selected ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(networkMenu, flags, option.commandId, label.c_str());
            SetMenuItemRadioStyle(networkMenu, option.commandId);
        }
    }
    state.storageDriveMenuOptions.clear();
    const auto& storageDriveCandidates = state.telemetry->StorageDriveCandidates();
    for (size_t i = 0; i < storageDriveCandidates.size() && (kCommandStorageDriveBase + i) <= kCommandStorageDriveMax;
        ++i) {
        StorageDriveMenuOption option;
        option.commandId = kCommandStorageDriveBase + static_cast<UINT>(i);
        option.driveLetter = storageDriveCandidates[i].letter;
        option.volumeLabel = storageDriveCandidates[i].volumeLabel;
        option.totalGb = storageDriveCandidates[i].totalGb;
        option.selected = storageDriveCandidates[i].selected;
        state.storageDriveMenuOptions.push_back(std::move(option));
    }
    if (state.storageDriveMenuOptions.empty()) {
        AppendMenuW(storageDrivesMenu, MF_STRING | MF_GRAYED, kCommandStorageDriveBase, L"No drives found");
    } else {
        for (const auto& option : state.storageDriveMenuOptions) {
            const std::wstring label =
                WideFromUtf8(FormatStorageDriveMenuText(option.driveLetter, option.volumeLabel, option.totalGb));
            const UINT flags = MF_STRING | (option.selected ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(storageDrivesMenu, flags, option.commandId, label.c_str());
        }
    }
    state.scaleMenuOptions.clear();
    {
        ScaleMenuOption option;
        option.commandId = kCommandScaleBase;
        option.label = "Default";
        option.selected = !HasExplicitDisplayScale(state.config.display.scale);
        option.isDefault = true;
        state.scaleMenuOptions.push_back(option);
    }
    std::vector<double> scaleEntries(std::begin(kPredefinedDisplayScales), std::end(kPredefinedDisplayScales));
    if (HasExplicitDisplayScale(state.config.display.scale) && !IsPredefinedDisplayScale(state.config.display.scale)) {
        scaleEntries.push_back(state.config.display.scale);
    }
    std::sort(scaleEntries.begin(), scaleEntries.end());
    scaleEntries.erase(std::unique(scaleEntries.begin(),
                           scaleEntries.end(),
                           [](double left, double right) { return AreScalesEqual(left, right); }),
        scaleEntries.end());
    for (size_t i = 0; i < scaleEntries.size() && (kCommandScaleBase + 1 + i) <= kCommandScaleMax; ++i) {
        ScaleMenuOption option;
        option.commandId = kCommandScaleBase + 1 + static_cast<UINT>(i);
        option.scale = scaleEntries[i];
        option.label = Utf8FromWide(FormatScaleLabel(option.scale));
        option.selected = HasExplicitDisplayScale(state.config.display.scale) &&
                          AreScalesEqual(state.config.display.scale, option.scale);
        option.isCustomEntry = !IsPredefinedDisplayScale(option.scale);
        state.scaleMenuOptions.push_back(std::move(option));
    }
    for (const auto& option : state.scaleMenuOptions) {
        const std::wstring label = WideFromUtf8(option.label);
        const UINT flags = MF_STRING | (option.selected ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(scaleMenu, flags, option.commandId, label.c_str());
        SetMenuItemRadioStyle(scaleMenu, option.commandId);
    }
    AppendMenuW(scaleMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(scaleMenu, MF_STRING, kCommandCustomScale, L"Custom...");
    state.configDisplayOptions = EnumerateDisplayMenuOptions(state.config);
    if (state.configDisplayOptions.empty()) {
        AppendMenuW(configureDisplayMenu, MF_STRING | MF_GRAYED, kCommandConfigureDisplayBase, L"No displays found");
    } else {
        for (const auto& option : state.configDisplayOptions) {
            const std::wstring label = WideFromUtf8(option.displayName);
            const UINT flags = MF_STRING | (option.layoutFits ? MF_ENABLED : MF_GRAYED) |
                               (option.matchesCurrentConfig ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(configureDisplayMenu, flags, option.commandId, label.c_str());
        }
    }
    AppendMenuW(diagnosticsMenu, MF_STRING, kCommandSaveFullConfigAs, L"Save Full Config To...");
    AppendMenuW(diagnosticsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(diagnosticsMenu, MF_STRING, kCommandSaveDumpAs, L"Save Dump To...");
    AppendMenuW(diagnosticsMenu, MF_STRING, kCommandSaveScreenshotAs, L"Save Screenshot To...");
    if (layoutEditTarget.has_value()) {
        std::wstring label;
        if (const auto* guide = std::get_if<LayoutEditGuide>(&layoutEditTarget->payload)) {
            label = BuildLayoutEditMenuLabel(BuildLayoutGuideEditLabel(*guide));
        } else {
            if (IsMetricListAddRowTarget(*layoutEditTarget)) {
                label = L"Add metric list row...";
            }
            const auto focusKey = TooltipPayloadFocusKey(layoutEditTarget->payload);
            if (label.empty() && focusKey.has_value() && std::holds_alternative<LayoutMetricEditKey>(*focusKey)) {
                label = BuildLayoutEditMenuLabel(
                    WideFromUtf8(std::get<LayoutMetricEditKey>(*focusKey).metricId + " metric"));
            } else if (label.empty() && focusKey.has_value() &&
                       std::holds_alternative<LayoutCardTitleEditKey>(*focusKey)) {
                label = BuildLayoutEditMenuLabel(L"card title");
            } else if (label.empty() && focusKey.has_value() &&
                       std::holds_alternative<LayoutMetricListOrderEditKey>(*focusKey)) {
                label = BuildLayoutEditMenuLabel(L"metrics list");
            } else if (label.empty() && focusKey.has_value() &&
                       std::holds_alternative<LayoutContainerEditKey>(*focusKey)) {
                label = BuildLayoutEditMenuLabel(L"layout container");
            } else if (label.empty()) {
                const auto parameter = TooltipPayloadParameter(layoutEditTarget->payload);
                if (parameter.has_value()) {
                    label = BuildLayoutEditMenuLabel(WideFromUtf8(GetLayoutEditParameterDisplayName(*parameter)));
                }
            }
        }
        AppendMenuW(menu, MF_STRING, kCommandEditLayoutTarget, label.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, kCommandMove, L"Move");
    AppendMenuW(
        menu, MF_STRING | (state.isEditingLayout ? MF_CHECKED : MF_UNCHECKED), kCommandEditLayout, L"Edit layout");
    AppendMenuW(menu, MF_STRING, kCommandBringOnTop, L"Bring On Top");
    AppendMenuW(menu, MF_STRING, kCommandReloadConfig, L"Reload Config");
    AppendMenuW(menu, MF_STRING, kCommandSaveConfig, L"Save Config");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(layoutMenu), L"Layout");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(scaleMenu), L"Scale");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(networkMenu), L"Network");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(storageDrivesMenu), L"Storage drives");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(configureDisplayMenu), L"Config To Display");
    AppendMenuW(menu, autoStartFlags, kCommandAutoStart, L"Auto-start on user logon");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(diagnosticsMenu), L"Diagnostics");
    AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");
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
        ExecuteCommand(selected, layoutEditTarget);
    }
}
