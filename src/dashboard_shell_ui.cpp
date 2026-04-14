#include "dashboard_shell_ui.h"

#include <cmath>
#include <cwchar>
#include <functional>
#include <sstream>

#include "app_diagnostics.h"
#include "dashboard_app.h"
#include "layout_edit_service.h"
#include "layout_edit_tooltip.h"

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

std::wstring BuildLayoutGuideEditLabel(const DashboardRenderer::LayoutEditGuide& guide) {
    return guide.editCardId.empty() ? L"cards weights" : L"layout weights";
}

const LayoutNodeConfig* FindLayoutGuideNode(const AppConfig& config, const DashboardRenderer::LayoutEditGuide& guide) {
    return layout_edit::FindGuideNode(config, LayoutEditHost::LayoutTarget::ForGuide(guide));
}

std::wstring BuildLayoutGuideItemLabel(
    const LayoutNodeConfig& node, size_t childIndex, DashboardRenderer::LayoutGuideAxis axis, bool first) {
    const std::wstring side = axis == DashboardRenderer::LayoutGuideAxis::Vertical ? (first ? L"Left" : L"Right")
                                                                                   : (first ? L"Top" : L"Bottom");
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

struct LayoutEditValueDialogState {
    std::wstring title;
    std::wstring prompt;
    std::wstring initialText;
    configschema::ValueFormat valueFormat = configschema::ValueFormat::Integer;
    std::function<bool(double)> preview;
    std::function<void()> restore;
    std::optional<double> result;
    bool accepted = false;
};

struct LayoutEditWeightsDialogState {
    std::wstring title;
    std::wstring firstLabel;
    std::wstring secondLabel;
    int firstValue = 1;
    int secondValue = 1;
    std::function<bool(const std::pair<int, int>&)> preview;
    std::function<void()> restore;
    std::optional<std::pair<int, int>> result;
    bool accepted = false;
};

struct LayoutEditFontDialogState {
    std::wstring title;
    std::wstring prompt;
    UiFontConfig initialValue;
    std::function<bool(const UiFontConfig&)> preview;
    std::function<void()> restore;
    std::optional<UiFontConfig> result;
    bool accepted = false;
};

void RestoreLayoutEditValueDialog(LayoutEditValueDialogState* state) {
    if (state != nullptr && !state->accepted && state->restore) {
        state->restore();
    }
}

void RestoreLayoutEditWeightsDialog(LayoutEditWeightsDialogState* state) {
    if (state != nullptr && !state->accepted && state->restore) {
        state->restore();
    }
}

void RestoreLayoutEditFontDialog(LayoutEditFontDialogState* state) {
    if (state != nullptr && !state->accepted && state->restore) {
        state->restore();
    }
}

void PreviewLayoutEditValueDialog(LayoutEditValueDialogState* state, HWND hwnd) {
    if (state == nullptr || !state->preview) {
        return;
    }

    wchar_t buffer[128] = {};
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, buffer, ARRAYSIZE(buffer));
    std::optional<double> value;
    if (state->valueFormat == configschema::ValueFormat::Integer) {
        if (const auto parsed = TryParseDialogInteger(buffer); parsed.has_value()) {
            value = static_cast<double>(*parsed);
        }
    } else {
        value = TryParseDialogDouble(buffer);
    }
    if (!value.has_value()) {
        return;
    }

    if (state->preview(*value)) {
        state->result = *value;
    }
}

void PreviewLayoutEditWeightsDialog(LayoutEditWeightsDialogState* state, HWND hwnd) {
    if (state == nullptr || !state->preview) {
        return;
    }

    wchar_t firstBuffer[64] = {};
    wchar_t secondBuffer[64] = {};
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, firstBuffer, ARRAYSIZE(firstBuffer));
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT, secondBuffer, ARRAYSIZE(secondBuffer));
    const std::optional<int> first = TryParseDialogInteger(firstBuffer);
    const std::optional<int> second = TryParseDialogInteger(secondBuffer);
    if (!first.has_value() || !second.has_value() || *first < 1 || *second < 1) {
        return;
    }

    const std::pair<int, int> weights{*first, *second};
    if (state->preview(weights)) {
        state->result = weights;
    }
}

void PreviewLayoutEditFontDialog(LayoutEditFontDialogState* state, HWND hwnd, UINT notificationCode = 0) {
    if (state == nullptr || !state->preview) {
        return;
    }

    wchar_t sizeBuffer[64] = {};
    wchar_t weightBuffer[64] = {};
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, sizeBuffer, ARRAYSIZE(sizeBuffer));
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, weightBuffer, ARRAYSIZE(weightBuffer));
    const std::wstring faceText = ReadFontDialogFaceText(hwnd, notificationCode);
    const std::optional<int> size = TryParseDialogInteger(sizeBuffer);
    const std::optional<int> weight = TryParseDialogInteger(weightBuffer);
    if (faceText.empty() || !size.has_value() || *size < 1 || !weight.has_value()) {
        return;
    }

    const UiFontConfig font{Utf8FromWide(faceText), *size, *weight};
    if (state->preview(font)) {
        state->result = font;
    }
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

INT_PTR CALLBACK LayoutEditValueDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<LayoutEditValueDialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<LayoutEditValueDialogState*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            SetWindowTextW(hwnd, state->title.c_str());
            SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_PROMPT, state->prompt.c_str());
            SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, state->initialText.c_str());
            SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, EM_SETSEL, 0, -1);
            PreviewLayoutEditValueDialog(state, hwnd);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_LAYOUT_EDIT_VALUE_EDIT && HIWORD(wParam) == EN_CHANGE) {
                PreviewLayoutEditValueDialog(state, hwnd);
                return TRUE;
            }
            switch (LOWORD(wParam)) {
                case IDOK: {
                    wchar_t buffer[128] = {};
                    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, buffer, ARRAYSIZE(buffer));
                    if (state->valueFormat == configschema::ValueFormat::Integer) {
                        const std::optional<int> value = TryParseDialogInteger(buffer);
                        if (!value.has_value()) {
                            MessageBoxW(hwnd, L"Enter a whole number.", state->title.c_str(), MB_ICONERROR);
                            SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT));
                            SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, EM_SETSEL, 0, -1);
                            return TRUE;
                        }
                        state->result = static_cast<double>(*value);
                    } else {
                        const std::optional<double> value = TryParseDialogDouble(buffer);
                        if (!value.has_value()) {
                            MessageBoxW(hwnd, L"Enter a valid number.", state->title.c_str(), MB_ICONERROR);
                            SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT));
                            SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, EM_SETSEL, 0, -1);
                            return TRUE;
                        }
                        state->result = *value;
                    }
                    state->accepted = true;
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    RestoreLayoutEditValueDialog(state);
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
        case WM_CLOSE:
            RestoreLayoutEditValueDialog(state);
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK LayoutEditWeightsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<LayoutEditWeightsDialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<LayoutEditWeightsDialogState*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            SetWindowTextW(hwnd, state->title.c_str());
            SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_LABEL, state->firstLabel.c_str());
            SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL, state->secondLabel.c_str());
            SetDlgItemTextW(
                hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, WideFromUtf8(std::to_string(state->firstValue)).c_str());
            SetDlgItemTextW(
                hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT, WideFromUtf8(std::to_string(state->secondValue)).c_str());
            SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, EM_SETSEL, 0, -1);
            PreviewLayoutEditWeightsDialog(state, hwnd);
            return TRUE;
        }
        case WM_COMMAND:
            if ((LOWORD(wParam) == IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT ||
                    LOWORD(wParam) == IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT) &&
                HIWORD(wParam) == EN_CHANGE) {
                PreviewLayoutEditWeightsDialog(state, hwnd);
                return TRUE;
            }
            switch (LOWORD(wParam)) {
                case IDOK: {
                    wchar_t firstBuffer[64] = {};
                    wchar_t secondBuffer[64] = {};
                    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, firstBuffer, ARRAYSIZE(firstBuffer));
                    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT, secondBuffer, ARRAYSIZE(secondBuffer));
                    const std::optional<int> first = TryParseDialogInteger(firstBuffer);
                    const std::optional<int> second = TryParseDialogInteger(secondBuffer);
                    if (!first.has_value() || !second.has_value() || *first < 1 || *second < 1) {
                        MessageBoxW(hwnd,
                            L"Enter positive integer weights for both neighboring items.",
                            state->title.c_str(),
                            MB_ICONERROR);
                        return TRUE;
                    }
                    state->result = std::make_pair(*first, *second);
                    state->accepted = true;
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    RestoreLayoutEditWeightsDialog(state);
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
        case WM_CLOSE:
            RestoreLayoutEditWeightsDialog(state);
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK LayoutEditFontDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<LayoutEditFontDialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<LayoutEditFontDialogState*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            SetWindowTextW(hwnd, state->title.c_str());
            SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_PROMPT, state->prompt.c_str());
            PopulateFontFaceComboBox(hwnd, WideFromUtf8(state->initialValue.face));
            SetDlgItemTextW(
                hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, WideFromUtf8(std::to_string(state->initialValue.size)).c_str());
            SetDlgItemTextW(
                hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, WideFromUtf8(std::to_string(state->initialValue.weight)).c_str());
            SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT, EM_SETSEL, 0, -1);
            PreviewLayoutEditFontDialog(state, hwnd);
            return TRUE;
        }
        case WM_COMMAND:
            if ((LOWORD(wParam) == IDC_LAYOUT_EDIT_FONT_FACE_EDIT &&
                    (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_EDITCHANGE)) ||
                ((LOWORD(wParam) == IDC_LAYOUT_EDIT_FONT_SIZE_EDIT ||
                     LOWORD(wParam) == IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT) &&
                    HIWORD(wParam) == EN_CHANGE)) {
                PreviewLayoutEditFontDialog(state, hwnd, HIWORD(wParam));
                return TRUE;
            }
            switch (LOWORD(wParam)) {
                case IDOK: {
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
                        MessageBoxW(hwnd, L"Enter a font name.", state->title.c_str(), MB_ICONERROR);
                        SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT));
                        return TRUE;
                    }
                    if (!size.has_value() || *size < 1) {
                        MessageBoxW(hwnd, L"Enter a font size of 1 or greater.", state->title.c_str(), MB_ICONERROR);
                        SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT));
                        SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, EM_SETSEL, 0, -1);
                        return TRUE;
                    }
                    if (!weight.has_value()) {
                        MessageBoxW(hwnd, L"Enter an integer font weight.", state->title.c_str(), MB_ICONERROR);
                        SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT));
                        SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, EM_SETSEL, 0, -1);
                        return TRUE;
                    }
                    state->result = UiFontConfig{Utf8FromWide(faceText), *size, *weight};
                    state->accepted = true;
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    RestoreLayoutEditFontDialog(state);
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
        case WM_CLOSE:
            RestoreLayoutEditFontDialog(state);
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

std::optional<double> DashboardShellUi::PromptLayoutEditValue(DashboardRenderer::LayoutEditParameter parameter,
    const LayoutEditTooltipDescriptor& descriptor,
    double initialValue,
    const std::wstring& title) {
    LayoutEditValueDialogState state;
    state.title = title;
    state.prompt = WideFromUtf8("[" + descriptor.sectionName + "] " + descriptor.memberName);
    state.initialText = WideFromUtf8(FormatLayoutEditTooltipValue(initialValue, descriptor.valueFormat));
    state.valueFormat = descriptor.valueFormat;
    state.preview = [this, parameter](double value) { return app_.ApplyLayoutEditValue(parameter, value); };
    state.restore = [this, parameter, initialValue]() { app_.ApplyLayoutEditValue(parameter, initialValue); };
    DashboardShellUiModalScope scopedModalUi(*this);
    if (DialogBoxParamW(app_.instance_,
            MAKEINTRESOURCEW(IDD_LAYOUT_EDIT_VALUE),
            app_.hwnd_,
            LayoutEditValueDialogProc,
            reinterpret_cast<LPARAM>(&state)) == IDOK) {
        return state.result;
    }
    return std::nullopt;
}

std::optional<std::vector<int>> DashboardShellUi::PromptLayoutGuideWeights(
    const DashboardRenderer::LayoutEditGuide& guide, const std::wstring& title) {
    const LayoutNodeConfig* node = FindLayoutGuideNode(app_.controller_.State().config, guide);
    if (node == nullptr) {
        return std::nullopt;
    }
    std::vector<int> weights = layout_edit::SeedGuideWeights(guide, node);
    if (guide.separatorIndex + 1 >= weights.size() || guide.separatorIndex + 1 >= node->children.size()) {
        return std::nullopt;
    }

    LayoutEditWeightsDialogState state;
    state.title = title;
    state.firstLabel = BuildLayoutGuideItemLabel(*node, guide.separatorIndex, guide.axis, true);
    state.secondLabel = BuildLayoutGuideItemLabel(*node, guide.separatorIndex + 1, guide.axis, false);
    state.firstValue = std::max(1, weights[guide.separatorIndex]);
    state.secondValue = std::max(1, weights[guide.separatorIndex + 1]);
    const LayoutEditHost::LayoutTarget target = LayoutEditHost::LayoutTarget::ForGuide(guide);
    const std::vector<int> originalWeights = weights;
    state.preview = [this, target, originalWeights, separatorIndex = guide.separatorIndex](
                        const std::pair<int, int>& editedWeights) {
        std::vector<int> previewWeights = originalWeights;
        if (separatorIndex + 1 >= previewWeights.size()) {
            return false;
        }
        previewWeights[separatorIndex] = editedWeights.first;
        previewWeights[separatorIndex + 1] = editedWeights.second;
        return app_.ApplyLayoutGuideWeights(target, previewWeights);
    };
    state.restore = [this, target, originalWeights]() { app_.ApplyLayoutGuideWeights(target, originalWeights); };
    DashboardShellUiModalScope scopedModalUi(*this);
    if (DialogBoxParamW(app_.instance_,
            MAKEINTRESOURCEW(IDD_LAYOUT_EDIT_WEIGHTS),
            app_.hwnd_,
            LayoutEditWeightsDialogProc,
            reinterpret_cast<LPARAM>(&state)) != IDOK ||
        !state.result.has_value()) {
        return std::nullopt;
    }

    weights[guide.separatorIndex] = state.result->first;
    weights[guide.separatorIndex + 1] = state.result->second;
    return weights;
}

std::optional<UiFontConfig> DashboardShellUi::PromptLayoutEditFont(DashboardRenderer::LayoutEditParameter parameter,
    const LayoutEditTooltipDescriptor& descriptor,
    const UiFontConfig& initialValue,
    const std::wstring& title) {
    LayoutEditFontDialogState state;
    state.title = title;
    state.prompt = WideFromUtf8("[" + descriptor.sectionName + "] " + descriptor.memberName);
    state.initialValue = initialValue;
    state.preview = [this, parameter](const UiFontConfig& value) {
        return app_.controller_.ApplyLayoutEditFont(app_, parameter, value);
    };
    state.restore =
        [this, parameter, initialValue]() { app_.controller_.ApplyLayoutEditFont(app_, parameter, initialValue); };
    DashboardShellUiModalScope scopedModalUi(*this);
    if (DialogBoxParamW(app_.instance_,
            MAKEINTRESOURCEW(IDD_LAYOUT_EDIT_FONT),
            app_.hwnd_,
            LayoutEditFontDialogProc,
            reinterpret_cast<LPARAM>(&state)) == IDOK) {
        return state.result;
    }
    return std::nullopt;
}

bool DashboardShellUi::PromptAndApplyLayoutEditTarget(const LayoutEditController::TooltipTarget& target) {
    if (target.kind == LayoutEditController::TooltipTarget::Kind::LayoutGuide) {
        const std::wstring title = BuildLayoutEditDialogTitle(BuildLayoutGuideEditLabel(target.layoutGuide));
        const auto weights = PromptLayoutGuideWeights(target.layoutGuide, title);
        return weights.has_value() &&
               app_.ApplyLayoutGuideWeights(LayoutEditHost::LayoutTarget::ForGuide(target.layoutGuide), *weights);
    }

    const DashboardRenderer::LayoutEditParameter parameter =
        target.kind == LayoutEditController::TooltipTarget::Kind::WidgetGuide
            ? target.widgetGuide.parameter
            : target.kind == LayoutEditController::TooltipTarget::Kind::GapEditAnchor
                  ? target.gapEditAnchor.key.parameter
                  : target.editableAnchor.key.parameter;
    const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
    if (!descriptor.has_value()) {
        return false;
    }
    const std::wstring title = BuildLayoutEditDialogTitle(WideFromUtf8(GetLayoutEditParameterDisplayName(parameter)));
    if (descriptor->valueFormat == configschema::ValueFormat::FontSpec) {
        const auto font = FindLayoutEditTooltipFontValue(app_.controller_.State().config, parameter);
        if (!font.has_value() || *font == nullptr) {
            return false;
        }
        const auto updated = PromptLayoutEditFont(parameter, *descriptor, **font, title);
        return updated.has_value() && app_.controller_.ApplyLayoutEditFont(app_, parameter, *updated);
    }

    const double currentValue =
        target.kind == LayoutEditController::TooltipTarget::Kind::WidgetGuide
            ? target.widgetGuide.value
            : target.kind == LayoutEditController::TooltipTarget::Kind::GapEditAnchor
                  ? target.gapEditAnchor.value
                  : static_cast<double>(target.editableAnchor.value);
    const auto updated = PromptLayoutEditValue(parameter, *descriptor, currentValue, title);
    return updated.has_value() && app_.ApplyLayoutEditValue(parameter, *updated);
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

void DashboardShellUi::ExecuteCommand(
    UINT selected,
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

void DashboardShellUi::InvokeDefaultAction(
    MenuSource source,
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
        if (layoutEditTarget->kind == LayoutEditController::TooltipTarget::Kind::LayoutGuide) {
            label = BuildLayoutEditMenuLabel(BuildLayoutGuideEditLabel(layoutEditTarget->layoutGuide));
        } else {
            const DashboardRenderer::LayoutEditParameter parameter =
                layoutEditTarget->kind == LayoutEditController::TooltipTarget::Kind::WidgetGuide
                    ? layoutEditTarget->widgetGuide.parameter
                    : layoutEditTarget->kind == LayoutEditController::TooltipTarget::Kind::GapEditAnchor
                          ? layoutEditTarget->gapEditAnchor.key.parameter
                          : layoutEditTarget->editableAnchor.key.parameter;
            label = BuildLayoutEditMenuLabel(WideFromUtf8(GetLayoutEditParameterDisplayName(parameter)));
        }
        AppendMenuW(menu, MF_STRING, kCommandEditLayoutTarget, label.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, kCommandMove, L"Move");
    AppendMenuW(menu,
        MF_STRING | (state.isEditingLayout ? MF_CHECKED : MF_UNCHECKED),
        kCommandEditLayout,
        L"Edit layout");
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
