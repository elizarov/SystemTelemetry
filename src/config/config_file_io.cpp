#include "config/config_file_io.h"

#include <cstdio>

#include "util/utf8.h"

std::string ReadConfigFileUtf8(const FilePath& path) {
    std::FILE* input = nullptr;
    if (_wfopen_s(&input, path.c_str(), L"rb") != 0 || input == nullptr) {
        return {};
    }

    fseek(input, 0, SEEK_END);
    const long size = ftell(input);
    if (size < 0) {
        fclose(input);
        return {};
    }
    fseek(input, 0, SEEK_SET);
    std::string text(static_cast<size_t>(size), '\0');
    if (!text.empty()) {
        const size_t read = fread(text.data(), 1, text.size(), input);
        if (read != text.size()) {
            fclose(input);
            return {};
        }
    }
    fclose(input);
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    if (!IsValidUtf8(text)) {
        return {};
    }
    return text;
}

bool WriteConfigFileUtf8(const FilePath& path, const std::string& text) {
    if (!IsValidUtf8(text)) {
        return false;
    }

    std::FILE* output = nullptr;
    if (_wfopen_s(&output, path.c_str(), L"wb") != 0 || output == nullptr) {
        return false;
    }

    const bool written = fwrite(text.data(), 1, text.size(), output) == text.size();
    const bool closed = fclose(output) == 0;
    return written && closed;
}
