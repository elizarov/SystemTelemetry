#include "layout_edit_dialog/impl/editors.h"

#include <algorithm>
#include <array>

#include "config/color_expression.h"
#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_tooltip.h"
#include "layout_edit_dialog/impl/pane.h"
#include "layout_edit_dialog/impl/trace.h"
#include "layout_edit_dialog/impl/util.h"
#include "util/strings.h"
#include "util/utf8.h"

namespace {

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

std::optional<ColorConfig> FindThemeColorValue(const AppConfig& config, const ThemeColorEditKey& key) {
    const auto it = std::find_if(config.layout.themes.begin(),
        config.layout.themes.end(),
        [&](const ThemeConfig& theme) { return theme.name == key.themeName; });
    if (it == config.layout.themes.end()) {
        return std::nullopt;
    }
    if (key.tokenName == "background")
        return it->background;
    if (key.tokenName == "foreground")
        return it->foreground;
    if (key.tokenName == "accent")
        return it->accent;
    if (key.tokenName == "guide")
        return it->guide;
    return std::nullopt;
}

std::optional<ColorConfig> FindColorRoleValue(const AppConfig& config, LayoutEditParameter parameter) {
    switch (parameter) {
        case LayoutEditParameter::ColorBackground:
            return config.layout.colors.backgroundColor;
        case LayoutEditParameter::ColorForeground:
            return config.layout.colors.foregroundColor;
        case LayoutEditParameter::ColorIcon:
            return config.layout.colors.iconColor;
        case LayoutEditParameter::ColorPeakGhost:
            return config.layout.colors.peakGhostColor;
        case LayoutEditParameter::ColorAccent:
            return config.layout.colors.accentColor;
        case LayoutEditParameter::ColorLayoutGuide:
            return config.layout.colors.layoutGuideColor;
        case LayoutEditParameter::ColorActiveEdit:
            return config.layout.colors.activeEditColor;
        case LayoutEditParameter::ColorPanelBorder:
            return config.layout.colors.panelBorderColor;
        case LayoutEditParameter::ColorMutedText:
            return config.layout.colors.mutedTextColor;
        case LayoutEditParameter::ColorTrack:
            return config.layout.colors.trackColor;
        case LayoutEditParameter::ColorPanelFill:
            return config.layout.colors.panelFillColor;
        case LayoutEditParameter::ColorGraphBackground:
            return config.layout.colors.graphBackgroundColor;
        case LayoutEditParameter::ColorGraphAxis:
            return config.layout.colors.graphAxisColor;
        case LayoutEditParameter::ColorGraphMarker:
            return config.layout.colors.graphMarkerColor;
        default:
            return std::nullopt;
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

void PopulateTextCombo(HWND hwnd, int controlId, const std::vector<std::string>& options, std::string_view selected) {
    HWND combo = GetDlgItem(hwnd, controlId);
    if (combo == nullptr) {
        return;
    }
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int selectedIndex = CB_ERR;
    for (const std::string& option : options) {
        const std::wstring wideOption = WideFromUtf8(option);
        const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wideOption.c_str()));
        if (index != CB_ERR && option == selected) {
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

std::wstring FormatDialogAlphaByte(unsigned int alpha) {
    constexpr wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring text = L"0x00";
    text[2] = kHex[(alpha >> 4) & 0x0Fu];
    text[3] = kHex[alpha & 0x0Fu];
    return text;
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
        const std::wstring text = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT);
        const auto value = TryParseDialogDouble(text.c_str());
        if (!value.has_value()) {
            return std::nullopt;
        }
        expression.rotateHue = *value;
    }
    if (IsDlgButtonChecked(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_CHECK) == BST_CHECKED) {
        const std::string target = ReadComboTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO);
        const std::wstring amountText = ReadDialogControlTextWide(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT);
        const auto amount = TryParseDialogDouble(amountText.c_str());
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
    PopulateTextCombo(hwnd, IDC_LAYOUT_EDIT_COLOR_MODE_COMBO, {"Literal", "Derived"}, derived ? "Derived" : "Literal");

    ColorExpression expression = parsed.value_or(ColorExpression{DefaultDerivedBase(parameter)});
    if (expression.mix.has_value() && expression.mix->target.empty()) {
        expression.mix->target = "accent";
    }
    const std::vector<std::string> tokens = {"background", "foreground", "accent", "guide"};
    PopulateTextCombo(hwnd, IDC_LAYOUT_EDIT_COLOR_BASE_COMBO, tokens, expression.base);
    PopulateTextCombo(hwnd,
        IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO,
        tokens,
        expression.mix.has_value() ? expression.mix->target : "accent");
    CheckDlgButton(
        hwnd, IDC_LAYOUT_EDIT_COLOR_ROTATE_CHECK, expression.rotateHue.has_value() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_CHECK, expression.mix.has_value() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_CHECK, expression.alpha.has_value() ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(hwnd,
        IDC_LAYOUT_EDIT_COLOR_ROTATE_EDIT,
        WideFromUtf8(FormatDialogDouble(expression.rotateHue.value_or(0.0))).c_str());
    SetDlgItemTextW(hwnd,
        IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT,
        WideFromUtf8(FormatDialogDouble(expression.mix.has_value() ? expression.mix->amount : 0.5)).c_str());
    SetDlgItemTextW(hwnd,
        IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT,
        FormatDialogAlphaByte(expression.alpha.value_or(color.Alpha())).c_str());
}

std::vector<std::string> StandardDateTimeFormats(const LayoutNodeFieldEditKey& key) {
    if (key.widgetClass == WidgetClass::ClockTime) {
        return {"HH:MM",
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
    }
    if (key.widgetClass == WidgetClass::ClockDate) {
        return {"YYYY-MM-DD",
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
    }
    return {};
}

void PopulateDateTimeFormatCombo(HWND hwnd, const LayoutNodeFieldEditKey& key, std::string_view selected) {
    HWND combo = GetDlgItem(hwnd, IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO);
    if (combo == nullptr) {
        return;
    }
    std::vector<std::string> options = StandardDateTimeFormats(key);
    if (!selected.empty() && std::find(options.begin(), options.end(), selected) == options.end()) {
        options.push_back(std::string(selected));
    }
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int selectedIndex = CB_ERR;
    for (const std::string& option : options) {
        const std::wstring wideOption = WideFromUtf8(option);
        const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wideOption.c_str()));
        if (index != CB_ERR && option == selected) {
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

std::wstring CommonFontFamilyText(const UiFontSetConfig& fonts) {
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
    return WideFromUtf8(firstFace);
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

bool ApplyMetricListOrderRows(LayoutEditDialogState* state, HWND, const std::vector<std::string>& metricRefs) {
    const auto* key = SelectedMetricListOrderKey(state);
    return key != nullptr &&
           state->dialog->Host().ApplyLayoutEditPreview(LayoutEditFocusKey{*key}, LayoutEditValue{metricRefs});
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
        const std::wstring wideOption = WideFromUtf8(option);
        const LRESULT index = SendMessageW(row.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wideOption.c_str()));
        if (index != CB_ERR && selectedIndex == CB_ERR && option == selected) {
            selectedIndex = static_cast<int>(index);
        }
    }
    if (selectedIndex != CB_ERR) {
        SendMessageW(row.combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);
    } else {
        SetWindowTextW(row.combo, WideFromUtf8(std::string(selected)).c_str());
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
    if (!ApplyMetricListOrderRows(state, hwnd, metricRefs)) {
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

    std::vector<std::string> metricRefs;
    if (const LayoutNodeConfig* node = FindLayoutNodeFieldNode(state->dialog->Host().CurrentConfig(), *key);
        node != nullptr) {
        metricRefs = ParseMetricListMetricRefs(node->parameter);
    }
    std::vector<std::string> options = AvailableMetricDefinitionIds(state->dialog->Host().CurrentConfig());
    for (const auto& metricRef : metricRefs) {
        const MetricDefinitionConfig* definition =
            FindMetricDefinition(state->dialog->Host().CurrentConfig().layout.metrics, metricRef);
        if (definition != nullptr && IsMetricListSupportedDisplayStyle(definition->style) &&
            std::find(options.begin(), options.end(), metricRef) == options.end()) {
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
    ShowLayoutEditEditors(hwnd, false, false, false, false, false, false, true);
    SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection",
        BuildTraceNodeText(state->selectedNode) + " editor=\"metric_list_order\"" +
            " rows=" + QuoteTraceText(std::to_string(metricRefs.size())));
    return true;
}

bool PopulateDateTimeFormatSelection(LayoutEditDialogState* state, HWND hwnd) {
    const auto* key = SelectedNodeFieldKey(state);
    const LayoutNodeFieldEditDescriptor* descriptor = SelectedNodeFieldDescriptor(state);
    if (key == nullptr || descriptor == nullptr || descriptor->editorKind != LayoutEditEditorKind::DateTimeFormat) {
        return false;
    }

    std::string format;
    if (const LayoutNodeConfig* node = FindLayoutNodeFieldNode(state->dialog->Host().CurrentConfig(), *key);
        node != nullptr) {
        format = ReadLayoutNodeFieldValue(*node, key->field);
    }
    PopulateDateTimeFormatCombo(hwnd, *key, format);
    DestroyMetricListOrderEditorControls(state);
    ShowLayoutEditEditors(hwnd, false, false, false, false, false, false, false, false, true);
    SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection",
        BuildTraceNodeText(state->selectedNode) + " editor=\"date_time_format\"" + " format=" + QuoteTraceText(format));
    return true;
}

LayoutEditValidationResult ValidateMetricListOrderSelection(LayoutEditDialogState* state, HWND hwnd) {
    if (SelectedMetricListOrderKey(state) == nullptr) {
        return {false, L"Choose a metric for each row."};
    }
    for (const auto& metricRef : ReadMetricListOrderDialogRows(state, hwnd)) {
        if (metricRef.empty()) {
            return {false, L"Choose a metric for each row."};
        }
    }
    return {true, L""};
}

LayoutEditValidationResult ValidateDateTimeFormatSelection(LayoutEditDialogState* state, HWND hwnd) {
    const LayoutNodeFieldEditDescriptor* descriptor = SelectedNodeFieldDescriptor(state);
    if (descriptor == nullptr || descriptor->editorKind != LayoutEditEditorKind::DateTimeFormat) {
        return {false, L"Choose a date or time format."};
    }
    const std::string format = Trim(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_DATETIME_FORMAT_COMBO));
    return !format.empty() ? LayoutEditValidationResult{true, L""}
                           : LayoutEditValidationResult{false, L"Choose a date or time format."};
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
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_date_time_format",
        BuildTraceNodeText(state->selectedNode) + " format=" + QuoteTraceText(format) +
            " applied=" + QuoteTraceText(applied ? "true" : "false"));
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
    const auto it = std::find_if(kDescriptorEditorHandlers.begin(),
        kDescriptorEditorHandlers.end(),
        [&](const DescriptorLayoutEditEditorHandler& handler) { return handler.kind == kind; });
    return it != kDescriptorEditorHandlers.end() ? &(*it) : nullptr;
}

const DescriptorLayoutEditEditorHandler* SelectedDescriptorLayoutEditEditorHandler(const LayoutEditDialogState* state) {
    const LayoutNodeFieldEditDescriptor* descriptor = SelectedNodeFieldDescriptor(state);
    return descriptor != nullptr ? FindDescriptorLayoutEditEditorHandler(descriptor->editorKind) : nullptr;
}

bool PopulateDescriptorLayoutEditSelection(LayoutEditDialogState* state, HWND hwnd) {
    const DescriptorLayoutEditEditorHandler* handler = SelectedDescriptorLayoutEditEditorHandler(state);
    return handler != nullptr && handler->populate != nullptr && handler->populate(state, hwnd);
}

}  // namespace

LayoutEditEditorKind CurrentLayoutEditEditorKind(const LayoutEditDialogState* state) {
    if (state == nullptr || state->selectedLeaf == nullptr) {
        return IsFontsSectionNode(state) ? LayoutEditEditorKind::GlobalFontFamily : LayoutEditEditorKind::Summary;
    }
    if (std::holds_alternative<LayoutWeightEditKey>(state->selectedLeaf->focusKey)) {
        return LayoutEditEditorKind::Weights;
    }
    if (std::holds_alternative<LayoutMetricEditKey>(state->selectedLeaf->focusKey)) {
        return LayoutEditEditorKind::Metric;
    }
    if (std::holds_alternative<ThemeColorEditKey>(state->selectedLeaf->focusKey)) {
        return LayoutEditEditorKind::Color;
    }
    if (const LayoutNodeFieldEditDescriptor* descriptor = SelectedNodeFieldDescriptor(state); descriptor != nullptr) {
        return descriptor->editorKind;
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

void PopulateLayoutEditSelection(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr) {
        return;
    }

    state->updatingControls = true;
    SetLayoutEditDescription(hwnd, state->selectedNode);
    if (IsFontsSectionNode(state)) {
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_LABEL, L"Family:");
        PopulateFontFaceComboBox(hwnd, CommonFontFamilyText(state->dialog->Host().CurrentConfig().layout.fonts));
        DestroyMetricListOrderEditorControls(state);
        ShowLayoutEditEditors(hwnd, false, false, false, false, false, false, false, true);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        SetLayoutEditStatus(state, hwnd, LayoutEditStatusKind::Info, L"Previewing changes in the dashboard.");
        state->activeSelectionValid = true;
        state->updatingControls = false;
        LayoutLayoutEditRightPane(state, hwnd);
        UpdateLayoutEditActionState(state, hwnd);
        RefreshLayoutEditRightPane(hwnd);
        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection",
            BuildTraceNodeText(state->selectedNode) + " editor=\"font_family\"");
        return;
    }
    if (state->selectedLeaf == nullptr) {
        DestroyMetricListOrderEditorControls(state);
        ShowLayoutEditEditors(hwnd, false, false, false, false, false, false, false);
        SetLayoutEditStatus(state, hwnd, LayoutEditStatusKind::Info, L"Select a field to edit it here.");
        state->activeSelectionValid = true;
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        state->updatingControls = false;
        LayoutLayoutEditRightPane(state, hwnd);
        UpdateLayoutEditActionState(state, hwnd);
        RefreshLayoutEditRightPane(hwnd);
        state->dialog->Host().TraceLayoutEditDialogEvent(
            "layout_edit_dialog:populate_selection", BuildTraceNodeText(state->selectedNode) + " editor=\"none\"");
        return;
    }

    if (const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey)) {
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec) {
            SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_LABEL, L"Font name:");
            const auto font = FindLayoutEditTooltipFontValue(state->dialog->Host().CurrentConfig(), *parameter);
            PopulateFontFaceComboBox(hwnd, font.has_value() && *font != nullptr ? WideFromUtf8((**font).face) : L"");
            SetDlgItemTextW(hwnd,
                IDC_LAYOUT_EDIT_FONT_SIZE_EDIT,
                font.has_value() && *font != nullptr ? WideFromUtf8(std::to_string((**font).size)).c_str() : L"");
            SetDlgItemTextW(hwnd,
                IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT,
                font.has_value() && *font != nullptr ? WideFromUtf8(std::to_string((**font).weight)).c_str() : L"");
            DestroyMetricListOrderEditorControls(state);
            ShowLayoutEditEditors(hwnd, false, true, false, false, false, false, false);
            SetFontSamplePreview(state,
                hwnd,
                std::optional<LayoutEditParameter>(*parameter),
                font.has_value() && *font != nullptr ? *font : nullptr);
            state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection",
                BuildTraceNodeText(state->selectedNode) + " editor=\"font\"" +
                    " face=" + QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_FACE_EDIT)) +
                    " size=" + QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_SIZE_EDIT)) +
                    " weight=" + QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_FONT_WEIGHT_EDIT)));
        } else if (state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex) {
            const auto value = FindColorRoleValue(state->dialog->Host().CurrentConfig(), *parameter);
            const unsigned int color = value.has_value() ? value->ToRgba() : 0x000000FFu;
            PopulateColorExpressionControls(hwnd, *parameter, value.value_or(ColorConfig::FromRgba(color)));
            SetColorDialogHex(hwnd, color);
            SetColorDialogChannel(hwnd, kColorDialogControls[0], (color >> 24) & 0xFFu);
            SetColorDialogChannel(hwnd, kColorDialogControls[1], (color >> 16) & 0xFFu);
            SetColorDialogChannel(hwnd, kColorDialogControls[2], (color >> 8) & 0xFFu);
            SetColorDialogChannel(hwnd, kColorDialogControls[3], color & 0xFFu);
            DestroyMetricListOrderEditorControls(state);
            ShowLayoutEditEditors(hwnd, false, false, true, false, false, false, false);
            SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
            SetColorSamplePreview(state, hwnd, color);
            state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection",
                BuildTraceNodeText(state->selectedNode) + " editor=\"color\"" + BuildColorDialogTraceValues(hwnd) +
                    " config_value=" +
                    QuoteTraceText(value.has_value() ? FormatTraceColorHex(value->ToRgba()) : "none") +
                    " mode=" + QuoteTraceText(IsDerivedColorMode(hwnd) ? "derived" : "literal") + " expression=" +
                    QuoteTraceText(value.has_value() && !value->expression.empty() ? value->expression : ""));
        } else {
            const auto value = FindLayoutEditParameterNumericValue(state->dialog->Host().CurrentConfig(), *parameter);
            const std::wstring text =
                value.has_value() ? WideFromUtf8(FormatLayoutEditTooltipValue(*value, state->selectedLeaf->valueFormat))
                                  : L"";
            SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, text.c_str());
            DestroyMetricListOrderEditorControls(state);
            ShowLayoutEditEditors(hwnd, true, false, false, false, false, false, false);
            SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
            state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection",
                BuildTraceNodeText(state->selectedNode) + " editor=\"numeric\"" +
                    " text=" + QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT)));
        }
    } else if (const auto* themeColorKey = std::get_if<ThemeColorEditKey>(&state->selectedLeaf->focusKey)) {
        const auto value = FindThemeColorValue(state->dialog->Host().CurrentConfig(), *themeColorKey);
        const unsigned int color = value.has_value() ? value->ToRgba() : 0x000000FFu;
        SetColorDialogHex(hwnd, color);
        SetColorDialogChannel(hwnd, kColorDialogControls[0], (color >> 24) & 0xFFu);
        SetColorDialogChannel(hwnd, kColorDialogControls[1], (color >> 16) & 0xFFu);
        SetColorDialogChannel(hwnd, kColorDialogControls[2], (color >> 8) & 0xFFu);
        SetColorDialogChannel(hwnd, kColorDialogControls[3], color & 0xFFu);
        DestroyMetricListOrderEditorControls(state);
        ShowLayoutEditEditors(hwnd, false, false, true, false, false, false, false);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        SetColorSamplePreview(state, hwnd, color);
        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection",
            BuildTraceNodeText(state->selectedNode) + " editor=\"theme_color\"" + BuildColorDialogTraceValues(hwnd) +
                " config_value=" + QuoteTraceText(value.has_value() ? FormatTraceColorHex(value->ToRgba()) : "none"));
    } else if (const auto* weightKey = std::get_if<LayoutWeightEditKey>(&state->selectedLeaf->focusKey)) {
        const auto values = FindWeightEditValues(state->dialog->Host().CurrentConfig(), *weightKey);
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
        DestroyMetricListOrderEditorControls(state);
        ShowLayoutEditEditors(hwnd, false, false, false, true, false, false, false);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection",
            BuildTraceNodeText(state->selectedNode) + " editor=\"weights\"" +
                " first=" + QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT)) +
                " second=" + QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT)));
    } else if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&state->selectedLeaf->focusKey)) {
        const std::wstring text =
            WideFromUtf8(FindCardTitleValue(state->dialog->Host().CurrentConfig(), *cardTitleKey).value_or(""));
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, text.c_str());
        DestroyMetricListOrderEditorControls(state);
        ShowLayoutEditEditors(hwnd, true, false, false, false, false, false, false);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection",
            BuildTraceNodeText(state->selectedNode) + " editor=\"text\"" +
                " text=" + QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT)));
    } else if (PopulateDescriptorLayoutEditSelection(state, hwnd)) {
    } else if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey)) {
        const MetricDefinitionConfig* definition =
            FindMetricDefinition(state->dialog->Host().CurrentConfig().layout.metrics, metricKey->metricId);
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
            showBinding ? FindConfiguredBoardMetricBinding(state->dialog->Host().CurrentConfig(), *metricKey)
                        : std::string();
        std::vector<std::string> bindingOptions =
            showBinding ? state->dialog->Host().AvailableBoardMetricSensorBindings(*metricKey)
                        : std::vector<std::string>{};
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
        DestroyMetricListOrderEditorControls(state);
        ShowLayoutEditEditors(hwnd, false, false, false, false, true, showBinding, false);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection",
            BuildTraceNodeText(state->selectedNode) + " editor=\"metric\"" + BuildMetricDialogTraceValues(hwnd) +
                " scale_editable=" + QuoteTraceText(scaleEditable ? "true" : "false") +
                " unit_editable=" + QuoteTraceText(unitEditable ? "true" : "false") +
                " binding_visible=" + QuoteTraceText(showBinding ? "true" : "false") +
                " binding_options=" + QuoteTraceText(std::to_string(bindingOptions.size())));
    } else {
        DestroyMetricListOrderEditorControls(state);
        ShowLayoutEditEditors(hwnd, false, false, false, false, false, false, false);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        state->dialog->Host().TraceLayoutEditDialogEvent(
            "layout_edit_dialog:populate_selection", BuildTraceNodeText(state->selectedNode) + " editor=\"none\"");
    }

    SetLayoutEditStatus(state, hwnd, LayoutEditStatusKind::Info, L"Previewing changes in the dashboard.");
    state->activeSelectionValid = true;
    RefreshSelectedColorDerivedControls(state, hwnd);
    state->updatingControls = false;
    LayoutLayoutEditRightPane(state, hwnd);
    UpdateLayoutEditActionState(state, hwnd);
    RefreshLayoutEditRightPane(hwnd);
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
            if (IsDerivedColorMode(hwnd)) {
                if (!ReadDerivedColorExpressionFromDialog(hwnd).has_value()) {
                    return {false, L"Complete the derived color controls with valid values."};
                }
                return {true, L""};
            }
            wchar_t hexBuffer[64] = {};
            GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, hexBuffer, ARRAYSIZE(hexBuffer));
            if (!std::wstring(hexBuffer).empty() && !TryParseDialogHexColor(hexBuffer).has_value()) {
                return {false, L"Enter a #RRGGBBAA color value."};
            }
            if (!ReadColorDialogValue(hwnd).has_value()) {
                return {false, L"Enter each RGBA channel as a whole number between 0 and 255."};
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

    if (std::holds_alternative<ThemeColorEditKey>(state->selectedLeaf->focusKey)) {
        wchar_t hexBuffer[64] = {};
        GetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, hexBuffer, ARRAYSIZE(hexBuffer));
        if (!std::wstring(hexBuffer).empty() && !TryParseDialogHexColor(hexBuffer).has_value()) {
            return {false, L"Enter a #RRGGBBAA color value."};
        }
        if (!ReadColorDialogValue(hwnd).has_value()) {
            return {false, L"Enter each RGBA channel as a whole number between 0 and 255."};
        }
        return {true, L""};
    }

    if (const DescriptorLayoutEditEditorHandler* handler = SelectedDescriptorLayoutEditEditorHandler(state);
        handler != nullptr && handler->validate != nullptr) {
        return handler->validate(state, hwnd);
    }

    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&state->selectedLeaf->focusKey);
        metricKey != nullptr) {
        const MetricDefinitionConfig* definition =
            FindMetricDefinition(state->dialog->Host().CurrentConfig().layout.metrics, metricKey->metricId);
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
        const bool applied = state->dialog->Host().ApplyCardTitlePreview(*cardTitleKey, title);
        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_value",
            BuildTraceNodeText(state->selectedNode) + " raw=" + QuoteTraceText(title) +
                " parsed=" + QuoteTraceText(title) + " applied=" + QuoteTraceText(applied ? "true" : "false"));
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
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_value",
        BuildTraceNodeText(state->selectedNode) + " raw=" + QuoteTraceText(Utf8FromWide(buffer)) + " parsed=" +
            QuoteTraceText(value.has_value() ? FormatLayoutEditTooltipValue(*value, state->selectedLeaf->valueFormat)
                                             : "invalid") +
            " applied=" + QuoteTraceText(applied ? "true" : "false"));
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
    const std::wstring faceText = ReadFontDialogFaceText(hwnd, notificationCode);
    const std::optional<int> size = TryParseDialogInteger(sizeBuffer);
    const std::optional<int> weight = TryParseDialogInteger(weightBuffer);
    if (faceText.empty() || !size.has_value() || *size < 1 || !weight.has_value()) {
        return false;
    }

    const UiFontConfig font{Utf8FromWide(faceText), *size, *weight};
    const bool applied = state->dialog->Host().ApplyFontPreview(*parameter, font);
    if (applied) {
        SetFontSamplePreview(state, hwnd, std::optional<LayoutEditParameter>(*parameter), &font);
    }
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_font",
        BuildTraceNodeText(state->selectedNode) + " face=" + QuoteTraceText(font.face) + " size=" +
            QuoteTraceText(std::to_string(font.size)) + " weight=" + QuoteTraceText(std::to_string(font.weight)) +
            " applied=" + QuoteTraceText(applied ? "true" : "false"));
    return applied;
}

bool PreviewSelectedGlobalFontFamily(LayoutEditDialogState* state, HWND hwnd, UINT notificationCode) {
    if (state == nullptr || !IsFontsSectionNode(state) || state->updatingControls) {
        return false;
    }

    const std::wstring familyText = ReadFontDialogFaceText(hwnd, notificationCode);
    if (familyText.empty()) {
        return false;
    }

    const std::string family = Utf8FromWide(familyText);
    const bool applied = state->dialog->Host().ApplyFontFamilyPreview(family);
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_font_family",
        BuildTraceNodeText(state->selectedNode) + " family=" + QuoteTraceText(family) +
            " applied=" + QuoteTraceText(applied ? "true" : "false"));
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

    const auto color = ReadColorDialogValue(hwnd);
    const bool derivedExpression = parameter != nullptr && IsDerivedColorMode(hwnd);
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
    const auto resolvedColor =
        parameter != nullptr
            ? FindLayoutEditParameterColorValue(state->dialog->Host().CurrentConfig(), *parameter)
            : std::optional<unsigned int>(FindThemeColorValue(state->dialog->Host().CurrentConfig(), *themeColorKey)
                      .value_or(ColorConfig::FromRgba(color.value_or(0x000000FFu)))
                      .ToRgba());
    if (applied && resolvedColor.has_value()) {
        SetColorSamplePreview(state, hwnd, *resolvedColor);
    }
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_color",
        BuildTraceNodeText(state->selectedNode) + BuildColorDialogTraceValues(hwnd) +
            " parsed=" + QuoteTraceText(color.has_value() ? FormatTraceColorHex(*color) : "invalid") +
            " mode=" + QuoteTraceText(derivedExpression ? "derived" : "literal") +
            " applied=" + QuoteTraceText(applied ? "true" : "false"));
    return applied;
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
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_TARGET_COMBO), mixEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_MIX_AMOUNT_EDIT), mixEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_DERIVED_EDIT), alphaEnabled);
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

    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:picker_apply_begin",
        BuildTraceNodeText(state->selectedNode) + " picked=" + QuoteTraceText(FormatTraceColorHex(color)));
    const bool applied = parameter != nullptr ? state->dialog->Host().ApplyColorPreview(*parameter, color)
                                              : state->dialog->Host().ApplyThemeColorPreview(*themeColorKey, color);
    if (!applied) {
        state->dialog->Host().TraceLayoutEditDialogEvent(
            "layout_edit_dialog:picker_apply_end", BuildTraceNodeText(state->selectedNode) + " applied=\"false\"");
        return false;
    }

    PopulateLayoutEditSelection(state, hwnd);
    SetFocus(GetDlgItem(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT));
    SendDlgItemMessageW(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT, EM_SETSEL, 0, -1);
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:picker_apply_end",
        BuildTraceNodeText(state->selectedNode) + " applied=\"true\"" + BuildColorDialogTraceValues(hwnd) +
            " config_value=" +
            QuoteTraceText(FormatTraceColorHex(
                parameter != nullptr
                    ? FindLayoutEditParameterColorValue(state->dialog->Host().CurrentConfig(), *parameter).value_or(0)
                    : FindThemeColorValue(state->dialog->Host().CurrentConfig(), *themeColorKey)
                          .value_or(ColorConfig::FromRgba(0))
                          .ToRgba())));
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
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_weights",
        BuildTraceNodeText(state->selectedNode) + " first=" + QuoteTraceText(std::to_string(*first)) + " second=" +
            QuoteTraceText(std::to_string(*second)) + " applied=" + QuoteTraceText(applied ? "true" : "false"));
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
        FindMetricDefinition(state->dialog->Host().CurrentConfig().layout.metrics, key->metricId);
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
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_metric",
        BuildTraceNodeText(state->selectedNode) + BuildMetricDialogTraceValues(hwnd) + " parsed_scale=" +
            QuoteTraceText(scale.has_value()
                               ? FormatLayoutEditTooltipValue(*scale, configschema::ValueFormat::FloatingPoint)
                               : "disabled") +
            " applied=" + QuoteTraceText(applied ? "true" : "false"));
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
        ApplyMetricListOrderRows(state, hwnd, metricRefs);
        return true;
    }

    if (controlId == IDC_LAYOUT_EDIT_METRIC_LIST_ADD_ROW && notificationCode == BN_CLICKED) {
        return MutateMetricListOrderRows(state, hwnd, [&](std::vector<std::string>& metricRefs) {
            const auto options = AvailableMetricDefinitionIds(state->dialog->Host().CurrentConfig());
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
                std::swap(metricRefs[static_cast<size_t>(rowIndex)], metricRefs[static_cast<size_t>(rowIndex + 1)]);
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
            const auto color = FindColorRoleValue(state->originalConfig, *parameter);
            if (!color.has_value()) {
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
        const auto color = FindThemeColorValue(state->originalConfig, *themeColorKey);
        if (!color.has_value()) {
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
