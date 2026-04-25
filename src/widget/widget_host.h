#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "renderer/renderer.h"
#include "widget/layout_edit_types.h"

struct WidgetLayout;
struct MetricDefinitionConfig;

enum class RenderMode {
    Normal,
    Blank,
};

class WidgetHost {
public:
    using LayoutEditParameter = ::LayoutEditParameter;
    using TextLayoutResult = ::TextLayoutResult;
    using TextStyleMetrics = ::TextStyleMetrics;
    using RenderMode = ::RenderMode;

    virtual ~WidgetHost() = default;

    virtual ::Renderer& Renderer() = 0;
    virtual const ::Renderer& Renderer() const = 0;
    virtual const AppConfig& Config() const = 0;
    virtual RenderMode CurrentRenderMode() const = 0;
    virtual LayoutEditAnchorBinding MakeEditableTextBinding(
        const WidgetLayout& widget, LayoutEditParameter parameter, int anchorId, int value) const = 0;
    virtual LayoutEditAnchorBinding MakeMetricTextBinding(
        const WidgetLayout& widget, std::string_view metricId, int anchorId) const = 0;
    virtual void RegisterStaticEditableAnchorRegion(const LayoutEditAnchorKey& key,
        const RenderRect& targetRect,
        const RenderRect& anchorRect,
        AnchorShape shape,
        AnchorDragAxis dragAxis,
        AnchorDragMode dragMode,
        RenderPoint dragOrigin,
        double dragScale,
        bool draggable,
        bool showWhenWidgetHovered,
        bool drawTargetOutline,
        int value) = 0;
    virtual void RegisterDynamicEditableAnchorRegion(const LayoutEditAnchorKey& key,
        const RenderRect& targetRect,
        const RenderRect& anchorRect,
        AnchorShape shape,
        AnchorDragAxis dragAxis,
        AnchorDragMode dragMode,
        RenderPoint dragOrigin,
        double dragScale,
        bool draggable,
        bool showWhenWidgetHovered,
        bool drawTargetOutline,
        int value) = 0;
    virtual void RegisterStaticTextAnchor(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt,
        bool drawTargetOutline = true) = 0;
    virtual void RegisterDynamicTextAnchor(const TextLayoutResult& layoutResult,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt,
        bool drawTargetOutline = true) = 0;
    virtual void RegisterDynamicTextAnchor(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt,
        bool drawTargetOutline = true) = 0;
    virtual void RegisterStaticColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) = 0;
    virtual void RegisterDynamicColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) = 0;
    virtual std::vector<LayoutEditWidgetGuide>& WidgetEditGuidesMutable() = 0;
    virtual const MetricDefinitionConfig* FindConfiguredMetricDefinition(std::string_view metricRef) const = 0;
    virtual const std::string& ResolveConfiguredMetricSampleValueText(std::string_view metricRef) const = 0;
    virtual std::optional<MetricListReorderOverlayState> ActiveMetricListReorderDrag(
        const LayoutEditWidgetIdentity& widget) const = 0;
};
