#include "layout_model/layout_edit_layout_target.h"

LayoutEditLayoutTarget LayoutEditLayoutTarget::ForGuide(const LayoutEditGuide& guide) {
    LayoutEditLayoutTarget target;
    target.editCardId = guide.editCardId;
    target.nodePath = guide.nodePath;
    return target;
}
