#include "layout_edit_dialog/impl/util.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <cwchar>
#include <cwctype>

#include "config/color_math.h"
#include "layout_edit/board_metric_binding.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_target_descriptor.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "resource.h"
#include "telemetry/metrics.h"
#include "util/utf8.h"

namespace {

constexpr double kScaleEpsilon = 0.0001;
constexpr double kLchChromaSliderScale = 1000.0;
constexpr double kLchChromaSliderMax = 0.4;
constexpr double kHsvUnitSliderScale = 1000.0;

int CALLBACK CollectFontFamilyProc(const LOGFONTW* logFont, const TEXTMETRICW*, DWORD, LPARAM lParam) {
    auto* families = reinterpret_cast<std::vector<std::wstring>*>(lParam);
    if (families == nullptr || logFont == nullptr || logFont->lfFaceName[0] == L'\0' ||
        logFont->lfFaceName[0] == L'@') {
        return 1;
    }
    families->push_back(logFont->lfFaceName);
    return 1;
}

bool CaseInsensitiveEqual(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool CaseInsensitiveLess(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
}

void SortUniqueFontFamilies(std::vector<std::wstring>& families) {
    // Size: keep the only wide-string sort local to this Win32 font boundary so util/strings stays UTF-8-only.
    for (size_t i = 1; i < families.size(); ++i) {
        std::wstring family = std::move(families[i]);
        size_t insert = i;
        while (insert > 0 && CaseInsensitiveLess(family, families[insert - 1])) {
            families[insert] = std::move(families[insert - 1]);
            --insert;
        }
        families[insert] = std::move(family);
    }

    size_t out = 0;
    for (auto& family : families) {
        if (out != 0 && CaseInsensitiveEqual(families[out - 1], family)) {
            continue;
        }
        families[out] = std::move(family);
        ++out;
    }
    families.resize(out);
}

const LayoutNodeConfig* FindWeightEditNode(const AppConfig& config, const LayoutWeightEditKey& key) {
    LayoutEditLayoutTarget target;
    target.editCardId = key.editCardId;
    target.nodePath = key.nodePath;
    return FindGuideNode(config, target);
}

const LayoutCardConfig* FindCardById(const AppConfig& config, std::string_view cardId) {
    const auto it = std::find_if(
        config.layout.cards.begin(), config.layout.cards.end(), [&](const auto& card) { return card.id == cardId; });
    return it != config.layout.cards.end() ? &(*it) : nullptr;
}

std::wstring ReadControlTextWide(HWND hwnd, int controlId) {
    wchar_t buffer[256] = {};
    GetDlgItemTextW(hwnd, controlId, buffer, ARRAYSIZE(buffer));
    return buffer;
}

double RoundToStep(double value, double step) {
    return std::round(value / step) * step;
}

void SetDialogControlRoundedDecimal(HWND hwnd, int controlId, double value, int decimalPlaces) {
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%.*f", decimalPlaces, value);
    size_t length = wcslen(buffer);
    while (length > 0 && buffer[length - 1] == L'0') {
        buffer[--length] = L'\0';
    }
    if (length > 0 && buffer[length - 1] == L'.') {
        buffer[--length] = L'\0';
    }
    SetDlgItemTextW(hwnd, controlId, length == 0 || wcscmp(buffer, L"-0") == 0 ? L"0" : buffer);
}

void SetDialogControlRoundedInteger(HWND hwnd, int controlId, double value) {
    SetDialogControlInteger(hwnd, controlId, static_cast<int>(std::lround(value)));
}

OklchColor DisplayRoundedLch(OklchColor lch) {
    lch.l = std::clamp(RoundToStep(lch.l, 0.001), 0.0, 1.0);
    lch.c = std::max(0.0, RoundToStep(lch.c, 0.001));
    lch.h = lch.c == 0.0 ? 0.0 : std::clamp(std::round(lch.h), 0.0, 360.0);
    return lch;
}

HsvColor DisplayRoundedHsv(HsvColor hsv) {
    hsv.h = std::clamp(std::round(hsv.h), 0.0, 360.0);
    hsv.s = std::clamp(RoundToStep(hsv.s, 0.001), 0.0, 1.0);
    hsv.v = std::clamp(RoundToStep(hsv.v, 0.001), 0.0, 1.0);
    return hsv;
}

void SetSliderRange(HWND hwnd, int sliderId, int minValue, int maxValue, int pageSize, int lineSize) {
    SendDlgItemMessageW(hwnd, sliderId, TBM_SETRANGEMIN, TRUE, minValue);
    SendDlgItemMessageW(hwnd, sliderId, TBM_SETRANGEMAX, TRUE, maxValue);
    SendDlgItemMessageW(hwnd, sliderId, TBM_SETPAGESIZE, 0, pageSize);
    SendDlgItemMessageW(hwnd, sliderId, TBM_SETLINESIZE, 0, lineSize);
}

void SetLchSliderPositions(HWND hwnd, OklchColor lch) {
    SendDlgItemMessageW(hwnd,
        IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(lch.l * 1000.0)), 0, 1000));
    SendDlgItemMessageW(hwnd,
        IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(lch.c * kLchChromaSliderScale)),
            0,
            static_cast<int>(std::lround(kLchChromaSliderMax * kLchChromaSliderScale))));
    SendDlgItemMessageW(hwnd,
        IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(lch.h)), 0, 360));
}

void SetHsvSliderPositions(HWND hwnd, HsvColor hsv) {
    SendDlgItemMessageW(hwnd,
        IDC_LAYOUT_EDIT_COLOR_HSV_HUE_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(hsv.h)), 0, 360));
    SendDlgItemMessageW(hwnd,
        IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(hsv.s * kHsvUnitSliderScale)), 0, 1000));
    SendDlgItemMessageW(hwnd,
        IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(hsv.v * kHsvUnitSliderScale)), 0, 1000));
}

std::optional<OklchColor> ReadColorDialogLch(HWND hwnd) {
    const auto lightness =
        TryParseDialogDouble(ReadControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT).c_str());
    const auto chroma = TryParseDialogDouble(ReadControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT).c_str());
    const auto hue = TryParseDialogDouble(ReadControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT).c_str());
    if (!lightness.has_value() || !chroma.has_value() || !hue.has_value() || *lightness < 0.0 || *lightness > 1.0 ||
        *chroma < 0.0 || *hue < 0.0 || *hue > 360.0) {
        return std::nullopt;
    }
    return OklchColor{*lightness, *chroma, *hue};
}

std::optional<HsvColor> ReadColorDialogHsv(HWND hwnd) {
    const auto hue = TryParseDialogDouble(ReadControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_HUE_EDIT).c_str());
    const auto saturation =
        TryParseDialogDouble(ReadControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT).c_str());
    const auto value = TryParseDialogDouble(ReadControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_EDIT).c_str());
    if (!hue.has_value() || !saturation.has_value() || !value.has_value() || *hue < 0.0 || *hue > 360.0 ||
        *saturation < 0.0 || *saturation > 1.0 || *value < 0.0 || *value > 1.0) {
        return std::nullopt;
    }
    return HsvColor{*hue, *saturation, *value};
}

void SetRgbColorDialogChannels(HWND hwnd, unsigned int color) {
    SetColorDialogChannel(hwnd, kColorDialogControls[0], (color >> 24) & 0xFFu);
    SetColorDialogChannel(hwnd, kColorDialogControls[1], (color >> 16) & 0xFFu);
    SetColorDialogChannel(hwnd, kColorDialogControls[2], (color >> 8) & 0xFFu);
}

}  // namespace

std::string FindConfiguredBoardMetricBinding(const AppConfig& config, const LayoutMetricEditKey& key) {
    const auto target = ParseBoardMetricBindingTarget(key.metricId);
    if (!target.has_value()) {
        return {};
    }

    const auto& bindings = target->kind == BoardMetricBindingKind::Temperature
                               ? config.layout.board.temperatureSensorNames
                               : config.layout.board.fanSensorNames;
    const auto it = bindings.find(target->logicalName);
    if (it != bindings.end() && !it->second.empty()) {
        return it->second;
    }
    return target->logicalName;
}

bool AreScalesEqual(double left, double right) {
    return std::abs(left - right) < kScaleEpsilon;
}

std::optional<double> TryParseDialogDouble(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return std::nullopt;
    }
    std::wstring normalized(text);
    std::replace(normalized.begin(), normalized.end(), L',', L'.');
    wchar_t* end = nullptr;
    const double value = std::wcstod(normalized.c_str(), &end);
    if (end == normalized.c_str() || end == nullptr || *end != L'\0' || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> TryParseDialogInteger(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return std::nullopt;
    }
    wchar_t* end = nullptr;
    const long value = std::wcstol(text, &end, 10);
    if (end == text || end == nullptr || *end != L'\0') {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

std::string LayoutGuideChildName(const LayoutNodeConfig& node) {
    return node.name.empty() ? "unknown" : node.name;
}

std::string ReadDialogControlTextUtf8(HWND hwnd, int controlId) {
    wchar_t buffer[256] = {};
    GetDlgItemTextW(hwnd, controlId, buffer, ARRAYSIZE(buffer));
    return Utf8FromWide(buffer);
}

void SetDialogControlTextUtf8(HWND hwnd, int controlId, std::string_view text) {
    const std::wstring wideText = WideFromUtf8(text);
    SetDlgItemTextW(hwnd, controlId, wideText.c_str());
}

void SetDialogControlInteger(HWND hwnd, int controlId, int value) {
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%d", value);
    SetDlgItemTextW(hwnd, controlId, buffer);
}

void SetDialogControlIntegerOrEmpty(HWND hwnd, int controlId, int value, bool hasValue) {
    if (hasValue) {
        SetDialogControlInteger(hwnd, controlId, value);
    } else {
        SetDlgItemTextW(hwnd, controlId, L"");
    }
}

void SetWindowTextUtf8(HWND hwnd, std::string_view text) {
    const std::wstring wideText = WideFromUtf8(text);
    SetWindowTextW(hwnd, wideText.c_str());
}

LRESULT AddComboStringUtf8(HWND combo, std::string_view text) {
    const std::wstring wideText = WideFromUtf8(text);
    return SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wideText.c_str()));
}

std::wstring FormatDialogColorHex(unsigned int color) {
    wchar_t buffer[16] = {};
    swprintf_s(buffer, L"#%08X", color);
    return buffer;
}

std::optional<unsigned int> TryParseDialogHexColor(const wchar_t* text) {
    if (text == nullptr) {
        return std::nullopt;
    }
    std::wstring value(text);
    if (value.empty()) {
        return std::nullopt;
    }
    if (!value.empty() && value.front() == L'#') {
        value.erase(value.begin());
    }
    if (value.size() != 8) {
        return std::nullopt;
    }

    unsigned int color = 0;
    for (const wchar_t ch : value) {
        color <<= 4;
        if (ch >= L'0' && ch <= L'9') {
            color |= static_cast<unsigned int>(ch - L'0');
        } else if (ch >= L'a' && ch <= L'f') {
            color |= static_cast<unsigned int>(10 + ch - L'a');
        } else if (ch >= L'A' && ch <= L'F') {
            color |= static_cast<unsigned int>(10 + ch - L'A');
        } else {
            return std::nullopt;
        }
    }
    return color;
}

std::wstring TitleCaseWords(std::string_view text) {
    std::wstring result;
    bool capitalize = true;
    for (const char ch : text) {
        if (ch == '_' || ch == '.' || ch == '-') {
            result.push_back(L' ');
            capitalize = true;
            continue;
        }
        wchar_t wide = static_cast<unsigned char>(ch);
        wide = capitalize ? static_cast<wchar_t>(std::towupper(wide)) : static_cast<wchar_t>(std::towlower(wide));
        result.push_back(wide);
        capitalize = std::isspace(static_cast<unsigned char>(ch)) != 0;
    }
    return result;
}

void ConfigureColorSliders(HWND hwnd) {
    for (const auto& channel : kColorDialogControls) {
        SetSliderRange(hwnd, channel.sliderId, 0, 255, 16, 1);
    }
    SetSliderRange(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER, -180, 180, 15, 1);
    SetSliderRange(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER, 0, 100, 10, 1);
    SetSliderRange(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER, 0, 255, 16, 1);
    SetSliderRange(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER, 0, 1000, 100, 1);
    SetSliderRange(hwnd,
        IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER,
        0,
        static_cast<int>(std::lround(kLchChromaSliderMax * kLchChromaSliderScale)),
        25,
        1);
    SetSliderRange(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER, 0, 360, 15, 1);
    SetSliderRange(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_HUE_SLIDER, 0, 360, 15, 1);
    SetSliderRange(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_SLIDER, 0, 1000, 100, 1);
    SetSliderRange(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_SLIDER, 0, 1000, 100, 1);
}

void ConfigureColorViewTabs(HWND hwnd, ColorEditViewMode selectedMode) {
    HWND tab = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_VIEW_TAB);
    if (tab == nullptr) {
        return;
    }
    if (TabCtrl_GetItemCount(tab) == 0) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"RGB");
        TabCtrl_InsertItem(tab, 0, &item);
        item.pszText = const_cast<wchar_t*>(L"LCH");
        TabCtrl_InsertItem(tab, 1, &item);
        item.pszText = const_cast<wchar_t*>(L"HSV");
        TabCtrl_InsertItem(tab, 2, &item);
    }
    int selectedTab = 0;
    if (selectedMode == ColorEditViewMode::Lch) {
        selectedTab = 1;
    } else if (selectedMode == ColorEditViewMode::Hsv) {
        selectedTab = 2;
    }
    TabCtrl_SetCurSel(tab, selectedTab);
}

void SetColorDialogChannel(HWND hwnd, const ColorDialogControls& channel, unsigned int value) {
    SetDialogControlInteger(hwnd, channel.editId, static_cast<int>(value));
    SendDlgItemMessageW(hwnd, channel.sliderId, TBM_SETPOS, TRUE, value);
}

void SetColorDialogHex(HWND hwnd, unsigned int color) {
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, FormatDialogColorHex(color).c_str());
}

void SetColorDialogLch(HWND hwnd, unsigned int color) {
    const OklchColor lch = DisplayRoundedLch(OklchFromColorBytes(ColorBytesFromRgba(color)));
    SetDialogControlRoundedDecimal(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT, lch.l, 3);
    SetDialogControlRoundedDecimal(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT, lch.c, 3);
    SetDialogControlRoundedInteger(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT, lch.h);
    SetLchSliderPositions(hwnd, lch);
}

void SetColorDialogHsv(HWND hwnd, unsigned int color) {
    const HsvColor hsv = DisplayRoundedHsv(HsvFromColorBytes(ColorBytesFromRgba(color)));
    SetDialogControlRoundedInteger(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_HUE_EDIT, hsv.h);
    SetDialogControlRoundedDecimal(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT, hsv.s, 3);
    SetDialogControlRoundedDecimal(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_EDIT, hsv.v, 3);
    SetHsvSliderPositions(hwnd, hsv);
}

void SetColorDialogRgbFromLch(HWND hwnd) {
    const auto lch = ReadColorDialogLch(hwnd);
    const auto alpha = ParseColorDialogChannel(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT);
    if (!lch.has_value() || !alpha.has_value()) {
        return;
    }
    const unsigned int color = RgbaFromColorBytes(ColorBytesFromOklch(*lch, static_cast<double>(*alpha)));
    SetRgbColorDialogChannels(hwnd, color);
    SetColorDialogHex(hwnd, color);
}

void SetColorDialogRgbFromHsv(HWND hwnd) {
    const auto hsv = ReadColorDialogHsv(hwnd);
    const auto alpha = ParseColorDialogChannel(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT);
    if (!hsv.has_value() || !alpha.has_value()) {
        return;
    }
    const unsigned int color = RgbaFromColorBytes(ColorBytesFromHsv(*hsv, static_cast<double>(*alpha)));
    SetRgbColorDialogChannels(hwnd, color);
    SetColorDialogHex(hwnd, color);
}

bool ColorDialogLchValueValid(HWND hwnd) {
    return ReadColorDialogLch(hwnd).has_value();
}

bool ColorDialogHsvValueValid(HWND hwnd) {
    return ReadColorDialogHsv(hwnd).has_value();
}

bool IsColorLchControlId(int controlId) {
    return controlId == IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT ||
           controlId == IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT || controlId == IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT;
}

bool IsColorHsvControlId(int controlId) {
    return controlId == IDC_LAYOUT_EDIT_COLOR_HSV_HUE_EDIT || controlId == IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT ||
           controlId == IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_EDIT;
}

bool IsColorLchSliderId(int controlId) {
    return controlId == IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER ||
           controlId == IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER || controlId == IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER;
}

bool IsColorHsvSliderId(int controlId) {
    return controlId == IDC_LAYOUT_EDIT_COLOR_HSV_HUE_SLIDER ||
           controlId == IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_SLIDER ||
           controlId == IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_SLIDER;
}

void SyncColorLchSliderFromEdit(HWND hwnd, int editId) {
    const auto value = TryParseDialogDouble(ReadControlTextWide(hwnd, editId).c_str());
    if (!value.has_value()) {
        return;
    }
    switch (editId) {
        case IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT:
            SendDlgItemMessageW(hwnd,
                IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value * 1000.0)), 0, 1000));
            break;
        case IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT:
            SendDlgItemMessageW(hwnd,
                IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value * kLchChromaSliderScale)),
                    0,
                    static_cast<int>(std::lround(kLchChromaSliderMax * kLchChromaSliderScale))));
            break;
        case IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT:
            SendDlgItemMessageW(hwnd,
                IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value)), 0, 360));
            break;
    }
}

void SyncColorHsvSliderFromEdit(HWND hwnd, int editId) {
    const auto value = TryParseDialogDouble(ReadControlTextWide(hwnd, editId).c_str());
    if (!value.has_value()) {
        return;
    }
    switch (editId) {
        case IDC_LAYOUT_EDIT_COLOR_HSV_HUE_EDIT:
            SendDlgItemMessageW(hwnd,
                IDC_LAYOUT_EDIT_COLOR_HSV_HUE_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value)), 0, 360));
            break;
        case IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT:
            SendDlgItemMessageW(hwnd,
                IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value * kHsvUnitSliderScale)), 0, 1000));
            break;
        case IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_EDIT:
            SendDlgItemMessageW(hwnd,
                IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value * kHsvUnitSliderScale)), 0, 1000));
            break;
    }
}

void SetColorLchEditFromSlider(HWND hwnd, int sliderId) {
    const int position = static_cast<int>(SendDlgItemMessageW(hwnd, sliderId, TBM_GETPOS, 0, 0));
    switch (sliderId) {
        case IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER:
            SetDialogControlRoundedDecimal(
                hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT, static_cast<double>(position) / 1000.0, 3);
            break;
        case IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER:
            SetDialogControlRoundedDecimal(
                hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT, static_cast<double>(position) / kLchChromaSliderScale, 3);
            break;
        case IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER:
            SetDialogControlInteger(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT, position);
            break;
    }
}

void SetColorHsvEditFromSlider(HWND hwnd, int sliderId) {
    const int position = static_cast<int>(SendDlgItemMessageW(hwnd, sliderId, TBM_GETPOS, 0, 0));
    switch (sliderId) {
        case IDC_LAYOUT_EDIT_COLOR_HSV_HUE_SLIDER:
            SetDialogControlInteger(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_HUE_EDIT, position);
            break;
        case IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_SLIDER:
            SetDialogControlRoundedDecimal(hwnd,
                IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT,
                static_cast<double>(position) / kHsvUnitSliderScale,
                3);
            break;
        case IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_SLIDER:
            SetDialogControlRoundedDecimal(
                hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_EDIT, static_cast<double>(position) / kHsvUnitSliderScale, 3);
            break;
    }
}

std::optional<unsigned int> ParseColorDialogChannel(HWND hwnd, int editId) {
    wchar_t buffer[64] = {};
    GetDlgItemTextW(hwnd, editId, buffer, ARRAYSIZE(buffer));
    const auto value = TryParseDialogInteger(buffer);
    if (!value.has_value() || *value < 0 || *value > 255) {
        return std::nullopt;
    }
    return static_cast<unsigned int>(*value);
}

std::optional<unsigned int> ReadColorDialogValue(HWND hwnd) {
    const auto red = ParseColorDialogChannel(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT);
    const auto green = ParseColorDialogChannel(hwnd, IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT);
    const auto blue = ParseColorDialogChannel(hwnd, IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT);
    const auto alpha = ParseColorDialogChannel(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT);
    if (!red.has_value() || !green.has_value() || !blue.has_value() || !alpha.has_value()) {
        return std::nullopt;
    }
    return (*red << 24) | (*green << 16) | (*blue << 8) | *alpha;
}

const ColorDialogControls* FindColorDialogControlsByEditId(int editId) {
    for (const auto& channel : kColorDialogControls) {
        if (channel.editId == editId) {
            return &channel;
        }
    }
    return nullptr;
}

const ColorDialogControls* FindColorDialogControlsBySliderId(int sliderId) {
    for (const auto& channel : kColorDialogControls) {
        if (channel.sliderId == sliderId) {
            return &channel;
        }
    }
    return nullptr;
}

std::vector<std::wstring> EnumerateInstalledFontFamilies(HWND hwnd) {
    std::vector<std::wstring> families;
    HDC dc = GetDC(hwnd);
    if (dc == nullptr) {
        return families;
    }

    LOGFONTW filter{};
    filter.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &filter, CollectFontFamilyProc, reinterpret_cast<LPARAM>(&families), 0);
    ReleaseDC(hwnd, dc);

    SortUniqueFontFamilies(families);
    return families;
}

void PopulateFontFaceComboBox(HWND hwnd, const std::wstring& selectedFace) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT);
    if (combo == nullptr) {
        return;
    }

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_SETMINVISIBLE, 10, 0);
    const auto families = EnumerateInstalledFontFamilies(hwnd);
    int selectedIndex = CB_ERR;
    for (const auto& family : families) {
        const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(family.c_str()));
        if (index != CB_ERR && selectedIndex == CB_ERR && CaseInsensitiveEqual(family, selectedFace)) {
            selectedIndex = static_cast<int>(index);
        }
    }

    if (selectedIndex != CB_ERR) {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
    } else {
        SetWindowTextW(combo, selectedFace.c_str());
    }
}

std::wstring ReadFontDialogFaceText(HWND hwnd, UINT notificationCode) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT);
    if (combo == nullptr) {
        return {};
    }

    if (notificationCode == CBN_SELCHANGE) {
        const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (selection != CB_ERR) {
            wchar_t selectedFace[256] = {};
            SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(selection), reinterpret_cast<LPARAM>(selectedFace));
            return selectedFace;
        }
    }

    wchar_t faceBuffer[256] = {};
    GetWindowTextW(combo, faceBuffer, ARRAYSIZE(faceBuffer));
    return faceBuffer;
}

void PopulateMetricBindingComboBox(
    HWND hwnd, const std::vector<std::string>& options, std::string_view selectedBinding, bool enableSelection) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT);
    if (combo == nullptr) {
        return;
    }

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_SETMINVISIBLE, 10, 0);
    int selectedIndex = CB_ERR;
    for (const auto& option : options) {
        const LRESULT index = AddComboStringUtf8(combo, option);
        if (index != CB_ERR && selectedIndex == CB_ERR && option == selectedBinding) {
            selectedIndex = static_cast<int>(index);
        }
    }

    if (selectedIndex != CB_ERR) {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
    } else {
        SetWindowTextUtf8(combo, selectedBinding);
    }

    EnableWindow(combo, enableSelection ? TRUE : FALSE);
}

std::optional<std::string> FindCardTitleValue(const AppConfig& config, const LayoutCardTitleEditKey& key) {
    const LayoutCardConfig* card = FindCardById(config, key.cardId);
    if (card == nullptr) {
        return std::nullopt;
    }
    return card->title;
}

std::optional<std::pair<int, int>> FindWeightEditValues(const AppConfig& config, const LayoutWeightEditKey& key) {
    const LayoutNodeConfig* node = FindWeightEditNode(config, key);
    if (node == nullptr || key.separatorIndex + 1 >= node->children.size()) {
        return std::nullopt;
    }
    return std::make_pair(std::max(1, node->children[key.separatorIndex].weight),
        std::max(1, node->children[key.separatorIndex + 1].weight));
}

std::vector<std::string> AvailableMetricDefinitionIds(const AppConfig& config) {
    return AvailableMetricListMetricIds(config, TelemetryMetricCatalog());
}

bool IsMetricListSupportedDisplayStyle(MetricDisplayStyle style) {
    switch (style) {
        case MetricDisplayStyle::Scalar:
        case MetricDisplayStyle::Percent:
        case MetricDisplayStyle::Memory:
            return true;
        case MetricDisplayStyle::Throughput:
        case MetricDisplayStyle::SizeAuto:
        case MetricDisplayStyle::LabelOnly:
            return false;
    }
    return false;
}

std::wstring BuildWeightEditorLabel(const LayoutEditTreeLeaf& leaf, bool first) {
    const std::wstring side =
        leaf.weightAxis == LayoutGuideAxis::Vertical ? (first ? L"Left" : L"Right") : (first ? L"Top" : L"Bottom");
    const std::wstring name = WideFromUtf8(first ? leaf.firstWeightName : leaf.secondWeightName);
    return side + L" " + name + L" weight:";
}

std::wstring BuildLayoutEditNodeTitle(const LayoutEditTreeNode* node) {
    if (node == nullptr) {
        return L"Select a setting";
    }
    if (const auto* parameterLeaf =
            node->leaf.has_value() ? std::get_if<LayoutEditParameter>(&node->leaf->focusKey) : nullptr;
        parameterLeaf != nullptr) {
        return TitleCaseWords(GetLayoutEditParameterDisplayName(*parameterLeaf));
    }
    if (const auto* metricLeaf =
            node->leaf.has_value() ? std::get_if<LayoutMetricEditKey>(&node->leaf->focusKey) : nullptr;
        metricLeaf != nullptr) {
        return L"Metric: " + WideFromUtf8(metricLeaf->metricId);
    }
    if (const auto* titleLeaf =
            node->leaf.has_value() ? std::get_if<LayoutCardTitleEditKey>(&node->leaf->focusKey) : nullptr;
        titleLeaf != nullptr) {
        return L"Card Title";
    }
    if (const auto* nodeFieldLeaf =
            node->leaf.has_value() ? std::get_if<LayoutNodeFieldEditKey>(&node->leaf->focusKey) : nullptr;
        nodeFieldLeaf != nullptr) {
        return LayoutNodeFieldEditTitle(*nodeFieldLeaf);
    }
    if (const auto* weightLeaf =
            node->leaf.has_value() ? std::get_if<LayoutWeightEditKey>(&node->leaf->focusKey) : nullptr;
        weightLeaf != nullptr) {
        return weightLeaf->editCardId.empty() ? L"Dashboard Split Weights" : L"Card Split Weights";
    }
    return TitleCaseWords(node->label);
}

std::wstring BuildLayoutEditSummaryText(const LayoutEditTreeNode* node) {
    if (node == nullptr) {
        return L"Type in the filter box or choose a tree item to start editing.";
    }
    if (node->leaf.has_value()) {
        return L"";
    }
    if (!node->selectionHighlight.has_value()) {
        return L"This item describes a configuration group and its matching dashboard highlight.";
    }
    if (const auto* special = std::get_if<LayoutEditSelectionHighlightSpecial>(&*node->selectionHighlight)) {
        switch (*special) {
            case LayoutEditSelectionHighlightSpecial::AllCards:
                return L"Highlights every rendered card while this node is selected.";
            case LayoutEditSelectionHighlightSpecial::AllTexts:
                return L"Highlights editable text targets and text widgets while this node is selected.";
            case LayoutEditSelectionHighlightSpecial::DashboardBounds:
                return L"Highlights the dashboard bounds while this node is selected.";
        }
    }
    if (std::holds_alternative<WidgetClass>(*node->selectionHighlight)) {
        return L"Highlights every rendered widget of this type while this node is selected.";
    }
    if (std::holds_alternative<LayoutContainerEditKey>(*node->selectionHighlight)) {
        return L"Highlights this container in the active layout while this node is selected.";
    }
    if (std::holds_alternative<LayoutEditWidgetIdentity>(*node->selectionHighlight)) {
        return L"Highlights every rendered instance of this card while this node is selected.";
    }
    return L"Highlights the matching dashboard region while this node is selected.";
}

std::wstring BuildLayoutEditHintText(const LayoutEditTreeNode* node) {
    if (node == nullptr || !node->leaf.has_value()) {
        return L"Select a field to edit it here.";
    }
    if (const auto* parameter = std::get_if<LayoutEditParameter>(&node->leaf->focusKey); parameter != nullptr) {
        switch (node->leaf->valueFormat) {
            case configschema::ValueFormat::Integer:
                return L"Enter a whole number. Valid values preview live.";
            case configschema::ValueFormat::FloatingPoint:
                return L"Enter a number. Use a period or comma for decimals.";
            case configschema::ValueFormat::String:
                return L"Type the replacement text. Changes preview live.";
            case configschema::ValueFormat::FontSpec:
                return L"Choose a font family, size, and weight. Changes preview live.";
            case configschema::ValueFormat::ColorHex:
                return L"Edit the color as #RRGGBBAA or use the RGBA controls and picker.";
        }
    }
    if (std::holds_alternative<LayoutWeightEditKey>(node->leaf->focusKey)) {
        return L"Enter positive integer weights for the two neighboring layout items.";
    }
    if (std::holds_alternative<LayoutMetricEditKey>(node->leaf->focusKey)) {
        return L"Adjust the metric label, unit, scale, and board sensor binding. Changes preview live.";
    }
    if (std::holds_alternative<LayoutCardTitleEditKey>(node->leaf->focusKey)) {
        return L"Edit the card title text. Changes preview live.";
    }
    if (std::holds_alternative<ThemeColorEditKey>(node->leaf->focusKey)) {
        return L"Edit the theme token as #RRGGBBAA or use the RGBA controls and picker.";
    }
    if (const auto* nodeFieldLeaf = std::get_if<LayoutNodeFieldEditKey>(&node->leaf->focusKey);
        nodeFieldLeaf != nullptr) {
        return LayoutNodeFieldEditHint(*nodeFieldLeaf);
    }
    return L"Select a field to edit it here.";
}
