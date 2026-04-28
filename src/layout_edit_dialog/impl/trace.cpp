#include "layout_edit_dialog/impl/trace.h"

#include <cstdio>
#include <sstream>

#include "layout_edit_dialog/impl/util.h"
#include "layout_model/layout_edit_parameter_metadata.h"

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

}  // namespace

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
    sprintf_s(buffer, "#%08X", color);
    return buffer;
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
    if (const auto* nodeFieldKey = std::get_if<LayoutNodeFieldEditKey>(&leaf->focusKey)) {
        std::ostringstream stream;
        stream << "focus="
               << QuoteTraceText(leaf->sectionName + "." + std::string(EnumToString(nodeFieldKey->widgetClass)));
        stream << " edit_card=" << QuoteTraceText(nodeFieldKey->editCardId);
        stream << " node_path=" << QuoteTraceText(JoinNodePath(nodeFieldKey->nodePath));
        return stream.str();
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

std::string BuildColorDialogTraceValues(HWND hwnd) {
    std::ostringstream trace;
    trace << " hex=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_HEX_EDIT))
          << " red=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_RED_EDIT))
          << " green=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_GREEN_EDIT))
          << " blue=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_BLUE_EDIT))
          << " alpha=" << QuoteTraceText(ReadDialogControlTextUtf8(hwnd, IDC_LAYOUT_EDIT_COLOR_ALPHA_EDIT));
    return trace.str();
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
