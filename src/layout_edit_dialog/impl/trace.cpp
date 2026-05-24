#include "layout_edit_dialog/impl/trace.h"

#include "layout_edit/layout_edit_target_descriptor.h"
#include "layout_edit_dialog/impl/util.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "util/resource_strings.h"
#include "util/text_format.h"

namespace {

ResourceStringId TreeNodeKindTraceName(LayoutEditTreeNodeKind kind) {
    switch (kind) {
        case LayoutEditTreeNodeKind::Section:
            return RES_STR("section");
        case LayoutEditTreeNodeKind::Group:
            return RES_STR("group");
        case LayoutEditTreeNodeKind::Container:
            return RES_STR("container");
        case LayoutEditTreeNodeKind::Leaf:
            return RES_STR("leaf");
    }
    return RES_STR("unknown");
}

ResourceStringId ValueFormatTraceName(configschema::ValueFormat format) {
    switch (format) {
        case configschema::ValueFormat::String:
            return RES_STR("string");
        case configschema::ValueFormat::Integer:
            return RES_STR("integer");
        case configschema::ValueFormat::FloatingPoint:
            return RES_STR("float");
        case configschema::ValueFormat::FontSpec:
            return RES_STR("font");
        case configschema::ValueFormat::ColorHex:
            return RES_STR("color");
    }
    return RES_STR("unknown");
}

struct DialogTraceField {
    ResourceStringId label{};
    int controlId = 0;
};

std::string BuildDialogTraceValues(HWND hwnd, const DialogTraceField* fields, size_t fieldCount) {
    std::string text;
    for (size_t i = 0; i < fieldCount; ++i) {
        const std::string value = ReadDialogControlText(hwnd, fields[i].controlId);
        AppendFormat(text,
            RES_STR(" %s=\"%.*s\""),
            ResourceStringText(fields[i].label),
            static_cast<int>(value.size()),
            value.data());
    }
    return text;
}

}  // namespace

std::string JoinNodePath(const std::vector<size_t>& path) {
    std::string text;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            text.push_back('.');
        }
        AppendFormat(text, RES_STR("%zu"), path[i]);
    }
    return text;
}

std::string BuildTraceFocusKeyText(const LayoutEditTreeLeaf* leaf) {
    if (leaf == nullptr) {
        return ResourceStringText(RES_STR("focus=\"none\""));
    }
    if (const auto* parameter = std::get_if<LayoutEditParameter>(&leaf->focusKey)) {
        const auto descriptor = FindLayoutEditTooltipDescriptor(*parameter);
        if (descriptor.has_value()) {
            return FormatText(RES_STR("focus=\"%.*s\""),
                static_cast<int>(descriptor->configKey.size()),
                descriptor->configKey.data());
        }
        const std::string displayName = GetLayoutEditParameterDisplayName(*parameter);
        return FormatText(RES_STR("focus=\"%.*s\""), static_cast<int>(displayName.size()), displayName.data());
    }
    if (const auto* weightKey = std::get_if<LayoutWeightEditKey>(&leaf->focusKey)) {
        const std::string focus = leaf->sectionName.empty()
                                      ? ResourceStringText(RES_STR("weight"))
                                      : FormatText(RES_STR("%s.layout"), leaf->sectionName.c_str());
        const std::string nodePath = JoinNodePath(weightKey->nodePath);
        std::string text = FormatText(RES_STR("focus=\"%.*s\" edit_card=\"%.*s\" node_path=\"%.*s\""),
            static_cast<int>(focus.size()),
            focus.data(),
            static_cast<int>(weightKey->editCardId.size()),
            weightKey->editCardId.data(),
            static_cast<int>(nodePath.size()),
            nodePath.data());
        AppendFormat(text, RES_STR(" separator=%zu"), weightKey->separatorIndex);
        return text;
    }
    if (const auto* metricKey = std::get_if<LayoutMetricEditKey>(&leaf->focusKey)) {
        const std::string focus = FormatText(RES_STR("[metrics] %s"), metricKey->metricId.c_str());
        return FormatText(RES_STR("focus=\"%.*s\""), static_cast<int>(focus.size()), focus.data());
    }
    if (const auto* cardTitleKey = std::get_if<LayoutCardTitleEditKey>(&leaf->focusKey)) {
        const std::string focus = FormatText(RES_STR("[card.%s] title"), cardTitleKey->cardId.c_str());
        return FormatText(RES_STR("focus=\"%.*s\""), static_cast<int>(focus.size()), focus.data());
    }
    if (const auto* nodeFieldKey = std::get_if<LayoutNodeFieldEditKey>(&leaf->focusKey)) {
        const std::string label = LayoutNodeFieldEditTraceLabel(*nodeFieldKey, leaf->sectionName);
        const std::string nodePath = JoinNodePath(nodeFieldKey->nodePath);
        return FormatText(RES_STR("focus=\"%.*s\" edit_card=\"%.*s\" node_path=\"%.*s\""),
            static_cast<int>(label.size()),
            label.data(),
            static_cast<int>(nodeFieldKey->editCardId.size()),
            nodeFieldKey->editCardId.data(),
            static_cast<int>(nodePath.size()),
            nodePath.data());
    }
    return ResourceStringText(RES_STR("focus=\"unknown\""));
}

std::string BuildTraceNodeText(const LayoutEditTreeNode* node) {
    if (node == nullptr) {
        return ResourceStringText(RES_STR("node=\"none\""));
    }

    std::string text = FormatText(RES_STR("node_kind=\"%s\" label=\"%.*s\" location=\"%.*s\""),
        ResourceStringText(TreeNodeKindTraceName(node->kind)),
        static_cast<int>(node->label.size()),
        node->label.data(),
        static_cast<int>(node->locationText.size()),
        node->locationText.data());
    if (node->leaf.has_value()) {
        text.push_back(' ');
        text += BuildTraceFocusKeyText(&*node->leaf);
        if (std::holds_alternative<LayoutMetricEditKey>(node->leaf->focusKey)) {
            text += ResourceStringText(RES_STR(" value_format=\"metric\""));
        } else {
            AppendFormat(text,
                RES_STR(" value_format=\"%s\""),
                ResourceStringText(ValueFormatTraceName(node->leaf->valueFormat)));
        }
    }
    return text;
}

std::string BuildTraceNodeDetail(const LayoutEditTreeNode* node, ResourceStringId format, ...) {
    std::string text = BuildTraceNodeText(node);
    va_list args;
    va_start(args, format);
    const std::string detail = FormatTextV(format, args);
    va_end(args);
    text += detail;
    return text;
}

std::string BuildColorDialogTraceValues(HWND hwnd) {
    static constexpr DialogTraceField kFields[] = {{RES_STR("hex"), IDC_LAYOUT_EDIT_COLOR_HEX_EDIT},
        {RES_STR("red"), IDC_LAYOUT_EDIT_COLOR_RED_EDIT},
        {RES_STR("green"), IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT},
        {RES_STR("blue"), IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT},
        {RES_STR("alpha"), IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT}};
    return BuildDialogTraceValues(hwnd, kFields, sizeof(kFields) / sizeof(kFields[0]));
}

std::string BuildMetricDialogTraceValues(HWND hwnd) {
    static constexpr DialogTraceField kFields[] = {{RES_STR("style"), IDC_LAYOUT_EDIT_METRIC_STYLE_VALUE},
        {RES_STR("scale"), IDC_LAYOUT_EDIT_METRIC_SCALE_EDIT},
        {RES_STR("unit"), IDC_LAYOUT_EDIT_METRIC_UNIT_EDIT},
        {RES_STR("label"), IDC_LAYOUT_EDIT_METRIC_LABEL_EDIT},
        {RES_STR("binding"), IDC_LAYOUT_EDIT_METRIC_BINDING_EDIT}};
    return BuildDialogTraceValues(hwnd, kFields, sizeof(kFields) / sizeof(kFields[0]));
}
