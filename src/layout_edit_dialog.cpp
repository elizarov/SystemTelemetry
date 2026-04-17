#include "layout_edit_dialog.h"

#include <algorithm>
#include <commctrl.h>

#include "resource.h"
#include "app_monitor.h"
#include "layout_edit_dialog/dialog_proc.h"
#include "layout_edit_dialog/pane.h"
#include "layout_edit_dialog/state.h"
#include "layout_edit_dialog/trace.h"
#include "layout_edit_dialog/tree.h"
#include "layout_edit_dialog/util.h"
#include "layout_edit_tree.h"
LayoutEditDialog::LayoutEditDialog(LayoutEditDialogHost& host) : host_(host) {}

LayoutEditDialog::~LayoutEditDialog() {
    Close();
}

LayoutEditDialogHost& LayoutEditDialog::Host() {
    return host_;
}

const LayoutEditDialogHost& LayoutEditDialog::Host() const {
    return host_;
}

bool LayoutEditDialog::HandleDialogMessage(MSG* msg) const {
    return msg != nullptr && hwnd_ != nullptr && IsWindow(hwnd_) && IsDialogMessageW(hwnd_, msg) != FALSE;
}

bool LayoutEditDialog::IsForegroundWindow() const {
    if (hwnd_ == nullptr || !IsWindow(hwnd_)) {
        return false;
    }

    const HWND foreground = GetForegroundWindow();
    return foreground != nullptr && (foreground == hwnd_ || IsChild(hwnd_, foreground));
}

void LayoutEditDialog::BringToFront() {
    if (hwnd_ == nullptr || !IsWindow(hwnd_)) {
        return;
    }

    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    BringWindowToTop(hwnd_);
    SetActiveWindow(hwnd_);
    SetForegroundWindow(hwnd_);
}

void LayoutEditDialog::PositionWindow(HWND hwnd) const {
    const HWND anchorHwnd = host_.LayoutEditDialogAnchorWindow();
    if (anchorHwnd == nullptr || hwnd == nullptr) {
        return;
    }

    RECT anchorRect{};
    RECT windowRect{};
    if (!GetWindowRect(anchorHwnd, &anchorRect) || !GetWindowRect(hwnd, &windowRect)) {
        return;
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = MonitorFromWindow(anchorHwnd, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return;
    }

    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    const int gap = ScaleLogicalToPhysical(16, host_.LayoutEditDialogAnchorDpi());
    int left = anchorRect.right + gap;
    if (left + width > monitorInfo.rcWork.right) {
        left = anchorRect.left - gap - width;
    }
    int top = anchorRect.top;

    const int minLeft = static_cast<int>(monitorInfo.rcWork.left);
    const int maxLeft = std::max(minLeft, static_cast<int>(monitorInfo.rcWork.right) - width);
    const int minTop = static_cast<int>(monitorInfo.rcWork.top);
    const int maxTop = std::max(minTop, static_cast<int>(monitorInfo.rcWork.bottom) - height);
    left = std::clamp(left, minLeft, maxLeft);
    top = std::clamp(top, minTop, maxTop);
    SetWindowPos(hwnd, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void LayoutEditDialog::UpdateSelectionHighlight(const std::optional<LayoutEditSelectionHighlight>& highlight) {
    selectionHighlight_ = highlight;
    ApplySelectionHighlightVisibility();
}

void LayoutEditDialog::ApplySelectionHighlightVisibility() {
    host_.UpdateLayoutEditSelectionHighlight(selectionHighlightVisible_ ? selectionHighlight_ : std::nullopt);
}

void LayoutEditDialog::SetSelectionHighlightVisible(bool visible) {
    if (selectionHighlightVisible_ == visible) {
        return;
    }

    selectionHighlightVisible_ = visible;
    ApplySelectionHighlightVisibility();
}

void LayoutEditDialog::HandleDestroyed(HWND hwnd) {
    if (hwnd_ == hwnd) {
        hwnd_ = nullptr;
    }
    selectionHighlightVisible_ = false;
    selectionHighlight_.reset();
    host_.UpdateLayoutEditSelectionHighlight(std::nullopt);
    state_.reset();
}

void LayoutEditDialog::Close() {
    if (hwnd_ == nullptr) {
        UpdateSelectionHighlight(std::nullopt);
        return;
    }

    HWND dialog = hwnd_;
    hwnd_ = nullptr;
    DestroyWindow(dialog);
    UpdateSelectionHighlight(std::nullopt);
}

bool LayoutEditDialog::Ensure(const std::optional<LayoutEditFocusKey>& focusKey, bool bringToFront) {
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        Refresh(focusKey);
        if (bringToFront) {
            BringToFront();
        }
        return true;
    }

    state_ = std::make_unique<LayoutEditDialogState>();
    state_->dialog = this;
    state_->originalConfig = host_.BuildLayoutEditOriginalConfig();
    state_->treeModel = BuildLayoutEditTreeModel(host_.CurrentConfig());
    state_->initialFocus = focusKey;

    std::string initialFocusTrace = "session";
    if (focusKey.has_value()) {
        if (const auto* parameter = std::get_if<LayoutEditParameter>(&*focusKey)) {
            initialFocusTrace =
                FindLayoutEditTooltipDescriptor(*parameter).value_or(LayoutEditTooltipDescriptor{}).configKey;
        } else if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&*focusKey)) {
            initialFocusTrace = "[metrics] " + metricKey->metricId;
        } else if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&*focusKey)) {
            initialFocusTrace = "[card." + cardTitleKey->cardId + "] title";
        } else if (const auto* metricListKey = std::get_if<LayoutMetricListOrderEditKey>(&*focusKey)) {
            initialFocusTrace = metricListKey->editCardId.empty() ? "[layout] metric_list"
                                                                  : "[card." + metricListKey->editCardId + "] metric_list";
        } else {
            initialFocusTrace = "weight";
        }
    }
    host_.TraceLayoutEditDialogEvent("layout_edit_dialog:open", "initial_focus=" + QuoteTraceText(initialFocusTrace));

    HWND dialog = CreateDialogParamW(host_.LayoutEditDialogInstance(),
        MAKEINTRESOURCEW(IDD_LAYOUT_EDIT_CONFIGURATION),
        nullptr,
        LayoutEditDialog::DialogProc,
        reinterpret_cast<LPARAM>(state_.get()));
    if (dialog == nullptr) {
        state_.reset();
        return false;
    }

    hwnd_ = dialog;
    if (bringToFront) {
        BringToFront();
        SetSelectionHighlightVisible(true);
    }
    return true;
}

void LayoutEditDialog::Refresh(const std::optional<LayoutEditFocusKey>& preferredFocus) {
    if (hwnd_ == nullptr || !IsWindow(hwnd_) || state_ == nullptr) {
        return;
    }

    state_->originalConfig = host_.BuildLayoutEditOriginalConfig();
    state_->treeModel = BuildLayoutEditTreeModel(host_.CurrentConfig());
    RefreshLayoutEditDialogControls(state_.get(), hwnd_, preferredFocus, true);
}

void LayoutEditDialog::RefreshSelection() {
    if (hwnd_ == nullptr || !IsWindow(hwnd_) || state_ == nullptr) {
        return;
    }

    state_->originalConfig = host_.BuildLayoutEditOriginalConfig();
    RefreshLayoutEditDialogControls(state_.get(), hwnd_, std::nullopt, false);
}

bool LayoutEditDialog::SyncSelection(
    const std::optional<LayoutEditController::TooltipTarget>& target, bool bringToFront) {
    if (!target.has_value()) {
        return true;
    }

    const auto focusKey = TooltipPayloadFocusKey(target->payload);
    if (!focusKey.has_value()) {
        if (bringToFront && hwnd_ != nullptr && IsWindow(hwnd_)) {
            BringToFront();
        }
        return true;
    }

    if (hwnd_ == nullptr || !IsWindow(hwnd_)) {
        if (!bringToFront) {
            return true;
        }
        return Ensure(focusKey, true);
    }

    if (state_ != nullptr) {
        state_->originalConfig = host_.BuildLayoutEditOriginalConfig();
        RefreshLayoutEditDialogControls(state_.get(), hwnd_, focusKey, false);
    }
    if (bringToFront) {
        BringToFront();
    }
    return true;
}

bool LayoutEditDialog::ShouldDashboardIgnoreMouse(POINT screenPoint) const {
    if (hwnd_ == nullptr || !IsWindow(hwnd_) || !IsWindowVisible(hwnd_)) {
        return false;
    }

    RECT dialogRect{};
    if (!GetWindowRect(hwnd_, &dialogRect) || !PtInRect(&dialogRect, screenPoint)) {
        return false;
    }

    if (IsForegroundWindow()) {
        return true;
    }

    if (HWND hitWindow = WindowFromPoint(screenPoint);
        hitWindow != nullptr && (hitWindow == hwnd_ || IsChild(hwnd_, hitWindow))) {
        return true;
    }

    const HWND anchor = host_.LayoutEditDialogAnchorWindow();
    for (HWND window = GetWindow(anchor, GW_HWNDPREV); window != nullptr; window = GetWindow(window, GW_HWNDPREV)) {
        if (window == hwnd_) {
            return true;
        }
    }
    return false;
}

INT_PTR CALLBACK LayoutEditDialog::DialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto* state = reinterpret_cast<LayoutEditDialogState*>(lParam);
        SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        SetWindowTextW(hwnd, L"Edit Configuration");
        state->dialog->UpdateSelectionHighlight(std::nullopt);
        ConfigureColorSliders(hwnd);
        ConfigureDialogFonts(state, hwnd);
        if (HWND tree = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE); tree != nullptr) {
            TreeView_SetExtendedStyle(tree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
        }
        SendDlgItemMessageW(
            hwnd, IDC_LAYOUT_EDIT_FILTER_EDIT, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"Filter settings"));
        RebuildLayoutEditTree(state, hwnd, state->initialFocus);
        state->dialog->PositionWindow(hwnd);
        ShowWindow(hwnd, SW_SHOWNORMAL);
        return TRUE;
    }

    if (message == WM_NCDESTROY) {
        if (auto* state = DialogStateFromWindow(hwnd); state != nullptr) {
            state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:close");
            SetWindowLongPtrW(hwnd, DWLP_USER, 0);
            state->dialog->HandleDestroyed(hwnd);
        }
        return FALSE;
    }

    if (const auto handled = HandleLayoutEditDialogProcMessage(hwnd, message, wParam, lParam); handled.has_value()) {
        return *handled;
    }
    return FALSE;
}
