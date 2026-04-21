#pragma once

#include <optional>

#include "layout_edit_dialog/impl/state.h"

std::optional<INT_PTR> HandleLayoutEditDialogProcMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
