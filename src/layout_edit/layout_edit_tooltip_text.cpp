#include "layout_edit/layout_edit_tooltip_text.h"

#include <algorithm>

#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_target_descriptor.h"
#include "layout_edit/layout_edit_tooltip.h"
#include "layout_edit/layout_edit_tooltip_payload.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "util/localization_catalog.h"
#include "util/utf8.h"

namespace {

std::wstring BuildTooltipText(
    const LayoutEditTooltipDescriptor& descriptor, double value, const std::wstring& descriptionText) {
    std::wstring text = WideFromUtf8(BuildLayoutEditTooltipLine(descriptor, value));
    if (!descriptionText.empty()) {
        text += L"\r\n";
        text += descriptionText;
    }
    return text;
}

std::wstring BuildTooltipText(
    const LayoutEditTooltipDescriptor& descriptor, const UiFontConfig& value, const std::wstring& descriptionText) {
    std::wstring text = WideFromUtf8(BuildLayoutEditTooltipLine(descriptor, value));
    if (!descriptionText.empty()) {
        text += L"\r\n";
        text += descriptionText;
    }
    return text;
}

std::wstring BuildTooltipText(
    const LayoutEditTooltipDescriptor& descriptor, unsigned int value, const std::wstring& descriptionText) {
    std::wstring text = WideFromUtf8(BuildLayoutEditTooltipLine(descriptor, value));
    if (!descriptionText.empty()) {
        text += L"\r\n";
        text += descriptionText;
    }
    return text;
}

std::wstring BuildTooltipText(
    const LayoutEditTooltipDescriptor& descriptor, std::string_view value, const std::wstring& descriptionText) {
    std::wstring text = WideFromUtf8(BuildLayoutEditTooltipLine(descriptor, value));
    if (!descriptionText.empty()) {
        text += L"\r\n";
        text += descriptionText;
    }
    return text;
}

std::string LayoutGuideTooltipSectionName(const AppConfig& config, const LayoutEditGuide& guide) {
    if (!guide.editCardId.empty()) {
        return "card." + guide.editCardId;
    }
    if (!config.display.layout.empty()) {
        return "layout." + config.display.layout;
    }
    return "layout";
}

std::string LayoutGuideTooltipConfigMember(const LayoutEditGuide& guide) {
    return guide.editCardId.empty() ? "cards" : "layout";
}

const LayoutCardConfig* FindCardById(const AppConfig& config, std::string_view cardId) {
    const auto it = std::find_if(
        config.layout.cards.begin(), config.layout.cards.end(), [&](const auto& card) { return card.id == cardId; });
    return it != config.layout.cards.end() ? &(*it) : nullptr;
}

LayoutEditTooltipDescriptor CardTitleTooltipDescriptor(const LayoutCardTitleEditKey& key) {
    LayoutEditTooltipDescriptor descriptor;
    descriptor.configKey = "config.card.title";
    descriptor.sectionName = "card." + key.cardId;
    descriptor.memberName = "title";
    descriptor.valueFormat = configschema::ValueFormat::String;
    return descriptor;
}

const LayoutNodeConfig* FindLayoutGuideNode(const AppConfig& config, const LayoutEditGuide& guide) {
    return FindGuideNode(config, LayoutEditLayoutTarget::ForGuide(guide));
}

std::string LayoutGuideChildName(const LayoutNodeConfig& node) {
    return node.name.empty() ? "unknown" : node.name;
}

std::string BuildLayoutGuideTooltipLine(const AppConfig& config, const LayoutEditGuide& guide) {
    const std::string sectionName = LayoutGuideTooltipSectionName(config, guide);
    const std::string configMember = LayoutGuideTooltipConfigMember(guide);
    const LayoutNodeConfig* node = FindLayoutGuideNode(config, guide);
    if (node == nullptr || node->children.size() < 2 || guide.separatorIndex + 1 >= node->children.size()) {
        return "[" + sectionName + "] " + configMember;
    }

    const LayoutNodeConfig& leftChild = node->children[guide.separatorIndex];
    const LayoutNodeConfig& rightChild = node->children[guide.separatorIndex + 1];
    return "[" + sectionName + "] " + configMember + " = ... " + node->name + "(" + LayoutGuideChildName(leftChild) +
           ":" + std::to_string((std::max)(1, leftChild.weight)) + ", " + LayoutGuideChildName(rightChild) + ":" +
           std::to_string((std::max)(1, rightChild.weight)) + ")";
}

std::wstring BuildLayoutGuideTooltipText(const AppConfig& config, const LayoutEditGuide& guide) {
    std::wstring text = WideFromUtf8(BuildLayoutGuideTooltipLine(config, guide));
    const std::wstring description = WideFromUtf8(FindLocalizedText("layout_edit.layout_guide"));
    if (!description.empty()) {
        text += L"\r\n";
        text += description;
    }
    return text;
}

std::wstring BuildMetricTooltipText(const LayoutMetricEditKey& key, const MetricDefinitionConfig& definition) {
    std::wstring text = WideFromUtf8("[metrics] " + key.metricId + " = " + FormatMetricDefinitionValue(definition));
    const std::wstring description = WideFromUtf8(FindLocalizedText("layout_edit.metric_definition"));
    if (!description.empty()) {
        text += L"\r\n";
        text += description;
    }
    return text;
}

std::wstring BuildMetricListOrderTooltipText(const AppConfig& config, const LayoutNodeFieldEditKey& key, int rowIndex) {
    const auto firstLine = BuildMetricListOrderTooltipLine(config, key, rowIndex);
    if (!firstLine.has_value()) {
        return L"";
    }
    std::wstring text = WideFromUtf8(*firstLine);
    const std::wstring description = WideFromUtf8(FindLocalizedText("layout_edit.metric_list_reorder"));
    if (!description.empty()) {
        text += L"\r\n";
        text += description;
    }
    return text;
}

std::wstring BuildMetricListAddRowTooltipText(const AppConfig& config, const LayoutNodeFieldEditKey& key) {
    const auto firstLine = BuildMetricListAddRowTooltipLine(config, key);
    if (!firstLine.has_value()) {
        return L"";
    }
    std::wstring text = WideFromUtf8(*firstLine);
    const std::wstring description = WideFromUtf8(FindLocalizedText("layout_edit.metric_list_add_row"));
    if (!description.empty()) {
        text += L"\r\n";
        text += description;
    }
    return text;
}

std::optional<std::wstring> AbortTooltipBuild(std::string* errorReason, std::string_view reason) {
    if (errorReason != nullptr) {
        *errorReason = std::string(reason);
    }
    return std::nullopt;
}

}  // namespace

const char* LayoutEditTooltipPayloadTraceKind(const TooltipPayload& payload) {
    if (std::holds_alternative<LayoutEditGuide>(payload)) {
        return "layout_guide";
    }
    if (std::holds_alternative<LayoutEditWidgetGuide>(payload)) {
        return "widget_guide";
    }
    if (std::holds_alternative<LayoutEditGapAnchor>(payload)) {
        return "gap_anchor";
    }
    if (std::holds_alternative<LayoutEditAnchorRegion>(payload)) {
        return "anchor_region";
    }
    if (std::holds_alternative<LayoutEditColorRegion>(payload)) {
        return "color_region";
    }
    return "unknown";
}

std::optional<std::wstring> BuildLayoutEditTooltipTextForPayload(
    const AppConfig& config, const TooltipPayload& payload, std::string* errorReason) {
    if (errorReason != nullptr) {
        errorReason->clear();
    }

    std::optional<LayoutEditTooltipDescriptor> descriptor;
    std::optional<LayoutMetricEditKey> metricKey;
    std::optional<LayoutCardTitleEditKey> cardTitleKey;
    std::optional<LayoutNodeFieldEditKey> nodeFieldKey;
    double value = 0.0;
    std::optional<UiFontConfig> fontValue;
    std::optional<unsigned int> colorValue;
    std::optional<std::string> stringValue;

    if (const auto* guide = std::get_if<LayoutEditGuide>(&payload)) {
        return BuildLayoutGuideTooltipText(config, *guide);
    }

    if (const auto focusKey = TooltipPayloadFocusKey(payload);
        focusKey.has_value() && std::holds_alternative<LayoutMetricEditKey>(*focusKey)) {
        metricKey = std::get<LayoutMetricEditKey>(*focusKey);
    } else if (focusKey.has_value() && std::holds_alternative<LayoutCardTitleEditKey>(*focusKey)) {
        cardTitleKey = std::get<LayoutCardTitleEditKey>(*focusKey);
    } else if (focusKey.has_value() && std::holds_alternative<LayoutNodeFieldEditKey>(*focusKey)) {
        nodeFieldKey = std::get<LayoutNodeFieldEditKey>(*focusKey);
    }
    if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&payload)) {
        if (const auto anchorNodeFieldKey = LayoutEditAnchorNodeFieldKey(anchor->key); anchorNodeFieldKey.has_value()) {
            nodeFieldKey = *anchorNodeFieldKey;
        }
    }
    if (const auto parameter = TooltipPayloadParameter(payload); parameter.has_value()) {
        descriptor = FindLayoutEditTooltipDescriptor(*parameter);
        value = TooltipPayloadNumericValue(payload).value_or(0.0);
        if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&payload)) {
            if (const auto anchorParameter = LayoutEditAnchorParameter(anchor->key); anchorParameter.has_value()) {
                if (const auto currentFont = FindLayoutEditTooltipFontValue(config, *anchorParameter);
                    currentFont.has_value() && *currentFont != nullptr) {
                    fontValue = **currentFont;
                }
            }
        } else if (const auto currentColor = FindLayoutEditParameterColorValue(config, *parameter);
            currentColor.has_value()) {
            colorValue = *currentColor;
        }
    } else if (metricKey.has_value()) {
        const MetricDefinitionConfig* definition = FindMetricDefinition(config.layout.metrics, metricKey->metricId);
        if (definition == nullptr) {
            return AbortTooltipBuild(errorReason, "missing_metric_definition");
        }
        return BuildMetricTooltipText(*metricKey, *definition);
    } else if (cardTitleKey.has_value()) {
        descriptor = CardTitleTooltipDescriptor(*cardTitleKey);
        const LayoutCardConfig* card = FindCardById(config, cardTitleKey->cardId);
        if (card == nullptr) {
            return AbortTooltipBuild(errorReason, "missing_card_title");
        }
        stringValue = card->title;
    } else if (nodeFieldKey.has_value()) {
        const LayoutNodeFieldEditDescriptor* nodeFieldDescriptor = FindLayoutNodeFieldEditDescriptor(*nodeFieldKey);
        if (nodeFieldDescriptor == nullptr) {
            return AbortTooltipBuild(errorReason, "missing_node_field_descriptor");
        }
        if (nodeFieldDescriptor->editorKind == LayoutEditEditorKind::MetricListOrder) {
            int rowIndex = 0;
            bool addRowAnchor = false;
            if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&payload)) {
                rowIndex = anchor->key.anchorId;
                addRowAnchor = anchor->shape == AnchorShape::Plus;
            }
            std::wstring tooltipText = addRowAnchor ? BuildMetricListAddRowTooltipText(config, *nodeFieldKey)
                                                    : BuildMetricListOrderTooltipText(config, *nodeFieldKey, rowIndex);
            if (tooltipText.empty()) {
                return AbortTooltipBuild(errorReason, "empty_metric_list_text");
            }
            return tooltipText;
        }
        const LayoutNodeConfig* node = FindLayoutNodeFieldNode(config, *nodeFieldKey);
        if (node == nullptr) {
            return AbortTooltipBuild(errorReason, "missing_node_field");
        }
        const std::string valueLabel = nodeFieldDescriptor->editorKind == LayoutEditEditorKind::DateTimeFormat
                                           ? std::string(EnumToString(nodeFieldKey->widgetClass)) + " format"
                                           : std::string(EnumToString(nodeFieldKey->widgetClass));
        std::wstring tooltipText =
            WideFromUtf8(valueLabel + " = " + ReadLayoutNodeFieldValue(*node, nodeFieldKey->field));
        const std::wstring description = WideFromUtf8(FindLocalizedText(nodeFieldDescriptor->descriptionKey));
        if (!description.empty()) {
            tooltipText += L"\r\n";
            tooltipText += description;
        }
        return tooltipText;
    }

    if (!descriptor.has_value() && !metricKey.has_value() && !cardTitleKey.has_value() && !nodeFieldKey.has_value()) {
        return AbortTooltipBuild(errorReason, "unsupported_target");
    }

    if (!metricKey.has_value() && !nodeFieldKey.has_value()) {
        const std::wstring description = WideFromUtf8(FindLocalizedText(descriptor->configKey));
        if (descriptor->valueFormat == configschema::ValueFormat::String && stringValue.has_value()) {
            return BuildTooltipText(*descriptor, *stringValue, description);
        }
        if (descriptor->valueFormat == configschema::ValueFormat::FontSpec && fontValue.has_value()) {
            return BuildTooltipText(*descriptor, *fontValue, description);
        }
        if (descriptor->valueFormat == configschema::ValueFormat::ColorHex && colorValue.has_value()) {
            return BuildTooltipText(*descriptor, *colorValue, description);
        }
        return BuildTooltipText(*descriptor, value, description);
    }

    return AbortTooltipBuild(errorReason, "unsupported_target");
}
