#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <vector>
#include <utility>

namespace configschema {

template <size_t N> struct FixedString {
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

enum class ValueFormat {
    Integer,
    FloatingPoint,
    FontSpec,
};

struct NoLayoutEditPolicy {};

struct PositiveIntPolicy {};

struct NonNegativeIntPolicy {};

struct FontSizePolicy {};

struct DegreesPolicy {};

struct NoLayoutEditFieldTraits {
    using policy_tag = NoLayoutEditPolicy;
    static constexpr bool enabled = false;
    static constexpr ValueFormat value_format = ValueFormat::Integer;
};

template <typename PolicyTag, typename Value> struct PolicyClamp {
    static Value Apply(Value value) {
        return value;
    }
};

template <> struct PolicyClamp<PositiveIntPolicy, int> {
    static int Apply(int value) {
        return (std::max)(1, value);
    }
};

template <> struct PolicyClamp<NonNegativeIntPolicy, int> {
    static int Apply(int value) {
        return (std::max)(0, value);
    }
};

template <typename Value>
concept FontSizeEditableValue = requires(Value value) { value.size; };

template <FontSizeEditableValue Value> struct PolicyClamp<FontSizePolicy, Value> {
    static Value Apply(Value value) {
        value.size = (std::max)(1, value.size);
        return value;
    }
};

template <> struct PolicyClamp<DegreesPolicy, double> {
    static double Apply(double value) {
        return std::clamp(value, 0.0, 360.0);
    }
};

template <typename PolicyTag, ValueFormat Format = ValueFormat::Integer> struct LayoutEditFieldTraits {
    using policy_tag = PolicyTag;
    static constexpr bool enabled = true;
    static constexpr configschema::ValueFormat value_format = Format;
};

template <typename PolicyTag> struct LayoutEditTraitsForPolicy {
    using type = LayoutEditFieldTraits<PolicyTag>;
};

template <> struct LayoutEditTraitsForPolicy<FontSizePolicy> {
    using type = LayoutEditFieldTraits<FontSizePolicy, ValueFormat::FontSpec>;
};

template <> struct LayoutEditTraitsForPolicy<DegreesPolicy> {
    using type = LayoutEditFieldTraits<DegreesPolicy, ValueFormat::FloatingPoint>;
};

template <typename T> struct DefaultLayoutEditTraits {
    using type = NoLayoutEditFieldTraits;
};

template <typename T> struct DefaultCodec;

template <typename T> struct DefaultSectionCodec {
    using codec_type = StructuredSectionCodec;
};

template <> struct DefaultCodec<int> {
    using codec_type = IntCodec;
};

template <> struct DefaultCodec<double> {
    using codec_type = DoubleCodec;
};

template <> struct DefaultCodec<std::string> {
    using codec_type = StringCodec;
};

template <> struct DefaultCodec<unsigned int> {
    using codec_type = HexColorCodec;
};

template <> struct DefaultLayoutEditTraits<int> {
    using type = typename LayoutEditTraitsForPolicy<PositiveIntPolicy>::type;
};

template <typename T>
    requires std::is_same_v<typename DefaultCodec<T>::codec_type, FontSpecCodec>
struct DefaultLayoutEditTraits<T> {
    using type = typename LayoutEditTraitsForPolicy<FontSizePolicy>::type;
};

template <typename Owner,
    typename Field,
    FixedString Key,
    Field Owner::* Member,
    typename Codec,
    typename LayoutEditTraits = NoLayoutEditFieldTraits>
struct FieldDescriptor {
    using owner_type = Owner;
    using field_type = Field;
    using codec_type = Codec;
    using layout_edit_traits_type = LayoutEditTraits;

    static constexpr auto key = Key;
    static constexpr auto member = Member;
    static constexpr bool has_layout_edit_traits = LayoutEditTraits::enabled;

    static field_type& RawGet(owner_type& owner) {
        return owner.*member;
    }

    static const field_type& RawGet(const owner_type& owner) {
        return owner.*member;
    }

    static field_type Clamp(field_type value) {
        return PolicyClamp<typename layout_edit_traits_type::policy_tag, field_type>::Apply(std::move(value));
    }

    static void Set(owner_type& owner, field_type value) {
        RawGet(owner) = Clamp(std::move(value));
    }
};

template <typename Owner, typename Section, typename Section::owner_type Owner::* Member>
struct StructuredBindingDescriptor {
    using owner_type = Owner;
    using section_type = Section;
    static constexpr bool is_dynamic = false;
    static constexpr bool is_recursive = false;

    static constexpr auto member = Member;

    static typename Section::owner_type& Get(Owner& owner) {
        return owner.*member;
    }

    static const typename Section::owner_type& Get(const Owner& owner) {
        return owner.*member;
    }
};

template <typename Owner, typename Item, std::vector<Item> Owner::* Member, std::string Item::* KeyMember>
struct DynamicStructuredBindingDescriptor {
    using owner_type = Owner;
    using item_type = Item;
    using section_type = typename Item::Section;
    static constexpr bool is_dynamic = true;
    static constexpr bool is_recursive = false;

    static constexpr auto member = Member;
    static constexpr auto key_member = KeyMember;

    static std::vector<Item>& Get(Owner& owner) {
        return owner.*member;
    }

    static const std::vector<Item>& Get(const Owner& owner) {
        return owner.*member;
    }

    static Item* Find(Owner& owner, std::string_view key) {
        for (auto& item : Get(owner)) {
            if (item.*key_member == key) {
                return &item;
            }
        }
        return nullptr;
    }

    static const Item* Find(const Owner& owner, std::string_view key) {
        for (const auto& item : Get(owner)) {
            if (item.*key_member == key) {
                return &item;
            }
        }
        return nullptr;
    }

    static Item& Ensure(Owner& owner, std::string_view key) {
        if (Item* existing = Find(owner, key)) {
            return *existing;
        }
        Get(owner).push_back(Item{});
        Item& item = Get(owner).back();
        item.*key_member = std::string(key);
        return item;
    }

    static std::string_view Key(const Item& item) {
        return item.*key_member;
    }
};

template <typename Owner, typename NestedOwner, NestedOwner Owner::* Member>
struct RecursiveStructuredBindingDescriptor {
    using owner_type = Owner;
    using nested_owner_type = NestedOwner;
    static constexpr bool is_dynamic = false;
    static constexpr bool is_recursive = true;

    static constexpr auto member = Member;

    static NestedOwner& Get(Owner& owner) {
        return owner.*member;
    }

    static const NestedOwner& Get(const Owner& owner) {
        return owner.*member;
    }
};

template <FixedString Name, typename Owner, typename... Fields> struct SectionDescriptor {
    using owner_type = Owner;

    static constexpr auto name = Name;
    static constexpr auto fields = std::tuple<Fields...>{};
};

template <FixedString Prefix, typename Owner, typename... Fields> struct DynamicSectionDescriptor {
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

template <typename Owner, size_t Index> struct FieldTag {};

template <typename Owner, size_t Index> struct BindingTag {
    using owner_type = Owner;
    static constexpr size_t index = Index;
};

template <typename Owner, size_t Index> using ReflectedField = decltype(reflect_field(FieldTag<Owner, Index>{}));

template <typename Owner, size_t Index> using ReflectedBinding = decltype(reflect_binding(BindingTag<Owner, Index>{}));

template <typename Owner, size_t Index>
concept HasReflectedField = requires { reflect_field(FieldTag<Owner, Index>{}); };

template <typename Owner, size_t Index>
concept HasReflectedBinding = requires { reflect_binding(BindingTag<Owner, Index>{}); };

template <typename Owner, size_t Index = 0> consteval size_t CountReflectedFields() {
    if constexpr (HasReflectedField<Owner, Index>) {
        return CountReflectedFields<Owner, Index + 1>();
    } else {
        return Index;
    }
}

template <typename Owner, size_t Index = 0> consteval size_t CountReflectedBindings() {
    if constexpr (HasReflectedBinding<Owner, Index>) {
        return CountReflectedBindings<Owner, Index + 1>();
    } else {
        return Index;
    }
}

template <typename Owner, size_t... Index> consteval auto MakeReflectedFieldTuple(std::index_sequence<Index...>) {
    return std::tuple{reflect_field(FieldTag<Owner, Index>{})...};
}

template <typename Owner, size_t... Index> consteval auto MakeReflectedBindingTuple(std::index_sequence<Index...>) {
    return std::tuple{reflect_binding(BindingTag<Owner, Index>{})...};
}

template <FixedString Name, typename Owner> struct AutoSectionDescriptor {
    using owner_type = Owner;
    using codec_type = typename DefaultSectionCodec<Owner>::codec_type;

    static constexpr auto name = Name;
    static constexpr auto fields =
        MakeReflectedFieldTuple<Owner>(std::make_index_sequence<CountReflectedFields<Owner>()>{});
};

template <FixedString Prefix, typename Owner> struct AutoDynamicSectionDescriptor {
    using owner_type = Owner;
    using codec_type = typename DefaultSectionCodec<Owner>::codec_type;

    static constexpr auto prefix = Prefix;
    static constexpr auto fields =
        MakeReflectedFieldTuple<Owner>(std::make_index_sequence<CountReflectedFields<Owner>()>{});

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

template <typename Owner> struct AutoStructuredBindingListDescriptor {
    using owner_type = Owner;

    static constexpr auto bindings =
        MakeReflectedBindingTuple<Owner>(std::make_index_sequence<CountReflectedBindings<Owner>()>{});
};

template <typename Lens> struct LensValueProxy {
    using value_type = typename Lens::value_type;

    explicit LensValueProxy(typename Lens::root_type& root) : root_(&root) {}

    LensValueProxy& operator=(value_type value) {
        Lens::Set(*root_, std::move(value));
        return *this;
    }

    value_type* operator->() const {
        return &Lens::RawGet(*root_);
    }

private:
    typename Lens::root_type* root_ = nullptr;
};

template <typename Owner, typename... Bindings> struct BindingPathLens;

template <typename Owner> struct BindingPathLens<Owner> {
    using owner_type = Owner;

    static Owner& Get(Owner& owner) {
        return owner;
    }

    static const Owner& Get(const Owner& owner) {
        return owner;
    }
};

template <typename Owner, typename Binding, typename... Rest> struct BindingPathLens<Owner, Binding, Rest...> {
    using next_owner_type = std::remove_reference_t<decltype(Binding::Get(std::declval<Owner&>()))>;
    using owner_type = typename BindingPathLens<next_owner_type, Rest...>::owner_type;

    static owner_type& Get(Owner& owner) {
        return BindingPathLens<next_owner_type, Rest...>::Get(Binding::Get(owner));
    }

    static const owner_type& Get(const Owner& owner) {
        return BindingPathLens<next_owner_type, Rest...>::Get(Binding::Get(owner));
    }
};

template <typename Root, typename Field, typename... Bindings> struct RootFieldLens {
    using root_type = Root;
    using field_descriptor = Field;
    using value_type = typename Field::field_type;
    using traits_type = typename Field::layout_edit_traits_type;
    using owner_type = typename BindingPathLens<Root, Bindings...>::owner_type;

    static constexpr std::string_view section_name = Field::owner_type::Section::name.view();
    static constexpr std::string_view parameter_name = Field::key.view();

    static_assert(std::is_same_v<owner_type, typename Field::owner_type>);

    static value_type& RawGet(Root& root) {
        owner_type& owner = BindingPathLens<Root, Bindings...>::Get(root);
        return Field::RawGet(owner);
    }

    static LensValueProxy<RootFieldLens> Get(Root& root) {
        return LensValueProxy<RootFieldLens>(root);
    }

    static const value_type& RawGet(const Root& root) {
        const owner_type& owner = BindingPathLens<Root, Bindings...>::Get(root);
        return Field::RawGet(owner);
    }

    static const value_type& Get(const Root& root) {
        return RawGet(root);
    }

    static void Set(Root& root, value_type value) {
        owner_type& owner = BindingPathLens<Root, Bindings...>::Get(root);
        Field::Set(owner, std::move(value));
    }
};

struct NoRootFieldPath {
    static constexpr bool enabled = false;

    template <typename Field> using Lens = void;
};

template <typename Tag> struct ResolveBindingTag;

template <typename Owner, size_t Index> struct ResolveBindingTag<BindingTag<Owner, Index>> {
    using type = ReflectedBinding<Owner, Index>;
};

template <typename Root, typename Field, typename... BindingTags> struct DeferredRootFieldLens {
    using resolved_type = RootFieldLens<Root, Field, typename ResolveBindingTag<BindingTags>::type...>;
    using root_type = typename resolved_type::root_type;
    using field_descriptor = typename resolved_type::field_descriptor;
    using value_type = typename resolved_type::value_type;
    using traits_type = typename resolved_type::traits_type;
    using owner_type = typename resolved_type::owner_type;

    static constexpr std::string_view section_name = resolved_type::section_name;
    static constexpr std::string_view parameter_name = resolved_type::parameter_name;

    static value_type& RawGet(Root& root) {
        return resolved_type::RawGet(root);
    }

    static LensValueProxy<DeferredRootFieldLens> Get(Root& root) {
        return LensValueProxy<DeferredRootFieldLens>(root);
    }

    static const value_type& RawGet(const Root& root) {
        return resolved_type::RawGet(root);
    }

    static const value_type& Get(const Root& root) {
        return RawGet(root);
    }

    static void Set(Root& root, value_type value) {
        resolved_type::Set(root, std::move(value));
    }
};

template <typename Root, typename... BindingTags> struct RootBindingPath {
    template <typename Field> using Lens = DeferredRootFieldLens<Root, Field, BindingTags...>;
};

template <typename Owner, typename Field> struct EditableFieldLens;

}  // namespace configschema

#define CONFIG_REFLECTED_STRUCT_GET_MACRO(_1, _2, NAME, ...) NAME

#define CONFIG_REFLECTED_STRUCT(...)                                                                                   \
    CONFIG_REFLECTED_STRUCT_GET_MACRO(__VA_ARGS__, CONFIG_REFLECTED_STRUCT_WITH_PATH, CONFIG_REFLECTED_STRUCT_NO_PATH) \
    (__VA_ARGS__)

#define CONFIG_REFLECTED_STRUCT_NO_PATH(owner)                                                                         \
private:                                                                                                               \
    static constexpr std::size_t _configschema_field_base = __COUNTER__;                                               \
    using Self = owner;                                                                                                \
                                                                                                                       \
public:

#define CONFIG_REFLECTED_STRUCT_WITH_PATH(owner, root_path)                                                            \
private:                                                                                                               \
    static constexpr std::size_t _configschema_field_base = __COUNTER__;                                               \
    using Self = owner;                                                                                                \
                                                                                                                       \
public:                                                                                                                \
    using layout_edit_root_path = root_path;

#define CONFIG_CODEC(value_type, codec)                                                                                \
    template <> struct configschema::DefaultCodec<value_type> {                                                        \
        using codec_type = codec;                                                                                      \
    }

#define CONFIG_SECTION_CODEC(value_type, codec)                                                                        \
    template <> struct configschema::DefaultSectionCodec<value_type> {                                                 \
        using codec_type = codec;                                                                                      \
    }

#define CONFIG_VALUE(field_type, member, key)                                                                          \
    field_type member{};                                                                                               \
    using member##Field = configschema::FieldDescriptor<Self,                                                          \
        field_type,                                                                                                    \
        key,                                                                                                           \
        &Self::member,                                                                                                 \
        typename configschema::DefaultCodec<field_type>::codec_type>;                                                  \
    friend consteval auto reflect_field(                                                                               \
        configschema::FieldTag<Self, __COUNTER__ - Self::_configschema_field_base - 1>) {                              \
        return member##Field{};                                                                                        \
    }

#define CONFIG_EDITABLE_VALUE(field_type, member, key)                                                                 \
    CONFIG_EDITABLE_VALUE_WITH_TRAITS(                                                                                 \
        field_type, member, key, typename configschema::DefaultLayoutEditTraits<field_type>::type)

#define CONFIG_EDITABLE_VALUE_WITH(field_type, member, key, policy_tag)                                                \
    CONFIG_EDITABLE_VALUE_WITH_TRAITS(                                                                                 \
        field_type, member, key, typename configschema::LayoutEditTraitsForPolicy<policy_tag>::type)

#define CONFIG_EDITABLE_VALUE_WITH_TRAITS(field_type, member, key, layout_edit_traits)                                 \
    field_type member{};                                                                                               \
    using member##Field = configschema::FieldDescriptor<Self,                                                          \
        field_type,                                                                                                    \
        key,                                                                                                           \
        &Self::member,                                                                                                 \
        typename configschema::DefaultCodec<field_type>::codec_type,                                                   \
        layout_edit_traits>;                                                                                           \
    using member##Meta = configschema::EditableFieldLens<Self, member##Field>;                                         \
    friend consteval auto reflect_field(                                                                               \
        configschema::FieldTag<Self, __COUNTER__ - Self::_configschema_field_base - 1>) {                              \
        return member##Field{};                                                                                        \
    }

#define CONFIG_SECTION(name) using Section = configschema::AutoSectionDescriptor<name, Self>

#define CONFIG_DYNAMIC_SECTION(prefix) using Section = configschema::AutoDynamicSectionDescriptor<prefix, Self>

#define CONFIG_ROOT_BINDING_PATH(root_type, ...) configschema::RootBindingPath<root_type, __VA_ARGS__>

#define CONFIG_EDITABLE_ROOT_BINDING_PATH(owner, root_type, ...)                                                       \
    using owner##RootPath = CONFIG_ROOT_BINDING_PATH(root_type, __VA_ARGS__);                                          \
    template <typename Field>                                                                                          \
    struct configschema::EditableFieldLens<owner, Field> : owner##RootPath::template Lens<Field> {}

#define CONFIG_REFLECTED_BINDINGS(owner)                                                                               \
private:                                                                                                               \
    static constexpr std::size_t _configschema_binding_base = __COUNTER__;                                             \
    using Self = owner;                                                                                                \
                                                                                                                       \
public:

#define CONFIG_SECTION_VALUE(field_type, member)                                                                       \
    field_type member{};                                                                                               \
    using member##Binding = configschema::BindingTag<Self, __COUNTER__ - Self::_configschema_binding_base - 1>;        \
    using member##BindingDescriptor =                                                                                  \
        configschema::StructuredBindingDescriptor<Self, typename field_type::Section, &Self::member>;                  \
    friend consteval auto reflect_binding(member##Binding) {                                                           \
        return member##BindingDescriptor{};                                                                            \
    }

#define CONFIG_BINDING_LIST() using BindingList = configschema::AutoStructuredBindingListDescriptor<Self>

#define CONFIG_DYNAMIC_SECTION_VALUE(item_type, member, key_member)                                                    \
    std::vector<item_type> member{};                                                                                   \
    using member##Binding = configschema::BindingTag<Self, __COUNTER__ - Self::_configschema_binding_base - 1>;        \
    using member##BindingDescriptor =                                                                                  \
        configschema::DynamicStructuredBindingDescriptor<Self, item_type, &Self::member, &item_type::key_member>;      \
    friend consteval auto reflect_binding(member##Binding) {                                                           \
        return member##BindingDescriptor{};                                                                            \
    }

#define CONFIG_RECURSIVE_BINDING_VALUE(field_type, member)                                                             \
    field_type member{};                                                                                               \
    using member##Binding = configschema::BindingTag<Self, __COUNTER__ - Self::_configschema_binding_base - 1>;        \
    using member##BindingDescriptor =                                                                                  \
        configschema::RecursiveStructuredBindingDescriptor<Self, field_type, &Self::member>;                           \
    friend consteval auto reflect_binding(member##Binding) {                                                           \
        return member##BindingDescriptor{};                                                                            \
    }
