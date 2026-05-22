#include "layout_edit/layout_edit_active_region_trace.h"

#include <string>

#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "util/enum_string.h"
#include "util/text_format.h"

namespace {

std::string FormatNodePath(const std::vector<size_t>& nodePath) {
    if (nodePath.empty()) {
        return "root";
    }
    std::string text;
    for (size_t index : nodePath) {
        if (!text.empty()) {
            AppendFormat(text, "/");
        }
        AppendFormat(text, "children[%zu]", index);
    }
    return text;
}

std::string ActiveLayoutSectionName(const AppConfig& config) {
    return config.display.layout.empty() ? "layout" : FormatText("layout.%s", config.display.layout.c_str());
}

std::string FormatLayoutConfigPath(
    const AppConfig& config, const std::string& editCardId, const std::vector<size_t>& nodePath) {
    if (editCardId.empty()) {
        return FormatText("%s.cards/%s", ActiveLayoutSectionName(config).c_str(), FormatNodePath(nodePath).c_str());
    }
    return FormatText("card.%s.layout/%s", editCardId.c_str(), FormatNodePath(nodePath).c_str());
}

std::string FormatLayoutEditParameterPath(LayoutEditParameter parameter) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
    if (!descriptor.has_value()) {
        return "parameter";
    }
    return descriptor->configKey;
}

std::string FormatLayoutEditParameterDetail(LayoutEditParameter parameter) {
    return FormatText("%s (%s)",
        GetLayoutEditParameterDisplayName(parameter).c_str(),
        FormatLayoutEditParameterPath(parameter).c_str());
}

std::string FormatWidgetIdentityPath(const AppConfig& config, const LayoutEditWidgetIdentity& widget) {
    switch (widget.kind) {
        case LayoutEditWidgetIdentity::Kind::DashboardChrome:
            return FormatText("%s.dashboard", ActiveLayoutSectionName(config).c_str());
        case LayoutEditWidgetIdentity::Kind::CardChrome:
            return FormatText("%s.cards/card[%s]", ActiveLayoutSectionName(config).c_str(), widget.editCardId.c_str());
        case LayoutEditWidgetIdentity::Kind::Widget:
        default:
            return FormatLayoutConfigPath(config, widget.editCardId, widget.nodePath);
    }
}

std::string FormatGuideAxis(LayoutGuideAxis axis) {
    return axis == LayoutGuideAxis::Vertical ? "vertical" : "horizontal";
}

std::string FormatAnchorShape(AnchorShape shape) {
    switch (shape) {
        case AnchorShape::Circle:
            return "circle";
        case AnchorShape::Diamond:
            return "diamond";
        case AnchorShape::Square:
            return "square";
        case AnchorShape::Wedge:
            return "wedge";
        case AnchorShape::VerticalReorder:
            return "vertical-reorder";
        case AnchorShape::HorizontalReorder:
            return "horizontal-reorder";
        case AnchorShape::Plus:
            return "plus";
    }
    return "unknown";
}

std::string FormatAnchorSubject(const AppConfig& config, const LayoutEditAnchorKey& key) {
    if (const auto parameter = LayoutEditAnchorParameter(key); parameter.has_value()) {
        return FormatLayoutEditParameterDetail(*parameter);
    }
    if (const auto metric = LayoutEditAnchorMetricKey(key); metric.has_value()) {
        return FormatText("metric binding %s", metric->metricId.c_str());
    }
    if (const auto title = LayoutEditAnchorCardTitleKey(key); title.has_value()) {
        return FormatText("card title %s", title->cardId.c_str());
    }
    if (const auto nodeField = LayoutEditAnchorNodeFieldKey(key); nodeField.has_value()) {
        return FormatText("%s parameter %s",
            EnumToString(nodeField->widgetClass),
            FormatLayoutConfigPath(config, nodeField->editCardId, nodeField->nodePath).c_str());
    }
    if (const auto order = LayoutEditAnchorContainerChildOrderKey(key); order.has_value()) {
        return FormatText(
            "container child order %s", FormatLayoutConfigPath(config, order->editCardId, order->nodePath).c_str());
    }
    return "unknown anchor subject";
}

std::string FormatActiveRegionPhase(LayoutEditActiveRegionKind kind) {
    switch (kind) {
        case LayoutEditActiveRegionKind::DynamicEditAnchorHandle:
        case LayoutEditActiveRegionKind::DynamicEditAnchorTarget:
        case LayoutEditActiveRegionKind::DynamicColorTarget:
            return "dynamic";
        default:
            return "static";
    }
}

bool IsActiveRegionAnchorHandle(LayoutEditActiveRegionKind kind) {
    return kind == LayoutEditActiveRegionKind::StaticEditAnchorHandle ||
        kind == LayoutEditActiveRegionKind::DynamicEditAnchorHandle;
}

std::string FormatActiveRegionVisualType(LayoutEditActiveRegionKind kind) {
    switch (kind) {
        case LayoutEditActiveRegionKind::Card:
            return "card";
        case LayoutEditActiveRegionKind::CardHeader:
            return "card-header";
        case LayoutEditActiveRegionKind::WidgetHover:
            return "widget-hover";
        case LayoutEditActiveRegionKind::LayoutWeightGuide:
            return "layout-weight-guide";
        case LayoutEditActiveRegionKind::ContainerChildReorderTarget:
            return "container-child-reorder-target";
        case LayoutEditActiveRegionKind::GapHandle:
            return "gap-handle";
        case LayoutEditActiveRegionKind::WidgetGuide:
            return "widget-guide";
        case LayoutEditActiveRegionKind::StaticEditAnchorHandle:
        case LayoutEditActiveRegionKind::DynamicEditAnchorHandle:
            return "edit-anchor-handle";
        case LayoutEditActiveRegionKind::StaticEditAnchorTarget:
        case LayoutEditActiveRegionKind::DynamicEditAnchorTarget:
            return "edit-anchor-target";
        case LayoutEditActiveRegionKind::StaticColorTarget:
        case LayoutEditActiveRegionKind::DynamicColorTarget:
            return "color-target";
    }
    return "unknown";
}

std::string FormatActiveRegionPath(const AppConfig& config, const LayoutEditActiveRegion& activeRegion) {
    switch (activeRegion.kind) {
        case LayoutEditActiveRegionKind::Card: {
            if (const auto* card = LayoutEditActiveRegionPayloadAs<LayoutEditCardRegion>(activeRegion)) {
                return FormatText("%s.cards/%s/card[%s]",
                    ActiveLayoutSectionName(config).c_str(),
                    FormatNodePath(card->nodePath).c_str(),
                    card->id.c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::CardHeader: {
            if (const auto* card = LayoutEditActiveRegionPayloadAs<LayoutEditCardRegion>(activeRegion)) {
                return FormatText("%s.cards/%s/card[%s]/header",
                    ActiveLayoutSectionName(config).c_str(),
                    FormatNodePath(card->nodePath).c_str(),
                    card->id.c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::WidgetHover: {
            if (const auto* widget = LayoutEditActiveRegionPayloadAs<LayoutEditWidgetRegion>(activeRegion)) {
                return FormatText("%s/widget[%s]",
                    FormatLayoutConfigPath(config, widget->widget.editCardId, widget->widget.nodePath).c_str(),
                    EnumToString(widget->widgetClass));
            }
            break;
        }
        case LayoutEditActiveRegionKind::LayoutWeightGuide: {
            if (const auto* guide = LayoutEditActiveRegionPayloadAs<LayoutEditGuide>(activeRegion)) {
                return FormatText("%s/separator[%zu]",
                    FormatLayoutConfigPath(config, guide->editCardId, guide->nodePath).c_str(),
                    guide->separatorIndex);
            }
            break;
        }
        case LayoutEditActiveRegionKind::ContainerChildReorderTarget: {
            if (const auto* target =
                    LayoutEditActiveRegionPayloadAs<LayoutEditContainerChildReorderRegion>(activeRegion)) {
                return FormatText("%s/child-reorder-target",
                    FormatLayoutConfigPath(config, target->editCardId, target->nodePath).c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::GapHandle: {
            if (const auto* anchor = LayoutEditActiveRegionPayloadAs<LayoutEditGapAnchor>(activeRegion)) {
                return FormatText("%s/gap/%s",
                    FormatWidgetIdentityPath(config, anchor->key.widget).c_str(),
                    FormatLayoutConfigPath(config, anchor->key.widget.editCardId, anchor->key.nodePath).c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::WidgetGuide: {
            if (const auto* guide = LayoutEditActiveRegionPayloadAs<LayoutEditWidgetGuide>(activeRegion)) {
                return FormatText(
                    "%s/guide[%d]", FormatWidgetIdentityPath(config, guide->widget).c_str(), guide->guideId);
            }
            break;
        }
        case LayoutEditActiveRegionKind::StaticEditAnchorHandle:
        case LayoutEditActiveRegionKind::StaticEditAnchorTarget:
        case LayoutEditActiveRegionKind::DynamicEditAnchorHandle:
        case LayoutEditActiveRegionKind::DynamicEditAnchorTarget: {
            if (const auto* region = LayoutEditActiveRegionPayloadAs<LayoutEditAnchorRegion>(activeRegion)) {
                const std::string suffix = IsActiveRegionAnchorHandle(activeRegion.kind) ? "/handle" : "/target";
                return FormatText("%s/anchor[%d]%s",
                    FormatWidgetIdentityPath(config, region->key.widget).c_str(),
                    region->key.anchorId,
                    suffix.c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::StaticColorTarget:
        case LayoutEditActiveRegionKind::DynamicColorTarget: {
            if (const auto* region = LayoutEditActiveRegionPayloadAs<LayoutEditColorRegion>(activeRegion)) {
                return FormatLayoutEditParameterPath(region->parameter);
            }
            break;
        }
    }
    return {};
}

std::string FormatActiveRegionDetail(const AppConfig& config, const LayoutEditActiveRegion& activeRegion) {
    switch (activeRegion.kind) {
        case LayoutEditActiveRegionKind::Card: {
            if (const auto* card = LayoutEditActiveRegionPayloadAs<LayoutEditCardRegion>(activeRegion)) {
                return FormatText("card chrome %s", card->id.c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::CardHeader: {
            if (const auto* card = LayoutEditActiveRegionPayloadAs<LayoutEditCardRegion>(activeRegion)) {
                return FormatText("card header %s", card->id.c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::WidgetHover: {
            if (const auto* widget = LayoutEditActiveRegionPayloadAs<LayoutEditWidgetRegion>(activeRegion)) {
                return FormatText("hoverable widget %s in card %s",
                    EnumToString(widget->widgetClass),
                    widget->widget.renderCardId.c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::LayoutWeightGuide: {
            if (const auto* guide = LayoutEditActiveRegionPayloadAs<LayoutEditGuide>(activeRegion)) {
                return FormatText("%s layout weight separator", FormatGuideAxis(guide->axis).c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::ContainerChildReorderTarget: {
            if (const auto* target =
                    LayoutEditActiveRegionPayloadAs<LayoutEditContainerChildReorderRegion>(activeRegion)) {
                return FormatText("%s container child reorder target",
                    FormatGuideAxis(target->horizontal ? LayoutGuideAxis::Horizontal : LayoutGuideAxis::Vertical)
                        .c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::GapHandle: {
            if (const auto* anchor = LayoutEditActiveRegionPayloadAs<LayoutEditGapAnchor>(activeRegion)) {
                return FormatLayoutEditParameterDetail(anchor->key.parameter);
            }
            break;
        }
        case LayoutEditActiveRegionKind::WidgetGuide: {
            if (const auto* guide = LayoutEditActiveRegionPayloadAs<LayoutEditWidgetGuide>(activeRegion)) {
                return FormatText("%s %s",
                    FormatGuideAxis(guide->axis).c_str(),
                    FormatLayoutEditParameterDetail(guide->parameter).c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::StaticEditAnchorHandle:
        case LayoutEditActiveRegionKind::StaticEditAnchorTarget:
        case LayoutEditActiveRegionKind::DynamicEditAnchorHandle:
        case LayoutEditActiveRegionKind::DynamicEditAnchorTarget: {
            if (const auto* region = LayoutEditActiveRegionPayloadAs<LayoutEditAnchorRegion>(activeRegion)) {
                return FormatText("%s %s %s",
                    FormatActiveRegionPhase(activeRegion.kind).c_str(),
                    FormatAnchorShape(region->shape).c_str(),
                    FormatAnchorSubject(config, region->key).c_str());
            }
            break;
        }
        case LayoutEditActiveRegionKind::StaticColorTarget:
        case LayoutEditActiveRegionKind::DynamicColorTarget: {
            if (const auto* region = LayoutEditActiveRegionPayloadAs<LayoutEditColorRegion>(activeRegion)) {
                return FormatText("%s color %s",
                    FormatActiveRegionPhase(activeRegion.kind).c_str(),
                    FormatLayoutEditParameterDetail(region->parameter).c_str());
            }
            break;
        }
    }
    return {};
}

}  // namespace

void WriteLayoutEditActiveRegionTrace(Trace& trace,
    const AppConfig& config,
    const LayoutEditActiveRegions& regions,
    const DashboardOverlayState& overlayState) {
    for (const LayoutEditActiveRegion& region : regions) {
        const std::string visualType = FormatActiveRegionVisualType(region.kind);
        const std::string path = FormatActiveRegionPath(config, region);
        const std::string detail = FormatActiveRegionDetail(config, region);
        trace.WriteFmt(TracePrefix::Diagnostics,
            RES_STR("active_region box=(%d,%d,%d,%d) visual_type=\"%s\" path=\"%s\" detail=\"%s\""),
            region.box.left,
            region.box.top,
            region.box.right,
            region.box.bottom,
            visualType.c_str(),
            path.c_str(),
            detail.c_str());
    }

    trace.WriteFmt(TracePrefix::Diagnostics,
        RES_STR("active_regions count=%zu layout_edit=%s"),
        regions.Size(),
        Trace::BoolText(overlayState.showLayoutEditGuides));
}
