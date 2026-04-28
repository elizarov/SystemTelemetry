#include "layout_edit_dialog/impl/util.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <cwchar>
#include <cwctype>

#include "layout_edit/layout_edit_service.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "resource.h"
#include "telemetry/metrics.h"
#include "util/utf8.h"

namespace {

constexpr double kScaleEpsilon = 0.0001;
constexpr std::string_view kBoardTemperatureMetricPrefix = "board.temp.";
constexpr std::string_view kBoardFanMetricPrefix = "board.fan.";

int CALLBACK CollectFontFamilyProc(const LOGFONTW* logFont, const TEXTMETRICW*, DWORD, LPARAM lParam) {
    auto* families = reinterpret_cast<std::vector<std::wstring>*>(lParam);
    if (families == nullptr || logFont == nullptr || logFont->lfFaceName[0] == L'\0' ||
        logFont->lfFaceName[0] == L'@') {
        return 1;
    }
    families->push_back(logFont->lfFaceName);
    return 1;
}

bool CaseInsensitiveLess(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
}

bool CaseInsensitiveEqual(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
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

}  // namespace

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
        SendDlgItemMessageW(hwnd, channel.sliderId, TBM_SETRANGEMIN, TRUE, 0);
        SendDlgItemMessageW(hwnd, channel.sliderId, TBM_SETRANGEMAX, TRUE, 255);
        SendDlgItemMessageW(hwnd, channel.sliderId, TBM_SETPAGESIZE, 0, 16);
        SendDlgItemMessageW(hwnd, channel.sliderId, TBM_SETLINESIZE, 0, 1);
    }
}

void SetColorDialogChannel(HWND hwnd, const ColorDialogControls& channel, unsigned int value) {
    SetDlgItemTextW(hwnd, channel.editId, WideFromUtf8(std::to_string(value)).c_str());
    SendDlgItemMessageW(hwnd, channel.sliderId, TBM_SETPOS, TRUE, value);
}

void SetColorDialogHex(HWND hwnd, unsigned int color) {
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, FormatDialogColorHex(color).c_str());
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

    std::sort(families.begin(), families.end(), CaseInsensitiveLess);
    families.erase(std::unique(families.begin(), families.end(), CaseInsensitiveEqual), families.end());
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
        const std::wstring wideOption = WideFromUtf8(option);
        const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wideOption.c_str()));
        if (index != CB_ERR && selectedIndex == CB_ERR && option == selectedBinding) {
            selectedIndex = static_cast<int>(index);
        }
    }

    if (selectedIndex != CB_ERR) {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
    } else {
        SetWindowTextW(combo, WideFromUtf8(std::string(selectedBinding)).c_str());
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
    if (const auto* metricListLeaf =
            node->leaf.has_value() ? std::get_if<LayoutMetricListOrderEditKey>(&node->leaf->focusKey) : nullptr;
        metricListLeaf != nullptr) {
        return L"Metric List";
    }
    if (const auto* formatLeaf =
            node->leaf.has_value() ? std::get_if<LayoutDateTimeFormatEditKey>(&node->leaf->focusKey) : nullptr;
        formatLeaf != nullptr) {
        return formatLeaf->widgetClass == WidgetClass::ClockTime ? L"Time Format" : L"Date Format";
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
    if (std::holds_alternative<LayoutMetricListOrderEditKey>(node->leaf->focusKey)) {
        return L"Choose the metric for each row, move rows up or down, remove rows, or add a new row.";
    }
    return L"Select a field to edit it here.";
}
