#pragma once

#include <windows.h>

#include <optional>

std::optional<INT_PTR> HandleLayoutEditDialogProcMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
