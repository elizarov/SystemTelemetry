#include "layout_edit_dialog/impl/editors.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "config/color_expression.h"
#include "layout_edit/board_metric_binding.h"
#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_tooltip.h"
#include "layout_edit_dialog/impl/pane.h"
#include "layout_edit_dialog/impl/trace.h"
#include "layout_edit_dialog/impl/util.h"
#include "util/numeric_format.h"
#include "util/strings.h"
#include "util/text_format.h"
#include "util/utf8.h"

namespace {

struct StringLiteralList {
    const char* const* values = nullptr;
    size_t count = 0;
};

// Size: static literal lists keep combo setup off vector initializer-list construction.
constexpr const char* kColorModeOptions[] = {"Literal", "Derived"};
constexpr const char* kColorExpressionTokens[] = {"background", "foreground", "accent", "guide"};
constexpr const char* kClockTimeFormats[] = {"HH:MM",
    "HH:MM:SS",
    "H:MM",
    "H:MM:SS",
    "HH.MM",
    "HH.MM.SS",
    "hh:MM AM",
    "h:MM AM",
    "hh:MM:SS AM",
    "h:MM:SS AM",
    "hh:MM am",
    "h:MM am",
    "hh:MM:SS am",
    "h:MM:SS am"};
constexpr const char* kClockDateFormats[] = {"YYYY-MM-DD",
    "YYYY/MM/DD",
    "YYYY.MM.DD",
    "DD.MM.YYYY",
    "D.M.YYYY",
    "MM/DD/YYYY",
    "M/D/YYYY",
    "DD/MM/YYYY",
    "D/M/YYYY",
    "DD-MM-YYYY",
    "MM-DD-YYYY",
    "MMM DD, YYYY",
    "MMM D, YYYY",
    "MMMM DD, YYYY",
    "MMMM D, YYYY",
    "DD MMM YYYY",
    "D MMM YYYY",
    "DD MMMM YYYY",
    "D MMMM YYYY",
    "dddd, MMMM DD",
    "dddd, MMMM D",
    "ddd, MMM DD"};

const char* QuotedBoolText(bool value) {
    return value ? "\"true\"" : "\"false\"";
}

StringLiteralList MakeStringLiteralList(const char* const* values, size_t count) {
    return {values, count};
}

void ShowLayoutEditSelectionEditor(
    LayoutEditDialogState* state, HWND hwnd, LayoutEditEditorKind kind, bool metricBinding = false);

const LayoutNodeFieldEditKey* SelectedNodeFieldKey(const LayoutEditDialogState* state) {
    return state != nullptr && state->selectedLeaf != nullptr
               ? std::get_if<LayoutNodeFieldEditKey>(&state->selectedLeaf->focusKey)
               : nullptr;
}

const LayoutNodeFieldEditDescriptor* SelectedNodeFieldDescriptor(const LayoutEditDialogState* state) {
    const LayoutNodeFieldEditKey* key = SelectedNodeFieldKey(state);
    return key != nullptr ? FindLayoutNodeFieldEditDescriptor(*key) : nullptr;
}

const LayoutNodeFieldEditKey* SelectedMetricListOrderKey(const LayoutEditDialogState* state) {
    const LayoutNodeFieldEditKey* key = SelectedNodeFieldKey(state);
    const LayoutNodeFieldEditDescriptor* descriptor = SelectedNodeFieldDescriptor(state);
    return key != nullptr && descriptor != nullptr && descriptor->editorKind == LayoutEditEditorKind::MetricListOrder
               ? key
               : nullptr;
}

bool IsFontsSectionNode(const LayoutEditDialogState* state) {
    return state != nullptr && state->selectedLeaf == nullptr && state->selectedNode != nullptr &&
           state->selectedNode->kind == LayoutEditTreeNodeKind::Section && state->selectedNode->label == "fonts";
}

bool IsThemeSectionNode(const LayoutEditDialogState* state) {
    return state != nullptr && state->selectedLeaf == nullptr && state->selectedNode != nullptr &&
           state->selectedNode->kind == LayoutEditTreeNodeKind::Section &&
           state->selectedNode->label.rfind("theme.", 0) == 0;
}

bool IsLayoutSectionNode(const LayoutEditDialogState* state) {
    return state != nullptr && state->selectedLeaf == nullptr && state->selectedNode != nullptr &&
           state->selectedNode->kind == LayoutEditTreeNodeKind::Section &&
           state->selectedNode->label.rfind("layout.", 0) == 0;
}

const ColorConfig* FindThemeColorValue(const AppConfig& config, const ThemeColorEditKey& key) {
    const ThemeConfig* selectedTheme = nullptr;
    for (const ThemeConfig& theme : config.layout.themes) {
        if (theme.name == key.themeName) {
            selectedTheme = &theme;
            break;
        }
    }
    if (selectedTheme == nullptr) {
        return nullptr;
    }
    if (key.tokenName == "background")
        return &selectedTheme->background;
    if (key.tokenName == "foreground")
        return &selectedTheme->foreground;
    if (key.tokenName == "accent")
        return &selectedTheme->accent;
    if (key.tokenName == "guide")
        return &selectedTheme->guide;
    return nullptr;
}

const ColorConfig* FindColorRoleValue(const AppConfig& config, LayoutEditParameter parameter) {
    switch (parameter) {
        case LayoutEditParameter::ColorBackground:
            return &config.layout.colors.backgroundColor;
        case LayoutEditParameter::ColorForeground:
            return &config.layout.colors.foregroundColor;
        case LayoutEditParameter::ColorIcon:
            return &config.layout.colors.iconColor;
        case LayoutEditParameter::ColorPeakGhost:
            return &config.layout.colors.peakGhostColor;
        case LayoutEditParameter::ColorWarning:
            return &config.layout.colors.warningColor;
        case LayoutEditParameter::ColorAccent:
            return &config.layout.colors.accentColor;
        case LayoutEditParameter::ColorLayoutGuide:
            return &config.layout.colors.layoutGuideColor;
        case LayoutEditParameter::ColorActiveEdit:
            return &config.layout.colors.activeEditColor;
        case LayoutEditParameter::ColorPanelBorder:
            return &config.layout.colors.panelBorderColor;
        case LayoutEditParameter::ColorMutedText:
            return &config.layout.colors.mutedTextColor;
        case LayoutEditParameter::ColorTrack:
            return &config.layout.colors.trackColor;
        case LayoutEditParameter::ColorPanelFill:
            return &config.layout.colors.panelFillColor;
        case LayoutEditParameter::ColorGraphBackground:
            return &config.layout.colors.graphBackgroundColor;
        case LayoutEditParameter::ColorGraphAxis:
            return &config.layout.colors.graphAxisColor;
        case LayoutEditParameter::ColorGraphMarker:
            return &config.layout.colors.graphMarkerColor;
        default:
            return nullptr;
    }
}

bool IsLiteralColorExpressionText(const std::string& text) {
    return text.empty() || text.front() == '#';
}

std::string DefaultDerivedBase(LayoutEditParameter parameter) {
    switch (parameter) {
        case LayoutEditParameter::ColorBackground:
        case LayoutEditParameter::ColorPanelFill:
        case LayoutEditParameter::ColorGraphBackground:
        case LayoutEditParameter::ColorPanelBorder:
        case LayoutEditParameter::ColorTrack:
        case LayoutEditParameter::ColorGraphAxis:
        case LayoutEditParameter::ColorGraphMarker:
            return "background";
        case LayoutEditParameter::ColorForeground:
        case LayoutEditParameter::ColorIcon:
        case LayoutEditParameter::ColorMutedText:
            return "foreground";
        case LayoutEditParameter::ColorAccent:
        case LayoutEditParameter::ColorPeakGhost:
            return "accent";
        case LayoutEditParameter::ColorLayoutGuide:
        case LayoutEditParameter::ColorActiveEdit:
        case LayoutEditParameter::ColorWarning:
            return "guide";
        default:
            return "accent";
    }
}

std::string ReadComboTextUtf8(HWND hwnd, int controlId) {
    HWND combo = GetDlgItem(hwnd, controlId);
    if (combo == nullptr) {
        return {};
    }
    const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection != CB_ERR) {
        wchar_t buffer[128] = {};
        SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(selection), reinterpret_cast<LPARAM>(buffer));
        return Utf8FromWide(buffer);
    }
    return ReadDialogControlTextUtf8(hwnd, controlId);
}

void PopulateTextCombo(HWND hwnd, int controlId, StringLiteralList options, std::string_view selected) {
    HWND combo = GetDlgItem(hwnd, controlId);
    if (combo == nullptr) {
        return;
    }
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int selectedIndex = CB_ERR;
    for (size_t i = 0; i < options.count; ++i) {
        const char* option = options.values[i];
        const LRESULT index = AddComboStringUtf8(combo, option);
        if (index != CB_ERR && selectedIndex == CB_ERR && std::string_view(option) == selected) {
            selectedIndex = static_cast<int>(index);
        }
    }
    if (selectedIndex == CB_ERR && SendMessageW(combo, CB_GETCOUNT, 0, 0) > 0) {
        selectedIndex = 0;
    }
    if (selectedIndex != CB_ERR) {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
    }
}

// Size: fill config-name combos in place; temporary vector<string> lists measured larger here.
void PopulateThemeNameCombo(HWND hwnd, const AppConfig& config) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO);
    if (combo == nullptr) {
        return;
    }
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int selectedIndex = CB_ERR;
    for (const ThemeConfig& theme : config.layout.themes) {
        const LRESULT index = AddComboStringUtf8(combo, theme.name);
        if (index != CB_ERR && theme.name == config.display.theme) {
            selectedIndex = static_cast<int>(index);
        }
    }
    if (selectedIndex == CB_ERR && SendMessageW(combo, CB_GETCOUNT, 0, 0) > 0) {
        selectedIndex = 0;
    }
    if (selectedIndex != CB_ERR) {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
    }
}

void PopulateLayoutNameCombo(HWND hwnd, const AppConfig& config) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO);
    if (combo == nullptr) {
        return;
    }
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int selectedIndex = CB_ERR;
    for (const LayoutSectionConfig& layout : config.layout.layouts) {
        const LRESULT index = AddComboStringUtf8(combo, layout.name);
        if (index != CB_ERR && layout.name == config.display.layout) {
            selectedIndex = static_cast<int>(index);
        }
    }
    if (selectedIndex == CB_ERR && SendMessageW(combo, CB_GETCOUNT, 0, 0) > 0) {
        selectedIndex = 0;
    }
    if (selectedIndex != CB_ERR) {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
    }
}

std::string FormatDialogDouble(double value) {
    return FormatDoubleGeneral(value, 12);
}

std::string FormatDialogAlphaByte(unsigned int alpha) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string text = "0x00";
    text[2] = kHex[(alpha >> 4) & 0x0Fu];
    text[3] = kHex[alpha & 0x0Fu];
    return text;
}

int RoundToSliderPosition(double value, int minValue, int maxValue) {
    return std::clamp(static_cast<int>(std::lround(value)), minValue, maxValue);
}

void SetDerivedRotateSliderPosition(HWND hwnd, double value) {
    SendDlgItemMessageW(
        hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER, TBM_SETPOS, TRUE, RoundToSliderPosition(value, -180, 180));
}

void SetDerivedMixSliderPosition(HWND hwnd, double value) {
    SendDlgItemMessageW(
        hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER, TBM_SETPOS, TRUE, RoundToSliderPosition(value * 100.0, 0, 100));
}

void SetDerivedAlphaSliderPosition(HWND hwnd, unsigned int value) {
    SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER, TBM_SETPOS, TRUE, std::min(value, 255u));
}

bool IsDerivedColorMode(HWND hwnd) {
    return SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_COLOR_MODE_COMBO, CB_GETCURSEL, 0, 0) == 1;
}

std::optional<ColorExpression> ReadDerivedColorExpressionFromDialog(HWND hwnd) {
    ColorExpression expression;
    expression.base = ReadComboTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_BASE_COMBO);
    if (expression.base.empty()) {
        return std::nullopt;
    }
    if (IsDlgButtonChecked(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_CHECK) == BST_CHECKED) {
        const auto value = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT);
        if (!value.has_value()) {
            return std::nullopt;
        }
        expression.rotateHue = *value;
    }
    if (IsDlgButtonChecked(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_CHECK) == BST_CHECKED) {
        const std::string target = ReadComboTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO);
        const auto amount = TryParseDialogControlDouble(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT);
        if (target.empty() || !amount.has_value() || *amount < 0.0 || *amount > 1.0) {
            return std::nullopt;
        }
        expression.mix = ColorMixExpression{target, *amount};
    }
    if (IsDlgButtonChecked(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_CHECK) == BST_CHECKED) {
        expression.alpha =
            ParseColorExpressionAlphaByte(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT));
        if (!expression.alpha.has_value()) {
            return std::nullopt;
        }
    }
    return expression;
}

void PopulateColorExpressionControls(HWND hwnd, LayoutEditParameter parameter, const ColorConfig& color) {
    const std::optional<ColorExpression> parsed =
        !color.expression.empty() && !IsLiteralColorExpressionText(color.expression)
            ? ParseColorExpression(color.expression)
            : std::nullopt;
    const bool derived = parsed.has_value();
    PopulateTextCombo(hwnd,
        IDC_LAYOUT_EDIT_COLOR_MODE_COMBO,
        MakeStringLiteralList(kColorModeOptions, ARRAYSIZE(kColorModeOptions)),
        derived ? "Derived" : "Literal");

    ColorExpression expression = parsed.value_or(ColorExpression{DefaultDerivedBase(parameter)});
    if (expression.mix.has_value() && expression.mix->target.empty()) {
        expression.mix->target = "accent";
    }
    const StringLiteralList tokens = MakeStringLiteralList(kColorExpressionTokens, ARRAYSIZE(kColorExpressionTokens));
    PopulateTextCombo(hwnd, IDC_LAYOUT_EDIT_COLOR_BASE_COMBO, tokens, expression.base);
    PopulateTextCombo(hwnd,
        IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO,
        tokens,
        expression.mix.has_value() ? expression.mix->target : "accent");
    CheckDlgButton(
        hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_CHECK, expression.rotateHue.has_value() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_CHECK, expression.mix.has_value() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_CHECK, expression.alpha.has_value() ? BST_CHECKED : BST_UNCHECKED);
    SetDialogControlTextUtf8(
        hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT, FormatDialogDouble(expression.rotateHue.value_or(0.0)));
    SetDialogControlTextUtf8(hwnd,
        IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT,
        FormatDialogDouble(expression.mix.has_value() ? expression.mix->amount : 0.5));
    SetDialogControlTextUtf8(hwnd,
        IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT,
        FormatDialogAlphaByte(expression.alpha.value_or(color.Alpha())));
    SetDerivedRotateSliderPosition(hwnd, expression.rotateHue.value_or(0.0));
    SetDerivedMixSliderPosition(hwnd, expression.mix.has_value() ? expression.mix->amount : 0.5);
    SetDerivedAlphaSliderPosition(hwnd, expression.alpha.value_or(color.Alpha()));
}

StringLiteralList StandardDateTimeFormats(const LayoutNodeFieldEditKey& key) {
    if (key.widgetClass == WidgetClass::ClockTime) {
        return MakeStringLiteralList(kClockTimeFormats, ARRAYSIZE(kClockTimeFormats));
    }
    if (key.widgetClass == WidgetClass::ClockDate) {
        return MakeStringLiteralList(kClockDateFormats, ARRAYSIZE(kClockDateFormats));
    }
    return {};
}

void PopulateDateTimeFormatCombo(HWND hwnd, const LayoutNodeFieldEditKey& key, std::string_view selected) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO);
    if (combo == nullptr) {
        return;
    }
    const StringLiteralList options = StandardDateTimeFormats(key);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int selectedIndex = CB_ERR;
    for (size_t i = 0; i < options.count; ++i) {
        const char* option = options.values[i];
        const LRESULT index = AddComboStringUtf8(combo, option);
        if (index != CB_ERR && std::string_view(option) == selected) {
            selectedIndex = static_cast<int>(index);
        }
    }
    if (!selected.empty() && selectedIndex == CB_ERR) {
        const LRESULT index = AddComboStringUtf8(combo, selected);
        if (index != CB_ERR) {
            selectedIndex = static_cast<int>(index);
        }
    }
    if (selectedIndex != CB_ERR) {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
    }
    SendMessageW(combo, CB_SETMINVISIBLE, 8, 0);
}

std::array<const UiFontConfig*, 9> FontSetValues(const UiFontSetConfig& fonts) {
    return {
        &fonts.title,
        &fonts.big,
        &fonts.value,
        &fonts.label,
        &fonts.text,
        &fonts.smallText,
        &fonts.footer,
        &fonts.clockTime,
        &fonts.clockDate,
    };
}

std::string CommonFontFamilyText(const UiFontSetConfig& fonts) {
    const auto fontValues = FontSetValues(fonts);
    if (fontValues.empty() || fontValues.front() == nullptr) {
        return {};
    }
    const std::string& firstFace = fontValues.front()->face;
    for (const UiFontConfig* font : fontValues) {
        if (font == nullptr || font->face != firstFace) {
            return {};
        }
    }
    return firstFace;
}

std::vector<std::string> ReadMetricListOrderDialogRows(const LayoutEditDialogState* state, HWND hwnd) {
    std::vector<std::string> metricRefs;
    if (state == nullptr) {
        return metricRefs;
    }
    metricRefs.reserve(state->metricListRowControls.size());
    for (const auto& row : state->metricListRowControls) {
        const std::string metricId = Trim(ReadDialogControlTextUtf8(hwnd, row.comboId));
        if (!metricId.empty()) {
            metricRefs.push_back(metricId);
        }
    }
    return metricRefs;
}

bool ApplyMetricListOrderRows(LayoutEditDialogState* state, const std::vector<std::string>& metricRefs) {
    const auto* key = SelectedMetricListOrderKey(state);
    return key != nullptr &&
           state->dialog->Host().ApplyLayoutEditPreview(LayoutEditFocusKey{*key}, LayoutEditValue{metricRefs});
}

bool ContainsString(const std::vector<std::string>& values, const std::string& text) {
    for (const std::string& value : values) {
        if (value == text) {
            return true;
        }
    }
    return false;
}

void PopulateMetricListRowCombo(HWND,
    const LayoutEditMetricListRowControls& row,
    const std::vector<std::string>& options,
    std::string_view selected) {
    if (row.combo == nullptr) {
        return;
    }

    SendMessageW(row.combo, CB_RESETCONTENT, 0, 0);
    int selectedIndex = CB_ERR;
    for (const auto& option : options) {
        const LRESULT index = AddComboStringUtf8(row.combo, option);
        if (index != CB_ERR && selectedIndex == CB_ERR && option == selected) {
            selectedIndex = static_cast<int>(index);
        }
    }
    if (selectedIndex != CB_ERR) {
        SendMessageW(row.combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
    } else {
        SetWindowTextUtf8(row.combo, selected);
    }
    SendMessageW(row.combo, CB_SETMINVISIBLE, 10, 0);
}

int MetricListRowIndexFromControlId(int controlId, int baseId) {
    return controlId - baseId;
}

bool IsMetricListRowControlId(int controlId, int baseId, size_t rowCount) {
    const int index = MetricListRowIndexFromControlId(controlId, baseId);
    return index >= 0 && index < static_cast<int>(rowCount);
}

template <typename Mutate> bool MutateMetricListOrderRows(LayoutEditDialogState* state, HWND hwnd, Mutate&& mutate) {
    if (state == nullptr || state->updatingControls) {
        return false;
    }
    std::vector<std::string> metricRefs = ReadMetricListOrderDialogRows(state, hwnd);
    mutate(metricRefs);
    if (!ApplyMetricListOrderRows(state, metricRefs)) {
        return false;
    }
    PopulateLayoutEditSelection(state, hwnd);
    RefreshLayoutEditValidationState(state, hwnd);
    return true;
}

bool PopulateMetricListOrderSelection(LayoutEditDialogState* state, HWND hwnd) {
    const auto* key = SelectedMetricListOrderKey(state);
    if (key == nullptr) {
        return false;
    }

    const AppConfig& config = state->dialog->Host().CurrentConfig();
    std::vector<std::string> metricRefs;
    if (const LayoutNodeConfig* node = FindLayoutNodeFieldNode(config, *key); node != nullptr) {
        metricRefs = ParseMetricListMetricRefs(node->parameter);
    }
    std::vector<std::string> options = AvailableMetricDefinitionIds(config);
    for (const auto& metricRef : metricRefs) {
        const MetricDefinitionConfig* definition = FindMetricDefinition(config.layout.metrics, metricRef);
        if (definition != nullptr && IsMetricListSupportedDisplayStyle(definition->style) &&
            !ContainsString(options, metricRef)) {
            options.push_back(metricRef);
        }
    }
    EnsureMetricListOrderEditorControls(state, hwnd, metricRefs.size());
    for (size_t i = 0; i < state->metricListRowControls.size(); ++i) {
        PopulateMetricListRowCombo(hwnd, state->metricListRowControls[i], options, metricRefs[i]);
        EnableWindow(state->metricListRowControls[i].upButton, i > 0 ? TRUE : FALSE);
        EnableWindow(state->metricListRowControls[i].downButton, i + 1 < metricRefs.size() ? TRUE : FALSE);
        EnableWindow(state->metricListRowControls[i].deleteButton, TRUE);
    }
    if (state->metricListAddRowButton != nullptr) {
        EnableWindow(state->metricListAddRowButton, !options.empty() ? TRUE : FALSE);
    }
    ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::MetricListOrder);
    state->dialog->Host().TraceLayoutEditDialogEvent("populate_selection",
        BuildTraceNodeDetail(state->selectedNode, " editor=\"metric_list_order\" rows=\"%zu\"", metricRefs.size()));
    return true;
}

bool PopulateDateTimeFormatSelection(LayoutEditDialogState* state, HWND hwnd) {
    const auto* key = SelectedNodeFieldKey(state);
    const LayoutNodeFieldEditDescriptor* descriptor = SelectedNodeFieldDescriptor(state);
    if (key == nullptr || descriptor == nullptr || descriptor->editorKind != LayoutEditEditorKind::DateTimeFormat) {
        return false;
    }

    std::string format;
    const AppConfig& config = state->dialog->Host().CurrentConfig();
    if (const LayoutNodeConfig* node = FindLayoutNodeFieldNode(config, *key); node != nullptr) {
        format = ReadLayoutNodeFieldValue(*node, key->field);
    }
    PopulateDateTimeFormatCombo(hwnd, *key, format);
    ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::DateTimeFormat);
    state->dialog->Host().TraceLayoutEditDialogEvent("populate_selection",
        BuildTraceNodeDetail(
            state->selectedNode, " editor=\"date_time_format\" format=%s", QuoteTraceText(format).c_str()));
    return true;
}

LayoutEditValidationResult ValidateMetricListOrderSelection(LayoutEditDialogState* state, HWND hwnd) {
    if (SelectedMetricListOrderKey(state) == nullptr) {
        return {false, "Choose a metric for each row."};
    }
    for (const auto& metricRef : ReadMetricListOrderDialogRows(state, hwnd)) {
        if (metricRef.empty()) {
            return {false, "Choose a metric for each row."};
        }
    }
    return {true, ""};
}

LayoutEditValidationResult ValidateDateTimeFormatSelection(LayoutEditDialogState* state, HWND hwnd) {
    const LayoutNodeFieldEditDescriptor* descriptor = SelectedNodeFieldDescriptor(state);
    if (descriptor == nullptr || descriptor->editorKind != LayoutEditEditorKind::DateTimeFormat) {
        return {false, "Choose a date or time format."};
    }
    const std::string format = Trim(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO));
    return !format.empty() ? LayoutEditValidationResult{true, ""}
                           : LayoutEditValidationResult{false, "Choose a date or time format."};
}

bool PreviewDateTimeFormatSelection(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr || state->updatingControls) {
        return false;
    }
    const auto* key = SelectedNodeFieldKey(state);
    const LayoutNodeFieldEditDescriptor* descriptor = SelectedNodeFieldDescriptor(state);
    if (key == nullptr || descriptor == nullptr || descriptor->editorKind != LayoutEditEditorKind::DateTimeFormat) {
        return false;
    }
    const std::string format = Trim(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO));
    const bool applied = !format.empty() && state->dialog->Host().ApplyLayoutEditPreview(
                                                LayoutEditFocusKey{*key}, LayoutEditValue{format});
    state->dialog->Host().TraceLayoutEditDialogEvent("preview_date_time_format",
        BuildTraceNodeDetail(
            state->selectedNode, " format=%s applied=%s", QuoteTraceText(format).c_str(), QuotedBoolText(applied)));
    return applied;
}

bool RevertNodeFieldSelection(LayoutEditDialogState* state, HWND hwnd) {
    const auto* nodeFieldKey = SelectedNodeFieldKey(state);
    if (nodeFieldKey == nullptr) {
        return false;
    }
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(*nodeFieldKey);
    const LayoutNodeConfig* node = FindLayoutNodeFieldNode(state->originalConfig, *nodeFieldKey);
    if (node == nullptr) {
        return false;
    }
    LayoutEditValue value = ReadLayoutNodeFieldValue(*node, nodeFieldKey->field);
    if (descriptor != nullptr && descriptor->editorKind == LayoutEditEditorKind::MetricListOrder) {
        value = ParseMetricListMetricRefs(node->parameter);
    }
    const bool applied = state->dialog->Host().ApplyLayoutEditPreview(LayoutEditFocusKey{*nodeFieldKey}, value);
    if (applied) {
        PopulateLayoutEditSelection(state, hwnd);
        RefreshLayoutEditValidationState(state, hwnd);
    }
    return applied;
}

struct DescriptorLayoutEditEditorHandler {
    LayoutEditEditorKind kind = LayoutEditEditorKind::Summary;
    bool (*populate)(LayoutEditDialogState*, HWND) = nullptr;
    LayoutEditValidationResult (*validate)(LayoutEditDialogState*, HWND) = nullptr;
    bool (*preview)(LayoutEditDialogState*, HWND) = nullptr;
    bool (*revert)(LayoutEditDialogState*, HWND) = nullptr;
};

constexpr std::array<DescriptorLayoutEditEditorHandler, 2> kDescriptorEditorHandlers{{
    {LayoutEditEditorKind::MetricListOrder,
        PopulateMetricListOrderSelection,
        ValidateMetricListOrderSelection,
        nullptr,
        RevertNodeFieldSelection},
    {LayoutEditEditorKind::DateTimeFormat,
        PopulateDateTimeFormatSelection,
        ValidateDateTimeFormatSelection,
        PreviewDateTimeFormatSelection,
        RevertNodeFieldSelection},
}};

const DescriptorLayoutEditEditorHandler* FindDescriptorLayoutEditEditorHandler(LayoutEditEditorKind kind) {
    for (const DescriptorLayoutEditEditorHandler& handler : kDescriptorEditorHandlers) {
        if (handler.kind == kind) {
            return &handler;
        }
    }
    return nullptr;
}

const DescriptorLayoutEditEditorHandler* SelectedDescriptorLayoutEditEditorHandler(const LayoutEditDialogState* state) {
    const LayoutNodeFieldEditDescriptor* descriptor = SelectedNodeFieldDescriptor(state);
    return descriptor != nullptr ? FindDescriptorLayoutEditEditorHandler(descriptor->editorKind) : nullptr;
}

bool PopulateDescriptorLayoutEditSelection(LayoutEditDialogState* state, HWND hwnd) {
    const DescriptorLayoutEditEditorHandler* handler = SelectedDescriptorLayoutEditEditorHandler(state);
    return handler != nullptr && handler->populate != nullptr && handler->populate(state, hwnd);
}

void ShowLayoutEditSelectionEditor(
    LayoutEditDialogState* state, HWND hwnd, LayoutEditEditorKind kind, bool metricBinding) {
    if (kind != LayoutEditEditorKind::MetricListOrder) {
        DestroyMetricListOrderEditorControls(state);
    }
    state->activeEditorKind = kind;
    state->activeEditorShowsMetricBinding = kind == LayoutEditEditorKind::Metric && metricBinding;
    ShowLayoutEditEditors(hwnd, kind, state->activeEditorShowsMetricBinding);
    SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
}

void FinishPopulateLayoutEditSelectionUi(LayoutEditDialogState* state, HWND hwnd, std::string_view status) {
    SetLayoutEditStatus(state, hwnd, LayoutEditStatusKind::Info, status);
    state->activeSelectionValid = true;
    state->updatingControls = false;
    LayoutLayoutEditRightPane(state, hwnd);
    UpdateLayoutEditActionState(state, hwnd);
    RefreshLayoutEditRightPane(hwnd);
}

void TracePopulateLayoutEditSelection(LayoutEditDialogState* state, const std::string& detail) {
    const std::string text = FormatText("%s%s", BuildTraceNodeText(state->selectedNode).c_str(), detail.c_str());
    state->dialog->Host().TraceLayoutEditDialogEvent("populate_selection", text);
}

void PopulateColorEditorControls(LayoutEditDialogState* state, HWND hwnd, unsigned int color) {
    SetColorDialogHex(hwnd, color);
    SetColorDialogChannel(hwnd, kColorDialogControls[0], (color >> 24) & 0xFFu);
    SetColorDialogChannel(hwnd, kColorDialogControls[1], (color >> 16) & 0xFFu);
    SetColorDialogChannel(hwnd, kColorDialogControls[2], (color >> 8) & 0xFFu);
    SetColorDialogChannel(hwnd, kColorDialogControls[3], color & 0xFFu);
    SetColorDialogLch(hwnd, color);
    SetColorDialogHsv(hwnd, color);
    ConfigureColorViewTabs(hwnd, state->colorEditViewMode);
    ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::Color);
    SetColorSamplePreview(state, hwnd, color);
}

}  // namespace

LayoutEditEditorKind CurrentLayoutEditEditorKind(const LayoutEditDialogState* state) {
    if (state == nullptr) {
        return LayoutEditEditorKind::Summary;
    }
    return state->activeEditorKind;
}

bool CurrentLayoutEditShowsMetricBinding(const LayoutEditDialogState* state) {
    return state != nullptr && state->activeEditorShowsMetricBinding;
}

void PopulateLayoutEditSelection(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr) {
        return;
    }

    state->updatingControls = true;
    SetLayoutEditDescription(state, hwnd, state->selectedNode);
    const AppConfig& config = state->dialog->Host().CurrentConfig();
    if (IsFontsSectionNode(state)) {
        SetDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_LABEL, "Family:");
        PopulateFontFaceComboBox(hwnd, CommonFontFamilyText(config.layout.fonts));
        ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::GlobalFontFamily);
        FinishPopulateLayoutEditSelectionUi(state, hwnd, "Previewing changes in the dashboard.");
        TracePopulateLayoutEditSelection(state, " editor=\"font_family\"");
        return;
    }
    if (IsThemeSectionNode(state)) {
        SetDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_THEME_LABEL, "Theme:");
        PopulateThemeNameCombo(hwnd, config);
        ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::ThemeSelector);
        FinishPopulateLayoutEditSelectionUi(state, hwnd, "Previewing changes in the dashboard.");
        InvalidateRect(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_THEME_PREVIEW), nullptr, TRUE);
        TracePopulateLayoutEditSelection(
            state, FormatText(" editor=\"theme_selector\" theme=%s", QuoteTraceText(config.display.theme).c_str()));
        return;
    }
    if (IsLayoutSectionNode(state)) {
        SetDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_THEME_LABEL, "Layout:");
        PopulateLayoutNameCombo(hwnd, config);
        ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::LayoutSelector);
        FinishPopulateLayoutEditSelectionUi(state, hwnd, "Previewing changes in the dashboard.");
        TracePopulateLayoutEditSelection(
            state, FormatText(" editor=\"layout_selector\" layout=%s", QuoteTraceText(config.display.layout).c_str()));
        return;
    }
    if (state->selectedLeaf == nullptr) {
        ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::Summary);
        FinishPopulateLayoutEditSelectionUi(state, hwnd, "Select a field to edit it here.");
        TracePopulateLayoutEditSelection(state, " editor=\"none\"");
        return;
    }

    std::string traceDetail;
    if (const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey)) {
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec) {
            SetDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_LABEL, "Font name:");
            const auto font = FindLayoutEditTooltipFontValue(config, *parameter);
            PopulateFontFaceComboBox(
                hwnd, font.has_value() && *font != nullptr ? std::string_view((**font).face) : std::string_view());
            const bool hasFont = font.has_value() && *font != nullptr;
            SetDialogControlIntegerOrEmpty(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT, hasFont ? (**font).size : 0, hasFont);
            SetDialogControlIntegerOrEmpty(
                hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT, hasFont ? (**font).weight : 0, hasFont);
            ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::Font);
            SetFontSamplePreview(state,
                hwnd,
                std::optional<LayoutEditParameter>(*parameter),
                font.has_value() && *font != nullptr ? *font : nullptr);
            traceDetail = FormatText(" editor=\"font\" face=%s size=%s weight=%s",
                QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT)).c_str(),
                QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT)).c_str(),
                QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT)).c_str());
        } else if (state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex) {
            const ColorConfig* value = FindColorRoleValue(config, *parameter);
            const unsigned int color = value != nullptr ? value->ToRgba() : 0x000000FFu;
            PopulateColorExpressionControls(hwnd, *parameter, value != nullptr ? *value : ColorConfig::FromRgba(color));
            PopulateColorEditorControls(state, hwnd, color);
            traceDetail = FormatText(" editor=\"color\"%s config_value=%s mode=%s expression=%s",
                BuildColorDialogTraceValues(hwnd).c_str(),
                QuoteTraceText(value != nullptr ? FormatTraceColorHex(value->ToRgba()) : "none").c_str(),
                QuoteTraceText(IsDerivedColorMode(hwnd) ? "derived" : "literal").c_str(),
                QuoteTraceText(value != nullptr && !value->expression.empty() ? value->expression : "").c_str());
        } else {
            const auto value = FindLayoutEditParameterNumericValue(config, *parameter);
            if (value.has_value()) {
                SetDialogControlTextUtf8(hwnd,
                    IDC_LAYOUT_EDIT_VALUE_EDIT,
                    FormatLayoutEditTooltipValue(*value, state->selectedLeaf->valueFormat));
            } else {
                SetDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, "");
            }
            ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::Numeric);
            traceDetail = FormatText(" editor=\"numeric\" text=%s",
                QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT)).c_str());
        }
    } else if (const auto* themeColorKey = std::get_if<ThemeColorEditKey>(&state->selectedLeaf->focusKey)) {
        const ColorConfig* value = FindThemeColorValue(config, *themeColorKey);
        const unsigned int color = value != nullptr ? value->ToRgba() : 0x000000FFu;
        PopulateColorEditorControls(state, hwnd, color);
        traceDetail = FormatText(" editor=\"theme_color\"%s config_value=%s",
            BuildColorDialogTraceValues(hwnd).c_str(),
            QuoteTraceText(value != nullptr ? FormatTraceColorHex(value->ToRgba()) : "none").c_str());
    } else if (const auto* weightKey = std::get_if<LayoutWeightEditKey>(&state->selectedLeaf->focusKey)) {
        const auto values = FindWeightEditValues(config, *weightKey);
        SetDialogControlTextUtf8(
            hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_LABEL, BuildWeightEditorLabel(*state->selectedLeaf, true));
        SetDialogControlTextUtf8(
            hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_LABEL, BuildWeightEditorLabel(*state->selectedLeaf, false));
        SetDialogControlIntegerOrEmpty(
            hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, values.has_value() ? values->first : 0, values.has_value());
        SetDialogControlIntegerOrEmpty(
            hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT, values.has_value() ? values->second : 0, values.has_value());
        ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::Weights);
        traceDetail = FormatText(" editor=\"weights\" first=%s second=%s",
            QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT)).c_str(),
            QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT)).c_str());
    } else if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&state->selectedLeaf->focusKey)) {
        SetDialogControlTextUtf8(
            hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, FindCardTitleValue(config, *cardTitleKey).value_or(""));
        ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::Numeric);
        traceDetail = FormatText(" editor=\"text\" text=%s",
            QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT)).c_str());
    } else if (PopulateDescriptorLayoutEditSelection(state, hwnd)) {
    } else if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey)) {
        const MetricDefinitionConfig* definition = FindMetricDefinition(config.layout.metrics, metricKey->metricId);
        SetDialogControlTextUtf8(
            hwnd, IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE, definition != nullptr ? EnumToString(definition->style) : "");
        const bool scaleEditable =
            definition != nullptr && !definition->telemetryScale && definition->style != MetricDisplayStyle::LabelOnly;
        const bool unitEditable = definition != nullptr && definition->style != MetricDisplayStyle::LabelOnly;
        if (definition == nullptr || definition->style == MetricDisplayStyle::LabelOnly) {
            SetDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT, "");
        } else if (definition->telemetryScale) {
            SetDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT, "*");
        } else {
            SetDialogControlTextUtf8(hwnd,
                IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT,
                FormatLayoutEditTooltipValue(definition->scale, configschema::ValueFormat::FloatingPoint));
        }
        SetDialogControlTextUtf8(hwnd,
            IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT,
            definition != nullptr && definition->style != MetricDisplayStyle::LabelOnly ? definition->unit : "");
        SetDialogControlTextUtf8(
            hwnd, IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT, definition != nullptr ? definition->label : "");
        const auto bindingTarget = ParseBoardMetricBindingTarget(metricKey->metricId);
        const bool showBinding = bindingTarget.has_value();
        const std::string selectedBinding =
            showBinding ? FindConfiguredBoardMetricBinding(config, *metricKey) : std::string();
        std::vector<std::string> bindingOptions =
            showBinding ? state->dialog->Host().AvailableBoardMetricSensorBindings(*metricKey)
                        : std::vector<std::string>{};
        if (!selectedBinding.empty() && !ContainsString(bindingOptions, selectedBinding)) {
            bindingOptions.push_back(selectedBinding);
        }
        SortUniqueStrings(bindingOptions);
        PopulateMetricBindingComboBox(hwnd, bindingOptions, selectedBinding, showBinding);
        EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT), scaleEditable ? TRUE : FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT), unitEditable ? TRUE : FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT), definition != nullptr ? TRUE : FALSE);
        ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::Metric, showBinding);
        traceDetail = FormatText(" editor=\"metric\"%s scale_editable=%s unit_editable=%s binding_visible=%s "
                                 "binding_options=\"%zu\"",
            BuildMetricDialogTraceValues(hwnd).c_str(),
            QuotedBoolText(scaleEditable),
            QuotedBoolText(unitEditable),
            QuotedBoolText(showBinding),
            bindingOptions.size());
    } else {
        ShowLayoutEditSelectionEditor(state, hwnd, LayoutEditEditorKind::Summary);
        traceDetail = " editor=\"none\"";
    }

    if (!traceDetail.empty()) {
        TracePopulateLayoutEditSelection(state, traceDetail);
    }
    RefreshSelectedColorDerivedControls(state, hwnd);
    FinishPopulateLayoutEditSelectionUi(state, hwnd, "Previewing changes in the dashboard.");
}

LayoutEditValidationResult ValidateCurrentSelectionInput(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr) {
        return {true, ""};
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
            if (faceBuffer[0] == wchar_t{}) {
                return {false, "Enter a font name."};
            }
            if (!size.has_value() || *size < 1) {
                return {false, "Enter a font size of 1 or greater."};
            }
            if (!weight.has_value()) {
                return {false, "Enter an integer font weight."};
            }
            return {true, ""};
        }
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex) {
            if (IsDerivedColorMode(hwnd)) {
                if (!ReadDerivedColorExpressionFromDialog(hwnd).has_value()) {
                    return {false, "Complete the derived color controls with valid values."};
                }
                return {true, ""};
            }
            wchar_t hexBuffer[64] = {};
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, hexBuffer, ARRAYSIZE(hexBuffer));
            if (hexBuffer[0] != wchar_t{} && !TryParseDialogHexColor(hexBuffer).has_value()) {
                return {false, "Enter a #RRGGBBAA color value."};
            }
            if (!ReadColorDialogValue(hwnd).has_value()) {
                return {false, "Enter each RGBA channel as a whole number between 0 and 255."};
            }
            return {true, ""};
        }
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::String) {
            return {true, ""};
        }

        wchar_t valueBuffer[128] = {};
        GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, valueBuffer, ARRAYSIZE(valueBuffer));
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::Integer) {
            return TryParseDialogInteger(valueBuffer).has_value()
                       ? LayoutEditValidationResult{true, ""}
                       : LayoutEditValidationResult{false, "Enter a whole number."};
        }
        return TryParseDialogDouble(valueBuffer).has_value()
                   ? LayoutEditValidationResult{true, ""}
                   : LayoutEditValidationResult{false, "Enter a valid number."};
    }

    if (std::holds_alternative<LayoutCardTitleEditKey>(state->selectedLeaf->focusKey)) {
        return {true, ""};
    }

    if (std::holds_alternative<ThemeColorEditKey>(state->selectedLeaf->focusKey)) {
        wchar_t hexBuffer[64] = {};
        GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, hexBuffer, ARRAYSIZE(hexBuffer));
        if (hexBuffer[0] != wchar_t{} && !TryParseDialogHexColor(hexBuffer).has_value()) {
            return {false, "Enter a #RRGGBBAA color value."};
        }
        if (!ReadColorDialogValue(hwnd).has_value()) {
            return {false, "Enter each RGBA channel as a whole number between 0 and 255."};
        }
        return {true, ""};
    }

    if (const DescriptorLayoutEditEditorHandler* handler = SelectedDescriptorLayoutEditEditorHandler(state);
        handler != nullptr && handler->validate != nullptr) {
        return handler->validate(state, hwnd);
    }

    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey);
        metricKey != nullptr) {
        const AppConfig& config = state->dialog->Host().CurrentConfig();
        const MetricDefinitionConfig* definition = FindMetricDefinition(config.layout.metrics, metricKey->metricId);
        if (definition == nullptr) {
            return {false, "Unable to find the current metric definition."};
        }
        if (!definition->telemetryScale && definition->style != MetricDisplayStyle::LabelOnly) {
            wchar_t scaleBuffer[128] = {};
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT, scaleBuffer, ARRAYSIZE(scaleBuffer));
            const std::optional<double> scale = TryParseDialogDouble(scaleBuffer);
            if (!scale.has_value() || *scale <= 0.0) {
                return {false, "Enter a metric scale greater than 0."};
            }
        }
        return {true, ""};
    }

    wchar_t firstBuffer[64] = {};
    wchar_t secondBuffer[64] = {};
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT, firstBuffer, ARRAYSIZE(firstBuffer));
    GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT, secondBuffer, ARRAYSIZE(secondBuffer));
    const std::optional<int> first = TryParseDialogInteger(firstBuffer);
    const std::optional<int> second = TryParseDialogInteger(secondBuffer);
    if (!first.has_value() || !second.has_value() || *first < 1 || *second < 1) {
        return {false, "Enter positive integer weights for both neighboring items."};
    }
    return {true, ""};
}

void RefreshLayoutEditValidationState(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr) {
        return;
    }
    const LayoutEditValidationResult validation = ValidateCurrentSelectionInput(state, hwnd);
    state->activeSelectionValid = validation.valid;
    if (state->selectedLeaf == nullptr && !IsFontsSectionNode(state) && !IsThemeSectionNode(state) &&
        !IsLayoutSectionNode(state)) {
        SetLayoutEditStatus(state, hwnd, LayoutEditStatusKind::Info, "Select a field to edit it here.");
    } else if (validation.valid) {
        SetLayoutEditStatus(state, hwnd, LayoutEditStatusKind::Info, "Previewing changes in the dashboard.");
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
        const bool applied = state->dialog->Host().ApplyCardTitlePreview(*cardTitleKey, title);
        state->dialog->Host().TraceLayoutEditDialogEvent("preview_value",
            BuildTraceNodeDetail(state->selectedNode,
                " raw=%s parsed=%s applied=%s",
                QuoteTraceText(title).c_str(),
                QuoteTraceText(title).c_str(),
                QuotedBoolText(applied)));
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
    const bool applied = value.has_value() && state->dialog->Host().ApplyParameterPreview(*parameter, *value);
    state->dialog->Host().TraceLayoutEditDialogEvent("preview_value",
        BuildTraceNodeDetail(state->selectedNode,
            " raw=%s parsed=%s applied=%s",
            QuoteTraceText(Utf8FromWide(buffer)).c_str(),
            QuoteTraceText(
                value.has_value() ? FormatLayoutEditTooltipValue(*value, state->selectedLeaf->valueFormat) : "invalid")
                .c_str(),
            QuotedBoolText(applied)));
    return applied;
}

bool PreviewSelectedFont(LayoutEditDialogState* state, HWND hwnd, UINT notificationCode) {
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
    const std::string faceText = ReadFontDialogFaceText(hwnd, notificationCode);
    const std::optional<int> size = TryParseDialogInteger(sizeBuffer);
    const std::optional<int> weight = TryParseDialogInteger(weightBuffer);
    if (faceText.empty() || !size.has_value() || *size < 1 || !weight.has_value()) {
        return false;
    }

    const UiFontConfig font{faceText, *size, *weight};
    const bool applied = state->dialog->Host().ApplyFontPreview(*parameter, font);
    if (applied) {
        SetFontSamplePreview(state, hwnd, std::optional<LayoutEditParameter>(*parameter), &font);
    }
    state->dialog->Host().TraceLayoutEditDialogEvent("preview_font",
        BuildTraceNodeDetail(state->selectedNode,
            " face=%s size=\"%d\" weight=\"%d\" applied=%s",
            QuoteTraceText(font.face).c_str(),
            font.size,
            font.weight,
            QuotedBoolText(applied)));
    return applied;
}

bool PreviewSelectedGlobalFontFamily(LayoutEditDialogState* state, HWND hwnd, UINT notificationCode) {
    if (state == nullptr || !IsFontsSectionNode(state) || state->updatingControls) {
        return false;
    }

    const std::string familyText = ReadFontDialogFaceText(hwnd, notificationCode);
    if (familyText.empty()) {
        return false;
    }

    const std::string family = familyText;
    const bool applied = state->dialog->Host().ApplyFontFamilyPreview(family);
    state->dialog->Host().TraceLayoutEditDialogEvent("preview_font_family",
        BuildTraceNodeDetail(
            state->selectedNode, " family=%s applied=%s", QuoteTraceText(family).c_str(), QuotedBoolText(applied)));
    return applied;
}

bool PreviewSelectedTheme(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || !IsThemeSectionNode(state) || state->updatingControls) {
        return false;
    }

    const std::string themeName = ReadComboTextUtf8(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO);
    const bool applied = !themeName.empty() && state->dialog->Host().ApplyThemePreview(themeName);
    state->dialog->Host().TraceLayoutEditDialogEvent("preview_theme",
        BuildTraceNodeDetail(
            state->selectedNode, " theme=%s applied=%s", QuoteTraceText(themeName).c_str(), QuotedBoolText(applied)));
    if (applied) {
        state->dialog->Refresh();
        SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO));
        state->dialog->Host().RestackLayoutEditDialogAnchor(hwnd);
    }
    return applied;
}

bool PreviewSelectedLayout(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || !IsLayoutSectionNode(state) || state->updatingControls) {
        return false;
    }

    const std::string layoutName = ReadComboTextUtf8(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO);
    const bool applied = !layoutName.empty() && state->dialog->Host().ApplyLayoutPreview(layoutName);
    state->dialog->Host().TraceLayoutEditDialogEvent("preview_layout",
        BuildTraceNodeDetail(
            state->selectedNode, " layout=%s applied=%s", QuoteTraceText(layoutName).c_str(), QuotedBoolText(applied)));
    if (applied) {
        state->dialog->Refresh();
        SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO));
    }
    return applied;
}

bool PreviewSelectedColor(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr || state->updatingControls) {
        return false;
    }
    const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey);
    const auto* themeColorKey = std::get_if<ThemeColorEditKey>(&state->selectedLeaf->focusKey);
    if ((parameter == nullptr && themeColorKey == nullptr) ||
        state->selectedLeaf->valueFormat != configschema::ValueFormat::ColorHex) {
        return false;
    }

    const bool derivedExpression = parameter != nullptr && IsDerivedColorMode(hwnd);
    const bool lchLiteralView = !derivedExpression && state->colorEditViewMode == ColorEditViewMode::Lch;
    const bool hsvLiteralView = !derivedExpression && state->colorEditViewMode == ColorEditViewMode::Hsv;
    const bool viewValid =
        (!lchLiteralView || ColorDialogLchValueValid(hwnd)) && (!hsvLiteralView || ColorDialogHsvValueValid(hwnd));
    const auto color = viewValid ? ReadColorDialogValue(hwnd) : std::nullopt;
    bool applied = false;
    if (derivedExpression) {
        const auto expression = ReadDerivedColorExpressionFromDialog(hwnd);
        applied = expression.has_value() &&
                  state->dialog->Host().ApplyColorExpressionPreview(*parameter, FormatColorExpression(*expression));
    } else {
        applied = color.has_value() &&
                  (parameter != nullptr ? state->dialog->Host().ApplyColorPreview(*parameter, *color)
                                        : state->dialog->Host().ApplyThemeColorPreview(*themeColorKey, *color));
    }
    const AppConfig& config = state->dialog->Host().CurrentConfig();
    std::optional<unsigned int> resolvedColor;
    if (parameter != nullptr) {
        resolvedColor = FindLayoutEditParameterColorValue(config, *parameter);
    } else if (const ColorConfig* themeColor = FindThemeColorValue(config, *themeColorKey)) {
        resolvedColor = themeColor->ToRgba();
    } else {
        resolvedColor = color.value_or(0x000000FFu);
    }
    if (applied && resolvedColor.has_value()) {
        SetColorSamplePreview(state, hwnd, *resolvedColor);
    }
    state->dialog->Host().TraceLayoutEditDialogEvent("preview_color",
        BuildTraceNodeDetail(state->selectedNode,
            "%s parsed=%s mode=%s applied=%s",
            BuildColorDialogTraceValues(hwnd).c_str(),
            QuoteTraceText(color.has_value() ? FormatTraceColorHex(*color) : "invalid").c_str(),
            QuoteTraceText(derivedExpression ? "derived" : "literal").c_str(),
            QuotedBoolText(applied)));
    return applied;
}

bool IsDerivedColorSlider(int sliderId) {
    return sliderId == IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER || sliderId == IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER ||
           sliderId == IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER;
}

void SyncDerivedColorSliderFromEdit(HWND hwnd, int editId) {
    if (hwnd == nullptr) {
        return;
    }
    if (editId == IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT) {
        const auto value = TryParseDialogControlDouble(hwnd, editId);
        if (value.has_value()) {
            SetDerivedRotateSliderPosition(hwnd, *value);
        }
        return;
    }
    if (editId == IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT) {
        const auto value = TryParseDialogControlDouble(hwnd, editId);
        if (value.has_value()) {
            SetDerivedMixSliderPosition(hwnd, *value);
        }
        return;
    }
    if (editId == IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT) {
        const auto value = ParseColorExpressionAlphaByte(ReadDialogControlTextUtf8(hwnd, editId));
        if (value.has_value()) {
            SetDerivedAlphaSliderPosition(hwnd, *value);
        }
    }
}

bool SetDerivedColorEditFromSlider(HWND hwnd, int sliderId) {
    if (hwnd == nullptr || !IsDerivedColorSlider(sliderId)) {
        return false;
    }
    const int position = static_cast<int>(SendDlgItemMessageW(hwnd, sliderId, TBM_GETPOS, 0, 0));
    switch (sliderId) {
        case IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER:
            SetDialogControlInteger(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT, position);
            return true;
        case IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER:
            SetDialogControlTextUtf8(
                hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT, FormatDialogDouble(static_cast<double>(position) / 100.0));
            return true;
        case IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER:
            SetDialogControlTextUtf8(hwnd,
                IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT,
                FormatDialogAlphaByte(static_cast<unsigned int>(position)));
            return true;
        default:
            return false;
    }
}

void RefreshSelectedColorDerivedControls(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || hwnd == nullptr) {
        return;
    }
    const bool derived = IsDerivedColorMode(hwnd);
    const BOOL rotateEnabled =
        derived && IsDlgButtonChecked(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_CHECK) == BST_CHECKED ? TRUE : FALSE;
    const BOOL mixEnabled =
        derived && IsDlgButtonChecked(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_CHECK) == BST_CHECKED ? TRUE : FALSE;
    const BOOL alphaEnabled =
        derived && IsDlgButtonChecked(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_CHECK) == BST_CHECKED ? TRUE : FALSE;
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_BASE_COMBO), derived ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT), rotateEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_SLIDER), rotateEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_LABEL), mixEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO), mixEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT), mixEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_SLIDER), mixEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT), alphaEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_SLIDER), alphaEnabled);
}

bool SetSelectedDialogColor(LayoutEditDialogState* state, HWND hwnd, unsigned int color) {
    if (state == nullptr || state->selectedLeaf == nullptr) {
        return false;
    }
    const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey);
    const auto* themeColorKey = std::get_if<ThemeColorEditKey>(&state->selectedLeaf->focusKey);
    if ((parameter == nullptr && themeColorKey == nullptr) ||
        state->selectedLeaf->valueFormat != configschema::ValueFormat::ColorHex) {
        return false;
    }

    state->dialog->Host().TraceLayoutEditDialogEvent("picker_apply_begin",
        BuildTraceNodeDetail(state->selectedNode, " picked=%s", QuoteTraceText(FormatTraceColorHex(color)).c_str()));
    const bool applied = parameter != nullptr ? state->dialog->Host().ApplyColorPreview(*parameter, color)
                                              : state->dialog->Host().ApplyThemeColorPreview(*themeColorKey, color);
    if (!applied) {
        state->dialog->Host().TraceLayoutEditDialogEvent(
            "picker_apply_end", BuildTraceNodeDetail(state->selectedNode, " applied=\"false\""));
        return false;
    }

    PopulateLayoutEditSelection(state, hwnd);
    SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT));
    SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, EM_SETSEL, 0, -1);
    const AppConfig& config = state->dialog->Host().CurrentConfig();
    const ColorConfig* resolvedThemeColor =
        parameter == nullptr ? FindThemeColorValue(config, *themeColorKey) : nullptr;
    state->dialog->Host().TraceLayoutEditDialogEvent("picker_apply_end",
        BuildTraceNodeDetail(state->selectedNode,
            " applied=\"true\"%s config_value=%s",
            BuildColorDialogTraceValues(hwnd).c_str(),
            QuoteTraceText(
                FormatTraceColorHex(parameter != nullptr
                                        ? FindLayoutEditParameterColorValue(config, *parameter).value_or(0)
                                        : (resolvedThemeColor != nullptr ? resolvedThemeColor->ToRgba() : 0)))
                .c_str()));
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

    const bool applied = state->dialog->Host().ApplyWeightPreview(*key, *first, *second);
    state->dialog->Host().TraceLayoutEditDialogEvent("preview_weights",
        BuildTraceNodeDetail(
            state->selectedNode, " first=\"%d\" second=\"%d\" applied=%s", *first, *second, QuotedBoolText(applied)));
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

    const AppConfig& config = state->dialog->Host().CurrentConfig();
    const MetricDefinitionConfig* definition = FindMetricDefinition(config.layout.metrics, key->metricId);
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
    const bool applied = state->dialog->Host().ApplyMetricPreview(*key, scale, unit, label, binding);
    state->dialog->Host().TraceLayoutEditDialogEvent("preview_metric",
        BuildTraceNodeDetail(state->selectedNode,
            "%s parsed_scale=%s applied=%s",
            BuildMetricDialogTraceValues(hwnd).c_str(),
            QuoteTraceText(scale.has_value()
                               ? FormatLayoutEditTooltipValue(*scale, configschema::ValueFormat::FloatingPoint)
                               : "disabled")
                .c_str(),
            QuotedBoolText(applied)));
    return applied;
}

bool PreviewSelectedDateTimeFormat(LayoutEditDialogState* state, HWND hwnd) {
    const DescriptorLayoutEditEditorHandler* handler = SelectedDescriptorLayoutEditEditorHandler(state);
    return handler != nullptr && handler->preview != nullptr && handler->preview(state, hwnd);
}

bool HandleMetricListOrderEditorCommand(LayoutEditDialogState* state, HWND hwnd, int controlId, UINT notificationCode) {
    if (state == nullptr || SelectedMetricListOrderKey(state) == nullptr) {
        return false;
    }

    if (IsMetricListRowControlId(
            controlId, IDC_LAYOUT_EDIT_METRIC_LIST_ROW_COMBO_BASE, state->metricListRowControls.size()) &&
        notificationCode == CBN_SELCHANGE) {
        const std::vector<std::string> metricRefs = ReadMetricListOrderDialogRows(state, hwnd);
        ApplyMetricListOrderRows(state, metricRefs);
        return true;
    }

    if (controlId == IDC_LAYOUT_EDIT_METRIC_LIST_ADD_ROW && notificationCode == BN_CLICKED) {
        const AppConfig& config = state->dialog->Host().CurrentConfig();
        return MutateMetricListOrderRows(state, hwnd, [&](std::vector<std::string>& metricRefs) {
            const auto options = AvailableMetricDefinitionIds(config);
            if (!options.empty()) {
                metricRefs.push_back(options.front());
            }
        });
    }

    if (notificationCode != BN_CLICKED) {
        return false;
    }

    if (IsMetricListRowControlId(
            controlId, IDC_LAYOUT_EDIT_METRIC_LIST_ROW_UP_BASE, state->metricListRowControls.size())) {
        const int rowIndex = MetricListRowIndexFromControlId(controlId, IDC_LAYOUT_EDIT_METRIC_LIST_ROW_UP_BASE);
        return MutateMetricListOrderRows(state, hwnd, [&](std::vector<std::string>& metricRefs) {
            if (rowIndex > 0 && rowIndex < static_cast<int>(metricRefs.size())) {
                std::swap(metricRefs[static_cast<size_t>(rowIndex)], metricRefs[static_cast<size_t>(rowIndex - 1)]);
            }
        });
    }
    if (IsMetricListRowControlId(
            controlId, IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DOWN_BASE, state->metricListRowControls.size())) {
        const int rowIndex = MetricListRowIndexFromControlId(controlId, IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DOWN_BASE);
        return MutateMetricListOrderRows(state, hwnd, [&](std::vector<std::string>& metricRefs) {
            if (rowIndex >= 0 && rowIndex + 1 < static_cast<int>(metricRefs.size())) {
                const size_t index = static_cast<size_t>(rowIndex);
                std::swap(metricRefs[index], metricRefs[index + 1]);
            }
        });
    }
    if (IsMetricListRowControlId(
            controlId, IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DELETE_BASE, state->metricListRowControls.size())) {
        const int rowIndex = MetricListRowIndexFromControlId(controlId, IDC_LAYOUT_EDIT_METRIC_LIST_ROW_DELETE_BASE);
        return MutateMetricListOrderRows(state, hwnd, [&](std::vector<std::string>& metricRefs) {
            if (rowIndex >= 0 && rowIndex < static_cast<int>(metricRefs.size())) {
                metricRefs.erase(metricRefs.begin() + rowIndex);
            }
        });
    }

    return false;
}

bool RevertSelectedLayoutEditField(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr) {
        if (IsFontsSectionNode(state)) {
            const bool applied = state->dialog->Host().ApplyFontSetPreview(state->originalConfig.layout.fonts);
            if (applied) {
                PopulateLayoutEditSelection(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
            }
            return applied;
        }
        if (IsThemeSectionNode(state)) {
            const bool applied = state->dialog->Host().ApplyThemePreview(state->originalConfig.display.theme);
            if (applied) {
                state->dialog->Refresh();
                RefreshLayoutEditValidationState(state, hwnd);
            }
            return applied;
        }
        if (IsLayoutSectionNode(state)) {
            const bool applied = state->dialog->Host().ApplyLayoutPreview(state->originalConfig.display.layout);
            if (applied) {
                state->dialog->Refresh();
                SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_THEME_COMBO));
                state->dialog->Host().RestackLayoutEditDialogAnchor(hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
            }
            return applied;
        }
        return false;
    }

    if (const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey);
        parameter != nullptr) {
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec) {
            const auto font = FindLayoutEditTooltipFontValue(state->originalConfig, *parameter);
            if (!font.has_value() || *font == nullptr) {
                return false;
            }
            const bool applied = state->dialog->Host().ApplyFontPreview(*parameter, **font);
            if (applied) {
                PopulateLayoutEditSelection(state, hwnd);
                RefreshLayoutEditValidationState(state, hwnd);
            }
            return applied;
        }
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex) {
            const ColorConfig* color = FindColorRoleValue(state->originalConfig, *parameter);
            if (color == nullptr) {
                return false;
            }
            const bool applied = !color->expression.empty() && !IsLiteralColorExpressionText(color->expression)
                                     ? state->dialog->Host().ApplyColorExpressionPreview(*parameter, color->expression)
                                     : state->dialog->Host().ApplyColorPreview(*parameter, color->ToRgba());
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
            const bool applied = state->dialog->Host().ApplyParameterPreview(*parameter, *value);
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
        const bool applied = state->dialog->Host().ApplyWeightPreview(*weightKey, values->first, values->second);
        if (applied) {
            PopulateLayoutEditSelection(state, hwnd);
            RefreshLayoutEditValidationState(state, hwnd);
        }
        return applied;
    }

    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey);
        metricKey != nullptr) {
        const MetricDefinitionConfig* definition =
            FindMetricDefinition(state->originalConfig.layout.metrics, metricKey->metricId);
        if (definition == nullptr) {
            return false;
        }
        std::optional<std::string> binding;
        if (const auto target = ParseBoardMetricBindingTarget(metricKey->metricId); target.has_value()) {
            const auto& bindings = target->kind == BoardMetricBindingKind::Temperature
                                       ? state->originalConfig.layout.board.temperatureSensorNames
                                       : state->originalConfig.layout.board.fanSensorNames;
            const auto it = bindings.find(target->logicalName);
            binding = it != bindings.end() ? std::optional<std::string>(it->second)
                                           : std::optional<std::string>(std::string());
        }
        const bool applied = state->dialog->Host().ApplyMetricPreview(*metricKey,
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
        const bool applied = state->dialog->Host().ApplyCardTitlePreview(*cardTitleKey, title);
        if (applied) {
            PopulateLayoutEditSelection(state, hwnd);
            RefreshLayoutEditValidationState(state, hwnd);
        }
        return applied;
    }

    if (const auto* themeColorKey = std::get_if<ThemeColorEditKey>(&state->selectedLeaf->focusKey);
        themeColorKey != nullptr) {
        const ColorConfig* color = FindThemeColorValue(state->originalConfig, *themeColorKey);
        if (color == nullptr) {
            return false;
        }
        const bool applied = state->dialog->Host().ApplyThemeColorPreview(*themeColorKey, color->ToRgba());
        if (applied) {
            PopulateLayoutEditSelection(state, hwnd);
            RefreshLayoutEditValidationState(state, hwnd);
        }
        return applied;
    }

    if (const DescriptorLayoutEditEditorHandler* handler = SelectedDescriptorLayoutEditEditorHandler(state);
        handler != nullptr && handler->revert != nullptr) {
        return handler->revert(state, hwnd);
    }

    return false;
}
