#include "layout_edit/layout_edit_active_region_trace.h"

#include <string>

#include "layout_edit/layout_edit_tooltip.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "util/enum_string.h"

namespace {

std::string FormatTraceRect(const RenderRect& rect) {
    return "(" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," + std::to_string(rect.right) + "," +
           std::to_string(rect.bottom) + ")";
}

std::string FormatNodePath(const std::vector<size_t>& nodePath) {
    if (nodePath.empty()) {
        return "root";
    }
    std::string text;
    for (size_t index : nodePath) {
        if (!text.empty()) {
            text += "/";
        }
        text += "children[";
        text += std::to_string(index);
        text += "]";
    }
    return text;
}

std::string ActiveLayoutSectionName(const AppConfig& config) {
    return config.display.layout.empty() ? "layout" : "layout." + config.display.layout;
}

std::string FormatLayoutConfigPath(
    const AppConfig& config, const std::string& editCardId, const std::vector<size_t>& nodePath) {
    if (editCardId.empty()) {
        return ActiveLayoutSectionName(config) + ".cards/" + FormatNodePath(nodePath);
    }
    return "card." + editCardId + ".layout/" + FormatNodePath(nodePath);
}

std::string FormatLayoutEditParameterPath(LayoutEditParameter parameter) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
    if (!descriptor.has_value()) {
        return "parameter";
    }
    return descriptor->configKey;
}

std::string FormatLayoutEditParameterDetail(LayoutEditParameter parameter) {
    return GetLayoutEditParameterDisplayName(parameter) + " (" + FormatLayoutEditParameterPath(parameter) + ")";
}

std::string FormatWidgetIdentityPath(const AppConfig& config, const LayoutEditWidgetIdentity& widget) {
    switch (widget.kind) {
        case LayoutEditWidgetIdentity::Kind::DashboardChrome:
            return ActiveLayoutSectionName(config) + ".dashboard";
        case LayoutEditWidgetIdentity::Kind::CardChrome:
            return ActiveLayoutSectionName(config) + ".cards/card[" + widget.editCardId + "]";
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
        return "metric binding " + metric->metricId;
    }
    if (const auto title = LayoutEditAnchorCardTitleKey(key); title.has_value()) {
        return "card title " + title->cardId;
    }
    if (const auto order = LayoutEditAnchorMetricListOrderKey(key); order.has_value()) {
        return "metric list order " + FormatLayoutConfigPath(config, order->editCardId, order->nodePath);
    }
    if (const auto order = LayoutEditAnchorContainerChildOrderKey(key); order.has_value()) {
        return "container child order " + FormatLayoutConfigPath(config, order->editCardId, order->nodePath);
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
            const auto& card = std::get<LayoutEditCardRegion>(activeRegion.payload);
            return ActiveLayoutSectionName(config) + ".cards/" + FormatNodePath(card.nodePath) + "/card[" + card.id +
                   "]";
        }
        case LayoutEditActiveRegionKind::CardHeader: {
            const auto& card = std::get<LayoutEditCardRegion>(activeRegion.payload);
            return ActiveLayoutSectionName(config) + ".cards/" + FormatNodePath(card.nodePath) + "/card[" + card.id +
                   "]/header";
        }
        case LayoutEditActiveRegionKind::WidgetHover: {
            const auto& widget = std::get<LayoutEditWidgetRegion>(activeRegion.payload);
            return FormatLayoutConfigPath(config, widget.widget.editCardId, widget.widget.nodePath) + "/widget[" +
                   std::string(EnumToString(widget.widgetClass)) + "]";
        }
        case LayoutEditActiveRegionKind::LayoutWeightGuide: {
            const auto& guide = std::get<LayoutEditGuide>(activeRegion.payload);
            return FormatLayoutConfigPath(config, guide.editCardId, guide.nodePath) + "/separator[" +
                   std::to_string(guide.separatorIndex) + "]";
        }
        case LayoutEditActiveRegionKind::ContainerChildReorderTarget: {
            const auto& target = std::get<LayoutEditContainerChildReorderRegion>(activeRegion.payload);
            return FormatLayoutConfigPath(config, target.editCardId, target.nodePath) + "/child-reorder-target";
        }
        case LayoutEditActiveRegionKind::GapHandle: {
            const auto& anchor = std::get<LayoutEditGapAnchor>(activeRegion.payload);
            return FormatWidgetIdentityPath(config, anchor.key.widget) + "/gap/" +
                   FormatLayoutConfigPath(config, anchor.key.widget.editCardId, anchor.key.nodePath);
        }
        case LayoutEditActiveRegionKind::WidgetGuide: {
            const auto& guide = std::get<LayoutEditWidgetGuide>(activeRegion.payload);
            return FormatWidgetIdentityPath(config, guide.widget) + "/guide[" + std::to_string(guide.guideId) + "]";
        }
        case LayoutEditActiveRegionKind::StaticEditAnchorHandle:
        case LayoutEditActiveRegionKind::StaticEditAnchorTarget:
        case LayoutEditActiveRegionKind::DynamicEditAnchorHandle:
        case LayoutEditActiveRegionKind::DynamicEditAnchorTarget: {
            const auto& region = std::get<LayoutEditAnchorRegion>(activeRegion.payload);
            const std::string suffix = IsActiveRegionAnchorHandle(activeRegion.kind) ? "/handle" : "/target";
            return FormatWidgetIdentityPath(config, region.key.widget) + "/anchor[" +
                   std::to_string(region.key.anchorId) + "]" + suffix;
        }
        case LayoutEditActiveRegionKind::StaticColorTarget:
        case LayoutEditActiveRegionKind::DynamicColorTarget: {
            const auto& region = std::get<LayoutEditColorRegion>(activeRegion.payload);
            return FormatLayoutEditParameterPath(region.parameter);
        }
    }
    return {};
}

std::string FormatActiveRegionDetail(const AppConfig& config, const LayoutEditActiveRegion& activeRegion) {
    switch (activeRegion.kind) {
        case LayoutEditActiveRegionKind::Card: {
            const auto& card = std::get<LayoutEditCardRegion>(activeRegion.payload);
            return "card chrome " + card.id;
        }
        case LayoutEditActiveRegionKind::CardHeader: {
            const auto& card = std::get<LayoutEditCardRegion>(activeRegion.payload);
            return "card header " + card.id;
        }
        case LayoutEditActiveRegionKind::WidgetHover: {
            const auto& widget = std::get<LayoutEditWidgetRegion>(activeRegion.payload);
            return "hoverable widget " + std::string(EnumToString(widget.widgetClass)) + " in card " +
                   widget.widget.renderCardId;
        }
        case LayoutEditActiveRegionKind::LayoutWeightGuide: {
            const auto& guide = std::get<LayoutEditGuide>(activeRegion.payload);
            return FormatGuideAxis(guide.axis) + " layout weight separator";
        }
        case LayoutEditActiveRegionKind::ContainerChildReorderTarget: {
            const auto& target = std::get<LayoutEditContainerChildReorderRegion>(activeRegion.payload);
            return FormatGuideAxis(target.horizontal ? LayoutGuideAxis::Horizontal : LayoutGuideAxis::Vertical) +
                   " container child reorder target";
        }
        case LayoutEditActiveRegionKind::GapHandle: {
            const auto& anchor = std::get<LayoutEditGapAnchor>(activeRegion.payload);
            return FormatLayoutEditParameterDetail(anchor.key.parameter);
        }
        case LayoutEditActiveRegionKind::WidgetGuide: {
            const auto& guide = std::get<LayoutEditWidgetGuide>(activeRegion.payload);
            return FormatGuideAxis(guide.axis) + " " + FormatLayoutEditParameterDetail(guide.parameter);
        }
        case LayoutEditActiveRegionKind::StaticEditAnchorHandle:
        case LayoutEditActiveRegionKind::StaticEditAnchorTarget:
        case LayoutEditActiveRegionKind::DynamicEditAnchorHandle:
        case LayoutEditActiveRegionKind::DynamicEditAnchorTarget: {
            const auto& region = std::get<LayoutEditAnchorRegion>(activeRegion.payload);
            return FormatActiveRegionPhase(activeRegion.kind) + " " + FormatAnchorShape(region.shape) + " " +
                   FormatAnchorSubject(config, region.key);
        }
        case LayoutEditActiveRegionKind::StaticColorTarget:
        case LayoutEditActiveRegionKind::DynamicColorTarget: {
            const auto& region = std::get<LayoutEditColorRegion>(activeRegion.payload);
            return FormatActiveRegionPhase(activeRegion.kind) + " color " +
                   FormatLayoutEditParameterDetail(region.parameter);
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
        trace.Write("diagnostics:active_region box=" + FormatTraceRect(region.box) +
                    " visual_type=" + Trace::QuoteText(FormatActiveRegionVisualType(region.kind)) +
                    " path=" + Trace::QuoteText(FormatActiveRegionPath(config, region)) +
                    " detail=" + Trace::QuoteText(FormatActiveRegionDetail(config, region)));
    }

    trace.Write("diagnostics:active_regions count=" + std::to_string(regions.Size()) +
                " layout_edit=" + Trace::BoolText(overlayState.showLayoutEditGuides));
}
