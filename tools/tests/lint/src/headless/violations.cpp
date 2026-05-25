#include "headless/violations.h"

#include "dashboard/violations.h"
#include "layout_edit_dialog/dialog.h"
#include "layout_guide_sheet/public.h"

int HeadlessValue() {
    return DashboardValue() + LayoutEditDialogValue() + LayoutGuideSheetValue();
}
