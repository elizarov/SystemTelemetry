#include "layout_edit/layout_edit_tooltip_text.h"

#include <algorithm>

#include "config/config_telemetry.h"
#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_edit/layout_edit_target_descriptor.h"
#include "layout_edit/layout_edit_tooltip.h"
#include "layout_edit/layout_edit_tooltip_payload.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "util/localization_catalog.h"
#include "util/text_format.h"

namespace {

void AppendTooltipDescription(std::string& text, std::string_view descriptionText) {
    // Size: keep tooltip assembly UTF-8 until the Win32 tooltip boundary to limit wide-string helper code.
    if (!descriptionText.empty()) {
        AppendFormat(text, "\r\n%.*s", static_cast<int>(descriptionText.size()), descriptionText.data());
    }
}

std::string TooltipText(std::string text, std::string_view descriptionText) {
    AppendTooltipDescription(text, descriptionText);
    return text;
}

std::string BuildTooltipText(
    const LayoutEditTooltipDescriptor& descriptor, double value, std::string_view descriptionText) {
    return TooltipText(BuildLayoutEditTooltipLine(descriptor, value), descriptionText);
}

std::string BuildTooltipText(
    const LayoutEditTooltipDescriptor& descriptor, const UiFontConfig& value, std::string_view descriptionText) {
    return TooltipText(BuildLayoutEditTooltipLine(descriptor, value), descriptionText);
}

std::string BuildTooltipText(
    const LayoutEditTooltipDescriptor& descriptor, std::string_view value, std::string_view descriptionText) {
    return TooltipText(BuildLayoutEditTooltipLine(descriptor, value), descriptionText);
}

std::string TooltipColorExpression(const ColorConfig& color) {
    return color.expression.empty() ? FormatLayoutEditTooltipValue(color.ToRgba()) : color.expression;
}

std::string LayoutGuideTooltipSectionName(const AppConfig& config, const LayoutEditGuide& guide) {
    if (!guide.editCardId.empty()) {
        return FormatText("card.%s", guide.editCardId.c_str());
    }
    if (!config.display.layout.empty()) {
        return FormatText("layout.%s", config.display.layout.c_str());
    }
    return "layout";
}

std::string LayoutGuideTooltipConfigMember(const LayoutEditGuide& guide) {
    return guide.editCardId.empty() ? "cards" : "layout";
}

const LayoutCardConfig* FindCardById(const AppConfig& config, std::string_view cardId) {
    for (const LayoutCardConfig& card : config.layout.cards) {
        if (card.id == cardId) {
            return &card;
        }
    }
    return nullptr;
}

LayoutEditTooltipDescriptor CardTitleTooltipDescriptor(const LayoutCardTitleEditKey& key) {
    LayoutEditTooltipDescriptor descriptor;
    descriptor.configKey = "config.card.title";
    descriptor.sectionName = FormatText("card.%s", key.cardId.c_str());
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
        return FormatText("[%s] %s", sectionName.c_str(), configMember.c_str());
    }

    const LayoutNodeConfig& leftChild = node->children[guide.separatorIndex];
    const LayoutNodeConfig& rightChild = node->children[guide.separatorIndex + 1];
    return FormatText("[%s] %s = %s(%s:%d, %s:%d)",
        sectionName.c_str(),
        configMember.c_str(),
        node->name.c_str(),
        LayoutGuideChildName(leftChild).c_str(),
        (std::max)(1, leftChild.weight),
        LayoutGuideChildName(rightChild).c_str(),
        (std::max)(1, rightChild.weight));
}

std::string BuildLayoutGuideTooltipText(const AppConfig& config, const LayoutEditGuide& guide) {
    return TooltipText(
        BuildLayoutGuideTooltipLine(config, guide), FindLocalizedText(RES_STR("layout_edit.layout_guide")));
}

std::string BuildMetricTooltipText(const LayoutMetricEditKey& key, const MetricDefinitionConfig& definition) {
    return TooltipText(
        FormatText("[metrics] %s = %s", key.metricId.c_str(), FormatMetricDefinitionValue(definition).c_str()),
        FindLocalizedText(RES_STR("layout_edit.metric_definition")));
}

std::string BuildMetricListOrderTooltipText(const AppConfig& config, const LayoutNodeFieldEditKey& key, int rowIndex) {
    const auto firstLine = BuildMetricListOrderTooltipLine(config, key, rowIndex);
    if (!firstLine.has_value()) {
        return {};
    }
    return TooltipText(*firstLine, FindLocalizedText(RES_STR("layout_edit.metric_list_reorder")));
}

std::string BuildMetricListAddRowTooltipText(const AppConfig& config, const LayoutNodeFieldEditKey& key) {
    const auto firstLine = BuildMetricListAddRowTooltipLine(config, key);
    if (!firstLine.has_value()) {
        return {};
    }
    return TooltipText(*firstLine, FindLocalizedText(RES_STR("layout_edit.metric_list_add_row")));
}

std::string BuildContainerChildOrderTooltipText(const AppConfig& config, const LayoutEditAnchorRegion& anchor) {
    const auto containerOrderKey = LayoutEditAnchorContainerChildOrderKey(anchor.key);
    if (!containerOrderKey.has_value()) {
        return {};
    }
    const auto firstLine = BuildContainerChildOrderTooltipLine(config, *containerOrderKey);
    if (!firstLine.has_value()) {
        return {};
    }
    const ResourceStringId descriptionKey = anchor.shape == AnchorShape::HorizontalReorder ?
        RES_STR("layout_edit.container_reorder_horizontal") :
        RES_STR("layout_edit.container_reorder_vertical");
    return TooltipText(*firstLine, FindLocalizedText(descriptionKey));
}

bool AbortTooltipBuild(std::string* errorReason, std::string_view reason) {
    if (errorReason != nullptr) {
        *errorReason = std::string(reason);
    }
    return false;
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

bool BuildLayoutEditTooltipTextForPayload(
    const AppConfig& config, const TooltipPayload& payload, std::string& tooltipText, std::string* errorReason) {
    if (errorReason != nullptr) {
        errorReason->clear();
    }

    std::optional<LayoutEditTooltipDescriptor> descriptor;
    std::optional<LayoutMetricEditKey> metricKey;
    std::optional<LayoutCardTitleEditKey> cardTitleKey;
    std::optional<LayoutNodeFieldEditKey> nodeFieldKey;
    std::optional<LayoutContainerEditKey> containerKey;
    double value = 0.0;
    std::optional<UiFontConfig> fontValue;
    std::optional<std::string> colorExpressionValue;
    std::optional<std::string> stringValue;

    if (const auto* guide = std::get_if<LayoutEditGuide>(&payload)) {
        tooltipText = BuildLayoutGuideTooltipText(config, *guide);
        return true;
    }

    if (const auto focusKey = TooltipPayloadFocusKey(payload); focusKey.has_value()) {
        if (const auto* metricCandidate = std::get_if<LayoutMetricEditKey>(&*focusKey)) {
            metricKey = *metricCandidate;
        } else if (const auto* cardTitleCandidate = std::get_if<LayoutCardTitleEditKey>(&*focusKey)) {
            cardTitleKey = *cardTitleCandidate;
        } else if (const auto* nodeFieldCandidate = std::get_if<LayoutNodeFieldEditKey>(&*focusKey)) {
            nodeFieldKey = *nodeFieldCandidate;
        } else if (const auto* containerCandidate = std::get_if<LayoutContainerEditKey>(&*focusKey)) {
            containerKey = *containerCandidate;
        }
    }
    if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&payload)) {
        if (const auto anchorNodeFieldKey = LayoutEditAnchorNodeFieldKey(anchor->key); anchorNodeFieldKey.has_value()) {
            nodeFieldKey = *anchorNodeFieldKey;
        }
        if (const auto containerOrderKey = LayoutEditAnchorContainerChildOrderKey(anchor->key);
            containerOrderKey.has_value()) {
            tooltipText = BuildContainerChildOrderTooltipText(config, *anchor);
            if (tooltipText.empty()) {
                return AbortTooltipBuild(errorReason, "empty_container_child_order_text");
            }
            return true;
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
        } else if (const auto currentColor = FindLayoutEditParameterColorConfigValue(config, *parameter);
            currentColor.has_value() && *currentColor != nullptr) {
            colorExpressionValue = TooltipColorExpression(**currentColor);
        }
    } else if (metricKey.has_value()) {
        const MetricDefinitionConfig* definition = FindMetricDefinition(config.layout.metrics, metricKey->metricId);
        if (definition == nullptr) {
            return AbortTooltipBuild(errorReason, "missing_metric_definition");
        }
        tooltipText = BuildMetricTooltipText(*metricKey, *definition);
        return true;
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
            tooltipText = addRowAnchor ?
                BuildMetricListAddRowTooltipText(config, *nodeFieldKey) :
                BuildMetricListOrderTooltipText(config, *nodeFieldKey, rowIndex);
            if (tooltipText.empty()) {
                return AbortTooltipBuild(errorReason, "empty_metric_list_text");
            }
            return true;
        }
        const LayoutNodeConfig* node = FindLayoutNodeFieldNode(config, *nodeFieldKey);
        if (node == nullptr) {
            return AbortTooltipBuild(errorReason, "missing_node_field");
        }
        const std::string valueLabel = nodeFieldDescriptor->editorKind == LayoutEditEditorKind::DateTimeFormat ?
            FormatText("%s format", EnumToString(nodeFieldKey->widgetClass)) :
            std::string(EnumToString(nodeFieldKey->widgetClass));
        std::string text =
            FormatText("%s = %s", valueLabel.c_str(), ReadLayoutNodeFieldValue(*node, nodeFieldKey->field).c_str());
        AppendTooltipDescription(text, FindLocalizedText(nodeFieldDescriptor->descriptionResourceKey));
        tooltipText = std::move(text);
        return true;
    }

    if (!descriptor.has_value() && !metricKey.has_value() && !cardTitleKey.has_value() && !nodeFieldKey.has_value() &&
        !containerKey.has_value()) {
        return AbortTooltipBuild(errorReason, "unsupported_target");
    }

    if (!metricKey.has_value() && !nodeFieldKey.has_value()) {
        const LayoutEditTooltipDescriptor& tooltipDescriptor = *descriptor;
        const std::string description = FindLocalizedText(tooltipDescriptor.configKey);
        if (tooltipDescriptor.valueFormat == configschema::ValueFormat::String && stringValue.has_value()) {
            tooltipText = BuildTooltipText(tooltipDescriptor, *stringValue, description);
            return true;
        }
        if (tooltipDescriptor.valueFormat == configschema::ValueFormat::FontSpec && fontValue.has_value()) {
            tooltipText = BuildTooltipText(tooltipDescriptor, *fontValue, description);
            return true;
        }
        if (tooltipDescriptor.valueFormat == configschema::ValueFormat::ColorHex && colorExpressionValue.has_value()) {
            tooltipText = BuildTooltipText(tooltipDescriptor, *colorExpressionValue, description);
            return true;
        }
        tooltipText = BuildTooltipText(tooltipDescriptor, value, description);
        return true;
    }

    return AbortTooltipBuild(errorReason, "unsupported_target");
}
