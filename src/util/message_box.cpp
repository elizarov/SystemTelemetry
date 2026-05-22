#include "util/message_box.h"

#include <windows.h>

#include <string>

#include "util/app_strings.h"

int ShowAppMessageBox(HWND owner, std::string_view text, UINT type) {
    const std::string message(text);
    return MessageBoxA(owner, message.c_str(), kAppTitle, type);
}

int ShowAppMessageBox(std::string_view text, UINT type) {
    return ShowAppMessageBox(nullptr, text, type);
}
