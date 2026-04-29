#include "layout_guide_sheet/layout_guide_sheet_layout.h"

LayoutGuideSheetCalloutGeometry PlanLayoutGuideSheetCalloutGeometry(const LayoutGuideSheetCalloutGeometryInput& input) {
    const RenderPoint target = input.targetRect.Center();
    const RenderPoint center = input.cardRect.Center();
    const LayoutGuideSheetCalloutSide side =
        target.x < center.x ? LayoutGuideSheetCalloutSide::Left : LayoutGuideSheetCalloutSide::Right;
    return LayoutGuideSheetCalloutGeometry{side, target.y};
}

std::vector<LayoutGuideSheetCalloutGeometry> PlanLayoutGuideSheetCalloutGeometry(
    const std::vector<LayoutGuideSheetCalloutGeometryInput>& inputs) {
    std::vector<LayoutGuideSheetCalloutGeometry> planned;
    planned.reserve(inputs.size());
    for (const LayoutGuideSheetCalloutGeometryInput& input : inputs) {
        planned.push_back(PlanLayoutGuideSheetCalloutGeometry(input));
    }
    return planned;
}
