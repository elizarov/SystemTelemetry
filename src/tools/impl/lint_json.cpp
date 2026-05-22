#include "tools/impl/lint_json.h"

#include <charconv>
#include <stdexcept>
#include <utility>

namespace tools::lint {

namespace {

class JsonParser {
public:
    explicit JsonParser(std::string_view text) :
        text_(text) {}

    JsonValue Parse() {
        SkipWhitespace();
        JsonValue value = ParseValue();
        SkipWhitespace();
        if (position_ != text_.size()) {
            Fail("unexpected trailing input");
        }
        return value;
    }

private:
    JsonValue ParseValue() {
        SkipWhitespace();
        if (position_ >= text_.size()) {
            Fail("unexpected end of input");
        }
        const char ch = text_[position_];
        if (ch == '{') {
            return ParseObject();
        }
        if (ch == '[') {
            return ParseArray();
        }
        if (ch == '"') {
            return JsonValue(ParseString());
        }
        if (ch == 't') {
            ConsumeLiteral("true");
            return JsonValue(true);
        }
        if (ch == 'f') {
            ConsumeLiteral("false");
            return JsonValue(false);
        }
        if (ch == 'n') {
            ConsumeLiteral("null");
            return JsonValue();
        }
        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            return JsonValue(ParseNumber());
        }
        Fail("unexpected token");
    }

    JsonValue ParseObject() {
        Expect('{');
        JsonValue::Object object;
        SkipWhitespace();
        if (TryConsume('}')) {
            return JsonValue(std::move(object));
        }
        while (true) {
            SkipWhitespace();
            if (position_ >= text_.size() || text_[position_] != '"') {
                Fail("object keys must be strings");
            }
            std::string key = ParseString();
            SkipWhitespace();
            Expect(':');
            object.emplace(std::move(key), ParseValue());
            SkipWhitespace();
            if (TryConsume('}')) {
                break;
            }
            Expect(',');
        }
        return JsonValue(std::move(object));
    }

    JsonValue ParseArray() {
        Expect('[');
        JsonValue::Array array;
        SkipWhitespace();
        if (TryConsume(']')) {
            return JsonValue(std::move(array));
        }
        while (true) {
            array.push_back(ParseValue());
            SkipWhitespace();
            if (TryConsume(']')) {
                break;
            }
            Expect(',');
        }
        return JsonValue(std::move(array));
    }

    std::string ParseString() {
        Expect('"');
        std::string value;
        while (position_ < text_.size()) {
            const char ch = text_[position_++];
            if (ch == '"') {
                return value;
            }
            if (ch != '\\') {
                value.push_back(ch);
                continue;
            }
            if (position_ >= text_.size()) {
                Fail("unfinished escape sequence");
            }
            const char escaped = text_[position_++];
            if (escaped == '"' || escaped == '\\' || escaped == '/') {
                value.push_back(escaped);
            } else if (escaped == 'b') {
                value.push_back('\b');
            } else if (escaped == 'f') {
                value.push_back('\f');
            } else if (escaped == 'n') {
                value.push_back('\n');
            } else if (escaped == 'r') {
                value.push_back('\r');
            } else if (escaped == 't') {
                value.push_back('\t');
            } else if (escaped == 'u') {
                AppendUtf8(ParseHexCodepoint(), value);
            } else {
                Fail("unknown escape sequence");
            }
        }
        Fail("unterminated string");
    }

    double ParseNumber() {
        const size_t start = position_;
        if (text_[position_] == '-') {
            ++position_;
        }
        ConsumeDigits();
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            ConsumeDigits();
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) {
                ++position_;
            }
            ConsumeDigits();
        }
        double value = 0.0;
        const std::string number(text_.substr(start, position_ - start));
        const char* begin = number.data();
        const char* end = begin + number.size();
        const std::from_chars_result result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end) {
            Fail("invalid number");
        }
        return value;
    }

    void ConsumeDigits() {
        const size_t start = position_;
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
            ++position_;
        }
        if (position_ == start) {
            Fail("expected digit");
        }
    }

    unsigned int ParseHexCodepoint() {
        if (position_ + 4 > text_.size()) {
            Fail("short unicode escape");
        }
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = text_[position_++];
            value *= 16;
            if (ch >= '0' && ch <= '9') {
                value += static_cast<unsigned int>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value += static_cast<unsigned int>(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                value += static_cast<unsigned int>(ch - 'A' + 10);
            } else {
                Fail("invalid unicode escape");
            }
        }
        return value;
    }

    void AppendUtf8(unsigned int codepoint, std::string& value) {
        if (codepoint <= 0x7f) {
            value.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            value.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            value.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            value.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            value.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            value.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    void ConsumeLiteral(std::string_view literal) {
        if (text_.substr(position_, literal.size()) != literal) {
            Fail("unexpected literal");
        }
        position_ += literal.size();
    }

    void Expect(char expected) {
        SkipWhitespace();
        if (position_ >= text_.size() || text_[position_] != expected) {
            Fail("expected token");
        }
        ++position_;
    }

    bool TryConsume(char expected) {
        SkipWhitespace();
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void SkipWhitespace() {
        while (position_ < text_.size()) {
            const char ch = text_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                break;
            }
            ++position_;
        }
    }

    [[noreturn]] void Fail(const char* message) const {
        throw std::runtime_error(
            std::string(message) + " at byte " + std::to_string(static_cast<unsigned long long>(position_)));
    }

    std::string_view text_;
    size_t position_ = 0;
};

}  // namespace

JsonValue::JsonValue() = default;

JsonValue::JsonValue(bool value) :
    type_(Type::Bool),
    boolValue_(value) {}

JsonValue::JsonValue(double value) :
    type_(Type::Number),
    numberValue_(value) {}

JsonValue::JsonValue(std::string value) :
    type_(Type::String),
    stringValue_(std::move(value)) {}

JsonValue::JsonValue(Array value) :
    type_(Type::Array),
    arrayValue_(std::move(value)) {}

JsonValue::JsonValue(Object value) :
    type_(Type::Object),
    objectValue_(std::move(value)) {}

JsonValue::Type JsonValue::type() const {
    return type_;
}

bool JsonValue::IsNull() const {
    return type_ == Type::Null;
}

bool JsonValue::IsBool() const {
    return type_ == Type::Bool;
}

bool JsonValue::IsNumber() const {
    return type_ == Type::Number;
}

bool JsonValue::IsString() const {
    return type_ == Type::String;
}

bool JsonValue::IsArray() const {
    return type_ == Type::Array;
}

bool JsonValue::IsObject() const {
    return type_ == Type::Object;
}

bool JsonValue::AsBool() const {
    if (!IsBool()) {
        throw std::runtime_error("JSON value is not a bool");
    }
    return boolValue_;
}

int JsonValue::AsInt() const {
    if (!IsNumber()) {
        throw std::runtime_error("JSON value is not a number");
    }
    return static_cast<int>(numberValue_);
}

const std::string& JsonValue::AsString() const {
    if (!IsString()) {
        throw std::runtime_error("JSON value is not a string");
    }
    return stringValue_;
}

const JsonValue::Array& JsonValue::AsArray() const {
    if (!IsArray()) {
        throw std::runtime_error("JSON value is not an array");
    }
    return arrayValue_;
}

const JsonValue::Object& JsonValue::AsObject() const {
    if (!IsObject()) {
        throw std::runtime_error("JSON value is not an object");
    }
    return objectValue_;
}

const JsonValue* JsonValue::Find(std::string_view key) const {
    if (!IsObject()) {
        return nullptr;
    }
    const auto found = objectValue_.find(std::string(key));
    return found == objectValue_.end() ? nullptr : &found->second;
}

const JsonValue& JsonValue::At(std::string_view key) const {
    const JsonValue* value = Find(key);
    if (value == nullptr) {
        throw std::runtime_error("missing JSON key " + std::string(key));
    }
    return *value;
}

JsonValue ParseJson(std::string_view text) {
    return JsonParser(text).Parse();
}

std::string JsonEscape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (char ch : text) {
        if (ch == '"') {
            escaped += "\\\"";
        } else if (ch == '\\') {
            escaped += "\\\\";
        } else if (ch == '\b') {
            escaped += "\\b";
        } else if (ch == '\f') {
            escaped += "\\f";
        } else if (ch == '\n') {
            escaped += "\\n";
        } else if (ch == '\r') {
            escaped += "\\r";
        } else if (ch == '\t') {
            escaped += "\\t";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

}  // namespace tools::lint
