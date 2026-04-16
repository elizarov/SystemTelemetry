#include "layout_edit_dialog/editors.h"

#include <algorithm>
#include <sstream>

#include "app_strings.h"
#include "layout_edit_tooltip.h"
#include "layout_edit_dialog/pane.h"
#include "layout_edit_dialog/trace.h"
#include "layout_edit_dialog/util.h"
#include "utf8.h"

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
        state->dialog->Host().TraceLayoutEditDialogEvent(
            "layout_edit_dialog:populate_selection", BuildTraceNodeText(state->selectedNode) + " editor=\"none\"");
        return;
    }

    if (const auto* parameter = std::get_if<LayoutEditParameter>(&state->selectedLeaf->focusKey)) {
        if (state->selectedLeaf->valueFormat == configschema::ValueFormat::FontSpec) {
            const auto font = FindLayoutEditTooltipFontValue(state->dialog->Host().CurrentConfig(), *parameter);
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
            state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
        } else if (state->selectedLeaf->valueFormat == configschema::ValueFormat::ColorHex) {
            const auto value = FindLayoutEditParameterColorValue(state->dialog->Host().CurrentConfig(), *parameter);
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
            state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
        } else {
            const auto value = FindLayoutEditParameterNumericValue(state->dialog->Host().CurrentConfig(), *parameter);
            const std::wstring text =
                value.has_value() ? WideFromUtf8(FormatLayoutEditTooltipValue(*value, state->selectedLeaf->valueFormat))
                                  : L"";
            SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, text.c_str());
            ShowLayoutEditEditors(hwnd, true, false, false, false, false, false);
            SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
            std::ostringstream trace;
            trace << BuildTraceNodeText(state->selectedNode) << " editor=\"numeric\""
                  << " text=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT));
            state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
        }
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
        ShowLayoutEditEditors(hwnd, false, false, false, true, false, false);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        std::ostringstream trace;
        trace << BuildTraceNodeText(state->selectedNode) << " editor=\"weights\""
              << " first=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_WEIGHT_FIRST_EDIT))
              << " second=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_WEIGHT_SECOND_EDIT));
        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
    } else if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&state->selectedLeaf->focusKey)) {
        const std::wstring text =
            WideFromUtf8(FindCardTitleValue(state->dialog->Host().CurrentConfig(), *cardTitleKey).value_or(""));
        SetDlgItemTextW(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT, text.c_str());
        ShowLayoutEditEditors(hwnd, true, false, false, false, false, false);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        std::ostringstream trace;
        trace << BuildTraceNodeText(state->selectedNode) << " editor=\"text\""
              << " text=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_VALUE_EDIT));
        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
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
        ShowLayoutEditEditors(hwnd, false, false, false, false, true, showBinding);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        std::ostringstream trace;
        trace << BuildTraceNodeText(state->selectedNode) << " editor=\"metric\"" << BuildMetricDialogTraceValues(hwnd)
              << " scale_editable=" << QuoteTraceText(scaleEditable ? "true" : "false")
              << " unit_editable=" << QuoteTraceText(unitEditable ? "true" : "false")
              << " binding_visible=" << QuoteTraceText(showBinding ? "true" : "false")
              << " binding_options=" << QuoteTraceText(std::to_string(bindingOptions.size()));
        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:populate_selection", trace.str());
    } else {
        ShowLayoutEditEditors(hwnd, false, false, false, false, false, false);
        SetFontSamplePreview(state, hwnd, std::nullopt, nullptr);
        state->dialog->Host().TraceLayoutEditDialogEvent(
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
        std::ostringstream trace;
        trace << BuildTraceNodeText(state->selectedNode) << " raw=" << QuoteTraceText(title)
              << " parsed=" << QuoteTraceText(title) << " applied=" << QuoteTraceText(applied ? "true" : "false");
        state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_value", trace.str());
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
    std::ostringstream trace;
    trace << BuildTraceNodeText(state->selectedNode) << " raw=" << QuoteTraceText(Utf8FromWide(buffer)) << " parsed="
          << QuoteTraceText(
                 value.has_value() ? FormatLayoutEditTooltipValue(*value, state->selectedLeaf->valueFormat) : "invalid")
          << " applied=" << QuoteTraceText(applied ? "true" : "false");
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_value", trace.str());
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
    std::ostringstream trace;
    trace << BuildTraceNodeText(state->selectedNode) << " face=" << QuoteTraceText(font.face)
          << " size=" << QuoteTraceText(std::to_string(font.size))
          << " weight=" << QuoteTraceText(std::to_string(font.weight))
          << " applied=" << QuoteTraceText(applied ? "true" : "false");
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_font", trace.str());
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
    const bool applied = color.has_value() && state->dialog->Host().ApplyColorPreview(*parameter, *color);
    if (applied && color.has_value()) {
        SetColorSamplePreview(state, hwnd, *color);
    }
    std::ostringstream trace;
    trace << BuildTraceNodeText(state->selectedNode) << BuildColorDialogTraceValues(hwnd)
          << " parsed=" << QuoteTraceText(color.has_value() ? FormatTraceColorHex(*color) : "invalid")
          << " applied=" << QuoteTraceText(applied ? "true" : "false");
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_color", trace.str());
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

    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:picker_apply_begin",
        BuildTraceNodeText(state->selectedNode) + " picked=" + QuoteTraceText(FormatTraceColorHex(color)));
    const bool applied = state->dialog->Host().ApplyColorPreview(*parameter, color);
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
                FindLayoutEditParameterColorValue(state->dialog->Host().CurrentConfig(), *parameter).value_or(0))));
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
    std::ostringstream trace;
    trace << BuildTraceNodeText(state->selectedNode) << " first=" << QuoteTraceText(std::to_string(*first))
          << " second=" << QuoteTraceText(std::to_string(*second))
          << " applied=" << QuoteTraceText(applied ? "true" : "false");
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_weights", trace.str());
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
    std::ostringstream trace;
    trace << BuildTraceNodeText(state->selectedNode) << BuildMetricDialogTraceValues(hwnd) << " parsed_scale="
          << QuoteTraceText(scale.has_value()
                                ? FormatLayoutEditTooltipValue(*scale, configschema::ValueFormat::FloatingPoint)
                                : "disabled")
          << " applied=" << QuoteTraceText(applied ? "true" : "false");
    state->dialog->Host().TraceLayoutEditDialogEvent("layout_edit_dialog:preview_metric", trace.str());
    return applied;
}

bool RevertSelectedLayoutEditField(LayoutEditDialogState* state, HWND hwnd) {
    if (state == nullptr || state->selectedLeaf == nullptr) {
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
            const auto color = FindLayoutEditParameterColorValue(state->originalConfig, *parameter);
            if (!color.has_value()) {
                return false;
            }
            const bool applied = state->dialog->Host().ApplyColorPreview(*parameter, *color);
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

    return false;
}
