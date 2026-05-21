#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace tools::lint {

class JsonValue {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;

    JsonValue();
    explicit JsonValue(bool value);
    explicit JsonValue(double value);
    explicit JsonValue(std::string value);
    explicit JsonValue(Array value);
    explicit JsonValue(Object value);

    Type type() const;
    bool IsNull() const;
    bool IsBool() const;
    bool IsNumber() const;
    bool IsString() const;
    bool IsArray() const;
    bool IsObject() const;

    bool AsBool() const;
    int AsInt() const;
    const std::string& AsString() const;
    const Array& AsArray() const;
    const Object& AsObject() const;

    const JsonValue* Find(std::string_view key) const;
    const JsonValue& At(std::string_view key) const;

private:
    Type type_ = Type::Null;
    bool boolValue_ = false;
    double numberValue_ = 0.0;
    std::string stringValue_;
    Array arrayValue_;
    Object objectValue_;
};

JsonValue ParseJson(std::string_view text);
std::string JsonEscape(std::string_view text);

}  // namespace tools::lint
