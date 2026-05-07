#include "util/message_box.h"

#include <windows.h>

#include <string>

#include "util/app_strings.h"
#include "util/utf8.h"

int MessageBoxUtf8(HWND owner, std::string_view text, UINT type) {
    const std::wstring wideText = WideFromUtf8(text);
    const std::wstring caption = WideFromUtf8(kAppTitleUtf8);
    return MessageBoxW(owner, wideText.c_str(), caption.c_str(), type);
}

int MessageBoxUtf8(std::string_view text, UINT type) {
    return MessageBoxUtf8(nullptr, text, type);
}
