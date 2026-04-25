#include "layout_model/dashboard_overlay_state.h"

bool DashboardOverlayState::ShouldDrawLayoutEditAffordances() const {
    if (!showLayoutEditGuides) {
        return false;
    }
    if (forceLayoutEditAffordances) {
        return true;
    }
    return activeLayoutEditGuide.has_value() || hoveredLayoutEditGuide.has_value() || hoveredLayoutCard.has_value() ||
           hoveredEditableCard.has_value() || hoveredEditableWidget.has_value() || activeWidgetEditGuide.has_value() ||
           hoveredGapEditAnchor.has_value() || activeGapEditAnchor.has_value() || hoveredEditableAnchor.has_value() ||
           activeEditableAnchor.has_value() || activeMetricListReorderDrag.has_value() ||
           activeContainerChildReorderDrag.has_value() || selectedTreeHighlight.has_value();
}

bool DashboardOverlayState::IsContainerGuideDragActive() const {
    return activeLayoutEditGuide.has_value();
}

bool DashboardOverlayState::ShouldDrawSelectedTreeHighlight() const {
    return showLayoutEditGuides && selectedTreeHighlight.has_value();
}

bool DashboardOverlayState::ShouldRegisterDynamicEditArtifacts() const {
    return showLayoutEditGuides && !IsContainerGuideDragActive();
}

void DashboardOverlayState::SetPreviewWidget(const LayoutEditWidgetIdentity& widget) {
    hoveredEditableWidget = widget;
}
