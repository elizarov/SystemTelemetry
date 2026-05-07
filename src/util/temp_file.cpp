#include "util/temp_file.h"

#include <windows.h>

#include <string>

#include "util/utf8.h"

FilePath CreateTempFilePath(std::string_view prefix) {
    wchar_t tempPathBuffer[MAX_PATH];
    const DWORD length = GetTempPathW(ARRAYSIZE(tempPathBuffer), tempPathBuffer);
    if (length == 0 || length >= ARRAYSIZE(tempPathBuffer)) {
        return {};
    }

    const std::wstring widePrefix = WideFromUtf8(prefix);
    wchar_t tempFileBuffer[MAX_PATH];
    if (GetTempFileNameW(tempPathBuffer, widePrefix.c_str(), 0, tempFileBuffer) == 0) {
        return {};
    }
    return FilePath(tempFileBuffer);
}
