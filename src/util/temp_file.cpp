#include "util/temp_file.h"

#include <windows.h>

#include <string>

FilePath CreateTempFilePath(std::string_view prefix) {
    char tempPathBuffer[MAX_PATH];
    const DWORD length = GetTempPathA(ARRAYSIZE(tempPathBuffer), tempPathBuffer);
    if (length == 0 || length >= ARRAYSIZE(tempPathBuffer)) {
        return {};
    }

    const std::string prefixText(prefix);
    char tempFileBuffer[MAX_PATH];
    if (GetTempFileNameA(tempPathBuffer, prefixText.c_str(), 0, tempFileBuffer) == 0) {
        return {};
    }
    return FilePath(tempFileBuffer);
}
