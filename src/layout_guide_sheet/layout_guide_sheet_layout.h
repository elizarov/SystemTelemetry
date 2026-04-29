#pragma once

#include <vector>

#include "renderer/render_types.h"

enum class LayoutGuideSheetCalloutSide {
    Left,
    Right,
};

struct LayoutGuideSheetCalloutGeometryInput {
    RenderRect cardRect{};
    RenderRect targetRect{};
};

struct LayoutGuideSheetCalloutGeometry {
    LayoutGuideSheetCalloutSide side = LayoutGuideSheetCalloutSide::Right;
    int order = 0;
};

LayoutGuideSheetCalloutGeometry PlanLayoutGuideSheetCalloutGeometry(const LayoutGuideSheetCalloutGeometryInput& input);

std::vector<LayoutGuideSheetCalloutGeometry> PlanLayoutGuideSheetCalloutGeometry(
    const std::vector<LayoutGuideSheetCalloutGeometryInput>& inputs);
