#pragma once

#include <ostream>
#include <string>
#include <string_view>

class Trace {
public:
    explicit Trace(std::ostream* output = nullptr);

    void SetOutput(std::ostream* output);

    void Write(const char* text) const;
    void Write(const std::string& text) const;

    template <typename Builder> void WriteLazy(Builder&& builder) const {
        if (output_ == nullptr) {
            return;
        }
        Write(builder());
    }

    static std::string BoolText(bool value);
    static std::string FormatValueDouble(const char* label, double value, int precision = 3);
    static std::string EscapeText(std::string_view text);
    static std::string QuoteText(std::string_view text);

private:
    std::ostream* output_ = nullptr;
};
