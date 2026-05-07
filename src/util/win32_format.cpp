#include "util/win32_format.h"

#include <cstdio>

std::string FormatHresult(long value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(value));
    return buffer;
}
