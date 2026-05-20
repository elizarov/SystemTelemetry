#include "config/config_file_io.h"

#include <cstdio>

#include "util/text_encoding.h"

namespace {

constexpr char kReadBinaryMode[] = "rb";
constexpr char kWriteBinaryMode[] = "wb";

bool HasValidUtf8Encoding(const std::string& text) {
    return IsValidUtf8(text);
}

}  // namespace

std::string ReadConfigFile(const FilePath& path) {
    std::FILE* input = nullptr;
    if (fopen_s(&input, path.string().c_str(), kReadBinaryMode) != 0 || input == nullptr) {
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
    if (!HasValidUtf8Encoding(text)) {
        return {};
    }
    return text;
}

bool WriteConfigFile(const FilePath& path, const std::string& text) {
    if (!HasValidUtf8Encoding(text)) {
        return false;
    }

    std::FILE* output = nullptr;
    if (fopen_s(&output, path.string().c_str(), kWriteBinaryMode) != 0 || output == nullptr) {
        return false;
    }

    const bool written = fwrite(text.data(), 1, text.size(), output) == text.size();
    const bool closed = fclose(output) == 0;
    return written && closed;
}
