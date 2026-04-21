#include "layout_edit_dialog/impl/state.h"

LayoutEditDialogState* DialogStateFromWindow(HWND hwnd) {
    return reinterpret_cast<LayoutEditDialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
}
