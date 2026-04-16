#pragma once

#include <optional>

#include "state.h"

std::optional<INT_PTR> HandleLayoutEditDialogProcMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
