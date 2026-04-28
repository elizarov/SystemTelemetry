#include "widget/layout_edit_types.h"

#include "widget/widget.h"

LayoutNodeFieldEditKey MakeLayoutNodeFieldEditKey(
    const LayoutEditWidgetIdentity& widget, WidgetClass widgetClass, LayoutNodeField field) {
    return LayoutNodeFieldEditKey{widget.editCardId, widget.nodePath, widgetClass, field};
}

LayoutEditAnchorKey MakeLayoutNodeFieldEditAnchorKey(
    const WidgetLayout& widget, WidgetClass widgetClass, int anchorId, LayoutNodeField field) {
    const LayoutEditWidgetIdentity identity{widget.cardId, widget.editCardId, widget.nodePath};
    return LayoutEditAnchorKey{identity, MakeLayoutNodeFieldEditKey(identity, widgetClass, field), anchorId};
}
