#include "layout_edit_dialog/impl/dialog_proc.h"

#include <algorithm>
#include <commctrl.h>
#include <commdlg.h>

#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_edit_dialog/impl/editors.h"
#include "layout_edit_dialog/impl/pane.h"
#include "layout_edit_dialog/impl/trace.h"
#include "layout_edit_dialog/impl/tree.h"
#include "layout_edit_dialog/impl/util.h"
#include "resource.h"
#include "util/utf8.h"

namespace {

bool IsMetricListOrderButtonId(int controlId) {
    return (controlId >= IDC_LAYOUT_EDIT_METRIC_LIST_ROW_UP_BASE &&
               controlId < IDC_LAYOUT_EDIT_METRIC_LIST_ROW_UP_BASE + 100) ||
           (controlId >= IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DOWN_BASE &&
               controlId < IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DOWN_BASE + 100) ||
           (controlId >= IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DELETE_BASE &&
               controlId < IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DELETE_BASE + 100);
}

bool IsDialogComboBoxControl(HWND hwnd, int controlId) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return false;
    }

    wchar_t className[64] = {};
    if (GetClassNameW(control, className, ARRAYSIZE(className)) == 0) {
        return false;
    }
    return CompareStringOrdinal(className, -1, WC_COMBOBOXW, -1, TRUE) == CSTR_EQUAL;
}

void RaiseComboDropList(HWND hwnd, int controlId) {
    BringDialogControlToTop(hwnd, controlId);

    HWND combo = GetDlgItem(hwnd, controlId);
    if (combo == nullptr) {
        return;
    }

    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(combo, &info) == FALSE || info.hwndList == nullptr) {
        return;
    }

    SetWindowPos(info.hwndList, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(info.hwndList, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ResizeComboDropList(HWND hwnd, int controlId, int maxVisibleItems = 10) {
    HWND combo = GetDlgItem(hwnd, controlId);
    if (combo == nullptr) {
        return;
    }

    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(combo, &info) == FALSE || info.hwndList == nullptr) {
        return;
    }

    const LRESULT itemCount = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    if (itemCount == CB_ERR || itemCount <= 0) {
        return;
    }

    const LRESULT listItemHeight = SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0);
    const LRESULT selectionItemHeight = SendMessageW(combo, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
    const int itemHeight =
        (std::max)(1, static_cast<int>(listItemHeight != CB_ERR ? listItemHeight : selectionItemHeight));
    const int visibleItems = (std::max)(1, (std::min)(static_cast<int>(itemCount), maxVisibleItems));
    const int borderHeight = GetSystemMetrics(SM_CYEDGE) * 2;
    const int desiredHeight = (visibleItems * itemHeight) + borderHeight;

    RECT comboRect{};
    RECT listRect{};
    if (GetWindowRect(combo, &comboRect) == FALSE || GetWindowRect(info.hwndList, &listRect) == FALSE) {
        return;
    }

    const int comboWidth = static_cast<int>(comboRect.right - comboRect.left);
    const int listWidth = static_cast<int>(listRect.right - listRect.left);
    const int width = (std::max)(comboWidth, listWidth);
    const bool opensBelow = listRect.top >= comboRect.bottom - 1;
    const int left = listRect.left;
    const int top = opensBelow ? comboRect.bottom : listRect.bottom - desiredHeight;

    SetWindowPos(info.hwndList, HWND_TOPMOST, left, top, width, desiredHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowPos(info.hwndList, HWND_NOTOPMOST, left, top, width, desiredHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

bool PrepareComboDropList(HWND hwnd, int controlId) {
    if (!IsDialogComboBoxControl(hwnd, controlId)) {
        return false;
    }

    ResizeComboDropList(hwnd, controlId);
    RaiseComboDropList(hwnd, controlId);
    return true;
}

void DrawCenteredFilledTriangle(HDC dc, const RECT& rect, bool up) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int size = std::max(6, std::min(width, height) - 10);
    const int centerX = rect.left + (width / 2);
    const int centerY = rect.top + (height / 2);
    POINT points[3]{};
    if (up) {
        points[0] = POINT{centerX, centerY - (size / 2)};
        points[1] = POINT{centerX - (size / 2), centerY + (size / 2)};
        points[2] = POINT{centerX + (size / 2), centerY + (size / 2)};
    } else {
        points[0] = POINT{centerX, centerY + (size / 2)};
        points[1] = POINT{centerX - (size / 2), centerY - (size / 2)};
        points[2] = POINT{centerX + (size / 2), centerY - (size / 2)};
    }
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(BLACK_PEN));
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(BLACK_BRUSH));
    Polygon(dc, points, 3);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

void DrawCenteredCross(HDC dc, const RECT& rect) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int size = std::max(6, std::min(width, height) - 10);
    const int centerX = rect.left + (width / 2);
    const int centerY = rect.top + (height / 2);
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, centerX - (size / 2), centerY - (size / 2), nullptr);
    LineTo(dc, centerX + (size / 2), centerY + (size / 2));
    MoveToEx(dc, centerX - (size / 2), centerY + (size / 2), nullptr);
    LineTo(dc, centerX + (size / 2), centerY - (size / 2));
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

INT_PTR DrawMetricListOrderButton(const DRAWITEMSTRUCT* draw) {
    FillRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_3DFACE));
    UINT edge = (draw->itemState & ODS_SELECTED) != 0 ? EDGE_SUNKEN : EDGE_RAISED;
    DrawEdge(draw->hDC, const_cast<RECT*>(&draw->rcItem), edge, BF_RECT);

    RECT content = draw->rcItem;
    InflateRect(&content, -2, -2);
    if ((draw->itemState & ODS_DISABLED) != 0) {
        SetTextColor(draw->hDC, RGB(140, 140, 140));
    }

    if (draw->CtlID >= IDC_LAYOUT_EDIT_METRIC_LIST_ROW_UP_BASE &&
        draw->CtlID < IDC_LAYOUT_EDIT_METRIC_LIST_ROW_UP_BASE + 100) {
        DrawCenteredFilledTriangle(draw->hDC, content, true);
    } else if (draw->CtlID >= IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DOWN_BASE &&
               draw->CtlID < IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DOWN_BASE + 100) {
        DrawCenteredFilledTriangle(draw->hDC, content, false);
    } else {
        DrawCenteredCross(draw->hDC, content);
    }
    return TRUE;
}

}  // namespace

std::optional<INT_PTR> HandleLayoutEditDialogProcMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = DialogStateFromWindow(hwnd);
    switch (message) {
        case WM_NOTIFY: {
            const auto* notify = reinterpret_cast<NMHDR*>(lParam);
            if (notify != nullptr && notify->idFrom == IDC_LAYOUT_EDIT_TREE && notify->code == TVN_SELCHANGEDW) {
                const auto* treeView = reinterpret_cast<NMTREEVIEWW*>(lParam);
                HandleLayoutEditTreeSelection(state, hwnd, treeView->itemNew.hItem);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (draw != nullptr && IsMetricListOrderButtonId(static_cast<int>(draw->CtlID))) {
                return DrawMetricListOrderButton(draw);
            }
            break;
        }
        case WM_COMMAND:
            if (HandleMetricListOrderEditorCommand(state, hwnd, LOWORD(wParam), HIWORD(wParam))) {
                return TRUE;
            }
            if (HIWORD(wParam) == CBN_DROPDOWN && PrepareComboDropList(hwnd, LOWORD(wParam))) {
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_LAYOUT_EDIT_FILTER_EDIT && HIWORD(wParam) == EN_CHANGE) {
                wchar_t filterBuffer[256] = {};
                GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FILTER_EDIT, filterBuffer, ARRAYSIZE(filterBuffer));
                state->currentFilter = filterBuffer;
                RebuildLayoutEditTree(state, hwnd);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_LAYOUT_EDIT_VALUE_EDIT && HIWORD(wParam) == EN_CHANGE) {
                PreviewSelectedValue(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
                return TRUE;
            }
            if ((LOWORD(wParam) == IDC_LAYOUT_EDIT_FONT_FACE_EDIT &&
                    (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_EDITCHANGE)) ||
                ((LOWORD(wParam) == IDC_LAYOUT_EDIT_FONT_SIZE_EDIT ||
                     LOWORD(wParam) == IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT) &&
                    HIWORD(wParam) == EN_CHANGE)) {
                if (LOWORD(wParam) == IDC_LAYOUT_EDIT_FONT_FACE_EDIT &&
                    CurrentLayoutEditEditorKind(state) == LayoutEditEditorKind::GlobalFontFamily) {
                    PreviewSelectedGlobalFontFamily(state, hwnd, HIWORD(wParam));
                    RefreshLayoutEditValidationState(state, hwnd);
                    return TRUE;
                }
                PreviewSelectedFont(state, hwnd, HIWORD(wParam));
                RefreshLayoutEditValidationState(state, hwnd);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_LAYOUT_EDIT_COLOR_HEX_EDIT && HIWORD(wParam) == EN_CHANGE) {
                if (state != nullptr && !state->updatingControls) {
                    wchar_t buffer[64] = {};
                    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, buffer, ARRAYSIZE(buffer));
                    if (const auto color = TryParseDialogHexColor(buffer); color.has_value()) {
                        state->updatingControls = true;
                        SetColorDialogChannel(hwnd, kColorDialogControls[0], (*color >> 24) & 0xFFu);
                        SetColorDialogChannel(hwnd, kColorDialogControls[1], (*color >> 16) & 0xFFu);
                        SetColorDialogChannel(hwnd, kColorDialogControls[2], (*color >> 8) & 0xFFu);
                        SetColorDialogChannel(hwnd, kColorDialogControls[3], *color & 0xFFu);
                        state->updatingControls = false;
                    }
                }
                PreviewSelectedColor(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
                return TRUE;
            }
            if ((LOWORD(wParam) == IDC_LAYOUT_EDIT_COLOR_RED_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT) &&
                HIWORD(wParam) == EN_CHANGE) {
                if (const auto* channel = FindColorDialogControlsByEditId(LOWORD(wParam));
                    channel != nullptr && state != nullptr && !state->updatingControls) {
                    const auto value = ParseColorDialogChannel(hwnd, channel->editId);
                    if (value.has_value()) {
                        const auto color = ReadColorDialogValue(hwnd);
                        SendDlgItemMessageW(hwnd, channel->sliderId, TBM_SETPOS, TRUE, *value);
                        if (color.has_value()) {
                            state->updatingControls = true;
                            SetColorDialogHex(hwnd, *color);
                            state->updatingControls = false;
                        }
                    }
                }
                PreviewSelectedColor(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
                return TRUE;
            }
            if ((LOWORD(wParam) == IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT) &&
                HIWORD(wParam) == EN_CHANGE) {
                PreviewSelectedWeights(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
                return TRUE;
            }
            if ((LOWORD(wParam) == IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT) &&
                HIWORD(wParam) == EN_CHANGE) {
                PreviewSelectedMetric(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT &&
                (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_EDITCHANGE)) {
                PreviewSelectedMetric(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO &&
                (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_EDITCHANGE)) {
                PreviewSelectedDateTimeFormat(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
                return TRUE;
            }
            switch (LOWORD(wParam)) {
                case IDC_LAYOUT_EDIT_REVERT:
                    RevertSelectedLayoutEditField(state, hwnd);
                    return TRUE;
                case IDC_LAYOUT_EDIT_COLOR_PICK: {
                    if (state == nullptr || state->selectedLeaf == nullptr) {
                        return TRUE;
                    }
                    if (!std::holds_alternative<LayoutEditParameter>(state->selectedLeaf->focusKey) ||
                        state->selectedLeaf->valueFormat != configschema::ValueFormat::ColorHex) {
                        return TRUE;
                    }
                    const auto parameter = std::get<LayoutEditParameter>(state->selectedLeaf->focusKey);
                    const unsigned int currentColor =
                        FindLayoutEditParameterColorValue(state->dialog->Host().CurrentConfig(), parameter)
                            .value_or(0x000000FFu);
                    CHOOSECOLORW chooseColor{};
                    chooseColor.lStructSize = sizeof(chooseColor);
                    chooseColor.hwndOwner = hwnd;
                    chooseColor.rgbResult =
                        RGB((currentColor >> 24) & 0xFFu, (currentColor >> 16) & 0xFFu, (currentColor >> 8) & 0xFFu);
                    chooseColor.lpCustColors = state->customColors;
                    chooseColor.Flags = CC_FULLOPEN | CC_RGBINIT;
                    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:picker_open",
                        BuildTraceNodeText(state->selectedNode) +
                            " current=" + QuoteTraceText(FormatTraceColorHex(currentColor)));
                    if (ChooseColorW(&chooseColor) == TRUE) {
                        const unsigned int currentAlpha = ReadColorDialogValue(hwnd).value_or(currentColor) & 0xFFu;
                        const unsigned int nextColor = (GetRValue(chooseColor.rgbResult) << 24) |
                                                       (GetGValue(chooseColor.rgbResult) << 16) |
                                                       (GetBValue(chooseColor.rgbResult) << 8) | currentAlpha;
                        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:picker_return",
                            BuildTraceNodeText(state->selectedNode) +
                                " accepted=\"true\" chosen=" + QuoteTraceText(FormatTraceColorHex(nextColor)));
                        SetSelectedDialogColor(state, hwnd, nextColor);
                    } else {
                        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:picker_return",
                            BuildTraceNodeText(state->selectedNode) + " accepted=\"false\"");
                    }
                    RefreshLayoutEditValidationState(state, hwnd);
                    return TRUE;
                }
            }
            break;
        case WM_HSCROLL:
            if (state != nullptr) {
                const int sliderId = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
                if (const auto* channel = FindColorDialogControlsBySliderId(sliderId); channel != nullptr) {
                    if (!state->updatingControls) {
                        const LRESULT position = SendDlgItemMessageW(hwnd, channel->sliderId, TBM_GETPOS, 0, 0);
                        state->updatingControls = true;
                        SetDlgItemTextW(hwnd, channel->editId, WideFromUtf8(std::to_string(position)).c_str());
                        if (const auto color = ReadColorDialogValue(hwnd); color.has_value()) {
                            SetColorDialogHex(hwnd, *color);
                        }
                        state->updatingControls = false;
                        PreviewSelectedColor(state, hwnd);
                        RefreshLayoutEditValidationState(state, hwnd);
                    }
                    return TRUE;
                }
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (state != nullptr) {
                const int controlId = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
                HDC dc = reinterpret_cast<HDC>(wParam);
                if (controlId == IDC_LAYOUT_EDIT_STATUS_TEXT) {
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, state->statusIsError ? RGB(180, 40, 40) : RGB(90, 90, 90));
                    return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_3DFACE));
                }
                if (controlId == IDC_LAYOUT_EDIT_LOCATION || controlId == IDC_LAYOUT_EDIT_HINT ||
                    controlId == IDC_LAYOUT_EDIT_FOOTER_HINT) {
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, RGB(96, 96, 96));
                    return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_3DFACE));
                }
                if (controlId == IDC_LAYOUT_EDIT_COLOR_SAMPLE) {
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, state->previewColor);
                    return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_3DFACE));
                }
                if (controlId == IDC_LAYOUT_EDIT_COLOR_SWATCH) {
                    SetBkColor(dc, state->previewColor);
                    SetDCBrushColor(dc, state->previewColor);
                    return reinterpret_cast<INT_PTR>(GetStockObject(DC_BRUSH));
                }
            }
            break;
        case WM_DESTROY:
            if (state != nullptr) {
                state->dialog->UpdateSelectionHighlight(std::nullopt);
                DestroyMetricListOrderEditorControls(state);
                DestroyDialogFonts(state);
            }
            break;
        case WM_ACTIVATE:
            if (state != nullptr) {
                if (LOWORD(wParam) != WA_INACTIVE) {
                    state->dialog->SetSelectionHighlightVisible(true);
                    state->dialog->Host().RestackLayoutEditDialogAnchor(hwnd);
                }
            }
            break;
        case WM_CLOSE:
            if (state != nullptr) {
                state->dialog->Host().OnLayoutEditDialogCloseRequested();
            } else {
                DestroyWindow(hwnd);
            }
            return TRUE;
    }
    return std::nullopt;
}
