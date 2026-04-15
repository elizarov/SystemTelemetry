#include "dashboard_shell_ui.h"

#include <cmath>
#include <cwchar>
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
    const LayoutEditTreeLeaf* selectedLeaf = nullptr;
    std::vector<LayoutEditTreeItemBinding> treeItems;
    bool accepted = false;
    bool updatingControls = false;
};

const LayoutNodeConfig* FindWeightEditNode(const AppConfig& config, const LayoutWeightEditKey& key) {
    LayoutEditHost::LayoutTarget target;
    target.editCardId = key.editCardId;
    target.nodePath = key.nodePath;
    return FindGuideNode(config, target);
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

void ShowLayoutEditEditors(HWND hwnd, bool showNumeric, bool showFont, bool showWeights) {
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, showNumeric);

    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_LABEL, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_LABEL, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_LABEL, showFont);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, showFont);

    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_LABEL, showWeights);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, showWeights);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL, showWeights);
    ShowDialogControl(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT, showWeights);
}

void RestoreLayoutEditDialog(LayoutEditDialogState* state) {
    if (state != nullptr && !state->accepted && state->shellUi != nullptr) {
        state->shellUi->RestoreConfigSnapshot(state->originalConfig);
    }
}

void SetLayoutEditDescription(HWND hwnd, const LayoutEditTreeLeaf* leaf) {
    if (leaf == nullptr) {
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_LOCATION, L"");
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, L"");
        return;
    }

    const std::wstring location = WideFromUtf8("[" + leaf->sectionName + "] " + leaf->memberName);
    const std::wstring description = WideFromUtf8(FindLocalizedText(leaf->descriptionKey));
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_LOCATION, location.c_str());
    SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_DESCRIPTION, description.c_str());
}

void PopulateLayoutEditSelection(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr) {
        return;
    }

    state->updatingControls = true;
    SetLayoutEditDescription(hwnd, state->selectedLeaf);
    if (state->selectedLeaf == nullptr) {
        ShowLayoutEditEditors(hwnd, false, false, false);
        state->updatingControls = false;
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
            ShowLayoutEditEditors(hwnd, false, true, false);
        } else {
            const auto value = FindLayoutEditParameterNumericValue(state->shellUi->CurrentConfig(), *parameter);
            const std::wstring text =
                value.has_value() ? WideFromUtf8(FormatLayoutEditTooltipValue(*value, state->selectedLeaf->valueFormat))
                                  : L"";
            SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, text.c_str());
            ShowLayoutEditEditors(hwnd, true, false, false);
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
        ShowLayoutEditEditors(hwnd, false, false, true);
    } else {
        ShowLayoutEditEditors(hwnd, false, false, false);
    }

    state->updatingControls = false;
}

bool PreviewSelectedNumeric(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr || state->updatingControls) {
        return false;
    }
    const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey);
    if (parameter == nullptr || state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec) {
        return false;
    }

    wchar_t buffer[128] = {};
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, buffer, ARRAYSIZE(buffer));
    std::optional<double> value;
    if (state->selectedLeaf->valueFormat == configschema::ValueFormat::Integer) {
        if (const auto parsed = TryParseDialogInteger(buffer); parsed.has_value()) {
            value = static_cast<double>(*parsed);
        }
    } else {
        value = TryParseDialogDouble(buffer);
    }
    return value.has_value() && state->shellUi->ApplyParameterPreview(*parameter, *value);
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

    return state->shellUi->ApplyFontPreview(*parameter, UiFontConfig{Utf8FromWide(faceText), *size, *weight});
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

    return state->shellUi->ApplyWeightPreview(*key, *first, *second);
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
    state->selectedLeaf = node != nullptr && node->leaf.has_value() ? &(*node->leaf) : nullptr;
    state->shellUi->SetLayoutEditTreeSelectionHighlight(SelectionHighlightForTreeNode(node));
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
                ShowLayoutEditEditors(hwnd, false, false, false);
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
                PreviewSelectedNumeric(state, hwnd);
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
            if ((LOWORD(wParam) == IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT) &&
                HIWORD(wParam) == EN_CHANGE) {
                PreviewSelectedWeights(state, hwnd);
                return TRUE;
            }
            switch (LOWORD(wParam)) {
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

    LayoutEditDialogState state;
    state.shellUi = this;
    state.originalConfig = app_.controller_.State().config;
    state.treeModel = BuildLayoutEditTreeModel(app_.controller_.State().config);
    state.initialFocus = focusKey;

    DashboardShellUiModalScope scopedModalUi(*this);
    const INT_PTR result = DialogBoxParamW(app_.instance_,
        MAKEINTRESOURCEW(IDD_LAYOUT_EDIT_CONFIGURATION),
        app_.hwnd_,
        LayoutEditDialogProc,
        reinterpret_cast<LPARAM>(&state));
    SetLayoutEditTreeSelectionHighlight(std::nullopt);
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
            const auto parameter = TooltipPayloadParameter(layoutEditTarget->payload);
            if (parameter.has_value()) {
                label = BuildLayoutEditMenuLabel(WideFromUtf8(GetLayoutEditParameterDisplayName(*parameter)));
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
