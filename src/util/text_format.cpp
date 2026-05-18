#include "util/text_format.h"

#include <cstdio>

#include "util/resource_strings.h"

namespace {

void AppendFormatArgs(std::string& text, const char* format, va_list args) {
    char buffer[256];
    va_list measureArgs;
    va_copy(measureArgs, args);
    const int length = vsnprintf(buffer, sizeof(buffer), format, measureArgs);
    va_end(measureArgs);
    if (length < 0) {
        return;
    }
    if (static_cast<std::size_t>(length) < sizeof(buffer)) {
        text.append(buffer, static_cast<std::size_t>(length));
        return;
    }

    std::string formatted(static_cast<std::size_t>(length) + 1, '\0');
    vsnprintf(formatted.data(), formatted.size(), format, args);
    formatted.resize(static_cast<std::size_t>(length));
    text += formatted;
}

}  // namespace

std::string FormatTextV(const char* format, va_list args) {
    std::string text;
    AppendFormatArgs(text, format, args);
    return text;
}

std::string FormatTextV(ResourceStringId format, va_list args) {
    return FormatTextV(ResourceStringText(format), args);
}

std::string FormatText(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string text = FormatTextV(format, args);
    va_end(args);
    return text;
}

std::string FormatText(ResourceStringId format, ...) {
    va_list args;
    va_start(args, format);
    std::string text = FormatTextV(format, args);
    va_end(args);
    return text;
}

void AssignFormat(std::string& text, const char* format, ...) {
    text.clear();
    va_list args;
    va_start(args, format);
    AppendFormatArgs(text, format, args);
    va_end(args);
}

void AssignFormat(std::string& text, ResourceStringId format, ...) {
    text.clear();
    va_list args;
    va_start(args, format);
    AppendFormatArgs(text, ResourceStringText(format), args);
    va_end(args);
}

void AppendFormat(std::string& text, const char* format, ...) {
    va_list args;
    va_start(args, format);
    AppendFormatArgs(text, format, args);
    va_end(args);
}

void AppendFormat(std::string& text, ResourceStringId format, ...) {
    va_list args;
    va_start(args, format);
    AppendFormatArgs(text, ResourceStringText(format), args);
    va_end(args);
}
