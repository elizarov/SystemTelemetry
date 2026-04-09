#pragma once

#include <optional>
#include <vector>

#include "config.h"
#include "dashboard_renderer.h"
#include "layout_edit_commands.h"
#include "layout_edit_controller.h"

namespace layout_edit {

const LayoutNodeConfig* FindGuideNode(const AppConfig& config, const LayoutEditHost::LayoutTarget& target);

std::vector<int> SeedGuideWeights(const DashboardRenderer::LayoutEditGuide& guide, const LayoutNodeConfig* node);

bool ApplyGuideWeights(AppConfig& config, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights);

std::optional<int> EvaluateWidgetExtentForGuideWeights(
    DashboardRenderer& renderer,
    const AppConfig& baseConfig,
    const LayoutEditHost::LayoutTarget& target,
    const std::vector<int>& weights,
    const DashboardRenderer::LayoutWidgetIdentity& widget,
    DashboardRenderer::LayoutGuideAxis axis);

}  // namespace layout_edit
