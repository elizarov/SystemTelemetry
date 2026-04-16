#include "dashboard_shell_ui.h"

#include <cmath>
#include <commdlg.h>
#include <cwchar>
#include <cwctype>
#include <sstream>

#include "app_diagnostics.h"
#include "dashboard_app.h"
#include "layout_edit_tree.h"
#include "layout_edit_service.h"
#include "layout_edit_tooltip.h"
#include "localization_catalog.h"

namespace {

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

constexpr double kPredefinedDisplayScales[] = {1.0, 1.5, 2.0, 2.5, 3.0};
constexpr double kScaleEpsilon = 0.0001;

bool AreScalesEqual(double left, double right) {
    return std::abs(left - right) < kScaleEpsilon;
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

std::wstring BuildLayoutEditMenuLabel(const std::wstring& subject) {
    return L"Edit " + subject + L" ...";
}

std::wstring BuildLayoutEditDialogTitle(const std::wstring& subject) {
    return L"Edit " + subject;
}

std::string LayoutGuideChildName(const LayoutNodeConfig& node) {
    return node.name.empty() ? "unknown" : node.name;
}

std::wstring BuildLayoutGuideEditLabel(const LayoutEditGuide& guide) {
    return guide.editCardId.empty() ? L"cards weights" : L"layout weights";
}

const LayoutNodeConfig* FindLayoutGuideNode(const AppConfig& config, const LayoutEditGuide& guide) {
    return FindGuideNode(config, LayoutEditHost::LayoutTarget::ForGuide(guide));
}

std::wstring BuildLayoutGuideItemLabel(
    const LayoutNodeConfig& node, size_t childIndex, LayoutGuideAxis axis, bool first) {
    const std::wstring side =
        axis == LayoutGuideAxis::Vertical ? (first ? L"Left" : L"Right") : (first ? L"Top" : L"Bottom");
    const std::wstring childName = WideFromUtf8(LayoutGuideChildName(node.children[childIndex]));
    return side + L" " + childName + L" weight:";
}

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

std::string FormatTraceColorHex(unsigned int color) {
    char buffer[16] = {};
    sprintf_s(buffer, "#%06X", color & 0xFFFFFFu);
    return buffer;
}

std::string ReadDialogControlTextUtf8(HWND hwnd, int controlId) {
    wchar_t buffer[256] = {};
    GetDlgItemTextW(hwnd, controlId, buffer, ARRAYSIZE(buffer));
    return Utf8FromWide(buffer);
}

const char* TreeNodeKindTraceName(LayoutEditTreeNodeKind kind) {
    switch (kind) {
        case LayoutEditTreeNodeKind::Section:
            return "section";
        case LayoutEditTreeNodeKind::Group:
            return "group";
        case LayoutEditTreeNodeKind::Container:
            return "container";
        case LayoutEditTreeNodeKind::Leaf:
            return "leaf";
    }
    return "unknown";
}

const char* ValueFormatTraceName(configschema::ValueFormat format) {
    switch (format) {
        case configschema::ValueFormat::String:
            return "string";
        case configschema::ValueFormat::Integer:
            return "integer";
        case configschema::ValueFormat::FloatingPoint:
            return "float";
        case configschema::ValueFormat::FontSpec:
            return "font";
        case configschema::ValueFormat::ColorHex:
            return "color";
    }
    return "unknown";
}

std::string JoinNodePath(const std::vector<size_t>& path) {
    std::ostringstream stream;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            stream << '.';
        }
        stream << path[i];
    }
    return stream.str();
}

std::string BuildTraceFocusKeyText(const LayoutEditTreeLeaf* leaf) {
    if (leaf == nullptr) {
        return "focus=\"none\"";
    }
    if (const auto* parameter = std::get_if<LayoutEditParameter>(&leaf->focusKey)) {
        const auto descriptor = FindLayoutEditTooltipDescriptor(*parameter);
        if (descriptor.has_value()) {
            return "focus=" + QuoteTraceText(descriptor->configKey);
        }
        return "focus=" + QuoteTraceText(GetLayoutEditParameterDisplayName(*parameter));
    }
    if (const auto* weightKey = std::get_if<LayoutWeightEditKey>(&leaf->focusKey)) {
        std::ostringstream stream;
        stream << "focus=" << QuoteTraceText(leaf->sectionName.empty() ? "weight" : leaf->sectionName + ".layout");
        stream << " edit_card=" << QuoteTraceText(weightKey->editCardId);
        stream << " node_path=" << QuoteTraceText(JoinNodePath(weightKey->nodePath));
        stream << " separator=" << weightKey->separatorIndex;
        return stream.str();
    }
    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&leaf->focusKey)) {
        return "focus=" + QuoteTraceText("[metrics] " + metricKey->metricId);
    }
    if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&leaf->focusKey)) {
        return "focus=" + QuoteTraceText("[card." + cardTitleKey->cardId + "] title");
    }
    return "focus=\"unknown\"";
}

std::string BuildTraceNodeText(const LayoutEditTreeNode* node) {
    if (node == nullptr) {
        return "node=\"none\"";
    }

    std::ostringstream stream;
    stream << "node_kind=" << QuoteTraceText(TreeNodeKindTraceName(node->kind));
    stream << " label=" << QuoteTraceText(node->label);
    stream << " location=" << QuoteTraceText(node->locationText);
    if (node->leaf.has_value()) {
        stream << " " << BuildTraceFocusKeyText(&*node->leaf);
        if (std::holds_alternative<LayoutMetricEditKey>(node->leaf->focusKey)) {
            stream << " value_format=\"metric\"";
        } else {
            stream << " value_format=" << QuoteTraceText(ValueFormatTraceName(node->leaf->valueFormat));
        }
    }
    return stream.str();
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

struct CustomScaleDialogState {
    double initialScale = 1.0;
    std::optional<double> result;
};

struct LayoutEditTreeItemBinding {
    const LayoutEditTreeNode* node = nullptr;
    HTREEITEM item = nullptr;
};

struct LayoutEditDialogState {
    DashboardShellUi* shellUi = nullptr;
    AppConfig originalConfig;
    LayoutEditTreeModel treeModel;
    std::optional<LayoutEditFocusKey> initialFocus;
    const LayoutEditTreeNode* selectedNode = nullptr;
    const LayoutEditTreeLeaf* selectedLeaf = nullptr;
    std::vector<LayoutEditTreeItemBinding> treeItems;
    COLORREF customColors[16]{};
    bool accepted = false;
    bool updatingControls = false;
};

struct ColorDialogControls {
    int labelId = 0;
    int editId = 0;
    int sliderId = 0;
    const char* channelName = "";
};

constexpr ColorDialogControls kColorDialogControls[] = {
    {IDC_LAYOUT_EDIT_COLOR_RED_LABEL, IDC_LAYOUT_EDIT_COLOR_RED_EDIT, IDC_LAYOUT_EDIT_COLOR_RED_SLIDER, "red"},
    {IDC_LAYOUT_EDIT_COLOR_GREEN_LABEL, IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT, IDC_LAYOUT_EDIT_COLOR_GREEN_SLIDER, "green"},
    {IDC_LAYOUT_EDIT_COLOR_BLUE_LABEL, IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT, IDC_LAYOUT_EDIT_COLOR_BLUE_SLIDER, "blue"},
};

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
    if (!red.has_value() || !green.has_value() || !blue.has_value()) {
        return std::nullopt;
    }
    return (*red << 16) | (*green << 8) | *blue;
}

std::string BuildColorDialogTraceValues(HWND hwnd) {
    std::ostringstream trace;
    trace << " red=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT))
          << " green=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT))
          << " blue=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT));
    return trace.str();
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

const LayoutNodeConfig* FindWeightEditNode(const AppConfig& config, const LayoutWeightEditKey& key) {
    LayoutEditHost::LayoutTarget target;
    target.editCardId = key.editCardId;
    target.nodePath = key.nodePath;
    return FindGuideNode(config, target);
}

const LayoutCardConfig* FindCardById(const AppConfig& config, std::string_view cardId) {
    const auto it = std::find_if(
        config.layout.cards.begin(), config.layout.cards.end(), [&](const auto& card) { return card.id == cardId; });
    return it != config.layout.cards.end() ? &(*it) : nullptr;
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

std::wstring BuildWeightEditorLabel(const LayoutEditTreeLeaf& leaf, bool first) {
    const std::wstring side =
        leaf.weightAxis == LayoutGuideAxis::Vertical ? (first ? L"Left" : L"Right") : (first ? L"Top" : L"Bottom");
    const std::wstring name = WideFromUtf8(first ? leaf.firstWeightName : leaf.secondWeightName);
    return side + L" " + name + L" weight:";
}

void ShowDialogControl(HWND hwnd, int controlId, bool show) {
    if (HWND control = GetDlgItem(hwnd, controlId); control != nullptr) {
        ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    }
}

std::optional<RECT> DialogControlRect(HWND hwnd, int controlId) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return std::nullopt;
    }
    RECT rect{};
    if (!GetWindowRect(control, &rect)) {
        return std::nullopt;
    }
    MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}

void ResizeDialogControlPreservingCenter(HWND hwnd, int controlId, int targetHeight) {
    const auto rect = DialogControlRect(hwnd, controlId);
    if (!rect.has_value()) {
        return;
    }

    const int width = rect->right - rect->left;
    const int centerY = (rect->top + rect->bottom) / 2;
    const int top = centerY - (targetHeight / 2);
    SetWindowPos(
        GetDlgItem(hwnd, controlId), nullptr, rect->left, top, width, targetHeight, SWP_NOZORDER | SWP_NOACTIVATE);
}

void CenterDialogLabelToControl(HWND hwnd, int labelId, int controlId) {
    const auto labelRect = DialogControlRect(hwnd, labelId);
    const auto controlRect = DialogControlRect(hwnd, controlId);
    if (!labelRect.has_value() || !controlRect.has_value()) {
        return;
    }

    const int width = labelRect->right - labelRect->left;
    const int height = labelRect->bottom - labelRect->top;
    const int centerY = (controlRect->top + controlRect->bottom) / 2;
    const int top = centerY - (height / 2);
    SetWindowPos(
        GetDlgItem(hwnd, labelId), nullptr, labelRect->left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void AlignFontEditorControls(HWND hwnd) {
    const auto comboRect = DialogControlRect(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT);
    if (!comboRect.has_value()) {
        return;
    }

    const int comboHeight = comboRect->bottom - comboRect->top;
    ResizeDialogControlPreservingCenter(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, comboHeight);
    ResizeDialogControlPreservingCenter(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, comboHeight);

    CenterDialogLabelToControl(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_LABEL, IDC_LAYOUT_EDIT_FONT_FACE_EDIT);
    CenterDialogLabelToControl(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_LABEL, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT);
    CenterDialogLabelToControl(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT);
}

void ShowLayoutEditEditors(
    HWND hwnd, bool showNumeric, bool showFont, bool showColor, bool showWeights, bool showMetric) {
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, showNumeric);

    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_LABEL, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_LABEL, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, showFont);

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
}

std::string BuildMetricDialogTraceValues(HWND hwnd) {
    std::ostringstream trace;
    trace << " style=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE))
          << " scale=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT))
          << " unit=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT))
          << " label=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT));
    return trace.str();
}

void RestoreLayoutEditDialog(LayoutEditDialogState* state) {
    if (state != nullptr && !state->accepted && state->shellUi != nullptr) {
        state->shellUi->RestoreConfigSnapshot(state->originalConfig);
    }
}

void SetLayoutEditDescription(HWND hwnd, const LayoutEditTreeNode* node) {
    if (node == nullptr) {
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_LOCATION, L"");
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, L"");
        return;
    }

    const std::wstring location = WideFromUtf8(node->locationText);
    const std::wstring description = WideFromUtf8(FindLocalizedText(node->descriptionKey));
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_LOCATION, location.c_str());
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, description.c_str());
}

void PopulateLayoutEditSelection(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr) {
        return;
    }

    state->updatingControls = true;
    SetLayoutEditDescription(hwnd, state->selectedNode);
    if (state->selectedLeaf == nullptr) {
        ShowLayoutEditEditors(hwnd, false, false, false, false, false);
        state->updatingControls = false;
        state->shellUi->TraceLayoutEditDialogEvent(
            "layout_edit_dialog:populate_selection", BuildTraceNodeText(state->selectedNode) + " editor=\"none\"");
        return;
    }

    if (const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey)) {
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec) {
            const auto font = FindLayoutEditTooltipFontValue(state->shellUi->CurrentConfig(), *parameter);
            PopulateFontFaceComboBox(hwnd, font.has_value() && *font != nullptr ? WideFromUtf8((**font).face) : L"");
            SetDlgItemTextW(hwnd,
                IDC_LAYOUT_EDIT_FONT_SIZE_EDIT,
                font.has_value() && *font != nullptr ? WideFromUtf8(std::to_string((**font).size)).c_str() : L"");
            SetDlgItemTextW(hwnd,
                IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT,
                font.has_value() && *font != nullptr ? WideFromUtf8(std::to_string((**font).weight)).c_str() : L"");
            ShowLayoutEditEditors(hwnd, false, true, false, false, false);
            std::ostringstream trace;
            trace << BuildTraceNodeText(state->selectedNode) << " editor=\"font\""
                  << " face=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT))
                  << " size=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT))
                  << " weight=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT));
            state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
        } else if (state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex) {
            const auto value = FindLayoutEditParameterColorValue(state->shellUi->CurrentConfig(), *parameter);
            const unsigned int color = value.value_or(0);
            SetColorDialogChannel(hwnd, kColorDialogControls[0], (color >> 16) & 0xFFu);
            SetColorDialogChannel(hwnd, kColorDialogControls[1], (color >> 8) & 0xFFu);
            SetColorDialogChannel(hwnd, kColorDialogControls[2], color & 0xFFu);
            ShowLayoutEditEditors(hwnd, false, false, true, false, false);
            std::ostringstream trace;
            trace << BuildTraceNodeText(state->selectedNode) << " editor=\"color\"" << BuildColorDialogTraceValues(hwnd)
                  << " config_value=" << QuoteTraceText(value.has_value() ? FormatTraceColorHex(*value) : "none");
            state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
        } else {
            const auto value = FindLayoutEditParameterNumericValue(state->shellUi->CurrentConfig(), *parameter);
            const std::wstring text =
                value.has_value() ? WideFromUtf8(FormatLayoutEditTooltipValue(*value, state->selectedLeaf->valueFormat))
                                  : L"";
            SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, text.c_str());
            ShowLayoutEditEditors(hwnd, true, false, false, false, false);
            std::ostringstream trace;
            trace << BuildTraceNodeText(state->selectedNode) << " editor=\"numeric\""
                  << " text=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT));
            state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
        }
    } else if (const auto* weightKey = std::get_if<LayoutWeightEditKey>(&state->selectedLeaf->focusKey)) {
        const auto values = FindWeightEditValues(state->shellUi->CurrentConfig(), *weightKey);
        SetDlgItemTextW(
            hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_LABEL, BuildWeightEditorLabel(*state->selectedLeaf, true).c_str());
        SetDlgItemTextW(
            hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL, BuildWeightEditorLabel(*state->selectedLeaf, false).c_str());
        SetDlgItemTextW(hwnd,
            IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT,
            values.has_value() ? WideFromUtf8(std::to_string(values->first)).c_str() : L"");
        SetDlgItemTextW(hwnd,
            IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT,
            values.has_value() ? WideFromUtf8(std::to_string(values->second)).c_str() : L"");
        ShowLayoutEditEditors(hwnd, false, false, false, true, false);
        std::ostringstream trace;
        trace << BuildTraceNodeText(state->selectedNode) << " editor=\"weights\""
              << " first=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT))
              << " second=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT));
        state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
    } else if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&state->selectedLeaf->focusKey)) {
        const std::wstring text =
            WideFromUtf8(FindCardTitleValue(state->shellUi->CurrentConfig(), *cardTitleKey).value_or(""));
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, text.c_str());
        ShowLayoutEditEditors(hwnd, true, false, false, false, false);
        std::ostringstream trace;
        trace << BuildTraceNodeText(state->selectedNode) << " editor=\"text\""
              << " text=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT));
        state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
    } else if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey)) {
        const MetricDefinitionConfig* definition =
            FindMetricDefinition(state->shellUi->CurrentConfig().metrics, metricKey->metricId);
        SetDlgItemTextW(hwnd,
            IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE,
            definition != nullptr ? WideFromUtf8(std::string(MetricDisplayStyleName(definition->style))).c_str() : L"");
        const bool scaleEditable =
            definition != nullptr && !definition->telemetryScale && definition->style != MetricDisplayStyle::LabelOnly;
        const bool unitEditable = definition != nullptr && definition->style != MetricDisplayStyle::LabelOnly;
        SetDlgItemTextW(hwnd,
            IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT,
            definition == nullptr        ? L""
            : definition->telemetryScale ? L"*"
            : definition->style == MetricDisplayStyle::LabelOnly
                ? L""
                : WideFromUtf8(
                      FormatLayoutEditTooltipValue(definition->scale, configschema::ValueFormat::FloatingPoint))
                      .c_str());
        SetDlgItemTextW(hwnd,
            IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT,
            definition != nullptr && definition->style != MetricDisplayStyle::LabelOnly
                ? WideFromUtf8(definition->unit).c_str()
                : L"");
        SetDlgItemTextW(hwnd,
            IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT,
            definition != nullptr ? WideFromUtf8(definition->label).c_str() : L"");
        EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT), scaleEditable ? TRUE : FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT), unitEditable ? TRUE : FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT), definition != nullptr ? TRUE : FALSE);
        ShowLayoutEditEditors(hwnd, false, false, false, false, true);
        std::ostringstream trace;
        trace << BuildTraceNodeText(state->selectedNode) << " editor=\"metric\"" << BuildMetricDialogTraceValues(hwnd)
              << " scale_editable=" << QuoteTraceText(scaleEditable ? "true" : "false")
              << " unit_editable=" << QuoteTraceText(unitEditable ? "true" : "false");
        state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
    } else {
        ShowLayoutEditEditors(hwnd, false, false, false, false, false);
        state->shellUi->TraceLayoutEditDialogEvent(
            "layout_edit_dialog:populate_selection", BuildTraceNodeText(state->selectedNode) + " editor=\"none\"");
    }

    state->updatingControls = false;
}

bool PreviewSelectedValue(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr || state->updatingControls) {
        return false;
    }
    const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey);
    const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&state->selectedLeaf->focusKey);
    if (parameter == nullptr && cardTitleKey == nullptr) {
        return false;
    }

    wchar_t buffer[256] = {};
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, buffer, ARRAYSIZE(buffer));
    if (cardTitleKey != nullptr) {
        const std::string title = Utf8FromWide(buffer);
        const bool applied = state->shellUi->ApplyCardTitlePreview(*cardTitleKey, title);
        std::ostringstream trace;
        trace << BuildTraceNodeText(state->selectedNode) << " raw=" << QuoteTraceText(title)
              << " parsed=" << QuoteTraceText(title) << " applied=" << QuoteTraceText(applied ? "true" : "false");
        state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:preview_value", trace.str());
        return applied;
    }
    if (state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec ||
        state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex ||
        state->selectedLeaf->valueFormat == configschema::ValueFormat::String) {
        return false;
    }

    std::optional<double> value;
    if (state->selectedLeaf->valueFormat == configschema::ValueFormat::Integer) {
        if (const auto parsed = TryParseDialogInteger(buffer); parsed.has_value()) {
            value = static_cast<double>(*parsed);
        }
    } else {
        value = TryParseDialogDouble(buffer);
    }
    const bool applied = value.has_value() && state->shellUi->ApplyParameterPreview(*parameter, *value);
    std::ostringstream trace;
    trace << BuildTraceNodeText(state->selectedNode) << " raw=" << QuoteTraceText(Utf8FromWide(buffer)) << " parsed="
          << QuoteTraceText(
                 value.has_value() ? FormatLayoutEditTooltipValue(*value, state->selectedLeaf->valueFormat) : "invalid")
          << " applied=" << QuoteTraceText(applied ? "true" : "false");
    state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:preview_value", trace.str());
    return applied;
}

bool PreviewSelectedFont(LayoutEditDialogState* state, HWND hwnd, UINT notificationCode = 0) {
    if (state == nullptr || state->selectedLeaf == nullptr || state->updatingControls) {
        return false;
    }
    const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey);
    if (parameter == nullptr || state->selectedLeaf->valueFormat != configschema::ValueFormat::FontSpec) {
        return false;
    }

    wchar_t sizeBuffer[64] = {};
    wchar_t weightBuffer[64] = {};
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, sizeBuffer, ARRAYSIZE(sizeBuffer));
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, weightBuffer, ARRAYSIZE(weightBuffer));
    const std::wstring faceText = ReadFontDialogFaceText(hwnd, notificationCode);
    const std::optional<int> size = TryParseDialogInteger(sizeBuffer);
    const std::optional<int> weight = TryParseDialogInteger(weightBuffer);
    if (faceText.empty() || !size.has_value() || *size < 1 || !weight.has_value()) {
        return false;
    }

    const UiFontConfig font{Utf8FromWide(faceText), *size, *weight};
    const bool applied = state->shellUi->ApplyFontPreview(*parameter, font);
    std::ostringstream trace;
    trace << BuildTraceNodeText(state->selectedNode) << " face=" << QuoteTraceText(font.face)
          << " size=" << QuoteTraceText(std::to_string(font.size))
          << " weight=" << QuoteTraceText(std::to_string(font.weight))
          << " applied=" << QuoteTraceText(applied ? "true" : "false");
    state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:preview_font", trace.str());
    return applied;
}

bool PreviewSelectedColor(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr || state->updatingControls) {
        return false;
    }
    const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey);
    if (parameter == nullptr || state->selectedLeaf->valueFormat != configschema::ValueFormat::ColorHex) {
        return false;
    }

    const auto color = ReadColorDialogValue(hwnd);
    const bool applied = color.has_value() && state->shellUi->ApplyColorPreview(*parameter, *color);
    std::ostringstream trace;
    trace << BuildTraceNodeText(state->selectedNode) << BuildColorDialogTraceValues(hwnd)
          << " parsed=" << QuoteTraceText(color.has_value() ? FormatTraceColorHex(*color) : "invalid")
          << " applied=" << QuoteTraceText(applied ? "true" : "false");
    state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:preview_color", trace.str());
    return applied;
}

bool SetSelectedDialogColor(LayoutEditDialogState* state, HWND hwnd, unsigned int color) {
    if (state == nullptr || state->selectedLeaf == nullptr) {
        return false;
    }
    const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey);
    if (parameter == nullptr || state->selectedLeaf->valueFormat != configschema::ValueFormat::ColorHex) {
        return false;
    }

    state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:picker_apply_begin",
        BuildTraceNodeText(state->selectedNode) + " picked=" + QuoteTraceText(FormatTraceColorHex(color)));
    const bool applied = state->shellUi->ApplyColorPreview(*parameter, color);
    if (!applied) {
        state->shellUi->TraceLayoutEditDialogEvent(
            "layout_edit_dialog:picker_apply_end", BuildTraceNodeText(state->selectedNode) + " applied=\"false\"");
        return false;
    }

    PopulateLayoutEditSelection(state, hwnd);
    SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT));
    SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT, EM_SETSEL, 0, -1);
    state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:picker_apply_end",
        BuildTraceNodeText(state->selectedNode) + " applied=\"true\"" + BuildColorDialogTraceValues(hwnd) +
            " config_value=" +
            QuoteTraceText(FormatTraceColorHex(
                FindLayoutEditParameterColorValue(state->shellUi->CurrentConfig(), *parameter).value_or(0))));
    return true;
}

bool PreviewSelectedWeights(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr || state->updatingControls) {
        return false;
    }
    const auto* key = std::get_if<LayoutWeightEditKey>(&state->selectedLeaf->focusKey);
    if (key == nullptr) {
        return false;
    }

    wchar_t firstBuffer[64] = {};
    wchar_t secondBuffer[64] = {};
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, firstBuffer, ARRAYSIZE(firstBuffer));
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT, secondBuffer, ARRAYSIZE(secondBuffer));
    const std::optional<int> first = TryParseDialogInteger(firstBuffer);
    const std::optional<int> second = TryParseDialogInteger(secondBuffer);
    if (!first.has_value() || !second.has_value() || *first < 1 || *second < 1) {
        return false;
    }

    const bool applied = state->shellUi->ApplyWeightPreview(*key, *first, *second);
    std::ostringstream trace;
    trace << BuildTraceNodeText(state->selectedNode) << " first=" << QuoteTraceText(std::to_string(*first))
          << " second=" << QuoteTraceText(std::to_string(*second))
          << " applied=" << QuoteTraceText(applied ? "true" : "false");
    state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:preview_weights", trace.str());
    return applied;
}

bool PreviewSelectedMetric(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr || state->updatingControls) {
        return false;
    }
    const auto* key = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey);
    if (key == nullptr) {
        return false;
    }

    const MetricDefinitionConfig* definition =
        FindMetricDefinition(state->shellUi->CurrentConfig().metrics, key->metricId);
    if (definition == nullptr) {
        return false;
    }

    std::optional<double> scale;
    if (!definition->telemetryScale && definition->style != MetricDisplayStyle::LabelOnly) {
        wchar_t scaleBuffer[128] = {};
        GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT, scaleBuffer, ARRAYSIZE(scaleBuffer));
        scale = TryParseDialogDouble(scaleBuffer);
        if (!scale.has_value() || *scale <= 0.0) {
            return false;
        }
    }

    wchar_t unitBuffer[256] = {};
    wchar_t labelBuffer[256] = {};
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT, unitBuffer, ARRAYSIZE(unitBuffer));
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT, labelBuffer, ARRAYSIZE(labelBuffer));
    const std::string unit =
        definition->style == MetricDisplayStyle::LabelOnly ? std::string() : Utf8FromWide(unitBuffer);
    const std::string label = Utf8FromWide(labelBuffer);
    const bool applied = state->shellUi->ApplyMetricPreview(*key, scale, unit, label);
    std::ostringstream trace;
    trace << BuildTraceNodeText(state->selectedNode) << BuildMetricDialogTraceValues(hwnd) << " parsed_scale="
          << QuoteTraceText(scale.has_value()
                                ? FormatLayoutEditTooltipValue(*scale, configschema::ValueFormat::FloatingPoint)
                                : "disabled")
          << " applied=" << QuoteTraceText(applied ? "true" : "false");
    state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:preview_metric", trace.str());
    return applied;
}

void InsertLayoutEditTreeNodes(
    LayoutEditDialogState* state, HWND tree, const std::vector<LayoutEditTreeNode>& nodes, HTREEITEM parent) {
    for (const auto& node : nodes) {
        TVINSERTSTRUCTW insert{};
        insert.hParent = parent;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        std::wstring label = WideFromUtf8(node.label);
        insert.item.pszText = label.data();
        insert.item.lParam = reinterpret_cast<LPARAM>(&node);
        HTREEITEM item = TreeView_InsertItem(tree, &insert);
        state->treeItems.push_back(LayoutEditTreeItemBinding{&node, item});
        InsertLayoutEditTreeNodes(state, tree, node.children, item);
        if (node.initiallyExpanded) {
            TreeView_Expand(tree, item, TVE_EXPAND);
        }
    }
}

const LayoutEditTreeNode* TreeNodeFromItem(HWND tree, HTREEITEM item) {
    if (item == nullptr) {
        return nullptr;
    }

    TVITEMW treeItem{};
    treeItem.mask = TVIF_PARAM;
    treeItem.hItem = item;
    if (!TreeView_GetItem(tree, &treeItem)) {
        return nullptr;
    }
    return reinterpret_cast<const LayoutEditTreeNode*>(treeItem.lParam);
}

HTREEITEM FindTreeItemByFocusKey(LayoutEditDialogState* state, const LayoutEditFocusKey& focusKey) {
    for (const auto& binding : state->treeItems) {
        if (binding.node != nullptr && binding.node->leaf.has_value() &&
            MatchesLayoutEditFocusKey(binding.node->leaf->focusKey, focusKey)) {
            return binding.item;
        }
    }
    return nullptr;
}

HTREEITEM FindFirstLeafTreeItem(const LayoutEditDialogState& state) {
    for (const auto& binding : state.treeItems) {
        if (binding.node != nullptr && binding.node->leaf.has_value()) {
            return binding.item;
        }
    }
    return nullptr;
}

void ExpandTreeAncestors(HWND tree, HTREEITEM item) {
    while (item != nullptr) {
        TreeView_Expand(tree, item, TVE_EXPAND);
        item = TreeView_GetParent(tree, item);
    }
}

std::optional<LayoutEditSelectionHighlight> SelectionHighlightForTreeNode(const LayoutEditTreeNode* node) {
    return node != nullptr ? node->selectionHighlight : std::nullopt;
}

void SelectLayoutEditTreeItem(LayoutEditDialogState* state, HWND hwnd, HTREEITEM item) {
    const LayoutEditTreeNode* node = TreeNodeFromItem(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE), item);
    state->selectedNode = node;
    state->selectedLeaf = node != nullptr && node->leaf.has_value() ? &(*node->leaf) : nullptr;
    state->shellUi->SetLayoutEditTreeSelectionHighlight(SelectionHighlightForTreeNode(node));
    state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:tree_select", BuildTraceNodeText(node));
    PopulateLayoutEditSelection(state, hwnd);
}

bool ValidateActiveLayoutEditSelection(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr) {
        return true;
    }

    if (const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey)) {
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec) {
            wchar_t faceBuffer[256] = {};
            wchar_t sizeBuffer[64] = {};
            wchar_t weightBuffer[64] = {};
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT, faceBuffer, ARRAYSIZE(faceBuffer));
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, sizeBuffer, ARRAYSIZE(sizeBuffer));
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, weightBuffer, ARRAYSIZE(weightBuffer));
            const std::optional<int> size = TryParseDialogInteger(sizeBuffer);
            const std::optional<int> weight = TryParseDialogInteger(weightBuffer);
            const std::wstring faceText(faceBuffer);
            if (faceText.empty()) {
                MessageBoxW(hwnd, L"Enter a font name.", L"Edit Configuration", MB_ICONERROR);
                SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT));
                return false;
            }
            if (!size.has_value() || *size < 1) {
                MessageBoxW(hwnd, L"Enter a font size of 1 or greater.", L"Edit Configuration", MB_ICONERROR);
                SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT));
                SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, EM_SETSEL, 0, -1);
                return false;
            }
            if (!weight.has_value()) {
                MessageBoxW(hwnd, L"Enter an integer font weight.", L"Edit Configuration", MB_ICONERROR);
                SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT));
                SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, EM_SETSEL, 0, -1);
                return false;
            }
            return state->shellUi->ApplyFontPreview(*parameter, UiFontConfig{Utf8FromWide(faceText), *size, *weight});
        }
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex) {
            const auto color = ReadColorDialogValue(hwnd);
            if (!color.has_value()) {
                MessageBoxW(hwnd,
                    L"Enter each RGB channel as a whole number between 0 and 255.",
                    L"Edit Configuration",
                    MB_ICONERROR);
                SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT));
                SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT, EM_SETSEL, 0, -1);
                return false;
            }
            return state->shellUi->ApplyColorPreview(*parameter, *color);
        }

        wchar_t valueBuffer[128] = {};
        GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, valueBuffer, ARRAYSIZE(valueBuffer));
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::Integer) {
            const std::optional<int> value = TryParseDialogInteger(valueBuffer);
            if (!value.has_value()) {
                MessageBoxW(hwnd, L"Enter a whole number.", L"Edit Configuration", MB_ICONERROR);
                SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT));
                SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, EM_SETSEL, 0, -1);
                return false;
            }
            return state->shellUi->ApplyParameterPreview(*parameter, static_cast<double>(*value));
        }

        const std::optional<double> value = TryParseDialogDouble(valueBuffer);
        if (!value.has_value()) {
            MessageBoxW(hwnd, L"Enter a valid number.", L"Edit Configuration", MB_ICONERROR);
            SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT));
            SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, EM_SETSEL, 0, -1);
            return false;
        }
        return state->shellUi->ApplyParameterPreview(*parameter, *value);
    }

    if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&state->selectedLeaf->focusKey)) {
        wchar_t titleBuffer[256] = {};
        GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, titleBuffer, ARRAYSIZE(titleBuffer));
        return state->shellUi->ApplyCardTitlePreview(*cardTitleKey, Utf8FromWide(titleBuffer));
    }

    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey)) {
        const MetricDefinitionConfig* definition =
            FindMetricDefinition(state->shellUi->CurrentConfig().metrics, metricKey->metricId);
        if (definition == nullptr) {
            return false;
        }

        std::optional<double> scale;
        if (!definition->telemetryScale && definition->style != MetricDisplayStyle::LabelOnly) {
            wchar_t scaleBuffer[128] = {};
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT, scaleBuffer, ARRAYSIZE(scaleBuffer));
            scale = TryParseDialogDouble(scaleBuffer);
            if (!scale.has_value() || *scale <= 0.0) {
                MessageBoxW(hwnd, L"Enter a metric scale greater than 0.", L"Edit Configuration", MB_ICONERROR);
                SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT));
                SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT, EM_SETSEL, 0, -1);
                return false;
            }
        }

        wchar_t unitBuffer[256] = {};
        wchar_t labelBuffer[256] = {};
        GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT, unitBuffer, ARRAYSIZE(unitBuffer));
        GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT, labelBuffer, ARRAYSIZE(labelBuffer));
        const std::string unit =
            definition->style == MetricDisplayStyle::LabelOnly ? std::string() : Utf8FromWide(unitBuffer);
        const std::string label = Utf8FromWide(labelBuffer);
        return state->shellUi->ApplyMetricPreview(*metricKey, scale, unit, label);
    }

    wchar_t firstBuffer[64] = {};
    wchar_t secondBuffer[64] = {};
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, firstBuffer, ARRAYSIZE(firstBuffer));
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT, secondBuffer, ARRAYSIZE(secondBuffer));
    const std::optional<int> first = TryParseDialogInteger(firstBuffer);
    const std::optional<int> second = TryParseDialogInteger(secondBuffer);
    if (!first.has_value() || !second.has_value() || *first < 1 || *second < 1) {
        MessageBoxW(
            hwnd, L"Enter positive integer weights for both neighboring items.", L"Edit Configuration", MB_ICONERROR);
        return false;
    }
    return PreviewSelectedWeights(state, hwnd);
}

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

INT_PTR CALLBACK LayoutEditDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<LayoutEditDialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<LayoutEditDialogState*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            SetWindowTextW(hwnd, L"Edit Configuration");
            state->shellUi->SetLayoutEditTreeSelectionHighlight(std::nullopt);
            ConfigureColorSliders(hwnd);
            AlignFontEditorControls(hwnd);
            HWND tree = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE);
            InsertLayoutEditTreeNodes(state, tree, state->treeModel.roots, TVI_ROOT);
            HTREEITEM selectedItem =
                state->initialFocus.has_value() ? FindTreeItemByFocusKey(state, *state->initialFocus) : nullptr;
            if (selectedItem == nullptr) {
                selectedItem = FindFirstLeafTreeItem(*state);
            }
            if (selectedItem != nullptr) {
                ExpandTreeAncestors(tree, selectedItem);
                TreeView_SelectItem(tree, selectedItem);
                TreeView_EnsureVisible(tree, selectedItem);
                SelectLayoutEditTreeItem(state, hwnd, selectedItem);
            } else {
                ShowLayoutEditEditors(hwnd, false, false, false, false, false);
                SetLayoutEditDescription(hwnd, nullptr);
            }
            return TRUE;
        }
        case WM_NOTIFY: {
            const auto* notify = reinterpret_cast<NMHDR*>(lParam);
            if (notify != nullptr && notify->idFrom == IDC_LAYOUT_EDIT_TREE && notify->code == TVN_SELCHANGEDW) {
                const auto* treeView = reinterpret_cast<NMTREEVIEWW*>(lParam);
                SelectLayoutEditTreeItem(state, hwnd, treeView->itemNew.hItem);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_LAYOUT_EDIT_VALUE_EDIT && HIWORD(wParam) == EN_CHANGE) {
                PreviewSelectedValue(state, hwnd);
                return TRUE;
            }
            if ((LOWORD(wParam) == IDC_LAYOUT_EDIT_FONT_FACE_EDIT &&
                    (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_EDITCHANGE)) ||
                ((LOWORD(wParam) == IDC_LAYOUT_EDIT_FONT_SIZE_EDIT ||
                     LOWORD(wParam) == IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT) &&
                    HIWORD(wParam) == EN_CHANGE)) {
                PreviewSelectedFont(state, hwnd, HIWORD(wParam));
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
                        SendDlgItemMessageW(hwnd, channel->sliderId, TBM_SETPOS, TRUE, *value);
                    }
                }
                PreviewSelectedColor(state, hwnd);
                return TRUE;
            }
            if ((LOWORD(wParam) == IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT) &&
                HIWORD(wParam) == EN_CHANGE) {
                PreviewSelectedWeights(state, hwnd);
                return TRUE;
            }
            if ((LOWORD(wParam) == IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT) &&
                HIWORD(wParam) == EN_CHANGE) {
                PreviewSelectedMetric(state, hwnd);
                return TRUE;
            }
            switch (LOWORD(wParam)) {
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
                        FindLayoutEditParameterColorValue(state->shellUi->CurrentConfig(), parameter).value_or(0);
                    CHOOSECOLORW chooseColor{};
                    chooseColor.lStructSize = sizeof(chooseColor);
                    chooseColor.hwndOwner = hwnd;
                    chooseColor.rgbResult =
                        RGB((currentColor >> 16) & 0xFFu, (currentColor >> 8) & 0xFFu, currentColor & 0xFFu);
                    chooseColor.lpCustColors = state->customColors;
                    chooseColor.Flags = CC_FULLOPEN | CC_RGBINIT;
                    state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:picker_open",
                        BuildTraceNodeText(state->selectedNode) +
                            " current=" + QuoteTraceText(FormatTraceColorHex(currentColor)));
                    if (ChooseColorW(&chooseColor) == TRUE) {
                        const unsigned int nextColor = (GetRValue(chooseColor.rgbResult) << 16) |
                                                       (GetGValue(chooseColor.rgbResult) << 8) |
                                                       GetBValue(chooseColor.rgbResult);
                        state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:picker_return",
                            BuildTraceNodeText(state->selectedNode) +
                                " accepted=\"true\" chosen=" + QuoteTraceText(FormatTraceColorHex(nextColor)));
                        SetSelectedDialogColor(state, hwnd, nextColor);
                    } else {
                        state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:picker_return",
                            BuildTraceNodeText(state->selectedNode) + " accepted=\"false\"");
                    }
                    return TRUE;
                }
                case IDOK:
                    if (!ValidateActiveLayoutEditSelection(state, hwnd)) {
                        return TRUE;
                    }
                    state->shellUi->SetLayoutEditTreeSelectionHighlight(std::nullopt);
                    state->accepted = true;
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                case IDCANCEL:
                    state->shellUi->SetLayoutEditTreeSelectionHighlight(std::nullopt);
                    RestoreLayoutEditDialog(state);
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
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
                        state->updatingControls = false;
                        PreviewSelectedColor(state, hwnd);
                    }
                    return TRUE;
                }
            }
            break;
        case WM_CLOSE:
            state->shellUi->SetLayoutEditTreeSelectionHighlight(std::nullopt);
            RestoreLayoutEditDialog(state);
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

}  // namespace

DashboardShellUi::DashboardShellUi(DashboardApp& app) : app_(app) {}

DashboardShellUi::~DashboardShellUi() = default;

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
    ++app_.layoutEditModalUiDepth_;
    if (app_.layoutEditModalUiDepth_ == 1 && app_.controller_.State().isEditingLayout) {
        app_.layoutEditController_.CancelInteraction();
    }
    app_.HideLayoutEditTooltip();
    app_.layoutEditMouseTracking_ = false;
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

void DashboardShellUi::EndLayoutEditModalUi() {
    if (app_.layoutEditModalUiDepth_ <= 0) {
        app_.layoutEditModalUiDepth_ = 0;
        return;
    }
    --app_.layoutEditModalUiDepth_;
    if (app_.layoutEditModalUiDepth_ == 0) {
        app_.UpdateLayoutEditTooltip();
    }
}

const AppConfig& DashboardShellUi::CurrentConfig() const {
    return app_.controller_.State().config;
}

void DashboardShellUi::RestoreConfigSnapshot(const AppConfig& config) {
    app_.controller_.ApplyConfigSnapshot(app_, config);
}

bool DashboardShellUi::ApplyParameterPreview(DashboardRenderer::LayoutEditParameter parameter, double value) {
    return app_.ApplyLayoutEditValue(parameter, value);
}

bool DashboardShellUi::ApplyFontPreview(DashboardRenderer::LayoutEditParameter parameter, const UiFontConfig& value) {
    return app_.controller_.ApplyLayoutEditFont(app_, parameter, value);
}

bool DashboardShellUi::ApplyColorPreview(DashboardRenderer::LayoutEditParameter parameter, unsigned int value) {
    return app_.controller_.ApplyLayoutEditColor(app_, parameter, value);
}

bool DashboardShellUi::ApplyMetricPreview(const LayoutMetricEditKey& key,
    const std::optional<double>& scale,
    const std::string& unit,
    const std::string& label) {
    AppConfig updatedConfig = CurrentConfig();
    MetricDefinitionConfig* definition = FindMetricDefinition(updatedConfig.metrics, key.metricId);
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

    LayoutEditHost::LayoutTarget target;
    target.editCardId = key.editCardId;
    target.nodePath = key.nodePath;
    return app_.ApplyLayoutGuideWeights(target, weights);
}

void DashboardShellUi::SetLayoutEditTreeSelectionHighlight(
    const std::optional<LayoutEditSelectionHighlight>& highlight) {
    app_.rendererEditOverlayState_.selectedTreeHighlight = highlight;
    app_.InvalidateShell();
}

bool DashboardShellUi::PromptAndApplyLayoutEditTarget(const LayoutEditController::TooltipTarget& target) {
    const auto focusKey = TooltipPayloadFocusKey(target.payload);
    if (!focusKey.has_value()) {
        return false;
    }

    std::string initialFocusTrace = "weight";
    if (const auto* parameter = std::get_if<LayoutEditParameter>(&*focusKey)) {
        initialFocusTrace =
            FindLayoutEditTooltipDescriptor(*parameter).value_or(LayoutEditTooltipDescriptor{}).configKey;
    } else if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&*focusKey)) {
        initialFocusTrace = "[metrics] " + metricKey->metricId;
    } else if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&*focusKey)) {
        initialFocusTrace = "[card." + cardTitleKey->cardId + "] title";
    }

    LayoutEditDialogState state;
    state.shellUi = this;
    state.originalConfig = app_.controller_.State().config;
    state.treeModel = BuildLayoutEditTreeModel(app_.controller_.State().config);
    state.initialFocus = focusKey;
    TraceLayoutEditDialogEvent("layout_edit_dialog:open", "initial_focus=" + QuoteTraceText(initialFocusTrace));

    DashboardShellUiModalScope scopedModalUi(*this);
    const INT_PTR result = DialogBoxParamW(app_.instance_,
        MAKEINTRESOURCEW(IDD_LAYOUT_EDIT_CONFIGURATION),
        app_.hwnd_,
        LayoutEditDialogProc,
        reinterpret_cast<LPARAM>(&state));
    SetLayoutEditTreeSelectionHighlight(std::nullopt);
    TraceLayoutEditDialogEvent(
        "layout_edit_dialog:close", std::string("accepted=") + QuoteTraceText(result == IDOK ? "true" : "false"));
    return result == IDOK;
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
            if (state.isEditingLayout) {
                app_.controller_.StopLayoutEditMode(
                    app_, app_.layoutEditController_, app_.diagnosticsOptions_.editLayout);
            } else {
                app_.controller_.StartLayoutEditMode(app_, app_.layoutEditController_);
            }
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
            if (!app_.controller_.ReloadConfigFromDisk(app_, app_.diagnosticsOptions_, app_.layoutEditController_)) {
                MessageBoxW(app_.hwnd_, L"Failed to reload config.ini.", L"System Telemetry", MB_ICONERROR);
            }
            break;
        case kCommandSaveConfig:
            app_.controller_.UpdateConfigFromCurrentPlacement(app_);
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
            DestroyWindow(app_.hwnd_);
            break;
        default:
            if (selected >= kCommandLayoutBase && selected <= kCommandLayoutMax) {
                const auto it = std::find_if(state.layoutMenuOptions.begin(),
                    state.layoutMenuOptions.end(),
                    [selected](const LayoutMenuOption& option) { return option.commandId == selected; });
                if (it != state.layoutMenuOptions.end() &&
                    !app_.controller_.SwitchLayout(
                        app_, it->name, app_.layoutEditController_, app_.diagnosticsOptions_.editLayout)) {
                    MessageBoxW(app_.hwnd_, L"Failed to switch layout.", L"System Telemetry", MB_ICONERROR);
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
                    app_.controller_.ConfigureDisplay(app_, *it);
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
    for (size_t i = 0; i < state.config.layouts.size() && (kCommandLayoutBase + i) <= kCommandLayoutMax; ++i) {
        LayoutMenuOption option;
        option.commandId = kCommandLayoutBase + static_cast<UINT>(i);
        option.name = state.config.layouts[i].name;
        option.description = state.config.layouts[i].description;
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
            const UINT flags = MF_STRING | (option.layoutFits ? MF_ENABLED : MF_GRAYED);
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
            const auto focusKey = TooltipPayloadFocusKey(layoutEditTarget->payload);
            if (focusKey.has_value() && std::holds_alternative<LayoutMetricEditKey>(*focusKey)) {
                label = BuildLayoutEditMenuLabel(
                    WideFromUtf8(std::get<LayoutMetricEditKey>(*focusKey).metricId + " metric"));
            } else if (focusKey.has_value() && std::holds_alternative<LayoutCardTitleEditKey>(*focusKey)) {
                label = BuildLayoutEditMenuLabel(L"card title");
            } else {
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
