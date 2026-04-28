#include "layout_edit_dialog/impl/editor_handler_registry.h"

bool HasDescriptorLayoutEditEditorHandler(LayoutEditEditorKind kind) {
    return kind == LayoutEditEditorKind::MetricListOrder || kind == LayoutEditEditorKind::DateTimeFormat;
}
