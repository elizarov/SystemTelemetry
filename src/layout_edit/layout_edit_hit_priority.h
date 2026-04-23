#pragma once

#include "widget/layout_edit_parameter_id.h"
#include "widget/layout_edit_types.h"

int GetLayoutEditParameterHitPriority(LayoutEditParameter parameter);
int LayoutEditAnchorHitPriority(const LayoutEditAnchorKey& key);
