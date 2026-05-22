#include "layout_edit_dialog/impl/util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <commctrl.h>
#include <cstdlib>
#include <cstring>

#include "config/color_math.h"
#include "config/metric_board_binding.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_target_descriptor.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "resource.h"
#include "telemetry/metrics.h"
#include "util/localization_catalog.h"
#include "util/text_format.h"

namespace {

constexpr double kScaleEpsilon = 0.0001;
constexpr double kLchChromaSliderScale = 1000.0;
constexpr double kLchChromaSliderMax = 0.4;
constexpr double kHsvUnitSliderScale = 1000.0;

int CALLBACK CollectFontFamilyProc(const LOGFONTA* logFont, const TEXTMETRICA*, DWORD, LPARAM lParam) {
    auto* families = reinterpret_cast<std::vector<std::string>*>(lParam);
    if (families == nullptr || logFont == nullptr || logFont->lfFaceName[0] == '\0' || logFont->lfFaceName[0] == '@') {
        return 1;
    }
    families->push_back(logFont->lfFaceName);
    return 1;
}

bool CaseInsensitiveEqual(const std::string& left, const std::string& right) {
    return CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, left.c_str(), -1, right.c_str(), -1) == CSTR_EQUAL;
}

bool CaseInsensitiveLess(const std::string& left, const std::string& right) {
    return CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, left.c_str(), -1, right.c_str(), -1) == CSTR_LESS_THAN;
}

void SortUniqueFontFamilies(std::vector<std::string>& families) {
    for (size_t i = 1; i < families.size(); ++i) {
        std::string family = std::move(families[i]);
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
    for (const LayoutCardConfig& card : config.layout.cards) {
        if (card.id == cardId) {
            return &card;
        }
    }
    return nullptr;
}

double RoundToStep(double value, double step) {
    return std::round(value / step) * step;
}

void SetDialogControlRoundedDecimal(HWND hwnd, int controlId, double value, int decimalPlaces) {
    std::string text = FormatText("%.*f", decimalPlaces, value);
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    SetDialogControlText(hwnd, controlId, text.empty() || text == "-0" ? "0" : text);
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
    SendDlgItemMessageA(hwnd, sliderId, TBM_SETRANGEMIN, TRUE, minValue);
    SendDlgItemMessageA(hwnd, sliderId, TBM_SETRANGEMAX, TRUE, maxValue);
    SendDlgItemMessageA(hwnd, sliderId, TBM_SETPAGESIZE, 0, pageSize);
    SendDlgItemMessageA(hwnd, sliderId, TBM_SETLINESIZE, 0, lineSize);
}

void SetLchSliderPositions(HWND hwnd, OklchColor lch) {
    SendDlgItemMessageA(hwnd,
        IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(lch.l * 1000.0)), 0, 1000));
    SendDlgItemMessageA(hwnd,
        IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(lch.c * kLchChromaSliderScale)),
            0,
            static_cast<int>(std::lround(kLchChromaSliderMax * kLchChromaSliderScale))));
    SendDlgItemMessageA(hwnd,
        IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(lch.h)), 0, 360));
}

void SetHsvSliderPositions(HWND hwnd, HsvColor hsv) {
    SendDlgItemMessageA(hwnd,
        IDC_LAYOUT_EDIT_COLOR_HSV_HUE_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(hsv.h)), 0, 360));
    SendDlgItemMessageA(hwnd,
        IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(hsv.s * kHsvUnitSliderScale)), 0, 1000));
    SendDlgItemMessageA(hwnd,
        IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_SLIDER,
        TBM_SETPOS,
        TRUE,
        std::clamp(static_cast<int>(std::lround(hsv.v * kHsvUnitSliderScale)), 0, 1000));
}

std::optional<OklchColor> ReadColorDialogLch(HWND hwnd) {
    const auto lightness = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT);
    const auto chroma = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT);
    const auto hue = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT);
    if (!lightness.has_value() || !chroma.has_value() || !hue.has_value() || *lightness < 0.0 || *lightness > 1.0 ||
        *chroma < 0.0 || *hue < 0.0 || *hue > 360.0) {
        return std::nullopt;
    }
    return OklchColor{*lightness, *chroma, *hue};
}

std::optional<HsvColor> ReadColorDialogHsv(HWND hwnd) {
    const auto hue = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_HUE_EDIT);
    const auto saturation = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT);
    const auto value = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_EDIT);
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
    const auto target = ResolveMetricBoardBindingTarget(key.metricId);
    if (!target.has_value()) {
        return {};
    }

    const auto& bindings = target->kind == BoardMetricBindingKind::Temperature ?
        config.layout.board.temperatureSensorNames :
        config.layout.board.fanSensorNames;
    const auto it = bindings.find(target->logicalName);
    if (it != bindings.end() && !it->second.empty()) {
        return it->second;
    }
    return target->logicalName;
}

bool AreScalesEqual(double left, double right) {
    return std::abs(left - right) < kScaleEpsilon;
}

std::optional<double> TryParseDialogDouble(const char* text) {
    if (text == nullptr || *text == '\0') {
        return std::nullopt;
    }
    char normalized[128] = {};
    size_t length = 0;
    while (text[length] != '\0' && length + 1 < ARRAYSIZE(normalized)) {
        normalized[length] = text[length] == ',' ? '.' : text[length];
        ++length;
    }
    if (text[length] != '\0') {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(normalized, &end);
    if (end == normalized || end == nullptr || *end != '\0' || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> TryParseDialogControlDouble(HWND hwnd, int controlId) {
    char buffer[128] = {};
    GetDlgItemTextA(hwnd, controlId, buffer, ARRAYSIZE(buffer));
    return TryParseDialogDouble(buffer);
}

std::optional<int> TryParseDialogInteger(const char* text) {
    if (text == nullptr || *text == '\0') {
        return std::nullopt;
    }
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

std::string LayoutGuideChildName(const LayoutNodeConfig& node) {
    return node.name.empty() ? "unknown" : node.name;
}

std::string ReadDialogControlText(HWND hwnd, int controlId) {
    char buffer[256] = {};
    GetDlgItemTextA(hwnd, controlId, buffer, ARRAYSIZE(buffer));
    return buffer;
}

void SetDialogControlText(HWND hwnd, int controlId, std::string_view text) {
    const std::string textStorage(text);
    SetDlgItemTextA(hwnd, controlId, textStorage.c_str());
}

void SetDialogControlInteger(HWND hwnd, int controlId, int value) {
    SetDialogControlText(hwnd, controlId, FormatText("%d", value));
}

void SetDialogControlIntegerOrEmpty(HWND hwnd, int controlId, int value, bool hasValue) {
    if (hasValue) {
        SetDialogControlInteger(hwnd, controlId, value);
    } else {
        SetDialogControlText(hwnd, controlId, "");
    }
}

void SetWindowTextValue(HWND hwnd, std::string_view text) {
    const std::string textStorage(text);
    SetWindowTextA(hwnd, textStorage.c_str());
}

LRESULT AddComboString(HWND combo, std::string_view text) {
    const std::string textStorage(text);
    return SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(textStorage.c_str()));
}

std::string FormatDialogColorHex(unsigned int color) {
    return FormatText("#%08X", color);
}

std::optional<unsigned int> TryParseDialogHexColor(const char* text) {
    if (text == nullptr) {
        return std::nullopt;
    }
    if (*text == '#') {
        ++text;
    }
    if (std::strlen(text) != 8) {
        return std::nullopt;
    }

    unsigned int color = 0;
    for (size_t i = 0; i < 8; ++i) {
        const char ch = text[i];
        color <<= 4;
        if (ch >= '0' && ch <= '9') {
            color |= static_cast<unsigned int>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            color |= static_cast<unsigned int>(10 + ch - 'a');
        } else if (ch >= 'A' && ch <= 'F') {
            color |= static_cast<unsigned int>(10 + ch - 'A');
        } else {
            return std::nullopt;
        }
    }
    return color;
}

std::string TitleCaseWords(std::string_view text) {
    std::string result;
    bool capitalize = true;
    for (const char ch : text) {
        if (ch == '_' || ch == '.' || ch == '-') {
            result.push_back(' ');
            capitalize = true;
            continue;
        }
        char formatted = ch;
        if (ch >= 'A' && ch <= 'Z') {
            formatted = capitalize ? ch : static_cast<char>(ch - 'A' + 'a');
        } else if (ch >= 'a' && ch <= 'z') {
            formatted = capitalize ? static_cast<char>(ch - 'a' + 'A') : ch;
        }
        result.push_back(formatted);
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
        TCITEMA item{};
        item.mask = TCIF_TEXT;
        const auto insertTab = [&](int index, std::string_view text) {
            const std::string textStorage(text);
            item.pszText = const_cast<char*>(textStorage.c_str());
            TabCtrl_InsertItem(tab, index, &item);
        };
        insertTab(0, "RGB");
        insertTab(1, "LCH");
        insertTab(2, "HSV");
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
    SendDlgItemMessageA(hwnd, channel.sliderId, TBM_SETPOS, TRUE, value);
}

void SetColorDialogHex(HWND hwnd, unsigned int color) {
    SetDialogControlText(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, FormatDialogColorHex(color));
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
        controlId == IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_SLIDER || controlId == IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_SLIDER;
}

void SyncColorLchSliderFromEdit(HWND hwnd, int editId) {
    const auto value = TryParseDialogControlDouble(hwnd, editId);
    if (!value.has_value()) {
        return;
    }
    switch (editId) {
        case IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_EDIT:
            SendDlgItemMessageA(hwnd,
                IDC_LAYOUT_EDIT_COLOR_LCH_LIGHTNESS_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value * 1000.0)), 0, 1000));
            break;
        case IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_EDIT:
            SendDlgItemMessageA(hwnd,
                IDC_LAYOUT_EDIT_COLOR_LCH_CHROMA_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value * kLchChromaSliderScale)),
                    0,
                    static_cast<int>(std::lround(kLchChromaSliderMax * kLchChromaSliderScale))));
            break;
        case IDC_LAYOUT_EDIT_COLOR_LCH_HUE_EDIT:
            SendDlgItemMessageA(hwnd,
                IDC_LAYOUT_EDIT_COLOR_LCH_HUE_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value)), 0, 360));
            break;
    }
}

void SyncColorHsvSliderFromEdit(HWND hwnd, int editId) {
    const auto value = TryParseDialogControlDouble(hwnd, editId);
    if (!value.has_value()) {
        return;
    }
    switch (editId) {
        case IDC_LAYOUT_EDIT_COLOR_HSV_HUE_EDIT:
            SendDlgItemMessageA(hwnd,
                IDC_LAYOUT_EDIT_COLOR_HSV_HUE_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value)), 0, 360));
            break;
        case IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_EDIT:
            SendDlgItemMessageA(hwnd,
                IDC_LAYOUT_EDIT_COLOR_HSV_SATURATION_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value * kHsvUnitSliderScale)), 0, 1000));
            break;
        case IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_EDIT:
            SendDlgItemMessageA(hwnd,
                IDC_LAYOUT_EDIT_COLOR_HSV_VALUE_SLIDER,
                TBM_SETPOS,
                TRUE,
                std::clamp(static_cast<int>(std::lround(*value * kHsvUnitSliderScale)), 0, 1000));
            break;
    }
}

void SetColorLchEditFromSlider(HWND hwnd, int sliderId) {
    const int position = static_cast<int>(SendDlgItemMessageA(hwnd, sliderId, TBM_GETPOS, 0, 0));
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
    const int position = static_cast<int>(SendDlgItemMessageA(hwnd, sliderId, TBM_GETPOS, 0, 0));
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
    char buffer[64] = {};
    GetDlgItemTextA(hwnd, editId, buffer, ARRAYSIZE(buffer));
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

std::vector<std::string> EnumerateInstalledFontFamilies(HWND hwnd) {
    std::vector<std::string> families;
    HDC dc = GetDC(hwnd);
    if (dc == nullptr) {
        return families;
    }

    LOGFONTA filter{};
    filter.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExA(dc, &filter, CollectFontFamilyProc, reinterpret_cast<LPARAM>(&families), 0);
    ReleaseDC(hwnd, dc);

    SortUniqueFontFamilies(families);
    return families;
}

void PopulateFontFaceComboBox(HWND hwnd, std::string_view selectedFace) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT);
    if (combo == nullptr) {
        return;
    }

    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    SendMessageA(combo, CB_SETMINVISIBLE, 10, 0);
    const auto families = EnumerateInstalledFontFamilies(hwnd);
    const std::string selectedFaceText(selectedFace);
    int selectedIndex = CB_ERR;
    for (const auto& family : families) {
        const LRESULT index = AddComboString(combo, family);
        if (index != CB_ERR && selectedIndex == CB_ERR && CaseInsensitiveEqual(family, selectedFaceText)) {
            selectedIndex = static_cast<int>(index);
        }
    }

    if (selectedIndex != CB_ERR) {
        SendMessageA(combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
    } else {
        SetWindowTextValue(combo, selectedFace);
    }
}

std::string ReadFontDialogFaceText(HWND hwnd, UINT notificationCode) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT);
    if (combo == nullptr) {
        return {};
    }

    if (notificationCode == CBN_SELCHANGE) {
        const LRESULT selection = SendMessageA(combo, CB_GETCURSEL, 0, 0);
        if (selection != CB_ERR) {
            char selectedFace[256] = {};
            SendMessageA(combo, CB_GETLBTEXT, static_cast<WPARAM>(selection), reinterpret_cast<LPARAM>(selectedFace));
            return selectedFace;
        }
    }

    char faceBuffer[256] = {};
    GetWindowTextA(combo, faceBuffer, ARRAYSIZE(faceBuffer));
    return faceBuffer;
}

void PopulateMetricBindingComboBox(
    HWND hwnd, const std::vector<std::string>& options, std::string_view selectedBinding, bool enableSelection) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT);
    if (combo == nullptr) {
        return;
    }

    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    SendMessageA(combo, CB_SETMINVISIBLE, 10, 0);
    int selectedIndex = CB_ERR;
    for (const auto& option : options) {
        const LRESULT index = AddComboString(combo, option);
        if (index != CB_ERR && selectedIndex == CB_ERR && option == selectedBinding) {
            selectedIndex = static_cast<int>(index);
        }
    }

    if (selectedIndex != CB_ERR) {
        SendMessageA(combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
    } else {
        SetWindowTextValue(combo, selectedBinding);
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

std::string BuildWeightEditorLabel(const LayoutEditTreeLeaf& leaf, bool first) {
    const char* side =
        leaf.weightAxis == LayoutGuideAxis::Vertical ? (first ? "Left" : "Right") : (first ? "Top" : "Bottom");
    return FormatText("%s %s weight:", side, (first ? leaf.firstWeightName : leaf.secondWeightName).c_str());
}

std::string BuildLayoutEditNodeTitle(const LayoutEditTreeNode* node) {
    if (node == nullptr) {
        return "Select a setting";
    }
    if (const auto* parameterLeaf =
            node->leaf.has_value() ? std::get_if<LayoutEditParameter>(&node->leaf->focusKey) : nullptr;
        parameterLeaf != nullptr) {
        return TitleCaseWords(GetLayoutEditParameterDisplayName(*parameterLeaf));
    }
    if (const auto* metricLeaf =
            node->leaf.has_value() ? std::get_if<LayoutMetricEditKey>(&node->leaf->focusKey) : nullptr;
        metricLeaf != nullptr) {
        return FormatText("Metric: %s", metricLeaf->metricId.c_str());
    }
    if (const auto* titleLeaf =
            node->leaf.has_value() ? std::get_if<LayoutCardTitleEditKey>(&node->leaf->focusKey) : nullptr;
        titleLeaf != nullptr) {
        return "Card Title";
    }
    if (const auto* nodeFieldLeaf =
            node->leaf.has_value() ? std::get_if<LayoutNodeFieldEditKey>(&node->leaf->focusKey) : nullptr;
        nodeFieldLeaf != nullptr) {
        return std::string(LayoutNodeFieldEditTitle(*nodeFieldLeaf));
    }
    if (const auto* weightLeaf =
            node->leaf.has_value() ? std::get_if<LayoutWeightEditKey>(&node->leaf->focusKey) : nullptr;
        weightLeaf != nullptr) {
        return weightLeaf->editCardId.empty() ? "Dashboard Split Weights" : "Card Split Weights";
    }
    return TitleCaseWords(node->label);
}

std::string_view BuildLayoutEditSummaryText(const LayoutEditTreeNode* node) {
    if (node == nullptr) {
        return FindLocalizedText(RES_STR("layout_edit.summary.start"));
    }
    if (node->leaf.has_value()) {
        return "";
    }
    if (!node->selectionHighlight.has_value()) {
        return FindLocalizedText(RES_STR("layout_edit.summary.group"));
    }
    if (const auto* special = std::get_if<LayoutEditSelectionHighlightSpecial>(&*node->selectionHighlight)) {
        switch (*special) {
            case LayoutEditSelectionHighlightSpecial::AllCards:
                return FindLocalizedText(RES_STR("layout_edit.summary.all_cards"));
            case LayoutEditSelectionHighlightSpecial::AllTexts:
                return FindLocalizedText(RES_STR("layout_edit.summary.all_texts"));
            case LayoutEditSelectionHighlightSpecial::DashboardBounds:
                return FindLocalizedText(RES_STR("layout_edit.summary.dashboard_bounds"));
        }
    }
    if (std::holds_alternative<WidgetClass>(*node->selectionHighlight)) {
        return FindLocalizedText(RES_STR("layout_edit.summary.widget_class"));
    }
    if (std::holds_alternative<LayoutContainerEditKey>(*node->selectionHighlight)) {
        return FindLocalizedText(RES_STR("layout_edit.summary.container"));
    }
    if (std::holds_alternative<LayoutEditWidgetIdentity>(*node->selectionHighlight)) {
        return FindLocalizedText(RES_STR("layout_edit.summary.card_instances"));
    }
    return FindLocalizedText(RES_STR("layout_edit.summary.dashboard_region"));
}

std::string_view BuildLayoutEditHintText(const LayoutEditTreeNode* node) {
    if (node == nullptr || !node->leaf.has_value()) {
        return FindLocalizedText(RES_STR("layout_edit.status.select_field"));
    }
    if (const auto* parameter = std::get_if<LayoutEditParameter>(&node->leaf->focusKey); parameter != nullptr) {
        switch (node->leaf->valueFormat) {
            case configschema::ValueFormat::Integer:
                return FindLocalizedText(RES_STR("layout_edit.hint.integer"));
            case configschema::ValueFormat::FloatingPoint:
                return FindLocalizedText(RES_STR("layout_edit.hint.floating_point"));
            case configschema::ValueFormat::String:
                return FindLocalizedText(RES_STR("layout_edit.hint.string"));
            case configschema::ValueFormat::FontSpec:
                return FindLocalizedText(RES_STR("layout_edit.hint.font"));
            case configschema::ValueFormat::ColorHex:
                return FindLocalizedText(RES_STR("layout_edit.hint.color"));
        }
    }
    if (std::holds_alternative<LayoutWeightEditKey>(node->leaf->focusKey)) {
        return FindLocalizedText(RES_STR("layout_edit.hint.weight"));
    }
    if (std::holds_alternative<LayoutMetricEditKey>(node->leaf->focusKey)) {
        return FindLocalizedText(RES_STR("layout_edit.hint.metric_definition"));
    }
    if (std::holds_alternative<LayoutCardTitleEditKey>(node->leaf->focusKey)) {
        return FindLocalizedText(RES_STR("layout_edit.hint.card_title"));
    }
    if (std::holds_alternative<ThemeColorEditKey>(node->leaf->focusKey)) {
        return FindLocalizedText(RES_STR("layout_edit.hint.theme_color"));
    }
    if (const auto* nodeFieldLeaf = std::get_if<LayoutNodeFieldEditKey>(&node->leaf->focusKey);
        nodeFieldLeaf != nullptr) {
        return LayoutNodeFieldEditHint(*nodeFieldLeaf);
    }
    return FindLocalizedText(RES_STR("layout_edit.status.select_field"));
}
