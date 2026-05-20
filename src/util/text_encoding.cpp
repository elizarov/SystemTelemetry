#include "util/text_encoding.h"

#include <windows.h>

namespace {

int CheckedSize(size_t size) {
    return size > static_cast<size_t>(INT_MAX) ? -1 : static_cast<int>(size);
}

}  // namespace

std::wstring WideFromText(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int length = CheckedSize(text.size());
    if (length < 0) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(required), wchar_t{});
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, result.data(), required);
    return result;
}

std::string TextFromWide(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int length = CheckedSize(text.size());
    if (length < 0) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), required, nullptr, nullptr);
    return result;
}

bool IsValidUtf8(std::string_view text) {
    return text.empty() || !WideFromText(text).empty();
}
