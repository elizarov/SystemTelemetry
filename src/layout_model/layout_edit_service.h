#pragma once

#include <vector>

#include "layout_model/layout_edit_layout_target.h"

struct AppConfig;

bool ApplyGuideWeights(AppConfig& config, const LayoutEditLayoutTarget& target, const std::vector<int>& weights);
bool ApplyGuideAdjacentWeights(
    AppConfig& config, const LayoutEditLayoutTarget& target, size_t separatorIndex, int firstWeight, int secondWeight);
