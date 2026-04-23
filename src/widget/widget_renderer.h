#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "widget/layout_edit_types.h"
#include "widget/render_types.h"

struct WidgetLayout;
struct MetricDefinitionConfig;

class WidgetRenderer {
public:
    using LayoutEditParameter = ::LayoutEditParameter;

    enum class RenderMode {
        Normal,
        Blank,
    };

    struct TextStyleMetrics {
        int title = 0;
        int big = 0;
        int value = 0;
        int label = 0;
        int text = 0;
        int smallText = 0;
        int footer = 0;
        int clockTime = 0;
        int clockDate = 0;
    };

    struct TextLayoutResult {
        RenderRect textRect{};
    };

    virtual ~WidgetRenderer() = default;

    virtual const AppConfig& Config() const = 0;
    virtual const TextStyleMetrics& TextMetrics() const = 0;
    virtual RenderMode CurrentRenderMode() const = 0;
    virtual int ScaleLogical(int value) const = 0;
    virtual int MeasureTextWidth(TextStyleId style, std::string_view text) const = 0;
    virtual TextLayoutResult MeasureTextBlock(
        const RenderRect& rect, const std::string& text, TextStyleId style, const TextLayoutOptions& options) const = 0;
    virtual void DrawText(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColorId color,
        const TextLayoutOptions& options) const = 0;
    virtual TextLayoutResult DrawTextBlock(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        RenderColorId color,
        const TextLayoutOptions& options) = 0;
    virtual void PushClipRect(const RenderRect& rect) = 0;
    virtual void PopClipRect() = 0;
    virtual bool FillSolidRect(const RenderRect& rect, RenderColorId color) = 0;
    virtual bool FillSolidRoundedRect(const RenderRect& rect, int radius, RenderColorId color) = 0;
    virtual bool FillSolidEllipse(const RenderRect& rect, RenderColorId color) = 0;
    virtual bool FillSolidDiamond(const RenderRect& rect, RenderColorId color) = 0;
    virtual bool DrawSolidRect(const RenderRect& rect, const RenderStroke& stroke) = 0;
    virtual bool DrawSolidEllipse(const RenderRect& rect, const RenderStroke& stroke) = 0;
    virtual bool DrawSolidLine(RenderPoint start, RenderPoint end, const RenderStroke& stroke) = 0;
    virtual bool DrawArc(const RenderArc& arc, const RenderStroke& stroke) = 0;
    virtual bool DrawArcs(std::span<const RenderArc> arcs, const RenderStroke& stroke) = 0;
    virtual bool DrawPolyline(std::span<const RenderPoint> points, const RenderStroke& stroke) = 0;
    virtual bool FillPath(const RenderPath& path, RenderColorId color) = 0;
    virtual bool FillPaths(std::span<const RenderPath> paths, RenderColorId color) = 0;
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
        std::optional<LayoutEditParameter> colorParameter = std::nullopt) = 0;
    virtual void RegisterDynamicTextAnchor(const TextLayoutResult& layoutResult,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt) = 0;
    virtual void RegisterDynamicTextAnchor(const RenderRect& rect,
        const std::string& text,
        TextStyleId style,
        const TextLayoutOptions& options,
        const LayoutEditAnchorBinding& editable,
        std::optional<LayoutEditParameter> colorParameter = std::nullopt) = 0;
    virtual void RegisterStaticColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) = 0;
    virtual void RegisterDynamicColorEditRegion(LayoutEditParameter parameter, const RenderRect& targetRect) = 0;
    virtual std::vector<LayoutEditWidgetGuide>& WidgetEditGuidesMutable() = 0;
    virtual const MetricDefinitionConfig* FindConfiguredMetricDefinition(std::string_view metricRef) const = 0;
    virtual const std::string& ResolveConfiguredMetricSampleValueText(std::string_view metricRef) const = 0;
};
