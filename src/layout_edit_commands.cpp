#include "layout_edit_commands.h"

#include "layout_edit_parameter.h"

namespace layout_edit {

bool ApplyValue(AppConfig& config, LayoutEditParameter parameter, double value) {
    return ApplyLayoutEditParameterValue(config, parameter, value);
}

}  // namespace layout_edit
