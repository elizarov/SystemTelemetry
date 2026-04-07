#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

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

namespace configschema {

template <typename Owner, size_t Index>
struct FieldTag {};

template <typename Owner, size_t Index>
concept HasReflectedField = requires {
    reflect_field(FieldTag<Owner, Index>{});
};

template <typename Owner, size_t Index = 0>
consteval size_t CountReflectedFields() {
    if constexpr (HasReflectedField<Owner, Index>) {
        return CountReflectedFields<Owner, Index + 1>();
    } else {
        return Index;
    }
}

template <typename Owner, size_t... Index>
consteval auto MakeReflectedFieldTuple(std::index_sequence<Index...>) {
    return std::tuple{reflect_field(FieldTag<Owner, Index>{})...};
}

template <FixedString Name, typename Owner>
struct AutoSectionDescriptor {
    using owner_type = Owner;

    static constexpr auto name = Name;
    static constexpr auto fields = MakeReflectedFieldTuple<Owner>(std::make_index_sequence<CountReflectedFields<Owner>()>{});
};

template <FixedString Prefix, typename Owner>
struct AutoDynamicSectionDescriptor {
    using owner_type = Owner;

    static constexpr auto prefix = Prefix;
    static constexpr auto fields = MakeReflectedFieldTuple<Owner>(std::make_index_sequence<CountReflectedFields<Owner>()>{});

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

#define CONFIG_REFLECTED_STRUCT(owner) \
private: \
    static constexpr std::size_t _configschema_field_base = __COUNTER__; \
    using Self = owner; \
public:

#define CONFIG_VALUE(type, member, key, codec) \
    type member{}; \
    friend consteval auto reflect_field(configschema::FieldTag<Self, __COUNTER__ - Self::_configschema_field_base - 1>) { \
        return configschema::FieldDescriptor<Self, type, key, &Self::member, codec>{}; \
    }

#define CONFIG_SECTION(name) \
    using Section = configschema::AutoSectionDescriptor<name, Self>

#define CONFIG_DYNAMIC_SECTION(prefix) \
    using Section = configschema::AutoDynamicSectionDescriptor<prefix, Self>
