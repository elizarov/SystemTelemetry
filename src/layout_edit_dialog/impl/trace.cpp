#include "layout_edit_dialog/impl/trace.h"

#include "config/color_format.h"
#include "layout_edit/layout_edit_target_descriptor.h"
#include "layout_edit_dialog/impl/util.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "util/text_format.h"

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
        AppendFormat(
            text, " %s=%s", fields[i].label, QuoteTraceText(ReadDialogControlText(hwnd, fields[i].controlId)).c_str());
    }
    return text;
}

}  // namespace

std::string QuoteTraceText(std::string_view text) {
    return FormatText("\"%.*s\"", static_cast<int>(text.size()), text.data());
}

std::string FormatTraceColorHex(unsigned int color) {
    return FormatRgbaColorText(color);
}

std::string JoinNodePath(const std::vector<size_t>& path) {
    std::string text;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            AppendFormat(text, ".");
        }
        AppendFormat(text, "%zu", path[i]);
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
            return FormatText("focus=%s", QuoteTraceText(descriptor->configKey).c_str());
        }
        return FormatText("focus=%s", QuoteTraceText(GetLayoutEditParameterDisplayName(*parameter)).c_str());
    }
    if (const auto* weightKey = std::get_if<LayoutWeightEditKey>(&leaf->focusKey)) {
        const std::string focus =
            leaf->sectionName.empty() ? "weight" : FormatText("%s.layout", leaf->sectionName.c_str());
        std::string text = FormatText("focus=%s edit_card=%s node_path=%s",
            QuoteTraceText(focus).c_str(),
            QuoteTraceText(weightKey->editCardId).c_str(),
            QuoteTraceText(JoinNodePath(weightKey->nodePath)).c_str());
        AppendFormat(text, " separator=%zu", weightKey->separatorIndex);
        return text;
    }
    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&leaf->focusKey)) {
        const std::string focus = FormatText("[metrics] %s", metricKey->metricId.c_str());
        return FormatText("focus=%s", QuoteTraceText(focus).c_str());
    }
    if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&leaf->focusKey)) {
        const std::string focus = FormatText("[card.%s] title", cardTitleKey->cardId.c_str());
        return FormatText("focus=%s", QuoteTraceText(focus).c_str());
    }
    if (const auto* nodeFieldKey = std::get_if<LayoutNodeFieldEditKey>(&leaf->focusKey)) {
        return FormatText("focus=%s edit_card=%s node_path=%s",
            QuoteTraceText(LayoutNodeFieldEditTraceLabel(*nodeFieldKey, leaf->sectionName)).c_str(),
            QuoteTraceText(nodeFieldKey->editCardId).c_str(),
            QuoteTraceText(JoinNodePath(nodeFieldKey->nodePath)).c_str());
    }
    return "focus=\"unknown\"";
}

std::string BuildTraceNodeText(const LayoutEditTreeNode* node) {
    if (node == nullptr) {
        return "node=\"none\"";
    }

    std::string text = FormatText("node_kind=%s label=%s location=%s",
        QuoteTraceText(TreeNodeKindTraceName(node->kind)).c_str(),
        QuoteTraceText(node->label).c_str(),
        QuoteTraceText(node->locationText).c_str());
    if (node->leaf.has_value()) {
        AppendFormat(text, " %s", BuildTraceFocusKeyText(&*node->leaf).c_str());
        if (std::holds_alternative<LayoutMetricEditKey>(node->leaf->focusKey)) {
            AppendFormat(text, " value_format=\"metric\"");
        } else {
            AppendFormat(
                text, " value_format=%s", QuoteTraceText(ValueFormatTraceName(node->leaf->valueFormat)).c_str());
        }
    }
    return text;
}

std::string BuildTraceNodeDetail(const LayoutEditTreeNode* node, const char* format, ...) {
    std::string text = BuildTraceNodeText(node);
    va_list args;
    va_start(args, format);
    const std::string detail = FormatTextV(format, args);
    va_end(args);
    AppendFormat(text, "%s", detail.c_str());
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
