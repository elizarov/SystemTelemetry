#pragma once

#include <windows.h>

#include "util/command_line.h"

bool IsFpsServiceCommandLine(const CommandLineArguments& commandLine);
int RunFpsServiceMode();
DWORD InstallOrUpdateFpsService();
DWORD StopAndDeleteFpsService();
bool IsFpsServiceRunningForCurrentExecutable();
