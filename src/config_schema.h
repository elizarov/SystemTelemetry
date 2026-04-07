#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>

namespace configschema {

template <size_t N>
struct FixedString {
    char value[N];

    constexpr FixedString(const char (&text)[N]) : value{} {
        for (size_t i = 0; i < N; ++i) {
            value[i] = text[i];
        }
    }

    constexpr std::string_view view() const {
        return std::string_view(value, N - 1);
    }
};

struct IntCodec {};
struct DoubleCodec {};
struct StringCodec {};
struct LogicalPointCodec {};
struct LogicalSizeCodec {};
struct HexColorCodec {};
struct FontSpecCodec {};
struct LayoutExpressionCodec {};

template <typename Owner, typename Field, FixedString Key, Field Owner::*Member, typename Codec>
struct FieldDescriptor {
    using owner_type = Owner;
    using field_type = Field;
    using codec_type = Codec;

    static constexpr auto key = Key;
    static constexpr auto member = Member;
};

template <FixedString Name, typename Owner, typename... Fields>
struct SectionDescriptor {
    using owner_type = Owner;

    static constexpr auto name = Name;
    static constexpr auto fields = std::tuple<Fields...>{};
};

template <FixedString Prefix, typename Owner, typename... Fields>
struct DynamicSectionDescriptor {
    using owner_type = Owner;

    static constexpr auto prefix = Prefix;
    static constexpr auto fields = std::tuple<Fields...>{};

    static constexpr bool Matches(std::string_view sectionName) {
        return sectionName.rfind(prefix.view(), 0) == 0;
    }

    static constexpr std::string_view Suffix(std::string_view sectionName) {
        return Matches(sectionName) ? sectionName.substr(prefix.view().size()) : std::string_view{};
    }

    static std::string FormatName(std::string_view suffix) {
        return "[" + std::string(prefix.view()) + std::string(suffix) + "]";
    }
};

}  // namespace configschema
