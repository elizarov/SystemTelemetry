#include "layout_edit_dialog/impl/state.h"

LayoutEditDialogState* DialogStateFromWindow(HWND hwnd) {
    return reinterpret_cast<LayoutEditDialogState*>(GetWindowLongPtrA(hwnd, DWLP_USER));
}
