#include "util/utf8.h"

#include <windows.h>

#include <string>
#include <string_view>

namespace {

template <typename CharT> int CheckedSize(size_t size) {
    return size > static_cast<size_t>(INT_MAX) ? -1 : static_cast<int>(size);
}

bool CanDecodeCodePage(std::string_view text, unsigned int codePage, DWORD flags) {
    if (text.empty()) {
        return true;
    }

    const int length = CheckedSize<char>(text.size());
    if (length < 0) {
        return false;
    }

    return MultiByteToWideChar(codePage, flags, text.data(), length, nullptr, 0) > 0;
}

std::wstring WideFromCodePage(std::string_view text, unsigned int codePage, DWORD flags) {
    if (text.empty()) {
        return {};
    }

    const int length = CheckedSize<char>(text.size());
    if (length < 0) {
        return {};
    }

    const int required = MultiByteToWideChar(codePage, flags, text.data(), length, nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(codePage, flags, text.data(), length, result.data(), required);
    return result;
}

}  // namespace

bool IsValidUtf8(std::string_view text) {
    return CanDecodeCodePage(text, CP_UTF8, MB_ERR_INVALID_CHARS);
}

std::wstring WideFromUtf8(std::string_view text) {
    if (!IsValidUtf8(text)) {
        return {};
    }
    return WideFromCodePage(text, CP_UTF8, MB_ERR_INVALID_CHARS);
}

std::string Utf8FromWide(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int length = CheckedSize<wchar_t>(text.size());
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

std::string Utf8FromCodePage(std::string_view text, unsigned int codePage) {
    if (text.empty()) {
        return {};
    }

    const int length = CheckedSize<char>(text.size());
    if (length < 0) {
        return {};
    }

    const int wideLength = MultiByteToWideChar(codePage, 0, text.data(), length, nullptr, 0);
    if (wideLength <= 0) {
        return {};
    }

    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(codePage, 0, text.data(), length, wide.data(), wideLength);
    return Utf8FromWide(wide);
}
