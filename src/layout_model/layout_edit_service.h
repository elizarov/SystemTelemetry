#pragma once

#include <vector>

#include "config/config.h"
#include "layout_model/layout_edit_layout_target.h"

bool ApplyGuideWeights(AppConfig& config, const LayoutEditLayoutTarget& target, const std::vector<int>& weights);
