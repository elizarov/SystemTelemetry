#include "util/temp_file.h"

#include <windows.h>

FilePath CreateTempFilePath(const wchar_t* prefix) {
    wchar_t tempPathBuffer[MAX_PATH];
    const DWORD length = GetTempPathW(ARRAYSIZE(tempPathBuffer), tempPathBuffer);
    if (length == 0 || length >= ARRAYSIZE(tempPathBuffer)) {
        return {};
    }

    wchar_t tempFileBuffer[MAX_PATH];
    if (GetTempFileNameW(tempPathBuffer, prefix, 0, tempFileBuffer) == 0) {
        return {};
    }
    return FilePath(tempFileBuffer);
}
