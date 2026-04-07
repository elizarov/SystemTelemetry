#pragma once

#include <cstddef>
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

}  // namespace configschema
