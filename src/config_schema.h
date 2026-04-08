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
struct StructuredSectionCodec {};
struct BoardSectionCodec {};

template <typename T>
struct DefaultCodec;

template <typename T>
struct DefaultSectionCodec {
    using codec_type = StructuredSectionCodec;
};

template <>
struct DefaultCodec<int> {
    using codec_type = IntCodec;
};

template <>
struct DefaultCodec<double> {
    using codec_type = DoubleCodec;
};

template <>
struct DefaultCodec<std::string> {
    using codec_type = StringCodec;
};

template <>
struct DefaultCodec<unsigned int> {
    using codec_type = HexColorCodec;
};

template <typename Owner, typename Field, FixedString Key, Field Owner::*Member, typename Codec>
struct FieldDescriptor {
    using owner_type = Owner;
    using field_type = Field;
    using codec_type = Codec;

    static constexpr auto key = Key;
    static constexpr auto member = Member;
};

template <typename Owner, typename Section, typename Section::owner_type Owner::*Member>
struct StructuredBindingDescriptor {
    using owner_type = Owner;
    using section_type = Section;

    static constexpr auto member = Member;

    static typename Section::owner_type& Get(Owner& owner) {
        return owner.*member;
    }

    static const typename Section::owner_type& Get(const Owner& owner) {
        return owner.*member;
    }
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
struct BindingTag {};

template <typename Owner, size_t Index>
concept HasReflectedField = requires {
    reflect_field(FieldTag<Owner, Index>{});
};

template <typename Owner, size_t Index>
concept HasReflectedBinding = requires {
    reflect_binding(BindingTag<Owner, Index>{});
};

template <typename Owner, size_t Index = 0>
consteval size_t CountReflectedFields() {
    if constexpr (HasReflectedField<Owner, Index>) {
        return CountReflectedFields<Owner, Index + 1>();
    } else {
        return Index;
    }
}

template <typename Owner, size_t Index = 0>
consteval size_t CountReflectedBindings() {
    if constexpr (HasReflectedBinding<Owner, Index>) {
        return CountReflectedBindings<Owner, Index + 1>();
    } else {
        return Index;
    }
}

template <typename Owner, size_t... Index>
consteval auto MakeReflectedFieldTuple(std::index_sequence<Index...>) {
    return std::tuple{reflect_field(FieldTag<Owner, Index>{})...};
}

template <typename Owner, size_t... Index>
consteval auto MakeReflectedBindingTuple(std::index_sequence<Index...>) {
    return std::tuple{reflect_binding(BindingTag<Owner, Index>{})...};
}

template <FixedString Name, typename Owner>
struct AutoSectionDescriptor {
    using owner_type = Owner;
    using codec_type = typename DefaultSectionCodec<Owner>::codec_type;

    static constexpr auto name = Name;
    static constexpr auto fields = MakeReflectedFieldTuple<Owner>(std::make_index_sequence<CountReflectedFields<Owner>()>{});
};

template <FixedString Prefix, typename Owner>
struct AutoDynamicSectionDescriptor {
    using owner_type = Owner;
    using codec_type = typename DefaultSectionCodec<Owner>::codec_type;

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

template <typename Owner>
struct AutoStructuredBindingListDescriptor {
    using owner_type = Owner;

    static constexpr auto bindings =
        MakeReflectedBindingTuple<Owner>(std::make_index_sequence<CountReflectedBindings<Owner>()>{});
};

}  // namespace configschema

#define CONFIG_REFLECTED_STRUCT(owner) \
private: \
    static constexpr std::size_t _configschema_field_base = __COUNTER__; \
    using Self = owner; \
public:

#define CONFIG_CODEC(value_type, codec) \
    template <> \
    struct configschema::DefaultCodec<value_type> { \
        using codec_type = codec; \
    }

#define CONFIG_SECTION_CODEC(value_type, codec) \
    template <> \
    struct configschema::DefaultSectionCodec<value_type> { \
        using codec_type = codec; \
    }

#define CONFIG_VALUE(field_type, member, key) \
    field_type member{}; \
    friend consteval auto reflect_field(configschema::FieldTag<Self, __COUNTER__ - Self::_configschema_field_base - 1>) { \
        return configschema::FieldDescriptor<Self, field_type, key, &Self::member, typename configschema::DefaultCodec<field_type>::codec_type>{}; \
    }

#define CONFIG_SECTION(name) \
    using Section = configschema::AutoSectionDescriptor<name, Self>

#define CONFIG_DYNAMIC_SECTION(prefix) \
    using Section = configschema::AutoDynamicSectionDescriptor<prefix, Self>

#define CONFIG_REFLECTED_BINDINGS(owner) \
private: \
    static constexpr std::size_t _configschema_binding_base = __COUNTER__; \
    using Self = owner; \
public:

#define CONFIG_SECTION_VALUE(field_type, member) \
    field_type member{}; \
    friend consteval auto reflect_binding(configschema::BindingTag<Self, __COUNTER__ - Self::_configschema_binding_base - 1>) { \
        return configschema::StructuredBindingDescriptor<Self, typename field_type::Section, &Self::member>{}; \
    }

#define CONFIG_BINDING_LIST() \
    using BindingList = configschema::AutoStructuredBindingListDescriptor<Self>
