#include "dashboard_shell_ui.h"

#include <algorithm>
#include <cmath>
#include <commdlg.h>
#include <cwchar>
#include <cwctype>
#include <sstream>

#include "app_diagnostics.h"
#include "app_strings.h"
#include "dashboard_app.h"
#include "layout_edit_parameter.h"
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

std::string FindConfiguredBoardMetricBinding(const AppConfig& config, const LayoutMetricEditKey& key) {
    const auto target = ParseBoardMetricBindingTarget(key.metricId);
    if (!target.has_value()) {
        return {};
    }

    const auto& bindings = target->kind == BoardMetricBindingKind::Temperature ? config.board.temperatureSensorNames
                                                                               : config.board.fanSensorNames;
    const auto it = bindings.find(target->logicalName);
    if (it != bindings.end() && !it->second.empty()) {
        return it->second;
    }
    return target->logicalName;
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

void PopulateMetricBindingComboBox(
    HWND hwnd, const std::vector<std::string>& options, std::string_view selectedBinding, bool enableSelection) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT);
    if (combo == nullptr) {
        return;
    }

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
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
    LayoutEditTreeModel visibleTreeModel;
    std::optional<LayoutEditFocusKey> initialFocus;
    const LayoutEditTreeNode* selectedNode = nullptr;
    const LayoutEditTreeLeaf* selectedLeaf = nullptr;
    std::vector<LayoutEditTreeItemBinding> treeItems;
    COLORREF customColors[16]{};
    HFONT titleFont = nullptr;
    HFONT fontSampleFont = nullptr;
    std::wstring currentFilter;
    std::wstring statusText;
    bool statusIsError = false;
    bool activeSelectionValid = true;
    COLORREF previewColor = RGB(255, 255, 255);
    bool updatingControls = false;
};

enum class LayoutEditStatusKind {
    Info,
    Error,
};

struct LayoutEditValidationResult {
    bool valid = true;
    std::wstring message;
};

enum class LayoutEditEditorKind {
    Summary,
    Numeric,
    Font,
    Color,
    Weights,
    Metric,
};

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

std::wstring FormatDialogColorHex(unsigned int color) {
    return WideFromUtf8(FormatTraceColorHex(color));
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
    if (value.size() != 6) {
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
    if (!red.has_value() || !green.has_value() || !blue.has_value()) {
        return std::nullopt;
    }
    return (*red << 16) | (*green << 8) | *blue;
}

std::string BuildColorDialogTraceValues(HWND hwnd) {
    std::ostringstream trace;
    trace << " hex=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT))
          << " red=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT))
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
    if (std::holds_alternative<DashboardWidgetClass>(*node->selectionHighlight)) {
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
                return L"Enter a number. Commas and dots are both accepted.";
            case configschema::ValueFormat::FontSpec:
                return L"Choose a face, then enter a size of 1 or greater and an integer weight.";
            case configschema::ValueFormat::ColorHex:
                return L"Edit #RRGGBB or RGB values from 0 to 255. Valid values preview live.";
            case configschema::ValueFormat::String:
                return L"Text changes preview live.";
        }
    }
    if (std::holds_alternative<LayoutWeightEditKey>(node->leaf->focusKey)) {
        return L"Enter positive integer weights for the two neighboring items.";
    }
    if (std::holds_alternative<LayoutMetricEditKey>(node->leaf->focusKey)) {
        return L"Editable metric fields preview live when the current values are valid.";
    }
    if (std::holds_alternative<LayoutCardTitleEditKey>(node->leaf->focusKey)) {
        return L"Title changes preview live in every rendered instance of this card.";
    }
    return L"";
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

int DialogControlWidth(HWND hwnd, int controlId) {
    const auto rect = DialogControlRect(hwnd, controlId);
    return rect.has_value() ? (rect->right - rect->left) : 0;
}

int DialogControlHeight(HWND hwnd, int controlId) {
    const auto rect = DialogControlRect(hwnd, controlId);
    return rect.has_value() ? (rect->bottom - rect->top) : 0;
}

int DialogControlVisibleHeight(HWND hwnd, int controlId) {
    HWND control = GetDlgItem(hwnd, controlId);
    if (control == nullptr) {
        return 0;
    }

    wchar_t className[32] = {};
    GetClassNameW(control, className, ARRAYSIZE(className));
    if (_wcsicmp(className, WC_COMBOBOXW) == 0 || _wcsicmp(className, L"ComboBox") == 0) {
        COMBOBOXINFO comboInfo{};
        comboInfo.cbSize = sizeof(comboInfo);
        if (GetComboBoxInfo(control, &comboInfo)) {
            const int itemHeight = static_cast<int>(comboInfo.rcItem.bottom - comboInfo.rcItem.top);
            const int buttonHeight = static_cast<int>(comboInfo.rcButton.bottom - comboInfo.rcButton.top);
            return std::max(1, std::max(itemHeight, buttonHeight));
        }
    }

    return DialogControlHeight(hwnd, controlId);
}

void SetDialogControlBounds(HWND hwnd, int controlId, int left, int top, int width, int height) {
    if (HWND control = GetDlgItem(hwnd, controlId); control != nullptr) {
        SetWindowPos(control, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

std::wstring ReadWindowTextWide(HWND window) {
    if (window == nullptr) {
        return L"";
    }
    const int length = GetWindowTextLengthW(window);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(window, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
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
        return 0;
    }
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
    HFONT previous = font != nullptr ? reinterpret_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    SIZE size{};
    if (text.empty()) {
        text = L" ";
    }
    GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &size);
    if (previous != nullptr) {
        SelectObject(dc, previous);
    }
    ReleaseDC(hwnd, dc);
    return size.cx;
}

int MeasureTextHeightForControl(HWND hwnd, int controlId, std::wstring_view text, int width, bool singleLine = false) {
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

int DialogUnitsToPixelsX(HWND hwnd, int dialogUnitsX) {
    RECT rect{0, 0, dialogUnitsX, 0};
    MapDialogRect(hwnd, &rect);
    return rect.right - rect.left;
}

int DialogUnitsToPixelsY(HWND hwnd, int dialogUnitsY) {
    RECT rect{0, 0, 0, dialogUnitsY};
    MapDialogRect(hwnd, &rect);
    return rect.bottom - rect.top;
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
    state->previewColor = RGB((color >> 16) & 0xFFu, (color >> 8) & 0xFFu, color & 0xFFu);
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE, L"Sample text in the selected color");
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_SWATCH), nullptr, TRUE);
    InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_SAMPLE), nullptr, TRUE);
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

LayoutEditEditorKind CurrentLayoutEditEditorKind(const LayoutEditDialogState* state) {
    if (state == nullptr || state->selectedLeaf == nullptr) {
        return LayoutEditEditorKind::Summary;
    }
    if (std::holds_alternative<LayoutWeightEditKey>(state->selectedLeaf->focusKey)) {
        return LayoutEditEditorKind::Weights;
    }
    if (std::holds_alternative<LayoutMetricEditKey>(state->selectedLeaf->focusKey)) {
        return LayoutEditEditorKind::Metric;
    }
    if (std::holds_alternative<LayoutCardTitleEditKey>(state->selectedLeaf->focusKey)) {
        return LayoutEditEditorKind::Numeric;
    }
    const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey);
    if (parameter == nullptr) {
        return LayoutEditEditorKind::Summary;
    }
    switch (state->selectedLeaf->valueFormat) {
        case configschema::ValueFormat::FontSpec:
            return LayoutEditEditorKind::Font;
        case configschema::ValueFormat::ColorHex:
            return LayoutEditEditorKind::Color;
        case configschema::ValueFormat::String:
        case configschema::ValueFormat::Integer:
        case configschema::ValueFormat::FloatingPoint:
            return LayoutEditEditorKind::Numeric;
    }
    return LayoutEditEditorKind::Summary;
}

bool CurrentLayoutEditShowsMetricBinding(const LayoutEditDialogState* state) {
    if (state == nullptr || state->selectedLeaf == nullptr) {
        return false;
    }
    const auto* metricKey = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey);
    if (metricKey == nullptr) {
        return false;
    }
    return ParseBoardMetricBindingTarget(metricKey->metricId).has_value();
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
    const int controlHeight = DialogControlHeight(hwnd, controlId);
    const int visibleControlHeight = DialogControlVisibleHeight(hwnd, controlId);
    const int labelHeight = MeasureTextHeightForControl(
        hwnd, labelId, ReadDialogControlTextWide(hwnd, labelId), std::max(1, labelWidth), true);
    const int controlLeft = left + labelWidth + gap;
    const int rowHeight = std::max(forcedRowHeight, std::max(visibleControlHeight, labelHeight));
    SetDialogControlBounds(
        hwnd, controlId, controlLeft, top + ((rowHeight - visibleControlHeight) / 2), controlWidth, controlHeight);
    SetDialogControlBounds(hwnd, labelId, left, top + ((rowHeight - labelHeight) / 2), labelWidth, labelHeight);
    return rowHeight;
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
            const int editHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, innerLeft, cursorY, innerWidth, editHeight);
            cursorY += editHeight + metrics.hintGap;
            const std::wstring hintText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_HINT);
            const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, innerLeft, cursorY, innerWidth, hintHeight);
            contentBottom = cursorY + hintHeight;
            break;
        }
        case LayoutEditEditorKind::Font: {
            const int faceRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_FONT_FACE_LABEL,
                IDC_LAYOUT_EDIT_FONT_FACE_EDIT,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                innerWidth - labelColumnWidth - metrics.labelGap);
            cursorY += faceRowHeight + metrics.rowGap;

            const int sizeEditWidth = 56;
            const int sizeEditHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT);
            const int sizeControlLeft = innerLeft + labelColumnWidth + metrics.labelGap;
            const int sizeLabelHeight = MeasureTextHeightForControl(hwnd,
                IDC_LAYOUT_EDIT_FONT_SIZE_LABEL,
                ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_LABEL),
                labelColumnWidth,
                true);
            SetDialogControlBounds(hwnd,
                IDC_LAYOUT_EDIT_FONT_SIZE_LABEL,
                innerLeft,
                cursorY + ((sizeEditHeight - sizeLabelHeight) / 2),
                labelColumnWidth,
                sizeLabelHeight);
            SetDialogControlBounds(
                hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, sizeControlLeft, cursorY, sizeEditWidth, sizeEditHeight);

            const int weightLabelWidth = MeasureTextWidthForControl(hwnd,
                                             IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL,
                                             ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL)) +
                                         8;
            const int weightLabelLeft = sizeControlLeft + sizeEditWidth + metrics.inlineGap;
            const int weightEditLeft = weightLabelLeft + weightLabelWidth + metrics.labelGap;
            const int weightEditWidth = std::max(60, innerRight - weightEditLeft);
            const int weightEditHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT);
            const int weightLabelHeight = MeasureTextHeightForControl(hwnd,
                IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL,
                ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL),
                weightLabelWidth,
                true);
            SetDialogControlBounds(hwnd,
                IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL,
                weightLabelLeft,
                cursorY + ((weightEditHeight - weightLabelHeight) / 2),
                weightLabelWidth,
                weightLabelHeight);
            SetDialogControlBounds(
                hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, weightEditLeft, cursorY, weightEditWidth, weightEditHeight);

            cursorY += std::max(sizeEditHeight, weightEditHeight) + metrics.sampleGap;
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
            const int hexEditHeight = DialogControlHeight(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT);
            const int pickLeft = innerRight - pickWidth;
            const int hexLabelLeft = innerLeft + swatchSize + metrics.inlineGap;
            const int hexEditLeft = hexLabelLeft + hexLabelWidth + metrics.labelGap;
            const int hexEditWidth = std::max(60, pickLeft - metrics.inlineGap - hexEditLeft);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_COLOR_SWATCH, innerLeft, cursorY, swatchSize, swatchSize);
            SetDialogControlBounds(hwnd,
                IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
                hexLabelLeft,
                cursorY + ((hexEditHeight - MeasureTextHeightForControl(hwnd,
                                                IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
                                                ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_LABEL),
                                                hexLabelWidth,
                                                true)) /
                              2),
                hexLabelWidth,
                MeasureTextHeightForControl(hwnd,
                    IDC_LAYOUT_EDIT_COLOR_HEX_LABEL,
                    ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_LABEL),
                    hexLabelWidth,
                    true));
            SetDialogControlBounds(
                hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, hexEditLeft, cursorY, hexEditWidth, hexEditHeight);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_COLOR_PICK, pickLeft, cursorY, pickWidth, pickHeight);
            cursorY += std::max(std::max(swatchSize, hexEditHeight), pickHeight) + metrics.sampleGap;

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
                const int editHeight = DialogControlHeight(hwnd, rgbEditIds[i]);
                const int sliderHeight = DialogControlHeight(hwnd, rgbSliderIds[i]);
                const int rowHeight = std::max(editHeight, sliderHeight);
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
                    cursorY,
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
                editWidth);
            cursorY += firstRowHeight + metrics.rowGap;
            const int secondRowHeight = LayoutLabeledControlRow(hwnd,
                IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL,
                IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT,
                innerLeft,
                cursorY,
                labelColumnWidth,
                metrics.labelGap,
                editWidth);
            cursorY += secondRowHeight + metrics.hintGap;
            const std::wstring hintText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_HINT);
            const int hintHeight = MeasureTextHeightForControl(hwnd, IDC_LAYOUT_EDIT_HINT, hintText, innerWidth);
            SetDialogControlBounds(hwnd, IDC_LAYOUT_EDIT_HINT, innerLeft, cursorY, innerWidth, hintHeight);
            contentBottom = cursorY + hintHeight;
            break;
        }
        case LayoutEditEditorKind::Metric: {
            const int controlWidth = innerWidth - labelColumnWidth - metrics.labelGap;
            const int metricRowHeight = std::max({
                DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE),
                DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT),
                DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT),
                DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT),
                showBinding ? DialogControlVisibleHeight(hwnd, IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT) : 0,
            });
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
            const auto font = FindLayoutEditTooltipFontValue(state->shellUi->CurrentConfig(), *parameter);
            SetFontSamplePreview(state,
                hwnd,
                std::optional<LayoutEditParameter>(*parameter),
                font.has_value() && *font != nullptr ? *font : nullptr);
        }
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

std::string BuildMetricDialogTraceValues(HWND hwnd) {
    std::ostringstream trace;
    trace << " style=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE))
          << " scale=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT))
          << " unit=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT))
          << " label=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT))
          << " binding=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT));
    return trace.str();
}

LayoutEditDialogState* LayoutEditDialogStateFromWindow(HWND hwnd) {
    return reinterpret_cast<LayoutEditDialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
}

void PositionLayoutEditDialogNearDashboard(HWND anchorHwnd, UINT dpi, HWND hwnd) {
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
    const int gap = ScaleLogicalToPhysical(16, dpi);
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

void SelectLayoutEditTreeItem(LayoutEditDialogState* state, HWND hwnd, HTREEITEM item);

void PopulateLayoutEditSelection(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr) {
        return;
    }

    state->updatingControls = true;
    SetLayoutEditDescription(hwnd, state->selectedNode);
    if (state->selectedLeaf == nullptr) {
        ShowLayoutEditEditors(hwnd, false, false, false, false, false, false);
        SetLayoutEditStatus(state, hwnd, LayoutEditStatusKind::Info, L"Select a field to edit it here.");
        state->activeSelectionValid = true;
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        state->updatingControls = false;
        LayoutLayoutEditRightPane(state, hwnd);
        UpdateLayoutEditActionState(state, hwnd);
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
            ShowLayoutEditEditors(hwnd, false, true, false, false, false, false);
            SetFontSamplePreview(state,
                hwnd,
                std::optional<LayoutEditParameter>(*parameter),
                font.has_value() && *font != nullptr ? *font : nullptr);
            std::ostringstream trace;
            trace << BuildTraceNodeText(state->selectedNode) << " editor=\"font\""
                  << " face=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT))
                  << " size=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT))
                  << " weight=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT));
            state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
        } else if (state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex) {
            const auto value = FindLayoutEditParameterColorValue(state->shellUi->CurrentConfig(), *parameter);
            const unsigned int color = value.value_or(0);
            SetColorDialogHex(hwnd, color);
            SetColorDialogChannel(hwnd, kColorDialogControls[0], (color >> 16) & 0xFFu);
            SetColorDialogChannel(hwnd, kColorDialogControls[1], (color >> 8) & 0xFFu);
            SetColorDialogChannel(hwnd, kColorDialogControls[2], color & 0xFFu);
            ShowLayoutEditEditors(hwnd, false, false, true, false, false, false);
            SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
            SetColorSamplePreview(state, hwnd, color);
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
            ShowLayoutEditEditors(hwnd, true, false, false, false, false, false);
            SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
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
        ShowLayoutEditEditors(hwnd, false, false, false, true, false, false);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        std::ostringstream trace;
        trace << BuildTraceNodeText(state->selectedNode) << " editor=\"weights\""
              << " first=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT))
              << " second=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT));
        state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
    } else if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&state->selectedLeaf->focusKey)) {
        const std::wstring text =
            WideFromUtf8(FindCardTitleValue(state->shellUi->CurrentConfig(), *cardTitleKey).value_or(""));
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, text.c_str());
        ShowLayoutEditEditors(hwnd, true, false, false, false, false, false);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        std::ostringstream trace;
        trace << BuildTraceNodeText(state->selectedNode) << " editor=\"text\""
              << " text=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT));
        state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
    } else if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey)) {
        const MetricDefinitionConfig* definition =
            FindMetricDefinition(state->shellUi->CurrentConfig().metrics, metricKey->metricId);
        SetDlgItemTextW(hwnd,
            IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE,
            definition != nullptr ? WideFromUtf8(std::string(EnumToString(definition->style))).c_str() : L"");
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
        const auto bindingTarget = ParseBoardMetricBindingTarget(metricKey->metricId);
        const bool showBinding = bindingTarget.has_value();
        const std::string selectedBinding =
            showBinding ? FindConfiguredBoardMetricBinding(state->shellUi->CurrentConfig(), *metricKey) : std::string();
        std::vector<std::string> bindingOptions =
            showBinding ? state->shellUi->AvailableBoardMetricSensorBindings(*metricKey) : std::vector<std::string>{};
        if (!selectedBinding.empty() &&
            std::find(bindingOptions.begin(), bindingOptions.end(), selectedBinding) == bindingOptions.end()) {
            bindingOptions.push_back(selectedBinding);
        }
        std::sort(bindingOptions.begin(), bindingOptions.end());
        bindingOptions.erase(std::unique(bindingOptions.begin(), bindingOptions.end()), bindingOptions.end());
        PopulateMetricBindingComboBox(hwnd, bindingOptions, selectedBinding, showBinding);
        EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT), scaleEditable ? TRUE : FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT), unitEditable ? TRUE : FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT), definition != nullptr ? TRUE : FALSE);
        ShowLayoutEditEditors(hwnd, false, false, false, false, true, showBinding);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        std::ostringstream trace;
        trace << BuildTraceNodeText(state->selectedNode) << " editor=\"metric\"" << BuildMetricDialogTraceValues(hwnd)
              << " scale_editable=" << QuoteTraceText(scaleEditable ? "true" : "false")
              << " unit_editable=" << QuoteTraceText(unitEditable ? "true" : "false")
              << " binding_visible=" << QuoteTraceText(showBinding ? "true" : "false")
              << " binding_options=" << QuoteTraceText(std::to_string(bindingOptions.size()));
        state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
    } else {
        ShowLayoutEditEditors(hwnd, false, false, false, false, false, false);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        state->shellUi->TraceLayoutEditDialogEvent(
            "layout_edit_dialog:populate_selection", BuildTraceNodeText(state->selectedNode) + " editor=\"none\"");
    }

    SetLayoutEditStatus(state, hwnd, LayoutEditStatusKind::Info, L"Previewing changes in the dashboard.");
    state->activeSelectionValid = true;
    state->updatingControls = false;
    LayoutLayoutEditRightPane(state, hwnd);
    UpdateLayoutEditActionState(state, hwnd);
}

LayoutEditValidationResult ValidateCurrentSelectionInput(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr) {
        return {true, L""};
    }

    if (const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey);
        parameter != nullptr) {
        (void)parameter;
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec) {
            wchar_t faceBuffer[256] = {};
            wchar_t sizeBuffer[64] = {};
            wchar_t weightBuffer[64] = {};
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT, faceBuffer, ARRAYSIZE(faceBuffer));
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, sizeBuffer, ARRAYSIZE(sizeBuffer));
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, weightBuffer, ARRAYSIZE(weightBuffer));
            const std::optional<int> size = TryParseDialogInteger(sizeBuffer);
            const std::optional<int> weight = TryParseDialogInteger(weightBuffer);
            if (std::wstring(faceBuffer).empty()) {
                return {false, L"Enter a font name."};
            }
            if (!size.has_value() || *size < 1) {
                return {false, L"Enter a font size of 1 or greater."};
            }
            if (!weight.has_value()) {
                return {false, L"Enter an integer font weight."};
            }
            return {true, L""};
        }
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex) {
            wchar_t hexBuffer[64] = {};
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, hexBuffer, ARRAYSIZE(hexBuffer));
            if (!std::wstring(hexBuffer).empty() && !TryParseDialogHexColor(hexBuffer).has_value()) {
                return {false, L"Enter a #RRGGBB color value."};
            }
            if (!ReadColorDialogValue(hwnd).has_value()) {
                return {false, L"Enter each RGB channel as a whole number between 0 and 255."};
            }
            return {true, L""};
        }
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::String) {
            return {true, L""};
        }

        wchar_t valueBuffer[128] = {};
        GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, valueBuffer, ARRAYSIZE(valueBuffer));
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::Integer) {
            return TryParseDialogInteger(valueBuffer).has_value()
                       ? LayoutEditValidationResult{true, L""}
                       : LayoutEditValidationResult{false, L"Enter a whole number."};
        }
        return TryParseDialogDouble(valueBuffer).has_value()
                   ? LayoutEditValidationResult{true, L""}
                   : LayoutEditValidationResult{false, L"Enter a valid number."};
    }

    if (std::holds_alternative<LayoutCardTitleEditKey>(state->selectedLeaf->focusKey)) {
        return {true, L""};
    }

    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey);
        metricKey != nullptr) {
        const MetricDefinitionConfig* definition =
            FindMetricDefinition(state->shellUi->CurrentConfig().metrics, metricKey->metricId);
        if (definition == nullptr) {
            return {false, L"Unable to find the current metric definition."};
        }
        if (!definition->telemetryScale && definition->style != MetricDisplayStyle::LabelOnly) {
            wchar_t scaleBuffer[128] = {};
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT, scaleBuffer, ARRAYSIZE(scaleBuffer));
            const std::optional<double> scale = TryParseDialogDouble(scaleBuffer);
            if (!scale.has_value() || *scale <= 0.0) {
                return {false, L"Enter a metric scale greater than 0."};
            }
        }
        return {true, L""};
    }

    wchar_t firstBuffer[64] = {};
    wchar_t secondBuffer[64] = {};
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, firstBuffer, ARRAYSIZE(firstBuffer));
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT, secondBuffer, ARRAYSIZE(secondBuffer));
    const std::optional<int> first = TryParseDialogInteger(firstBuffer);
    const std::optional<int> second = TryParseDialogInteger(secondBuffer);
    if (!first.has_value() || !second.has_value() || *first < 1 || *second < 1) {
        return {false, L"Enter positive integer weights for both neighboring items."};
    }
    return {true, L""};
}

void RefreshLayoutEditValidationState(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr) {
        return;
    }
    const LayoutEditValidationResult validation = ValidateCurrentSelectionInput(state, hwnd);
    state->activeSelectionValid = validation.valid;
    if (state->selectedLeaf == nullptr) {
        SetLayoutEditStatus(state, hwnd, LayoutEditStatusKind::Info, L"Select a field to edit it here.");
    } else if (validation.valid) {
        SetLayoutEditStatus(state, hwnd, LayoutEditStatusKind::Info, L"Previewing changes in the dashboard.");
    } else {
        SetLayoutEditStatus(state, hwnd, LayoutEditStatusKind::Error, validation.message);
    }
    LayoutLayoutEditRightPane(state, hwnd);
    UpdateLayoutEditActionState(state, hwnd);
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
    if (applied) {
        SetFontSamplePreview(state, hwnd, std::optional<LayoutEditParameter>(*parameter), &font);
    }
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
    if (applied && color.has_value()) {
        SetColorSamplePreview(state, hwnd, *color);
    }
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
    SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT));
    SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, EM_SETSEL, 0, -1);
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
    const auto bindingTarget = ParseBoardMetricBindingTarget(key->metricId);
    const std::optional<std::string> binding =
        bindingTarget.has_value()
            ? std::optional<std::string>(Trim(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT)))
            : std::nullopt;
    const bool applied = state->shellUi->ApplyMetricPreview(*key, scale, unit, label, binding);
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

HTREEITEM FindTreeItemByLocationText(LayoutEditDialogState* state, std::string_view locationText) {
    for (const auto& binding : state->treeItems) {
        if (binding.node != nullptr && binding.node->locationText == locationText) {
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

HTREEITEM FindFirstTreeItem(const LayoutEditDialogState& state) {
    return state.treeItems.empty() ? nullptr : state.treeItems.front().item;
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

void RebuildLayoutEditTree(
    LayoutEditDialogState* state, HWND hwnd, const std::optional<LayoutEditFocusKey>& preferredFocus = std::nullopt) {
    if (state == nullptr) {
        return;
    }
    HWND tree = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE);
    if (tree == nullptr) {
        return;
    }

    std::string preferredLocation;
    if (preferredFocus.has_value()) {
        if (const LayoutEditTreeLeaf* leaf = FindLayoutEditTreeLeaf(state->treeModel, *preferredFocus);
            leaf != nullptr) {
            preferredLocation = "[" + leaf->sectionName + "] " + leaf->memberName;
        }
    } else if (state->selectedNode != nullptr) {
        preferredLocation = state->selectedNode->locationText;
    }

    state->visibleTreeModel = FilterLayoutEditTreeModel(state->treeModel, Utf8FromWide(state->currentFilter));
    state->treeItems.clear();
    TreeView_DeleteAllItems(tree);
    InsertLayoutEditTreeNodes(state, tree, state->visibleTreeModel.roots, TVI_ROOT);

    HTREEITEM selectedItem = nullptr;
    if (preferredFocus.has_value()) {
        selectedItem = FindTreeItemByFocusKey(state, *preferredFocus);
    }
    if (selectedItem == nullptr && !preferredLocation.empty()) {
        selectedItem = FindTreeItemByLocationText(state, preferredLocation);
    }
    if (selectedItem == nullptr) {
        selectedItem = FindFirstLeafTreeItem(*state);
    }
    if (selectedItem == nullptr) {
        selectedItem = FindFirstTreeItem(*state);
    }

    if (selectedItem != nullptr) {
        ExpandTreeAncestors(tree, selectedItem);
        TreeView_SelectItem(tree, selectedItem);
        TreeView_EnsureVisible(tree, selectedItem);
        SelectLayoutEditTreeItem(state, hwnd, selectedItem);
        return;
    }

    state->selectedNode = nullptr;
    state->selectedLeaf = nullptr;
    state->shellUi->SetLayoutEditTreeSelectionHighlight(std::nullopt);
    PopulateLayoutEditSelection(state, hwnd);
}

void SelectLayoutEditTreeItem(LayoutEditDialogState* state, HWND hwnd, HTREEITEM item) {
    DialogRedrawScope redrawScope(hwnd);
    const LayoutEditTreeNode* node = TreeNodeFromItem(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE), item);
    state->selectedNode = node;
    state->selectedLeaf = node != nullptr && node->leaf.has_value() ? &(*node->leaf) : nullptr;
    state->shellUi->SetLayoutEditTreeSelectionHighlight(SelectionHighlightForTreeNode(node));
    state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:tree_select", BuildTraceNodeText(node));
    PopulateLayoutEditSelection(state, hwnd);
    RefreshLayoutEditValidationState(state, hwnd);
}

void RefreshLayoutEditDialogControls(LayoutEditDialogState* state,
    HWND hwnd,
    const std::optional<LayoutEditFocusKey>& preferredFocus,
    bool rebuildTree) {
    if (state == nullptr || hwnd == nullptr) {
        return;
    }

    DialogRedrawScope redrawScope(hwnd);
    if (rebuildTree) {
        RebuildLayoutEditTree(state, hwnd, preferredFocus);
        return;
    }

    if (preferredFocus.has_value()) {
        if (HWND tree = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_TREE); tree != nullptr) {
            if (HTREEITEM item = FindTreeItemByFocusKey(state, *preferredFocus); item != nullptr) {
                ExpandTreeAncestors(tree, item);
                TreeView_SelectItem(tree, item);
                TreeView_EnsureVisible(tree, item);
                SelectLayoutEditTreeItem(state, hwnd, item);
                return;
            }
        }
    }

    PopulateLayoutEditSelection(state, hwnd);
    RefreshLayoutEditValidationState(state, hwnd);
}

bool RevertSelectedLayoutEditField(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr || state->shellUi == nullptr) {
        return false;
    }

    if (const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey);
        parameter != nullptr) {
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec) {
            const auto font = FindLayoutEditTooltipFontValue(state->originalConfig, *parameter);
            if (!font.has_value() || *font == nullptr) {
                return false;
            }
            const bool applied = state->shellUi->ApplyFontPreview(*parameter, **font);
            if (applied) {
                PopulateLayoutEditSelection(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
            }
            return applied;
        }
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex) {
            const auto color = FindLayoutEditParameterColorValue(state->originalConfig, *parameter);
            if (!color.has_value()) {
                return false;
            }
            const bool applied = state->shellUi->ApplyColorPreview(*parameter, *color);
            if (applied) {
                PopulateLayoutEditSelection(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
            }
            return applied;
        }
        if (state->selectedLeaf->valueFormat != configschema::ValueFormat::String) {
            const auto value = FindLayoutEditParameterNumericValue(state->originalConfig, *parameter);
            if (!value.has_value()) {
                return false;
            }
            const bool applied = state->shellUi->ApplyParameterPreview(*parameter, *value);
            if (applied) {
                PopulateLayoutEditSelection(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
            }
            return applied;
        }
    }

    if (const auto* weightKey = std::get_if<LayoutWeightEditKey>(&state->selectedLeaf->focusKey);
        weightKey != nullptr) {
        const auto values = FindWeightEditValues(state->originalConfig, *weightKey);
        if (!values.has_value()) {
            return false;
        }
        const bool applied = state->shellUi->ApplyWeightPreview(*weightKey, values->first, values->second);
        if (applied) {
            PopulateLayoutEditSelection(state, hwnd);
            RefreshLayoutEditValidationState(state, hwnd);
        }
        return applied;
    }

    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey);
        metricKey != nullptr) {
        const MetricDefinitionConfig* definition =
            FindMetricDefinition(state->originalConfig.metrics, metricKey->metricId);
        if (definition == nullptr) {
            return false;
        }
        std::optional<std::string> binding;
        if (const auto target = ParseBoardMetricBindingTarget(metricKey->metricId); target.has_value()) {
            const auto& bindings = target->kind == BoardMetricBindingKind::Temperature
                                       ? state->originalConfig.board.temperatureSensorNames
                                       : state->originalConfig.board.fanSensorNames;
            const auto it = bindings.find(target->logicalName);
            binding = it != bindings.end() ? std::optional<std::string>(it->second)
                                           : std::optional<std::string>(std::string());
        }
        const bool applied = state->shellUi->ApplyMetricPreview(*metricKey,
            definition->telemetryScale ? std::nullopt : std::optional<double>(definition->scale),
            definition->style == MetricDisplayStyle::LabelOnly ? std::string() : definition->unit,
            definition->label,
            binding);
        if (applied) {
            PopulateLayoutEditSelection(state, hwnd);
            RefreshLayoutEditValidationState(state, hwnd);
        }
        return applied;
    }

    if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&state->selectedLeaf->focusKey);
        cardTitleKey != nullptr) {
        const std::string title = FindCardTitleValue(state->originalConfig, *cardTitleKey).value_or("");
        const bool applied = state->shellUi->ApplyCardTitlePreview(*cardTitleKey, title);
        if (applied) {
            PopulateLayoutEditSelection(state, hwnd);
            RefreshLayoutEditValidationState(state, hwnd);
        }
        return applied;
    }

    return false;
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
    auto* state = LayoutEditDialogStateFromWindow(hwnd);
    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<LayoutEditDialogState*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            SetWindowTextW(hwnd, L"Edit Configuration");
            state->shellUi->SetLayoutEditTreeSelectionHighlight(std::nullopt);
            ConfigureColorSliders(hwnd);
            ConfigureDialogFonts(state, hwnd);
            SendDlgItemMessageW(hwnd,
                IDC_LAYOUT_EDIT_FILTER_EDIT,
                EM_SETCUEBANNER,
                FALSE,
                reinterpret_cast<LPARAM>(L"Filter settings"));
            RebuildLayoutEditTree(state, hwnd, state->initialFocus);
            state->shellUi->PositionLayoutEditDialogWindow(hwnd);
            ShowWindow(hwnd, SW_SHOWNORMAL);
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
                state->shellUi->SetLayoutEditTreeSelectionHighlight(std::nullopt);
                DestroyDialogFont(state->titleFont);
                DestroyDialogFont(state->fontSampleFont);
            }
            break;
        case WM_ACTIVATE:
            if (state != nullptr && state->shellUi != nullptr) {
                state->shellUi->SetLayoutEditTreeSelectionHighlightVisible(LOWORD(wParam) != WA_INACTIVE);
            }
            break;
        case WM_CLOSE:
            if (state != nullptr && state->shellUi != nullptr) {
                state->shellUi->HandleEditLayoutToggle();
            } else {
                DestroyWindow(hwnd);
            }
            return TRUE;
        case WM_NCDESTROY:
            if (state != nullptr) {
                if (state->shellUi != nullptr) {
                    state->shellUi->TraceLayoutEditDialogEvent("layout_edit_dialog:close");
                    state->shellUi->OnLayoutEditDialogDestroyed(hwnd);
                }
                SetWindowLongPtrW(hwnd, DWLP_USER, 0);
                delete state;
            }
            return FALSE;
    }
    return FALSE;
}

}  // namespace

DashboardShellUi::DashboardShellUi(DashboardApp& app) : app_(app) {}

DashboardShellUi::~DashboardShellUi() {
    DestroyLayoutEditDialogWindow();
}

bool DashboardShellUi::HandleDialogMessage(MSG* msg) const {
    return msg != nullptr && layoutEditDialogHwnd_ != nullptr && IsWindow(layoutEditDialogHwnd_) &&
           IsDialogMessageW(layoutEditDialogHwnd_, msg) != FALSE;
}

void DashboardShellUi::PositionLayoutEditDialogWindow(HWND hwnd) const {
    PositionLayoutEditDialogNearDashboard(app_.hwnd_, app_.CurrentWindowDpi(), hwnd);
}

void DashboardShellUi::OnLayoutEditDialogDestroyed(HWND hwnd) {
    if (layoutEditDialogHwnd_ == hwnd) {
        layoutEditDialogHwnd_ = nullptr;
    }
    layoutEditTreeSelectionHighlightVisible_ = false;
    SetLayoutEditTreeSelectionHighlight(std::nullopt);
}

bool DashboardShellUi::IsLayoutEditDialogForegroundWindow() const {
    if (layoutEditDialogHwnd_ == nullptr || !IsWindow(layoutEditDialogHwnd_)) {
        return false;
    }

    const HWND foreground = GetForegroundWindow();
    return foreground != nullptr && (foreground == layoutEditDialogHwnd_ || IsChild(layoutEditDialogHwnd_, foreground));
}

bool DashboardShellUi::ShouldDashboardIgnoreMouse(POINT screenPoint) const {
    if (layoutEditDialogHwnd_ == nullptr || !IsWindow(layoutEditDialogHwnd_) ||
        !IsWindowVisible(layoutEditDialogHwnd_)) {
        return false;
    }

    RECT dialogRect{};
    if (!GetWindowRect(layoutEditDialogHwnd_, &dialogRect) || !PtInRect(&dialogRect, screenPoint)) {
        return false;
    }

    if (IsLayoutEditDialogForegroundWindow()) {
        return true;
    }

    if (HWND hitWindow = WindowFromPoint(screenPoint);
        hitWindow != nullptr && (hitWindow == layoutEditDialogHwnd_ || IsChild(layoutEditDialogHwnd_, hitWindow))) {
        return true;
    }

    for (HWND window = GetWindow(app_.hwnd_, GW_HWNDPREV); window != nullptr; window = GetWindow(window, GW_HWNDPREV)) {
        if (window == layoutEditDialogHwnd_) {
            return true;
        }
    }
    return false;
}

void DashboardShellUi::ApplyLayoutEditTreeSelectionHighlightVisibility() {
    app_.rendererEditOverlayState_.selectedTreeHighlight =
        layoutEditTreeSelectionHighlightVisible_ ? layoutEditTreeSelectionHighlight_ : std::nullopt;
}

void DashboardShellUi::SetLayoutEditTreeSelectionHighlightVisible(bool visible) {
    if (layoutEditTreeSelectionHighlightVisible_ == visible) {
        return;
    }

    layoutEditTreeSelectionHighlightVisible_ = visible;
    ApplyLayoutEditTreeSelectionHighlightVisibility();
    InvalidateRect(app_.hwnd_, nullptr, FALSE);
}

void DashboardShellUi::DestroyLayoutEditDialogWindow() {
    if (layoutEditDialogHwnd_ == nullptr) {
        SetLayoutEditTreeSelectionHighlight(std::nullopt);
        return;
    }

    HWND dialog = layoutEditDialogHwnd_;
    layoutEditDialogHwnd_ = nullptr;
    DestroyWindow(dialog);
    SetLayoutEditTreeSelectionHighlight(std::nullopt);
}

void BringModelessDialogToFront(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetForegroundWindow(hwnd);
}

bool DashboardShellUi::EnsureLayoutEditDialog(const std::optional<LayoutEditFocusKey>& focusKey, bool bringToFront) {
    if (layoutEditDialogHwnd_ != nullptr && IsWindow(layoutEditDialogHwnd_)) {
        RefreshLayoutEditDialog(focusKey);
        if (bringToFront) {
            BringModelessDialogToFront(layoutEditDialogHwnd_);
        }
        return true;
    }

    auto state = std::make_unique<LayoutEditDialogState>();
    state->shellUi = this;
    state->originalConfig = app_.controller_.State().hasLayoutEditSessionSavedConfig
                                ? app_.controller_.State().layoutEditSessionSavedConfig
                                : app_.controller_.State().config;
    state->treeModel = BuildLayoutEditTreeModel(app_.controller_.State().config);
    state->initialFocus = focusKey;

    std::string initialFocusTrace = "session";
    if (focusKey.has_value()) {
        if (const auto* parameter = std::get_if<LayoutEditParameter>(&*focusKey)) {
            initialFocusTrace =
                FindLayoutEditTooltipDescriptor(*parameter).value_or(LayoutEditTooltipDescriptor{}).configKey;
        } else if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&*focusKey)) {
            initialFocusTrace = "[metrics] " + metricKey->metricId;
        } else if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&*focusKey)) {
            initialFocusTrace = "[card." + cardTitleKey->cardId + "] title";
        } else {
            initialFocusTrace = "weight";
        }
    }
    TraceLayoutEditDialogEvent("layout_edit_dialog:open", "initial_focus=" + QuoteTraceText(initialFocusTrace));

    HWND dialog = CreateDialogParamW(app_.instance_,
        MAKEINTRESOURCEW(IDD_LAYOUT_EDIT_CONFIGURATION),
        nullptr,
        LayoutEditDialogProc,
        reinterpret_cast<LPARAM>(state.get()));
    if (dialog == nullptr) {
        return false;
    }

    layoutEditDialogHwnd_ = dialog;
    state.release();
    if (bringToFront) {
        BringModelessDialogToFront(layoutEditDialogHwnd_);
        SetLayoutEditTreeSelectionHighlightVisible(true);
    }
    return true;
}

void DashboardShellUi::RefreshLayoutEditDialog(const std::optional<LayoutEditFocusKey>& preferredFocus) {
    if (layoutEditDialogHwnd_ == nullptr || !IsWindow(layoutEditDialogHwnd_)) {
        return;
    }

    auto* state = LayoutEditDialogStateFromWindow(layoutEditDialogHwnd_);
    if (state == nullptr) {
        return;
    }

    state->originalConfig = app_.controller_.State().hasLayoutEditSessionSavedConfig
                                ? app_.controller_.State().layoutEditSessionSavedConfig
                                : app_.controller_.State().config;
    state->treeModel = BuildLayoutEditTreeModel(app_.controller_.State().config);
    RefreshLayoutEditDialogControls(state, layoutEditDialogHwnd_, preferredFocus, true);
}

void DashboardShellUi::RefreshLayoutEditDialogSelection() {
    if (layoutEditDialogHwnd_ == nullptr || !IsWindow(layoutEditDialogHwnd_)) {
        return;
    }

    auto* state = LayoutEditDialogStateFromWindow(layoutEditDialogHwnd_);
    if (state == nullptr) {
        return;
    }

    state->originalConfig = app_.controller_.State().hasLayoutEditSessionSavedConfig
                                ? app_.controller_.State().layoutEditSessionSavedConfig
                                : app_.controller_.State().config;
    RefreshLayoutEditDialogControls(state, layoutEditDialogHwnd_, std::nullopt, false);
}

void DashboardShellUi::SyncLayoutEditDialogSelection(
    const std::optional<LayoutEditController::TooltipTarget>& target, bool bringToFront) {
    if (!target.has_value()) {
        return;
    }

    const auto focusKey = TooltipPayloadFocusKey(target->payload);
    if (!focusKey.has_value()) {
        if (bringToFront && layoutEditDialogHwnd_ != nullptr && IsWindow(layoutEditDialogHwnd_)) {
            BringModelessDialogToFront(layoutEditDialogHwnd_);
        }
        return;
    }

    if (layoutEditDialogHwnd_ == nullptr || !IsWindow(layoutEditDialogHwnd_)) {
        if (!bringToFront) {
            return;
        }
        if (!EnsureLayoutEditDialog(focusKey, true)) {
            MessageBoxW(
                app_.hwnd_, L"Failed to open the Edit Configuration window.", L"System Telemetry", MB_ICONERROR);
        }
        return;
    }

    auto* state = LayoutEditDialogStateFromWindow(layoutEditDialogHwnd_);
    if (state != nullptr) {
        state->originalConfig = app_.controller_.State().hasLayoutEditSessionSavedConfig
                                    ? app_.controller_.State().layoutEditSessionSavedConfig
                                    : app_.controller_.State().config;
        RefreshLayoutEditDialogControls(state, layoutEditDialogHwnd_, focusKey, false);
    }
    if (bringToFront) {
        BringModelessDialogToFront(layoutEditDialogHwnd_);
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

    const HWND owner =
        layoutEditDialogHwnd_ != nullptr && IsWindow(layoutEditDialogHwnd_) ? layoutEditDialogHwnd_ : app_.hwnd_;
    DialogBoxParamW(app_.instance_,
        MAKEINTRESOURCEW(IDD_UNSAVED_LAYOUT_EDIT),
        owner,
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
        } else if (!app_.controller_.RestoreLayoutEditSessionSavedConfig(app_)) {
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

    if (!app_.controller_.ReloadConfigFromDisk(app_, app_.diagnosticsOptions_, app_.layoutEditController_)) {
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
    const std::string& label,
    const std::optional<std::string>& binding) {
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
    if (const auto target = ParseBoardMetricBindingTarget(key.metricId); target.has_value() && binding.has_value()) {
        auto& bindings = target->kind == BoardMetricBindingKind::Temperature
                             ? updatedConfig.board.temperatureSensorNames
                             : updatedConfig.board.fanSensorNames;
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
    layoutEditTreeSelectionHighlight_ = highlight;
    ApplyLayoutEditTreeSelectionHighlightVisibility();
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
                RefreshLayoutEditDialogSelection();
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
                    if (!app_.controller_.SwitchLayout(
                            app_, it->name, app_.layoutEditController_, app_.diagnosticsOptions_.editLayout)) {
                        MessageBoxW(app_.hwnd_, L"Failed to switch layout.", L"System Telemetry", MB_ICONERROR);
                    } else {
                        RefreshLayoutEditDialog();
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
