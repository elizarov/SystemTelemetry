#pragma once

#include "config.h"
#include "layout_edit_controller.h"

namespace layout_edit {

bool ApplyValue(AppConfig& config, const LayoutEditHost::ValueTarget& target, double value);

}  // namespace layout_edit
