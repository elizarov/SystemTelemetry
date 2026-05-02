#include "layout_edit_dialog/impl/pane.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>

#include "config/color_math.h"
#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_edit_dialog/impl/editors.h"
#include "layout_edit_dialog/impl/util.h"
#include "layout_edit_dialog/theme_preview.h"
#include "resource.h"
#include "util/localization_catalog.h"
#include "util/utf8.h"

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
constexpr wchar_t kDialogRedrawSuspendCountProperty[] = L"SystemTelemetry.LayoutEdit.RedrawSuspendCount";
constexpr double kLchGradientChromaMax = 0.4;

int WindowRedrawSuspendCount(HWND hwnd) {
    return static_cast<int>(reinterpret_cast<intptr_t>(GetPropW(hwnd, kDialogRedrawSuspendCountProperty)));
}

void BeginWindowRedrawSuspension(HWND hwnd) {
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return;
    }

    const int count = WindowRedrawSuspendCount(hwnd);
    if (count == 0) {
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    }
    SetPropW(hwnd, kDialogRedrawSuspendCountProperty, reinterpret_cast<HANDLE>(static_cast<intptr_t>(count + 1)));
}

void EndWindowRedrawSuspension(HWND hwnd, const RECT* redrawRect, UINT redrawFlags) {
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return;
    }

    const int count = WindowRedrawSuspendCount(hwnd);
    if (count <= 1) {
        RemovePropW(hwnd, kDialogRedrawSuspendCountProperty);
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        if (redrawFlags != 0) {
            RedrawWindow(hwnd, redrawRect, nullptr, redrawFlags);
        }
        return;
    }
    SetPropW(hwnd, kDialogRedrawSuspendCountProperty, reinterpret_cast<HANDLE>(static_cast<intptr_t>(count - 1)));
}

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

int DialogComboBoxSelectionHeight(HWND hwnd, int controlId) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return 0;
    }

    const LRESULT selectionHeight = SendMessageW(control, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
    if (selectionHeight != CB_ERR && selectionHeight > 0) {
        return static_cast<int>(selectionHeight);
    }

    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(control, &info) != FALSE) {
        const int itemHeight = (std::max)(0, static_cast<int>(info.rcItem.bottom - info.rcItem.top));
        if (itemHeight > 0) {
            return itemHeight;
        }
    }

    RECT rect{};
    if (GetClientRect(control, &rect) != FALSE) {
        return (std::max)(1, static_cast<int>(rect.bottom - rect.top));
    }
    return 0;
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

HWND CreateMetricListEditorControl(
    HWND hwnd, const wchar_t* className, const wchar_t* text, DWORD style, int controlId, DWORD exStyle = 0) {
    HWND control = CreateWindowExW(exStyle,
        className,
        text,
        WS_CHILD | WS_TABSTOP | style,
        0,
        0,
        1,
        1,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
        nullptr);
    if (control != nullptr) {
        if (HWND templateControl = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT); templateControl != nullptr) {
            SendMessageW(control, WM_SETFONT, SendMessageW(templateControl, WM_GETFONT, 0, 0), TRUE);
        }
    }
    return control;
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
        case LayoutEditEditorKind::GlobalFontFamily:
            return {IDC_LAYOUT_EDIT_FONT_FACE_LABEL};
        case LayoutEditEditorKind::Color:
            return {IDC_LAYOUT_EDIT_COLOR_MODE_LABEL,
                IDC_LAYOUT_EDIT_COLOR_BASE_LABEL,
                IDC_LAYOUT_EDIT_COLOR_RED_LABEL,
                IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL,
                IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL,
                IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL,
                IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_LABEL,
                IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_LABEL,
                IDC_LAYOUT_EDIT_COLOR_LCH_HUE_LABEL};
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
        case LayoutEditEditorKind::DateTimeFormat:
            return {IDC_LAYOUT_EDIT_DATETIME_FORMAT_LABEL};
        case LayoutEditEditorKind::LayoutSelector:
        case LayoutEditEditorKind::ThemeSelector:
            return {IDC_LAYOUT_EDIT_THEME_LABEL};
        case LayoutEditEditorKind::MetricListOrder:
        case LayoutEditEditorKind::Numeric:
        case LayoutEditEditorKind::Summary:
            return {};
    }
    return {};
}

bool ColorSelectionSupportsDerived(const LayoutEditDialogState* state) {
    return state != nullptr && state->selectedLeaf != nullptr &&
           std::holds_alternative<LayoutEditParameter>(state->selectedLeaf->focusKey) &&
           state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex;
}

bool ColorEditorDerivedMode(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_MODE_COMBO);
    if (combo == nullptr) {
        return false;
    }
    return SendMessageW(combo, CB_GETCURSEL, 0, 0) == 1;
}

bool ColorEditorLchView(HWND hwnd) {
    HWND tab = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_VIEW_TAB);
    return tab != nullptr && TabCtrl_GetCurSel(tab) == 1;
}

void ShowColorEditorControls(HWND hwnd, bool showColor, bool supportsDerived, bool derivedMode) {
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_MODE_LABEL, showColor && supportsDerived);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_MODE_COMBO, showColor && supportsDerived);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_BASE_LABEL, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_BASE_COMBO, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_CHECK, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_CHECK, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_CHECK, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER, showColor && supportsDerived && derivedMode);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL, showColor && supportsDerived && derivedMode);

    const bool showLiteral = showColor && (!supportsDerived || !derivedMode);
    const bool showLch = showLiteral && ColorEditorLchView(hwnd);
    const bool showRgb = showLiteral && !showLch;
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_LABEL, showLiteral);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, showLiteral);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_VIEW_TAB, showLiteral);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_LABEL, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_SLIDER, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_GRADIENT, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_GREEN_SLIDER, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_GREEN_GRADIENT, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_BLUE_SLIDER, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_BLUE_GRADIENT, showRgb);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_LABEL, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_GRADIENT, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_LABEL, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_GRADIENT, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_LABEL, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_GRADIENT, showLch);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL, showLiteral);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT, showLiteral);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_SLIDER, showLiteral);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_PICK, showLiteral);

    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_SWATCH, showColor);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE, showColor);
}

int MeasureLabelColumnWidth(HWND hwnd, const std::vector<int>& labelIds) {
    int width = 0;
    for (const int labelId : labelIds) {
        width = std::max(width, MeasureTextWidthForControl(hwnd, labelId, ReadDialogControlTextWide(hwnd, labelId)));
    }
    return width;
}

void EnableStaticVerticalCentering(HWND hwnd, int labelId) {
    HWND label = GetDlgItem(hwnd, labelId);
    if (label == nullptr) {
        return;
    }
    const LONG_PTR style = GetWindowLongPtrW(label, GWL_STYLE);
    if ((style & SS_CENTERIMAGE) == 0) {
        SetWindowLongPtrW(label, GWL_STYLE, style | SS_CENTERIMAGE);
    }
}

int RowLabelVisualTopAdjustment(HWND hwnd) {
    return -DialogUnitsToPixelsY(hwnd, 2);
}

COLORREF RgbColor(int red, int green, int blue) {
    return RGB(std::clamp(red, 0, 255), std::clamp(green, 0, 255), std::clamp(blue, 0, 255));
}

COLORREF ColorRefFromBytes(ColorBytes color) {
    return RgbColor(static_cast<int>(std::lround(color.r)),
        static_cast<int>(std::lround(color.g)),
        static_cast<int>(std::lround(color.b)));
}

std::optional<OklchColor> ReadDialogLchForGradient(HWND hwnd) {
    const auto lightness =
        TryParseDialogDouble(ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT).c_str());
    const auto chroma =
        TryParseDialogDouble(ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT).c_str());
    const auto hue = TryParseDialogDouble(ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT).c_str());
    if (!lightness.has_value() || !chroma.has_value() || !hue.has_value()) {
        return std::nullopt;
    }
    return OklchColor{
        std::clamp(*lightness, 0.0, 1.0),
        std::max(0.0, *chroma),
        std::clamp(*hue, 0.0, 360.0),
    };
}

OklchColor CurrentLchForGradient(HWND hwnd) {
    if (const auto lch = ReadDialogLchForGradient(hwnd); lch.has_value()) {
        return *lch;
    }
    if (const auto color = ReadColorDialogValue(hwnd); color.has_value()) {
        return OklchFromColorBytes(ColorBytesFromRgba(*color));
    }
    return OklchColor{0.5, 0.0, 0.0};
}

COLORREF ColorGradientBarColor(HWND hwnd, int controlId, double position) {
    const double t = std::clamp(position, 0.0, 1.0);
    switch (controlId) {
        case IDC_LAYOUT_EDIT_COLOR_RED_GRADIENT:
            return RgbColor(static_cast<int>(std::lround(t * 255.0)), 0, 0);
        case IDC_LAYOUT_EDIT_COLOR_GREEN_GRADIENT:
            return RgbColor(0, static_cast<int>(std::lround(t * 255.0)), 0);
        case IDC_LAYOUT_EDIT_COLOR_BLUE_GRADIENT:
            return RgbColor(0, 0, static_cast<int>(std::lround(t * 255.0)));
        case IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_GRADIENT: {
            const int value = static_cast<int>(std::lround(t * 255.0));
            return RgbColor(value, value, value);
        }
        case IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_GRADIENT: {
            const OklchColor current = CurrentLchForGradient(hwnd);
            return ColorRefFromBytes(
                ColorBytesFromOklch(OklchColor{current.l, t * kLchGradientChromaMax, current.h}, 255.0));
        }
        case IDC_LAYOUT_EDIT_COLOR_LCH_HUE_GRADIENT:
            return ColorRefFromBytes(ColorBytesFromOklch(OklchColor{0.65, kLchGradientChromaMax, t * 360.0}, 255.0));
    }
    return GetSysColor(COLOR_3DFACE);
}

std::pair<int, int> SliderTrackHorizontalBounds(HWND hwnd, int sliderId, int fallbackLeft, int fallbackWidth) {
    HWND slider = GetDlgItem(hwnd, sliderId);
    if (slider == nullptr) {
        return {fallbackLeft, fallbackWidth};
    }

    RECT channelRect{};
    SendMessageW(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channelRect));
    const int channelWidth = static_cast<int>(channelRect.right - channelRect.left);
    if (channelWidth <= 0) {
        return {fallbackLeft, fallbackWidth};
    }
    return {fallbackLeft + static_cast<int>(channelRect.left), channelWidth};
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
    const int labelBoxHeight = std::max(desiredVisibleControlHeight, labelHeight);
    const int rowHeight = std::max(forcedRowHeight, std::max(controlHeight, labelBoxHeight));
    const int controlTop = top + ((rowHeight - controlHeight) / 2);
    SetDialogControlBounds(hwnd, controlId, controlLeft, controlTop, controlWidth, controlHeight);

    const int labelTop =
        controlTop + ((desiredVisibleControlHeight - labelBoxHeight) / 2) + RowLabelVisualTopAdjustment(hwnd);
    EnableStaticVerticalCentering(hwnd, labelId);
    SetDialogControlBounds(hwnd, labelId, left, labelTop, labelWidth, labelBoxHeight);
    return rowHeight;
}

void SetDialogRowLabelBounds(
    HWND hwnd, int controlId, int left, int top, int width, int height, int rowHeight, bool applyVisualAdjustment) {
    SetDialogControlBounds(hwnd,
        controlId,
        left,
        top + ((rowHeight - height) / 2) + (applyVisualAdjustment ? RowLabelVisualTopAdjustment(hwnd) : 0),
        width,
        height);
}

}  // namespace

DialogRedrawScope::DialogRedrawScope(HWND hwnd, UINT redrawFlags) : hwnd_(hwnd), redrawFlags_(redrawFlags) {
    BeginWindowRedrawSuspension(hwnd_);
}

DialogRedrawScope::DialogRedrawScope(HWND hwnd, const RECT& redrawRect, UINT redrawFlags)
    : hwnd_(hwnd), redrawRect_(redrawRect), redrawFlags_(redrawFlags | RDW_ERASE) {
    BeginWindowRedrawSuspension(hwnd_);
}

DialogRedrawScope::~DialogRedrawScope() {
    const RECT* rect = redrawRect_.has_value() ? &*redrawRect_ : nullptr;
    EndWindowRedrawSuspension(hwnd_, rect, redrawFlags_);
}

DialogRedrawScope::DialogRedrawScope(DialogRedrawScope&& other) noexcept
    : hwnd_(other.hwnd_), redrawRect_(other.redrawRect_), redrawFlags_(other.redrawFlags_) {
    other.hwnd_ = nullptr;
    other.redrawRect_.reset();
    other.redrawFlags_ = 0;
}

DialogRedrawScope& DialogRedrawScope::operator=(DialogRedrawScope&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    const RECT* rect = redrawRect_.has_value() ? &*redrawRect_ : nullptr;
    EndWindowRedrawSuspension(hwnd_, rect, redrawFlags_);
    hwnd_ = other.hwnd_;
    redrawRect_ = other.redrawRect_;
    redrawFlags_ = other.redrawFlags_;
    other.hwnd_ = nullptr;
    other.redrawRect_.reset();
    other.redrawFlags_ = 0;
    return *this;
}

DialogDescendantRedrawScope::DialogDescendantRedrawScope(HWND hwnd, UINT redrawFlags)
    : root_(hwnd), redrawFlags_(redrawFlags) {
    BeginWindowRedrawSuspension(root_);
}

DialogDescendantRedrawScope::~DialogDescendantRedrawScope() {
    EndWindowRedrawSuspension(root_, nullptr, 0);
    if (root_ != nullptr && redrawFlags_ != 0) {
        RedrawWindow(root_, nullptr, nullptr, redrawFlags_);
    }
}

void ShowDialogControl(HWND hwnd, int controlId, bool show) {
    if (HWND control = GetDlgItem(hwnd, controlId); control != nullptr) {
        ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    }
}

void BringDialogControlToTop(HWND hwnd, int controlId) {
    if (HWND control = GetDlgItem(hwnd, controlId); control != nullptr) {
        SetWindowPos(control, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
    if (IsDialogComboBoxControl(hwnd, controlId)) {
        const int comboHeight = DialogComboBoxSelectionHeight(hwnd, controlId);
        if (comboHeight > 0) {
            return comboHeight;
        }
    }
    const auto rect = DialogControlRect(hwnd, controlId);
    if (!rect.has_value()) {
        return 0;
    }
    return std::max(1, static_cast<int>(rect->bottom - rect->top));
}

int DialogControlLayoutHeightForVisibleHeight(HWND hwnd, int controlId, int desiredVisibleHeight) {
    const int currentHeight = DialogControlHeight(hwnd, controlId);
    if (IsDialogComboBoxControl(hwnd, controlId)) {
        return desiredVisibleHeight;
    }
    if (!UsesSingleLineFieldFrame(hwnd, controlId)) {
        return desiredVisibleHeight;
    }
    const int currentVisibleHeight = DialogControlVisibleHeight(hwnd, controlId);
    const int framePadding = std::max(0, currentHeight - currentVisibleHeight);
    return desiredVisibleHeight + framePadding;
}

int MeasureControlFontHeight(HWND hwnd, int controlId) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return 0;
    }
    HDC dc = GetDC(control);
    if (dc == nullptr) {
        return 0;
    }
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
    HFONT previous = font != nullptr ? reinterpret_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    TEXTMETRICW metrics{};
    const BOOL measured = GetTextMetricsW(dc, &metrics);
    if (previous != nullptr) {
        SelectObject(dc, previous);
    }
    ReleaseDC(control, dc);
    return measured == TRUE ? std::max(1, static_cast<int>(metrics.tmHeight)) : 0;
}

int MeasureSingleLineFieldVisibleHeight(HWND hwnd) {
    const int textHeight = MeasureControlFontHeight(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT);
    const int paddedTextHeight = textHeight > 0 ? textHeight + DialogUnitsToPixelsY(hwnd, 4) : 0;
    const int comboHeight = DialogComboBoxSelectionHeight(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT);
    const int measuredHeight = std::max(paddedTextHeight, comboHeight);
    if (measuredHeight > 0) {
        return measuredHeight;
    }
    const int editHeight = DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT);
    if (editHeight > 0) {
        return editHeight;
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

int DialogUnitsToPixelsX(HWND hwnd, int dialogUnitsX) {
    RECT rect{0, 0, dialogUnitsX, 0};
    MapDialogRect(hwnd, &rect);
    return rect.right - rect.left;
}

std::optional<RECT> LayoutEditRightPaneRect(HWND hwnd) {
    RECT clientRect{};
    if (hwnd == nullptr || !GetClientRect(hwnd, &clientRect)) {
        return std::nullopt;
    }

    const auto dividerRect = DialogControlRect(hwnd, IDC_LAYOUT_EDIT_DIVIDER);
    if (!dividerRect.has_value()) {
        return clientRect;
    }

    RECT paneRect = clientRect;
    paneRect.left = dividerRect->right;
    return paneRect;
}

void RefreshLayoutEditRightPane(HWND hwnd) {
    if (const auto paneRect = LayoutEditRightPaneRect(hwnd); paneRect.has_value()) {
        RedrawWindow(hwnd, &*paneRect, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
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

void SetLayoutEditStatus(LayoutEditDialogState* state, HWND hwnd, LayoutEditStatusKind kind, const std::wstring& text) {
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
    state->previewColor = RGB((color >> 24) & 0xFFu, (color >> 16) & 0xFFu, (color >> 8) & 0xFFu);
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE, L"Sample text in the selected color");
    const std::wstring derivedHexText = L"Hex: " + FormatDialogColorHex(color);
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL, derivedHexText.c_str());
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_SWATCH), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_GRADIENT), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_GREEN_GRADIENT), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_BLUE_GRADIENT), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_GRADIENT), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_GRADIENT), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_GRADIENT), nullptr, TRUE);
}

bool IsColorGradientBarControlId(int controlId) {
    return controlId == IDC_LAYOUT_EDIT_COLOR_RED_GRADIENT || controlId == IDC_LAYOUT_EDIT_COLOR_GREEN_GRADIENT ||
           controlId == IDC_LAYOUT_EDIT_COLOR_BLUE_GRADIENT ||
           controlId == IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_GRADIENT ||
           controlId == IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_GRADIENT ||
           controlId == IDC_LAYOUT_EDIT_COLOR_LCH_HUE_GRADIENT;
}

void DrawColorGradientBar(HWND hwnd, const DRAWITEMSTRUCT& drawItem) {
    if (drawItem.hDC == nullptr) {
        return;
    }

    const int width = std::max(1, static_cast<int>(drawItem.rcItem.right - drawItem.rcItem.left));
    RECT lineRect = drawItem.rcItem;
    for (int x = 0; x < width; ++x) {
        const double position = width <= 1 ? 0.0 : static_cast<double>(x) / static_cast<double>(width - 1);
        lineRect.left = drawItem.rcItem.left + x;
        lineRect.right = lineRect.left + 1;
        HBRUSH brush = CreateSolidBrush(ColorGradientBarColor(hwnd, static_cast<int>(drawItem.CtlID), position));
        FillRect(drawItem.hDC, &lineRect, brush);
        DeleteObject(brush);
    }

    FrameRect(drawItem.hDC, &drawItem.rcItem, GetSysColorBrush(COLOR_3DSHADOW));
}

void DrawThemePreview(LayoutEditDialogState* state, const DRAWITEMSTRUCT& drawItem) {
    if (state == nullptr || drawItem.hDC == nullptr) {
        return;
    }
    const ThemeConfig* theme = FindActiveThemeConfig(state->dialog->Host().CurrentConfig());
    if (theme == nullptr) {
        FillRect(drawItem.hDC, &drawItem.rcItem, GetSysColorBrush(COLOR_3DFACE));
        return;
    }
    DrawThemePreviewTriangle(drawItem.hDC, drawItem.rcItem, *theme);
}

void SetFontSamplePreview(
    LayoutEditDialogState* state, HWND hwnd, std::optional<LayoutEditParameter> parameter, const UiFontConfig* font) {
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

void ShowMetricListOrderEditorControls(LayoutEditDialogState* state, bool show) {
    if (state == nullptr) {
        return;
    }
    for (const auto& row : state->metricListRowControls) {
        if (row.combo != nullptr) {
            ShowWindow(row.combo, show ? SW_SHOW : SW_HIDE);
        }
        if (row.upButton != nullptr) {
            ShowWindow(row.upButton, show ? SW_SHOW : SW_HIDE);
        }
        if (row.downButton != nullptr) {
            ShowWindow(row.downButton, show ? SW_SHOW : SW_HIDE);
        }
        if (row.deleteButton != nullptr) {
            ShowWindow(row.deleteButton, show ? SW_SHOW : SW_HIDE);
        }
    }
    if (state->metricListAddRowButton != nullptr) {
        ShowWindow(state->metricListAddRowButton, show ? SW_SHOW : SW_HIDE);
    }
}

void ShowLayoutEditEditors(HWND hwnd,
    bool showNumeric,
    bool showFont,
    bool showColor,
    bool showWeights,
    bool showMetric,
    bool showBinding,
    bool showMetricListOrder,
    bool showGlobalFontFamily,
    bool showDateTimeFormat,
    bool showThemeSelector,
    bool showLayoutSelector) {
    const bool showSectionSelector = showThemeSelector || showLayoutSelector;
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, showNumeric);
    ShowDialogControl(hwnd,
        IDC_LAYOUT_EDIT_SUMMARY,
        !(showNumeric || showFont || showColor || showWeights || showMetric || showMetricListOrder ||
            showGlobalFontFamily || showDateTimeFormat || showSectionSelector));
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_THEME_LABEL, showSectionSelector);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO, showSectionSelector);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_THEME_PREVIEW, showThemeSelector);

    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_LABEL, showFont || showGlobalFontFamily);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT, showFont || showGlobalFontFamily);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_LABEL, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_SAMPLE, showFont);

    ShowColorEditorControls(hwnd, showColor, false, false);

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
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_DATETIME_FORMAT_LABEL, showDateTimeFormat);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO, showDateTimeFormat);
    ShowDialogControl(hwnd,
        IDC_LAYOUT_EDIT_HINT,
        showNumeric || showFont || showColor || showWeights || showMetric || showMetricListOrder ||
            showGlobalFontFamily || showDateTimeFormat || showSectionSelector);
}

void DestroyMetricListOrderEditorControls(LayoutEditDialogState* state) {
    if (state == nullptr) {
        return;
    }
    for (auto& row : state->metricListRowControls) {
        if (row.combo != nullptr) {
            DestroyWindow(row.combo);
        }
        if (row.upButton != nullptr) {
            DestroyWindow(row.upButton);
        }
        if (row.downButton != nullptr) {
            DestroyWindow(row.downButton);
        }
        if (row.deleteButton != nullptr) {
            DestroyWindow(row.deleteButton);
        }
    }
    state->metricListRowControls.clear();
    if (state->metricListAddRowButton != nullptr) {
        DestroyWindow(state->metricListAddRowButton);
        state->metricListAddRowButton = nullptr;
    }
}

void EnsureMetricListOrderEditorControls(LayoutEditDialogState* state, HWND hwnd, size_t rowCount) {
    if (state == nullptr || hwnd == nullptr) {
        return;
    }
    if (state->metricListRowControls.size() == rowCount) {
        if (state->metricListAddRowButton == nullptr) {
            state->metricListAddRowButton = CreateMetricListEditorControl(
                hwnd, WC_BUTTONW, L"Add row", BS_PUSHBUTTON, IDC_LAYOUT_EDIT_METRIC_LIST_ADD_ROW);
        }
        return;
    }

    DestroyMetricListOrderEditorControls(state);

    state->metricListRowControls.reserve(rowCount);
    for (size_t i = 0; i < rowCount; ++i) {
        LayoutEditMetricListRowControls row;
        row.comboId = IDC_LAYOUT_EDIT_METRIC_LIST_ROW_COMBO_BASE + static_cast<int>(i);
        row.upButtonId = IDC_LAYOUT_EDIT_METRIC_LIST_ROW_UP_BASE + static_cast<int>(i);
        row.downButtonId = IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DOWN_BASE + static_cast<int>(i);
        row.deleteButtonId = IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DELETE_BASE + static_cast<int>(i);
        row.combo = CreateMetricListEditorControl(hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, row.comboId);
        row.upButton =
            CreateMetricListEditorControl(hwnd, WC_BUTTONW, L"", BS_PUSHBUTTON | BS_OWNERDRAW, row.upButtonId);
        row.downButton =
            CreateMetricListEditorControl(hwnd, WC_BUTTONW, L"", BS_PUSHBUTTON | BS_OWNERDRAW, row.downButtonId);
        row.deleteButton =
            CreateMetricListEditorControl(hwnd, WC_BUTTONW, L"", BS_PUSHBUTTON | BS_OWNERDRAW, row.deleteButtonId);
        state->metricListRowControls.push_back(row);
    }

    state->metricListAddRowButton =
        CreateMetricListEditorControl(hwnd, WC_BUTTONW, L"Add row", BS_PUSHBUTTON, IDC_LAYOUT_EDIT_METRIC_LIST_ADD_ROW);
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

    const int revertWidth = std::max(DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_REVERT),
        MeasureTextWidthForControl(
            hwnd, IDC_LAYOUT_EDIT_REVERT, ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_REVERT)) +
            24);
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
    ShowMetricListOrderEditorControls(state, false);
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
            const int weightEditHeight = DialogControlLayoutHeightForVisibleHeight(
                hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, singleLineFieldHeight);
            const int sizeRowHeight = std::max({singleLineFieldHeight, labelHeight, sizeEditHeight, weightEditHeight});
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
                cursorY + ((sizeRowHeight - sizeEditHeight) / 2),
                sizeEditWidth,
                sizeEditHeight);

            const int weightLabelWidth = MeasureTextWidthForControl(hwnd,
                                             IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL,
                                             ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL)) +
                                         8;
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
                cursorY + ((sizeRowHeight - weightEditHeight) / 2),
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
        case LayoutEditEditorKind::GlobalFontFamily: {
            const int fontFaceRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_FONT_FACE_LABEL,
                IDC_LAYOUT_EDIT_FONT_FACE_EDIT,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                innerWidth - labelColumnWidth - metrics.labelGap,
                singleLineFieldHeight);
            cursorY += fontFaceRowHeight + metrics.hintGap;

            const std::wstring hintText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_HINT);
            const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, innerLeft, cursorY, innerWidth, hintHeight);
            contentBottom = cursorY + hintHeight;
            break;
        }
        case LayoutEditEditorKind::LayoutSelector: {
            const int comboWidth = innerWidth - labelColumnWidth - metrics.labelGap;
            const int comboHeight =
                std::max(DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO), singleLineFieldHeight);
            const int rowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_THEME_LABEL,
                IDC_LAYOUT_EDIT_THEME_COMBO,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                comboWidth,
                comboHeight);
            cursorY += rowHeight + metrics.hintGap;

            const std::wstring hintText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_HINT);
            const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, innerLeft, cursorY, innerWidth, hintHeight);
            contentBottom = cursorY + hintHeight;
            break;
        }
        case LayoutEditEditorKind::ThemeSelector: {
            const int comboWidth = innerWidth - labelColumnWidth - metrics.labelGap;
            const int comboHeight =
                std::max(DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO), singleLineFieldHeight);
            const int rowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_THEME_LABEL,
                IDC_LAYOUT_EDIT_THEME_COMBO,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                comboWidth,
                comboHeight);
            cursorY += rowHeight + metrics.sampleGap;

            const int previewHeight = std::max(
                DialogUnitsToPixelsY(hwnd, 86), std::min(maxGroupHeight - metrics.groupPadding, innerWidth * 2 / 3));
            SetDialogControlBounds(
                hwnd, IDC_LAYOUT_EDIT_THEME_PREVIEW, innerLeft, cursorY, innerWidth, std::max(1, previewHeight));
            cursorY += std::max(1, previewHeight) + metrics.hintGap;

            const std::wstring hintText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_HINT);
            const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, innerLeft, cursorY, innerWidth, hintHeight);
            contentBottom = cursorY + hintHeight;
            break;
        }
        case LayoutEditEditorKind::Color: {
            const bool supportsDerived = ColorSelectionSupportsDerived(state);
            const bool derivedMode = supportsDerived && ColorEditorDerivedMode(hwnd);
            ShowColorEditorControls(hwnd, true, supportsDerived, derivedMode);

            if (supportsDerived) {
                const int modeRowHeight = LayoutLabeledControlRow(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_MODE_LABEL,
                    IDC_LAYOUT_EDIT_COLOR_MODE_COMBO,
                    innerLeft,
                    cursorY,
                    labelColumnWidth,
                    metrics.labelGap,
                    innerWidth - labelColumnWidth - metrics.labelGap,
                    singleLineFieldHeight);
                cursorY += modeRowHeight + metrics.rowGap;
            }

            const int swatchSize = std::max(DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_SWATCH),
                DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT) + 4);
            if (!derivedMode) {
                const int pickWidth = DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_COLOR_PICK);
                const int pickHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_PICK);
                const int hexLabelWidth = MeasureTextWidthForControl(hwnd,
                                              IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
                                              ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_LABEL)) +
                                          8;
                const int hexEditHeight = DialogControlLayoutHeightForVisibleHeight(
                    hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, singleLineFieldHeight);
                const int pickLeft = innerRight - pickWidth;
                const int hexLabelLeft = innerLeft + swatchSize + metrics.inlineGap;
                const int hexEditLeft = hexLabelLeft + hexLabelWidth + metrics.labelGap;
                const int hexEditWidth = std::max(76, pickLeft - metrics.inlineGap - hexEditLeft);
                const int firstRowHeight = std::max({swatchSize, hexEditHeight, pickHeight});
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
                    cursorY + ((firstRowHeight - hexEditHeight) / 2),
                    hexEditWidth,
                    hexEditHeight);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_PICK,
                    pickLeft,
                    cursorY + ((firstRowHeight - pickHeight) / 2),
                    pickWidth,
                    pickHeight);
                cursorY += firstRowHeight + metrics.sampleGap;
            }

            const int sampleHeight = MeasureTextHeightForControl(hwnd,
                IDC_LAYOUT_EDIT_COLOR_SAMPLE,
                ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE),
                innerWidth,
                true);
            if (derivedMode) {
                const int derivedHexLeft = innerLeft + swatchSize + metrics.inlineGap;
                const int derivedHexWidth = std::max(1, innerRight - derivedHexLeft);
                const int derivedHexHeight = MeasureTextHeightForControl(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL,
                    ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL),
                    derivedHexWidth,
                    true);
                const int firstRowHeight = std::max(swatchSize, derivedHexHeight);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_SWATCH,
                    innerLeft,
                    cursorY + ((firstRowHeight - swatchSize) / 2),
                    swatchSize,
                    swatchSize);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_DERIVED_HEX_LABEL,
                    derivedHexLeft,
                    cursorY + ((firstRowHeight - derivedHexHeight) / 2),
                    derivedHexWidth,
                    derivedHexHeight);
                cursorY += firstRowHeight + metrics.sampleGap;

                SetDialogControlBounds(
                    hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE, innerLeft, cursorY, innerWidth, sampleHeight);
                cursorY += sampleHeight + metrics.sampleGap;
            } else {
                SetDialogControlBounds(
                    hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE, innerLeft, cursorY, innerWidth, sampleHeight);
                cursorY += sampleHeight + metrics.sampleGap;
            }

            if (derivedMode) {
                const int baseRowHeight = LayoutLabeledControlRow(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_BASE_LABEL,
                    IDC_LAYOUT_EDIT_COLOR_BASE_COMBO,
                    innerLeft,
                    cursorY,
                    labelColumnWidth,
                    metrics.labelGap,
                    innerWidth - labelColumnWidth - metrics.labelGap,
                    singleLineFieldHeight);
                cursorY += baseRowHeight + metrics.rowGap;

                const int checkboxWidth = std::max(labelColumnWidth + metrics.labelGap + 82,
                    MeasureTextWidthForControl(hwnd,
                        IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL,
                        ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL)) +
                        metrics.labelGap);
                const int valueEditWidth = std::min(72, std::max(58, innerWidth - checkboxWidth));
                const int valueLeft = innerLeft + checkboxWidth;
                const int sliderLeft = valueLeft + valueEditWidth + metrics.inlineGap;
                const int sliderWidth = std::max(40, innerRight - sliderLeft);
                const int derivedFieldHeight =
                    std::max({DialogControlLayoutHeightForVisibleHeight(
                                  hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT, singleLineFieldHeight),
                        DialogControlLayoutHeightForVisibleHeight(
                            hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO, singleLineFieldHeight),
                        DialogControlLayoutHeightForVisibleHeight(
                            hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT, singleLineFieldHeight),
                        DialogControlLayoutHeightForVisibleHeight(
                            hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT, singleLineFieldHeight)});
                const int derivedSliderHeight =
                    std::max({DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER),
                        DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER),
                        DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER)});
                const int rotateCheckHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_CHECK);
                const int rotateRowHeight = std::max({rotateCheckHeight, derivedFieldHeight, derivedSliderHeight});
                SetDialogRowLabelBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ROTATE_CHECK,
                    innerLeft,
                    cursorY,
                    checkboxWidth,
                    rotateCheckHeight,
                    rotateRowHeight,
                    true);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT,
                    valueLeft,
                    cursorY + ((rotateRowHeight - derivedFieldHeight) / 2),
                    valueEditWidth,
                    derivedFieldHeight);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER,
                    sliderLeft,
                    cursorY + ((rotateRowHeight - derivedSliderHeight) / 2),
                    sliderWidth,
                    derivedSliderHeight);
                cursorY += rotateRowHeight + metrics.rowGap;

                const int mixCheckHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_CHECK);
                const int mixAmountRowHeight = std::max({mixCheckHeight, derivedFieldHeight, derivedSliderHeight});
                SetDialogRowLabelBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_MIX_CHECK,
                    innerLeft,
                    cursorY,
                    checkboxWidth,
                    mixCheckHeight,
                    mixAmountRowHeight,
                    true);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT,
                    valueLeft,
                    cursorY + ((mixAmountRowHeight - derivedFieldHeight) / 2),
                    valueEditWidth,
                    derivedFieldHeight);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER,
                    sliderLeft,
                    cursorY + ((mixAmountRowHeight - derivedSliderHeight) / 2),
                    sliderWidth,
                    derivedSliderHeight);
                cursorY += mixAmountRowHeight + metrics.rowGap;

                const int mixTargetLabelHeight = MeasureTextHeightForControl(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL,
                    ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL),
                    checkboxWidth,
                    true);
                const int mixTargetRowHeight = std::max(mixTargetLabelHeight, derivedFieldHeight);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL,
                    innerLeft,
                    cursorY + ((mixTargetRowHeight - mixTargetLabelHeight) / 2),
                    checkboxWidth,
                    mixTargetLabelHeight);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO,
                    valueLeft,
                    cursorY + ((mixTargetRowHeight - derivedFieldHeight) / 2),
                    std::max(1, innerRight - valueLeft),
                    derivedFieldHeight);
                cursorY += mixTargetRowHeight + metrics.rowGap;

                const int alphaCheckHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_CHECK);
                const int alphaRowHeight = std::max({alphaCheckHeight, derivedFieldHeight, derivedSliderHeight});
                SetDialogRowLabelBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_CHECK,
                    innerLeft,
                    cursorY,
                    checkboxWidth,
                    alphaCheckHeight,
                    alphaRowHeight,
                    true);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT,
                    valueLeft,
                    cursorY + ((alphaRowHeight - derivedFieldHeight) / 2),
                    valueEditWidth,
                    derivedFieldHeight);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER,
                    sliderLeft,
                    cursorY + ((alphaRowHeight - derivedSliderHeight) / 2),
                    sliderWidth,
                    derivedSliderHeight);
                cursorY += alphaRowHeight + metrics.rowGap;
            } else {
                const int valueEditWidth = std::max(DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT),
                    DialogControlWidth(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT));
                const int sliderLeft =
                    innerLeft + labelColumnWidth + metrics.labelGap + valueEditWidth + metrics.inlineGap;
                const int sliderWidth = std::max(40, innerRight - sliderLeft);
                const bool lchView = state->colorEditViewMode == ColorEditViewMode::Lch;
                const int channelLabelIds[] = {
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_LABEL : IDC_LAYOUT_EDIT_COLOR_RED_LABEL,
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_LABEL : IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL,
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_HUE_LABEL : IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL};
                const int channelEditIds[] = {
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT : IDC_LAYOUT_EDIT_COLOR_RED_EDIT,
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT : IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT,
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT : IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT};
                const int channelSliderIds[] = {
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER : IDC_LAYOUT_EDIT_COLOR_RED_SLIDER,
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER : IDC_LAYOUT_EDIT_COLOR_GREEN_SLIDER,
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER : IDC_LAYOUT_EDIT_COLOR_BLUE_SLIDER};
                const int channelGradientIds[] = {
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_GRADIENT : IDC_LAYOUT_EDIT_COLOR_RED_GRADIENT,
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_GRADIENT : IDC_LAYOUT_EDIT_COLOR_GREEN_GRADIENT,
                    lchView ? IDC_LAYOUT_EDIT_COLOR_LCH_HUE_GRADIENT : IDC_LAYOUT_EDIT_COLOR_BLUE_GRADIENT};
                const int alphaEditHeight = DialogControlLayoutHeightForVisibleHeight(
                    hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT, singleLineFieldHeight);
                const int alphaSliderHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_SLIDER);
                const int gradientBarHeight = std::max(1,
                    MeasureTextHeightForControl(hwnd,
                        channelLabelIds[0],
                        ReadDialogControlTextWide(hwnd, channelLabelIds[0]),
                        labelColumnWidth,
                        true));
                const int gradientGap = DialogUnitsToPixelsY(hwnd, 2);
                int tabContentHeight = 0;
                int channelRowHeights[3] = {};
                int channelLabelHeights[3] = {};
                int channelEditHeights[3] = {};
                int channelSliderHeights[3] = {};
                for (int i = 0; i < 3; ++i) {
                    channelEditHeights[i] =
                        DialogControlLayoutHeightForVisibleHeight(hwnd, channelEditIds[i], singleLineFieldHeight);
                    channelSliderHeights[i] = DialogControlHeight(hwnd, channelSliderIds[i]);
                    channelLabelHeights[i] = MeasureTextHeightForControl(hwnd,
                        channelLabelIds[i],
                        ReadDialogControlTextWide(hwnd, channelLabelIds[i]),
                        labelColumnWidth,
                        true);
                    const int sliderColumnHeight = channelSliderHeights[i] + gradientGap + gradientBarHeight;
                    channelRowHeights[i] =
                        std::max({channelEditHeights[i], sliderColumnHeight, channelLabelHeights[i]});
                    tabContentHeight += channelRowHeights[i] + (i < 2 ? metrics.rowGap : 0);
                }
                const int tabHeaderHeight = DialogUnitsToPixelsY(hwnd, 16);
                const int tabInsetX = DialogUnitsToPixelsX(hwnd, 6);
                const int tabInsetBottom = DialogUnitsToPixelsY(hwnd, 6);
                const int tabHeight = tabHeaderHeight + tabContentHeight + tabInsetBottom;
                SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_COLOR_VIEW_TAB, innerLeft, cursorY, innerWidth, tabHeight);
                BringDialogControlToTop(hwnd, IDC_LAYOUT_EDIT_COLOR_VIEW_TAB);
                int tabCursorY = cursorY + tabHeaderHeight;
                for (int i = 0; i < 3; ++i) {
                    const int rowHeight = channelRowHeights[i];
                    const int rowLeft = innerLeft + tabInsetX;
                    const int rowLabelWidth = std::max(1, labelColumnWidth);
                    const int rowEditLeft = rowLeft + rowLabelWidth + metrics.labelGap;
                    const int rowSliderLeft = rowEditLeft + valueEditWidth + metrics.inlineGap;
                    const int rowSliderWidth = std::max(40, innerRight - tabInsetX - rowSliderLeft);
                    const int labelHeight = MeasureTextHeightForControl(hwnd,
                        channelLabelIds[i],
                        ReadDialogControlTextWide(hwnd, channelLabelIds[i]),
                        rowLabelWidth,
                        true);
                    SetDialogControlBounds(
                        hwnd, channelSliderIds[i], rowSliderLeft, tabCursorY, rowSliderWidth, channelSliderHeights[i]);
                    const auto [gradientLeft, gradientWidth] =
                        SliderTrackHorizontalBounds(hwnd, channelSliderIds[i], rowSliderLeft, rowSliderWidth);
                    SetDialogControlBounds(hwnd,
                        channelGradientIds[i],
                        gradientLeft,
                        tabCursorY + channelSliderHeights[i] + gradientGap,
                        gradientWidth,
                        gradientBarHeight);
                    SetDialogControlBounds(hwnd,
                        channelLabelIds[i],
                        rowLeft,
                        tabCursorY + ((channelSliderHeights[i] - labelHeight) / 2),
                        rowLabelWidth,
                        labelHeight);
                    SetDialogControlBounds(hwnd,
                        channelEditIds[i],
                        rowEditLeft,
                        tabCursorY + ((channelSliderHeights[i] - channelEditHeights[i]) / 2),
                        valueEditWidth,
                        channelEditHeights[i]);
                    BringDialogControlToTop(hwnd, channelLabelIds[i]);
                    BringDialogControlToTop(hwnd, channelEditIds[i]);
                    BringDialogControlToTop(hwnd, channelSliderIds[i]);
                    BringDialogControlToTop(hwnd, channelGradientIds[i]);
                    tabCursorY += rowHeight + metrics.rowGap;
                }
                cursorY += tabHeight + metrics.rowGap;

                const int alphaLabelHeight = MeasureTextHeightForControl(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL,
                    ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL),
                    labelColumnWidth,
                    true);
                const int alphaRowHeight = std::max({alphaEditHeight, alphaSliderHeight, alphaLabelHeight});
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_LABEL,
                    innerLeft,
                    cursorY + ((alphaRowHeight - alphaLabelHeight) / 2),
                    labelColumnWidth,
                    alphaLabelHeight);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT,
                    innerLeft + labelColumnWidth + metrics.labelGap,
                    cursorY + ((alphaRowHeight - alphaEditHeight) / 2),
                    valueEditWidth,
                    alphaEditHeight);
                SetDialogControlBounds(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_ALPHA_SLIDER,
                    sliderLeft,
                    cursorY + ((alphaRowHeight - alphaSliderHeight) / 2),
                    sliderWidth,
                    alphaSliderHeight);
                cursorY += alphaRowHeight + metrics.rowGap;
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
        case LayoutEditEditorKind::DateTimeFormat: {
            const int controlWidth = innerWidth - labelColumnWidth - metrics.labelGap;
            const int comboHeight = std::max(
                DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO), singleLineFieldHeight);
            const int rowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_DATETIME_FORMAT_LABEL,
                IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                controlWidth,
                comboHeight);
            cursorY += rowHeight + metrics.hintGap;
            const std::wstring hintText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_HINT);
            const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, innerLeft, cursorY, innerWidth, hintHeight);
            contentBottom = cursorY + hintHeight;
            break;
        }
        case LayoutEditEditorKind::MetricListOrder: {
            const int buttonWidth = 38;
            const int comboFieldVisibleHeight =
                (std::max)(1, DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT));
            const int addButtonWidth = (std::max)(132,
                MeasureTextWidthForControl(hwnd,
                    IDC_LAYOUT_EDIT_REVERT,
                    state->metricListAddRowButton != nullptr ? ReadWindowTextWide(state->metricListAddRowButton)
                                                             : L"Add row") +
                    28);
            const int rowVisibleHeight = comboFieldVisibleHeight;
            const int comboDropHeight = std::max(220, rowVisibleHeight + 180);
            const int buttonsWidth = (buttonWidth * 3) + (metrics.inlineGap * 2);
            const int comboWidth = std::max(60, innerWidth - buttonsWidth - metrics.inlineGap);

            for (auto& row : state->metricListRowControls) {
                if (row.combo != nullptr) {
                    SetWindowPos(row.combo, nullptr, innerLeft, cursorY, comboWidth, comboDropHeight, SWP_NOZORDER);
                }
                int buttonLeft = innerLeft + comboWidth + metrics.inlineGap;
                if (row.upButton != nullptr) {
                    SetWindowPos(
                        row.upButton, nullptr, buttonLeft, cursorY, buttonWidth, rowVisibleHeight, SWP_NOZORDER);
                }
                buttonLeft += buttonWidth + metrics.inlineGap;
                if (row.downButton != nullptr) {
                    SetWindowPos(
                        row.downButton, nullptr, buttonLeft, cursorY, buttonWidth, rowVisibleHeight, SWP_NOZORDER);
                }
                buttonLeft += buttonWidth + metrics.inlineGap;
                if (row.deleteButton != nullptr) {
                    SetWindowPos(
                        row.deleteButton, nullptr, buttonLeft, cursorY, buttonWidth, rowVisibleHeight, SWP_NOZORDER);
                }
                cursorY += rowVisibleHeight + metrics.rowGap;
            }

            const int addButtonHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_REVERT);
            const int addButtonLeft = innerRight - addButtonWidth;
            if (state->metricListAddRowButton != nullptr) {
                SetWindowPos(state->metricListAddRowButton,
                    nullptr,
                    addButtonLeft,
                    cursorY,
                    addButtonWidth,
                    addButtonHeight,
                    SWP_NOZORDER);
            }
            cursorY += addButtonHeight + metrics.hintGap;

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
    if (HWND group = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_EDITOR_GROUP); group != nullptr) {
        SetWindowPos(group, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    switch (kind) {
        case LayoutEditEditorKind::Font:
        case LayoutEditEditorKind::GlobalFontFamily:
            BringDialogControlToTop(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT);
            break;
        case LayoutEditEditorKind::Metric:
            if (showBinding) {
                BringDialogControlToTop(hwnd, IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT);
            }
            break;
        case LayoutEditEditorKind::DateTimeFormat:
            BringDialogControlToTop(hwnd, IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO);
            break;
        case LayoutEditEditorKind::LayoutSelector:
        case LayoutEditEditorKind::ThemeSelector:
            BringDialogControlToTop(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO);
            break;
        case LayoutEditEditorKind::MetricListOrder:
            ShowMetricListOrderEditorControls(state, true);
            for (const auto& row : state->metricListRowControls) {
                if (row.combo != nullptr) {
                    SetWindowPos(row.combo, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            break;
        default:
            break;
    }

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
    const bool isFontsSection = state != nullptr && state->selectedLeaf == nullptr && state->selectedNode != nullptr &&
                                state->selectedNode->kind == LayoutEditTreeNodeKind::Section &&
                                state->selectedNode->label == "fonts";
    const bool isThemeSection = state != nullptr && state->selectedLeaf == nullptr && state->selectedNode != nullptr &&
                                state->selectedNode->kind == LayoutEditTreeNodeKind::Section &&
                                state->selectedNode->label.rfind("theme.", 0) == 0;
    const bool isLayoutSection = state != nullptr && state->selectedLeaf == nullptr && state->selectedNode != nullptr &&
                                 state->selectedNode->kind == LayoutEditTreeNodeKind::Section &&
                                 state->selectedNode->label.rfind("layout.", 0) == 0;
    const bool canRevert =
        state != nullptr && (state->selectedLeaf != nullptr || isFontsSection || isThemeSection || isLayoutSection);
    SetDlgItemTextW(hwnd,
        IDC_LAYOUT_EDIT_REVERT,
        isFontsSection    ? L"Revert Font Changes"
        : isThemeSection  ? L"Revert Theme"
        : isLayoutSection ? L"Revert Layout"
                          : L"Revert Field");
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
