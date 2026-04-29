#include "widget/layout_edit_types.h"

#include "widget/widget.h"

LayoutEditAnchorDragSpec LayoutEditAnchorDragSpec::AxisDelta(AnchorDragAxis axis, double scale) {
    return LayoutEditAnchorDragSpec{axis, AnchorDragMode::AxisDelta, scale};
}

LayoutEditAnchorDragSpec LayoutEditAnchorDragSpec::RadialDistance(double scale) {
    return LayoutEditAnchorDragSpec{AnchorDragAxis::Both, AnchorDragMode::RadialDistance, scale};
}

LayoutEditAnchorDrag LayoutEditAnchorDrag::AxisDelta(AnchorDragAxis axis, RenderPoint origin, double scale) {
    return LayoutEditAnchorDrag{axis, AnchorDragMode::AxisDelta, origin, scale};
}

LayoutEditAnchorDrag LayoutEditAnchorDrag::RadialDistance(RenderPoint origin, double scale) {
    return LayoutEditAnchorDrag{AnchorDragAxis::Both, AnchorDragMode::RadialDistance, origin, scale};
}

LayoutNodeFieldEditKey MakeLayoutNodeFieldEditKey(
    const LayoutEditWidgetIdentity& widget, WidgetClass widgetClass, LayoutNodeField field) {
    return LayoutNodeFieldEditKey{widget.editCardId, widget.nodePath, widgetClass, field};
}

LayoutEditAnchorKey MakeLayoutNodeFieldEditAnchorKey(
    const WidgetLayout& widget, WidgetClass widgetClass, int anchorId, LayoutNodeField field) {
    const LayoutEditWidgetIdentity identity{widget.cardId, widget.editCardId, widget.nodePath};
    return LayoutEditAnchorKey{identity, MakeLayoutNodeFieldEditKey(identity, widgetClass, field), anchorId};
}
