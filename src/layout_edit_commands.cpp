#include "layout_edit_commands.h"

#include "layout_edit_parameter.h"

bool ApplyValue(AppConfig& config, LayoutEditParameter parameter, double value) {
    return ApplyLayoutEditParameterValue(config, parameter, value);
}
