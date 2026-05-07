#include "layout_edit_dialog/impl/trace.h"

#include "config/color_format.h"
#include "layout_edit/layout_edit_target_descriptor.h"
#include "layout_edit_dialog/impl/util.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "util/trace.h"

namespace {

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

struct DialogTraceField {
    const char* label = nullptr;
    int controlId = 0;
};

std::string BuildDialogTraceValues(HWND hwnd, const DialogTraceField* fields, size_t fieldCount) {
    std::string text;
    for (size_t i = 0; i < fieldCount; ++i) {
        text += ' ';
        text += fields[i].label;
        text += '=';
        text += QuoteTraceText(ReadDialogControlTextUtf8(hwnd, fields[i].controlId));
    }
    return text;
}

}  // namespace

std::string QuoteTraceText(std::string_view text) {
    return Trace::QuoteText(text);
}

std::string FormatTraceColorHex(unsigned int color) {
    return FormatRgbaColorText(color);
}

std::string JoinNodePath(const std::vector<size_t>& path) {
    std::string text;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            text += '.';
        }
        text += std::to_string(path[i]);
    }
    return text;
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
        return "focus=" + QuoteTraceText(leaf->sectionName.empty() ? "weight" : leaf->sectionName + ".layout") +
               " edit_card=" + QuoteTraceText(weightKey->editCardId) +
               " node_path=" + QuoteTraceText(JoinNodePath(weightKey->nodePath)) +
               " separator=" + std::to_string(weightKey->separatorIndex);
    }
    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&leaf->focusKey)) {
        return "focus=" + QuoteTraceText("[metrics] " + metricKey->metricId);
    }
    if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&leaf->focusKey)) {
        return "focus=" + QuoteTraceText("[card." + cardTitleKey->cardId + "] title");
    }
    if (const auto* nodeFieldKey = std::get_if<LayoutNodeFieldEditKey>(&leaf->focusKey)) {
        return "focus=" + QuoteTraceText(LayoutNodeFieldEditTraceLabel(*nodeFieldKey, leaf->sectionName)) +
               " edit_card=" + QuoteTraceText(nodeFieldKey->editCardId) +
               " node_path=" + QuoteTraceText(JoinNodePath(nodeFieldKey->nodePath));
    }
    return "focus=\"unknown\"";
}

std::string BuildTraceNodeText(const LayoutEditTreeNode* node) {
    if (node == nullptr) {
        return "node=\"none\"";
    }

    std::string text = "node_kind=" + QuoteTraceText(TreeNodeKindTraceName(node->kind));
    text += " label=" + QuoteTraceText(node->label);
    text += " location=" + QuoteTraceText(node->locationText);
    if (node->leaf.has_value()) {
        text += ' ';
        text += BuildTraceFocusKeyText(&*node->leaf);
        if (std::holds_alternative<LayoutMetricEditKey>(node->leaf->focusKey)) {
            text += " value_format=\"metric\"";
        } else {
            text += " value_format=" + QuoteTraceText(ValueFormatTraceName(node->leaf->valueFormat));
        }
    }
    return text;
}

std::string BuildColorDialogTraceValues(HWND hwnd) {
    static constexpr DialogTraceField kFields[] = {{"hex", IDC_LAYOUT_EDIT_COLOR_HEX_EDIT},
        {"red", IDC_LAYOUT_EDIT_COLOR_RED_EDIT},
        {"green", IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT},
        {"blue", IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT},
        {"alpha", IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT}};
    return BuildDialogTraceValues(hwnd, kFields, sizeof(kFields) / sizeof(kFields[0]));
}

std::string BuildMetricDialogTraceValues(HWND hwnd) {
    static constexpr DialogTraceField kFields[] = {{"style", IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE},
        {"scale", IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT},
        {"unit", IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT},
        {"label", IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT},
        {"binding", IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT}};
    return BuildDialogTraceValues(hwnd, kFields, sizeof(kFields) / sizeof(kFields[0]));
}
