#include "util/win32_format.h"

#include <cstdio>

void AppendHresult(std::string& text, long value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(value));
    text += buffer;
}
