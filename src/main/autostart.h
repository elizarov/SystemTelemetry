#pragma once

#include <windows.h>

#include <optional>
#include <string>

std::optional<std::wstring> ReadAutoStartCommand();
bool IsAutoStartEnabledForCurrentExecutable();
LSTATUS WriteAutoStartRegistryValue(bool enabled);
bool UpdateAutoStartElevated(bool enabled, HWND owner);
bool UpdateAutoStartRegistration(bool enabled, HWND owner);
int RunElevatedAutoStartMode(bool enabled);
