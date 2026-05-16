#include "util/win32_format.h"

#include "util/text_format.h"

void AppendHresult(std::string& text, long value) {
    AppendFormat(text, "0x%08lX", static_cast<unsigned long>(value));
}
