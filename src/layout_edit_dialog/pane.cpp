#include "pane.h"

#include <algorithm>

#include <commctrl.h>

#include "../../resources/resource.h"
#include "editors.h"
#include "layout_edit_tooltip.h"
#include "localization_catalog.h"
#include "util.h"
#include "utf8.h"

namespace {

struct LayoutEditRightPaneMetrics {
    int paneLeftGap = 8;
    int paneRightMargin = 8;
    int topMargin = 10;
    int headerGap = 4;
    int headerToGroupGap = 8;
    int groupToStatusGap = 8;
    int statusToFooterGap = 10;
    int footerToButtonsGap = 10;
    int bottomMargin = 8;
    int buttonGap = 8;
    int groupPadding = 14;
    int rowGap = 10;
    int inlineGap = 10;
    int labelGap = 10;
    int hintGap = 10;
    int sampleGap = 10;
};

constexpr LayoutEditRightPaneMetrics kLayoutEditRightPaneMetrics{};

bool DialogControlHasClass(HWND hwnd, int controlId, const wchar_t* expectedClassName) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return false;
    }
    wchar_t className[64] = {};
    if (GetClassNameW(control, className, ARRAYSIZE(className)) == 0) {
        return false;
    }
    return CompareStringOrdinal(className, -1, expectedClassName, -1, TRUE) == CSTR_EQUAL;
}

bool IsDialogComboBoxControl(HWND hwnd, int controlId) {
    return DialogControlHasClass(hwnd, controlId, WC_COMBOBOXW);
}

bool IsDialogEditControl(HWND hwnd, int controlId) {
    return DialogControlHasClass(hwnd, controlId, WC_EDITW);
}

bool UsesSingleLineFieldFrame(HWND hwnd, int controlId) {
    return IsDialogEditControl(hwnd, controlId) || IsDialogComboBoxControl(hwnd, controlId);
}

std::wstring ReadWindowTextWide(HWND window) {
    if (window == nullptr) {
        return {};
    }

    const int length = GetWindowTextLengthW(window);
    std::wstring text(length, L'\0');
    if (length > 0) {
        GetWindowTextW(window, text.data(), length + 1);
    }
    return text;
}

void DestroyDialogFont(HFONT& font) {
    if (font != nullptr) {
        DeleteObject(font);
        font = nullptr;
    }
}

HFONT CreateDerivedDialogFont(HWND hwnd, int controlId, int weight, int heightDelta = 0) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return nullptr;
    }
    HFONT baseFont = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
    LOGFONTW logFont{};
    if (baseFont != nullptr && GetObjectW(baseFont, sizeof(logFont), &logFont) == sizeof(logFont)) {
        logFont.lfWeight = weight;
        logFont.lfHeight -= heightDelta;
        return CreateFontIndirectW(&logFont);
    }
    return nullptr;
}

std::wstring BuildFontSampleText(LayoutEditParameter parameter) {
    switch (parameter) {
        case LayoutEditParameter::FontTitle:
            return L"Card Title";
        case LayoutEditParameter::FontBig:
            return L"88";
        case LayoutEditParameter::FontValue:
            return L"42%";
        case LayoutEditParameter::FontLabel:
            return L"CPU";
        case LayoutEditParameter::FontText:
            return L"Sample text";
        case LayoutEditParameter::FontSmall:
            return L"42 C";
        case LayoutEditParameter::FontFooter:
            return L"192.168.1.20";
        case LayoutEditParameter::FontClockTime:
            return L"12:34";
        case LayoutEditParameter::FontClockDate:
            return L"Wed 31";
        default:
            return L"Sample";
    }
}

HFONT CreatePreviewFontToFit(HWND hwnd, int controlId, const UiFontConfig& font, std::wstring_view sampleText) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr || sampleText.empty()) {
        return nullptr;
    }

    RECT rect{};
    GetClientRect(control, &rect);
    const int controlWidth = static_cast<int>(rect.right - rect.left);
    const int controlHeight = static_cast<int>(rect.bottom - rect.top);
    const int availableWidth = std::max(1, controlWidth - 6);
    const int availableHeight = std::max(1, controlHeight - 6);
    const int dpi = GetDpiForWindow(hwnd);
    HDC dc = GetDC(control);
    if (dc == nullptr) {
        return nullptr;
    }

    HFONT fittedFont = nullptr;
    const int startingSize = std::max(1, font.size);
    for (int previewSize = startingSize; previewSize >= 1; --previewSize) {
        LOGFONTW logFont{};
        logFont.lfHeight = -MulDiv(previewSize, dpi, 72);
        logFont.lfWeight = font.weight;
        wcsncpy_s(logFont.lfFaceName, WideFromUtf8(font.face).c_str(), LF_FACESIZE - 1);
        HFONT candidate = CreateFontIndirectW(&logFont);
        if (candidate == nullptr) {
            continue;
        }

        HFONT previous = reinterpret_cast<HFONT>(SelectObject(dc, candidate));
        SIZE sampleSize{};
        const BOOL measured =
            GetTextExtentPoint32W(dc, sampleText.data(), static_cast<int>(sampleText.size()), &sampleSize);
        SelectObject(dc, previous);
        if (measured == TRUE && sampleSize.cx <= availableWidth && sampleSize.cy <= availableHeight) {
            fittedFont = candidate;
            break;
        }
        DeleteObject(candidate);
    }

    ReleaseDC(control, dc);
    return fittedFont;
}

std::vector<int> ActiveEditorLabelControls(LayoutEditEditorKind kind, bool showBinding) {
    switch (kind) {
        case LayoutEditEditorKind::Font:
            return {
                IDC_LAYOUT_EDIT_FONT_FACE_LABEL, IDC_LAYOUT_EDIT_FONT_SIZE_LABEL, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL};
        case LayoutEditEditorKind::Color:
            return {
                IDC_LAYOUT_EDIT_COLOR_RED_LABEL, IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL, IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL};
        case LayoutEditEditorKind::Weights:
            return {IDC_LAYOUT_EDIT_WEIGHT_FIRST_LABEL, IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL};
        case LayoutEditEditorKind::Metric: {
            std::vector<int> labels = {
                IDC_LAYOUT_EDIT_METRIC_STYLE_LABEL,
                IDC_LAYOUT_EDIT_METRIC_SCALE_LABEL,
                IDC_LAYOUT_EDIT_METRIC_UNIT_LABEL,
                IDC_LAYOUT_EDIT_METRIC_LABEL_LABEL,
            };
            if (showBinding) {
                labels.push_back(IDC_LAYOUT_EDIT_METRIC_BINDING_LABEL);
            }
            return labels;
        }
        case LayoutEditEditorKind::Numeric:
        case LayoutEditEditorKind::Summary:
            return {};
    }
    return {};
}

int MeasureLabelColumnWidth(HWND hwnd, const std::vector<int>& labelIds) {
    int width = 0;
    for (const int labelId : labelIds) {
        width = std::max(width, MeasureTextWidthForControl(hwnd, labelId, ReadDialogControlTextWide(hwnd, labelId)));
    }
    return width;
}

int LayoutLabeledControlRow(HWND hwnd,
    int labelId,
    int controlId,
    int left,
    int top,
    int labelWidth,
    int gap,
    int controlWidth,
    int forcedRowHeight = 0) {
    const int visibleControlHeight = DialogControlVisibleHeight(hwnd, controlId);
    const int desiredVisibleControlHeight =
        forcedRowHeight > 0 && UsesSingleLineFieldFrame(hwnd, controlId) ? forcedRowHeight : visibleControlHeight;
    const int controlHeight = DialogControlLayoutHeightForVisibleHeight(hwnd, controlId, desiredVisibleControlHeight);
    const int labelHeight = MeasureTextHeightForControl(
        hwnd, labelId, ReadDialogControlTextWide(hwnd, labelId), std::max(1, labelWidth), true);
    const int controlLeft = left + labelWidth + gap;
    const int rowHeight = std::max(forcedRowHeight, std::max(desiredVisibleControlHeight, labelHeight));
    SetDialogControlBounds(hwnd,
        controlId,
        controlLeft,
        top + ((rowHeight - desiredVisibleControlHeight) / 2),
        controlWidth,
        controlHeight);
    SetDialogControlBounds(hwnd, labelId, left, top + ((rowHeight - labelHeight) / 2), labelWidth, labelHeight);
    return rowHeight;
}

}  // namespace

DialogRedrawScope::DialogRedrawScope(HWND hwnd) : hwnd_(hwnd) {
    if (hwnd_ != nullptr) {
        SendMessageW(hwnd_, WM_SETREDRAW, FALSE, 0);
    }
}

DialogRedrawScope::~DialogRedrawScope() {
    if (hwnd_ != nullptr) {
        SendMessageW(hwnd_, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

void ShowDialogControl(HWND hwnd, int controlId, bool show) {
    if (HWND control = GetDlgItem(hwnd, controlId); control != nullptr) {
        ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    }
}

std::optional<RECT> DialogControlRect(HWND hwnd, int controlId) {
    RECT rect{};
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr || !GetWindowRect(control, &rect)) {
        return std::nullopt;
    }
    MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&rect), 2);
    return rect;
}

int DialogControlWidth(HWND hwnd, int controlId) {
    const auto rect = DialogControlRect(hwnd, controlId);
    return rect.has_value() ? static_cast<int>(rect->right - rect->left) : 0;
}

int DialogControlHeight(HWND hwnd, int controlId) {
    const auto rect = DialogControlRect(hwnd, controlId);
    return rect.has_value() ? static_cast<int>(rect->bottom - rect->top) : 0;
}

int DialogControlVisibleHeight(HWND hwnd, int controlId) {
    const auto rect = DialogControlRect(hwnd, controlId);
    if (!rect.has_value()) {
        return 0;
    }
    return std::max(1, static_cast<int>(rect->bottom - rect->top));
}

int DialogControlLayoutHeightForVisibleHeight(HWND hwnd, int controlId, int desiredVisibleHeight) {
    const int currentHeight = DialogControlHeight(hwnd, controlId);
    if (!UsesSingleLineFieldFrame(hwnd, controlId)) {
        return desiredVisibleHeight;
    }
    const int currentVisibleHeight = DialogControlVisibleHeight(hwnd, controlId);
    const int framePadding = std::max(0, currentHeight - currentVisibleHeight);
    return desiredVisibleHeight + framePadding;
}

int MeasureSingleLineFieldVisibleHeight(HWND hwnd) {
    const int editHeight = DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT);
    if (editHeight > 0) {
        return editHeight;
    }
    const int comboHeight = DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT);
    if (comboHeight > 0) {
        return comboHeight;
    }
    return 14;
}

void SetDialogControlBounds(HWND hwnd, int controlId, int left, int top, int width, int height) {
    if (HWND control = GetDlgItem(hwnd, controlId); control != nullptr) {
        SetWindowPos(control, nullptr, left, top, std::max(1, width), std::max(1, height), SWP_NOZORDER);
    }
}

std::wstring ReadDialogControlTextWide(HWND hwnd, int controlId) {
    return ReadWindowTextWide(GetDlgItem(hwnd, controlId));
}

int MeasureTextWidthForControl(HWND hwnd, int controlId, std::wstring_view text) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return 0;
    }
    HDC dc = GetDC(hwnd);
    if (dc == nullptr) {
        return DialogControlWidth(hwnd, controlId);
    }
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
    HFONT previous = font != nullptr ? reinterpret_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    SIZE size{};
    const std::wstring measuredText = text.empty() ? std::wstring(L" ") : std::wstring(text);
    GetTextExtentPoint32W(dc, measuredText.c_str(), static_cast<int>(measuredText.size()), &size);
    if (previous != nullptr) {
        SelectObject(dc, previous);
    }
    ReleaseDC(hwnd, dc);
    return size.cx;
}

int MeasureTextHeightForControl(HWND hwnd, int controlId, std::wstring_view text, int width, bool singleLine) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return 0;
    }
    HDC dc = GetDC(hwnd);
    if (dc == nullptr) {
        return DialogControlHeight(hwnd, controlId);
    }
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
    HFONT previous = font != nullptr ? reinterpret_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    RECT rect{0, 0, std::max(1, width), 0};
    std::wstring measuredText = text.empty() ? std::wstring(L" ") : std::wstring(text);
    UINT flags = DT_NOPREFIX | DT_CALCRECT | (singleLine ? DT_SINGLELINE : DT_WORDBREAK);
    DrawTextW(dc, measuredText.c_str(), static_cast<int>(measuredText.size()), &rect, flags);
    if (previous != nullptr) {
        SelectObject(dc, previous);
    }
    ReleaseDC(hwnd, dc);
    const int measuredHeight = static_cast<int>(rect.bottom - rect.top);
    return std::max(1, measuredHeight);
}

int DialogUnitsToPixelsY(HWND hwnd, int dialogUnitsY) {
    RECT rect{0, 0, 0, dialogUnitsY};
    MapDialogRect(hwnd, &rect);
    return rect.bottom - rect.top;
}

void ConfigureDialogFonts(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr) {
        return;
    }
    DestroyDialogFont(state->titleFont);
    DestroyDialogFont(state->fontSampleFont);
    state->titleFont = CreateDerivedDialogFont(hwnd, IDC_LAYOUT_EDIT_TITLE, FW_BOLD, 2);
    state->fontSampleFont = CreateDerivedDialogFont(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, FW_NORMAL);
    if (state->titleFont != nullptr) {
        SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);
    }
}

void DestroyDialogFonts(LayoutEditDialogState* state) {
    if (state == nullptr) {
        return;
    }
    DestroyDialogFont(state->titleFont);
    DestroyDialogFont(state->fontSampleFont);
}

void SetLayoutEditStatus(
    LayoutEditDialogState* state, HWND hwnd, LayoutEditStatusKind kind, const std::wstring& text) {
    if (state == nullptr) {
        return;
    }
    state->statusIsError = kind == LayoutEditStatusKind::Error;
    state->statusText = text;
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_STATUS_TEXT, text.c_str());
}

void SetColorSamplePreview(LayoutEditDialogState* state, HWND hwnd, unsigned int color) {
    if (state == nullptr) {
        return;
    }
    state->previewColor = RGB((color >> 16) & 0xFFu, (color >> 8) & 0xFFu, color & 0xFFu);
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE, L"Sample text in the selected color");
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_SWATCH), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE), nullptr, TRUE);
}

void SetFontSamplePreview(LayoutEditDialogState* state,
    HWND hwnd,
    std::optional<LayoutEditParameter> parameter,
    const UiFontConfig* font) {
    if (state == nullptr) {
        return;
    }
    DestroyDialogFont(state->fontSampleFont);
    state->fontSampleFont = nullptr;
    const std::wstring sampleText =
        font != nullptr && parameter.has_value() ? BuildFontSampleText(*parameter) : std::wstring();
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, sampleText.c_str());
    if (font == nullptr || !parameter.has_value()) {
        return;
    }

    state->fontSampleFont = CreatePreviewFontToFit(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, *font, sampleText);
    if (state->fontSampleFont != nullptr) {
        SendDlgItemMessageW(
            hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, WM_SETFONT, reinterpret_cast<WPARAM>(state->fontSampleFont), TRUE);
    }
}

void ShowLayoutEditEditors(
    HWND hwnd, bool showNumeric, bool showFont, bool showColor, bool showWeights, bool showMetric, bool showBinding) {
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, showNumeric);
    ShowDialogControl(
        hwnd, IDC_LAYOUT_EDIT_SUMMARY, !(showNumeric || showFont || showColor || showWeights || showMetric));

    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_LABEL, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_LABEL, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, showFont);

    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_LABEL, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_SWATCH, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_LABEL, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_SLIDER, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_GREEN_SLIDER, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_BLUE_SLIDER, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_PICK, showColor);

    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_LABEL, showWeights);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, showWeights);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL, showWeights);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT, showWeights);

    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_METRIC_STYLE_LABEL, showMetric);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE, showMetric);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_LABEL, showMetric);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT, showMetric);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_METRIC_UNIT_LABEL, showMetric);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT, showMetric);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_METRIC_LABEL_LABEL, showMetric);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT, showMetric);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_METRIC_BINDING_LABEL, showMetric && showBinding);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT, showMetric && showBinding);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_HINT, showNumeric || showFont || showColor || showWeights || showMetric);
}

void LayoutLayoutEditRightPane(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr) {
        return;
    }

    const LayoutEditRightPaneMetrics& metrics = kLayoutEditRightPaneMetrics;
    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    const auto dividerRect = DialogControlRect(hwnd, IDC_LAYOUT_EDIT_DIVIDER);
    const auto treeRect = DialogControlRect(hwnd, IDC_LAYOUT_EDIT_TREE);
    const auto filterEditRect = DialogControlRect(hwnd, IDC_LAYOUT_EDIT_FILTER_EDIT);
    if (!dividerRect.has_value() || !treeRect.has_value() || !filterEditRect.has_value()) {
        return;
    }

    const int outerEdgeMargin = static_cast<int>(treeRect->left);
    const int dividerGap = std::max(0, static_cast<int>(dividerRect->left - treeRect->right));
    const int leftPaneBottom = clientRect.bottom - outerEdgeMargin;
    SetDialogControlBounds(hwnd,
        IDC_LAYOUT_EDIT_TREE,
        static_cast<int>(treeRect->left),
        static_cast<int>(treeRect->top),
        static_cast<int>(treeRect->right - treeRect->left),
        std::max(1, leftPaneBottom - static_cast<int>(treeRect->top)));
    SetDialogControlBounds(hwnd,
        IDC_LAYOUT_EDIT_DIVIDER,
        static_cast<int>(dividerRect->left),
        static_cast<int>(dividerRect->top),
        static_cast<int>(dividerRect->right - dividerRect->left),
        std::max(1, leftPaneBottom - static_cast<int>(dividerRect->top)));
    const int paneLeft = static_cast<int>(dividerRect->right) + dividerGap;
    const int paneRight = clientRect.right - outerEdgeMargin;
    const int paneWidth = std::max(1, paneRight - paneLeft);

    const std::wstring footerText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_FOOTER_HINT);
    const int footerHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_FOOTER_HINT, footerText, paneWidth);
    const int footerBottom = leftPaneBottom;
    const int footerTop = footerBottom - footerHeight;
    SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_FOOTER_HINT, paneLeft, footerTop, paneWidth, footerHeight);

    const int revertWidth = DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_REVERT);
    const int revertHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_REVERT);
    const int statusWidth = std::max(1, paneWidth - revertWidth - metrics.inlineGap);
    const int statusHeight =
        MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_STATUS_TEXT, state->statusText, statusWidth);
    const int statusRowHeight = std::max(statusHeight, revertHeight);
    const int statusTop = footerTop - metrics.statusToFooterGap - statusRowHeight;
    SetDialogControlBounds(hwnd,
        IDC_LAYOUT_EDIT_STATUS_TEXT,
        paneLeft,
        statusTop + ((statusRowHeight - statusHeight) / 2),
        statusWidth,
        statusHeight);
    SetDialogControlBounds(hwnd,
        IDC_LAYOUT_EDIT_REVERT,
        paneRight - revertWidth,
        statusTop + ((statusRowHeight - revertHeight) / 2),
        revertWidth,
        revertHeight);

    int y = std::min(dividerRect->top, filterEditRect->top);
    const std::wstring titleText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_TITLE);
    const int titleHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_TITLE, titleText, paneWidth, true);
    SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_TITLE, paneLeft, y, paneWidth, titleHeight);
    y += titleHeight + metrics.headerGap;

    const std::wstring locationText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_LOCATION);
    const int locationHeight = MeasureTextHeightForControl(
        hwnd, IDC_LAYOUT_EDIT_LOCATION, locationText.empty() ? L" " : locationText, paneWidth, true);
    SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_LOCATION, paneLeft, y, paneWidth, locationHeight);
    y += locationHeight + metrics.headerGap;

    const std::wstring descriptionText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION);
    const int descriptionSingleLineHeight =
        MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, L"Ag", paneWidth, true);
    const int descriptionHeight =
        std::max(MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, descriptionText, paneWidth),
            descriptionSingleLineHeight * 2);
    SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, paneLeft, y, paneWidth, descriptionHeight);
    y += descriptionHeight + metrics.headerToGroupGap;

    const int groupTop = y;
    const int maxGroupHeight = std::max(60, (statusTop - metrics.groupToStatusGap) - groupTop);

    const int groupTopBorderInset = DialogUnitsToPixelsY(hwnd, 4);
    const int innerLeft = paneLeft + metrics.groupPadding;
    const int innerRight = paneRight - metrics.groupPadding;
    const int innerWidth = std::max(1, innerRight - innerLeft);
    const int innerTop = groupTop + metrics.groupPadding + groupTopBorderInset;

    const LayoutEditEditorKind kind = CurrentLayoutEditEditorKind(state);
    const bool showBinding = CurrentLayoutEditShowsMetricBinding(state);
    const int singleLineFieldHeight = MeasureSingleLineFieldVisibleHeight(hwnd);
    const int labelColumnWidth = ActiveEditorLabelControls(kind, showBinding).empty()
                                     ? 0
                                     : MeasureLabelColumnWidth(hwnd, ActiveEditorLabelControls(kind, showBinding)) + 8;

    int cursorY = innerTop;
    int contentBottom = innerTop;
    switch (kind) {
        case LayoutEditEditorKind::Summary: {
            const std::wstring summaryText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_SUMMARY);
            const int summaryHeight =
                MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_SUMMARY, summaryText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_SUMMARY, innerLeft, cursorY, innerWidth, summaryHeight);
            contentBottom = cursorY + summaryHeight;
            break;
        }
        case LayoutEditEditorKind::Numeric: {
            const int editHeight =
                DialogControlLayoutHeightForVisibleHeight(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, singleLineFieldHeight);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, innerLeft, cursorY, innerWidth, editHeight);
            cursorY += singleLineFieldHeight + metrics.hintGap;
            const std::wstring hintText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_HINT);
            const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, innerLeft, cursorY, innerWidth, hintHeight);
            contentBottom = cursorY + hintHeight;
            break;
        }
        case LayoutEditEditorKind::Font: {
            const int fontFaceRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_FONT_FACE_LABEL,
                IDC_LAYOUT_EDIT_FONT_FACE_EDIT,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                innerWidth - labelColumnWidth - metrics.labelGap,
                singleLineFieldHeight);
            cursorY += fontFaceRowHeight + metrics.rowGap;

            const int labelHeight = MeasureTextHeightForControl(hwnd,
                IDC_LAYOUT_EDIT_FONT_SIZE_LABEL,
                ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_LABEL),
                labelColumnWidth,
                true);
            const int sizeEditWidth =
                std::max(56, (innerWidth - labelColumnWidth - metrics.labelGap - metrics.inlineGap) / 3);
            const int sizeEditHeight =
                DialogControlLayoutHeightForVisibleHeight(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, singleLineFieldHeight);
            const int sizeRowHeight = std::max(singleLineFieldHeight, labelHeight);
            SetDialogControlBounds(hwnd,
                IDC_LAYOUT_EDIT_FONT_SIZE_LABEL,
                innerLeft,
                cursorY + ((sizeRowHeight - labelHeight) / 2),
                labelColumnWidth,
                labelHeight);
            const int sizeControlLeft = innerLeft + labelColumnWidth + metrics.labelGap;
            SetDialogControlBounds(hwnd,
                IDC_LAYOUT_EDIT_FONT_SIZE_EDIT,
                sizeControlLeft,
                cursorY + ((sizeRowHeight - singleLineFieldHeight) / 2),
                sizeEditWidth,
                sizeEditHeight);

            const int weightLabelWidth = MeasureTextWidthForControl(hwnd,
                                             IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL,
                                             ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL)) +
                                         8;
            const int weightEditHeight = DialogControlLayoutHeightForVisibleHeight(
                hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, singleLineFieldHeight);
            const int weightEditLeft = sizeControlLeft + sizeEditWidth + metrics.inlineGap + weightLabelWidth;
            const int weightEditWidth = std::max(72, innerRight - weightEditLeft);
            SetDialogControlBounds(hwnd,
                IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL,
                sizeControlLeft + sizeEditWidth + metrics.inlineGap,
                cursorY + ((sizeRowHeight - labelHeight) / 2),
                weightLabelWidth,
                labelHeight);
            SetDialogControlBounds(hwnd,
                IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT,
                weightEditLeft,
                cursorY + ((sizeRowHeight - singleLineFieldHeight) / 2),
                weightEditWidth,
                weightEditHeight);
            cursorY += sizeRowHeight + metrics.sampleGap;

            const int sampleHeight = std::max(28, DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE));
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, innerLeft, cursorY, innerWidth, sampleHeight);
            cursorY += sampleHeight + metrics.hintGap;

            const std::wstring hintText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_HINT);
            const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, innerLeft, cursorY, innerWidth, hintHeight);
            contentBottom = cursorY + hintHeight;
            break;
        }
        case LayoutEditEditorKind::Color: {
            const int swatchSize = std::max(DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_SWATCH),
                DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT) + 4);
            const int pickWidth = DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_COLOR_PICK);
            const int pickHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_PICK);
            const int hexLabelWidth = MeasureTextWidthForControl(hwnd,
                                          IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
                                          ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_LABEL)) +
                                      8;
            const int hexEditHeight =
                DialogControlLayoutHeightForVisibleHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, singleLineFieldHeight);
            const int pickLeft = innerRight - pickWidth;
            const int hexLabelLeft = innerLeft + swatchSize + metrics.inlineGap;
            const int hexEditLeft = hexLabelLeft + hexLabelWidth + metrics.labelGap;
            const int hexEditWidth = std::max(60, pickLeft - metrics.inlineGap - hexEditLeft);
            const int firstRowHeight = std::max(std::max(swatchSize, singleLineFieldHeight), pickHeight);
            const int hexLabelHeight = MeasureTextHeightForControl(hwnd,
                IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
                ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_LABEL),
                hexLabelWidth,
                true);
            SetDialogControlBounds(hwnd,
                IDC_LAYOUT_EDIT_COLOR_SWATCH,
                innerLeft,
                cursorY + ((firstRowHeight - swatchSize) / 2),
                swatchSize,
                swatchSize);
            SetDialogControlBounds(hwnd,
                IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
                hexLabelLeft,
                cursorY + ((firstRowHeight - hexLabelHeight) / 2),
                hexLabelWidth,
                hexLabelHeight);
            SetDialogControlBounds(hwnd,
                IDC_LAYOUT_EDIT_COLOR_HEX_EDIT,
                hexEditLeft,
                cursorY + ((firstRowHeight - singleLineFieldHeight) / 2),
                hexEditWidth,
                hexEditHeight);
            SetDialogControlBounds(hwnd,
                IDC_LAYOUT_EDIT_COLOR_PICK,
                pickLeft,
                cursorY + ((firstRowHeight - pickHeight) / 2),
                pickWidth,
                pickHeight);
            cursorY += firstRowHeight + metrics.sampleGap;

            const int sampleHeight = MeasureTextHeightForControl(hwnd,
                IDC_LAYOUT_EDIT_COLOR_SAMPLE,
                ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE),
                innerWidth,
                true);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE, innerLeft, cursorY, innerWidth, sampleHeight);
            cursorY += sampleHeight + metrics.sampleGap;

            const int valueEditWidth = DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT);
            const int sliderLeft = innerLeft + labelColumnWidth + metrics.labelGap + valueEditWidth + metrics.inlineGap;
            const int sliderWidth = std::max(40, innerRight - sliderLeft);
            const int rgbLabelIds[] = {
                IDC_LAYOUT_EDIT_COLOR_RED_LABEL,
                IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL,
                IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL,
            };
            const int rgbEditIds[] = {
                IDC_LAYOUT_EDIT_COLOR_RED_EDIT,
                IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT,
                IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT,
            };
            const int rgbSliderIds[] = {
                IDC_LAYOUT_EDIT_COLOR_RED_SLIDER,
                IDC_LAYOUT_EDIT_COLOR_GREEN_SLIDER,
                IDC_LAYOUT_EDIT_COLOR_BLUE_SLIDER,
            };
            for (int i = 0; i < 3; ++i) {
                const int editHeight =
                    DialogControlLayoutHeightForVisibleHeight(hwnd, rgbEditIds[i], singleLineFieldHeight);
                const int sliderHeight = DialogControlHeight(hwnd, rgbSliderIds[i]);
                const int rowHeight = std::max(singleLineFieldHeight, sliderHeight);
                const int labelHeight = MeasureTextHeightForControl(
                    hwnd, rgbLabelIds[i], ReadDialogControlTextWide(hwnd, rgbLabelIds[i]), labelColumnWidth, true);
                SetDialogControlBounds(hwnd,
                    rgbLabelIds[i],
                    innerLeft,
                    cursorY + ((rowHeight - labelHeight) / 2),
                    labelColumnWidth,
                    labelHeight);
                SetDialogControlBounds(hwnd,
                    rgbEditIds[i],
                    innerLeft + labelColumnWidth + metrics.labelGap,
                    cursorY + ((rowHeight - singleLineFieldHeight) / 2),
                    valueEditWidth,
                    editHeight);
                SetDialogControlBounds(hwnd,
                    rgbSliderIds[i],
                    sliderLeft,
                    cursorY + ((rowHeight - sliderHeight) / 2),
                    sliderWidth,
                    sliderHeight);
                cursorY += rowHeight + metrics.rowGap;
            }

            const std::wstring hintText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_HINT);
            const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, innerLeft, cursorY, innerWidth, hintHeight);
            contentBottom = cursorY + hintHeight;
            break;
        }
        case LayoutEditEditorKind::Weights: {
            const int editWidth = std::min(88, std::max(60, innerWidth - labelColumnWidth - metrics.labelGap));
            const int firstRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_WEIGHT_FIRST_LABEL,
                IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                editWidth,
                singleLineFieldHeight);
            cursorY += firstRowHeight + metrics.rowGap;
            const int secondRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL,
                IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                editWidth,
                singleLineFieldHeight);
            cursorY += secondRowHeight + metrics.hintGap;
            const std::wstring hintText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_HINT);
            const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, innerLeft, cursorY, innerWidth, hintHeight);
            contentBottom = cursorY + hintHeight;
            break;
        }
        case LayoutEditEditorKind::Metric: {
            const int controlWidth = innerWidth - labelColumnWidth - metrics.labelGap;
            const int metricRowHeight =
                std::max(DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE), singleLineFieldHeight);
            const int styleRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_METRIC_STYLE_LABEL,
                IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                controlWidth,
                metricRowHeight);
            cursorY += styleRowHeight + metrics.rowGap;
            const int scaleRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_METRIC_SCALE_LABEL,
                IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                controlWidth,
                metricRowHeight);
            cursorY += scaleRowHeight + metrics.rowGap;
            const int unitRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_METRIC_UNIT_LABEL,
                IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                controlWidth,
                metricRowHeight);
            cursorY += unitRowHeight + metrics.rowGap;
            const int labelRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_METRIC_LABEL_LABEL,
                IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                controlWidth,
                metricRowHeight);
            cursorY += labelRowHeight + metrics.rowGap;
            if (showBinding) {
                const int bindingRowHeight = LayoutLabeledControlRow(hwnd,
                    IDC_LAYOUT_EDIT_METRIC_BINDING_LABEL,
                    IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT,
                    innerLeft,
                    cursorY,
                    labelColumnWidth,
                    metrics.labelGap,
                    controlWidth,
                    metricRowHeight);
                cursorY += bindingRowHeight + metrics.hintGap;
            } else {
                cursorY += metrics.hintGap;
            }
            const std::wstring hintText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_HINT);
            const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, innerLeft, cursorY, innerWidth, hintHeight);
            contentBottom = cursorY + hintHeight;
            break;
        }
    }

    const int desiredGroupHeight = std::max(60, (contentBottom - groupTop) + metrics.groupPadding);
    const int groupHeight = std::min(maxGroupHeight, desiredGroupHeight);
    SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_EDITOR_GROUP, paneLeft, groupTop, paneWidth, groupHeight);

    if (kind == LayoutEditEditorKind::Font) {
        if (const auto* parameter = state->selectedLeaf != nullptr
                                        ? std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey)
                                        : nullptr;
            parameter != nullptr && state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec) {
            const auto font = FindLayoutEditTooltipFontValue(state->dialog->Host().CurrentConfig(), *parameter);
            SetFontSamplePreview(state,
                hwnd,
                std::optional<LayoutEditParameter>(*parameter),
                font.has_value() && *font != nullptr ? *font : nullptr);
        }
    }
}

void UpdateLayoutEditActionState(LayoutEditDialogState* state, HWND hwnd) {
    const bool canRevert = state != nullptr && state->selectedLeaf != nullptr;
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_REVERT), canRevert ? TRUE : FALSE);
}

void SetLayoutEditDescription(HWND hwnd, const LayoutEditTreeNode* node) {
    if (node == nullptr) {
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_TITLE, L"No matching setting");
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_LOCATION, L"");
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, L"Try a different filter or clear it to see the full tree.");
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_SUMMARY, L"");
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_HINT, L"Select a field to edit it here.");
        return;
    }

    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_TITLE, BuildLayoutEditNodeTitle(node).c_str());
    const std::wstring location = WideFromUtf8(node->locationText);
    const std::wstring description = WideFromUtf8(FindLocalizedText(node->descriptionKey));
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_LOCATION, location.c_str());
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, description.c_str());
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_SUMMARY, BuildLayoutEditSummaryText(node).c_str());
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_HINT, BuildLayoutEditHintText(node).c_str());
}
