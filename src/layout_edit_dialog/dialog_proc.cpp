#include "dialog_proc.h"

#include <commctrl.h>
#include <commdlg.h>

#include "../../resources/resource.h"
#include "editors.h"
#include "pane.h"
#include "trace.h"
#include "tree.h"
#include "util.h"
#include "utf8.h"

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
        case WM_COMMAND:
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
                        SetColorDialogChannel(hwnd, kColorDialogControls[0], (*color >> 16) & 0xFFu);
                        SetColorDialogChannel(hwnd, kColorDialogControls[1], (*color >> 8) & 0xFFu);
                        SetColorDialogChannel(hwnd, kColorDialogControls[2], *color & 0xFFu);
                        state->updatingControls = false;
                    }
                }
                PreviewSelectedColor(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
                return TRUE;
            }
            if ((LOWORD(wParam) == IDC_LAYOUT_EDIT_COLOR_RED_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT) &&
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
                        FindLayoutEditParameterColorValue(state->dialog->Host().CurrentConfig(), parameter).value_or(0);
                    CHOOSECOLORW chooseColor{};
                    chooseColor.lStructSize = sizeof(chooseColor);
                    chooseColor.hwndOwner = hwnd;
                    chooseColor.rgbResult =
                        RGB((currentColor >> 16) & 0xFFu, (currentColor >> 8) & 0xFFu, currentColor & 0xFFu);
                    chooseColor.lpCustColors = state->customColors;
                    chooseColor.Flags = CC_FULLOPEN | CC_RGBINIT;
                    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:picker_open",
                        BuildTraceNodeText(state->selectedNode) +
                            " current=" + QuoteTraceText(FormatTraceColorHex(currentColor)));
                    if (ChooseColorW(&chooseColor) == TRUE) {
                        const unsigned int nextColor = (GetRValue(chooseColor.rgbResult) << 16) |
                                                       (GetGValue(chooseColor.rgbResult) << 8) |
                                                       GetBValue(chooseColor.rgbResult);
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
                DestroyDialogFonts(state);
            }
            break;
        case WM_ACTIVATE:
            if (state != nullptr) {
                state->dialog->SetSelectionHighlightVisible(LOWORD(wParam) != WA_INACTIVE);
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
