#include "util/win32_format.h"

#include <windows.h>

#include "util/text_format.h"

void AppendHresult(std::string& text, long value) {
    AppendFormat(text, "0x%08lX", static_cast<unsigned long>(value));
}

std::string FormatWin32Error(unsigned long value) {
    char message[256]{};
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(value),
        0,
        message,
        static_cast<DWORD>(sizeof(message)),
        nullptr);
    std::string text = FormatText("%lu", value);
    if (length > 0) {
        size_t trimmedLength = length;
        while (trimmedLength > 0 && (message[trimmedLength - 1] == '\r' || message[trimmedLength - 1] == '\n')) {
            message[trimmedLength - 1] = '\0';
            --trimmedLength;
        }
        AppendFormat(text, " (%s)", message);
    }
    return text;
}
