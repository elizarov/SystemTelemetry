#pragma once

#include <windows.h>

bool IsFpsServiceCommandLine();
int RunFpsServiceMode();
DWORD InstallOrUpdateFpsService();
DWORD StopAndDeleteFpsService();
bool IsFpsServiceInstalledForCurrentExecutable();
