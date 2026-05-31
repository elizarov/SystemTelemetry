#ifndef FORMAT_USERVER_FIXTURE_HPP
#define FORMAT_USERVER_FIXTURE_HPP

// Golden fixture for userver formatting.
// Keep these examples representative of userver parser and include-preservation cases.
// Mirrors userver .clang-format include setting: IncludeBlocks: Preserve.
// The guarded header shape exercises include preservation away from the common userver #pragma once path.

#include <userver/utils/assert.hpp>
#include <algorithm>
#include <boost/atomic/atomic.hpp>

#include "src/kafka/impl/consumer_impl.hpp"
#include <userver/utest/utest.hpp>
#include <fmt/format.h>
#include <string_view>
#include "userver/chaotic/io/my/custom_object.hpp"

#include <userver/logging/log.hpp>
#include <google/protobuf/descriptor.h>
#include <grpcpp/grpcpp.h>
#include <array>

#define FORMAT_USERVER_DO_WHILE(flag) \
    do { \
        if (flag) break; \
        UseFlag(flag); \
    } while (false)
#ifdef FORMAT_USERVER_PROTECT_ATTR
#define FORMAT_USERVER_PROTECTED_ATTR __attribute__((noinline, flatten))
#else
#define FORMAT_USERVER_PROTECTED_ATTR __attribute__((always_inline, flatten))
#endif
#ifdef FORMAT_USERVER_HAS_ATTRIBUTE
#if FORMAT_USERVER_HAS_NODEBUG
#define USERVER_IMPL_NODEBUG __attribute__((__nodebug__))
#define USERVER_IMPL_NODEBUG_INLINE_FUNC __attribute__((__nodebug__, __always_inline__))
#elif FORMAT_USERVER_HAS_ALWAYS_INLINE
// GCC may have no __nodebug__ attribute.
#define USERVER_IMPL_NODEBUG_INLINE_FUNC __attribute__((__always_inline__))
#endif
#endif
#define USERVER_IMPL_FORCE_INLINE __attribute__((always_inline)) inline
#define LOG_FORMAT_USERVER_LIMITED(logger, level, ...) \
    if (const RateLimiter limiter{[]() -> RateLimitData& { \
            static RateLimitData data; \
            return data; \
        }()}; \
        !limiter.ShouldLog()) \
    { \
    } else \
        LOG_TO((logger), (level), __VA_ARGS__) << limiter
#if FORMAT_USERVER_LEGACY_FMT
#define FORMAT_USERVER_CONST
namespace compat_userver {
template <typename S>
const S& runtime(const S& s) {
return s;
}
}
#else
#define FORMAT_USERVER_CONST const
#endif
#if FORMAT_USERVER_HAS_NAMESPACE_ALIAS
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CURL_FORMAT_USERVER_NAMESPACE fixture::
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CURL_FORMAT_USERVER_NAMESPACE
#endif
#define FORMAT_USERVER_COMPLEX_OPTION(FUNCTION_NAME, OPTION_TYPE) \
    inline void FUNCTION_NAME(OPTION_TYPE arg) { \
        UseOption(arg, PP_STRINGIZE(FUNCTION_NAME)); \
    }
#define FORMAT_USERVER_HASH_JOIN(FUNCTION_NAME) \
private: \
    inline void FUNCTION_NAME##_impl() {} \
public: \
    static constexpr bool is_##FUNCTION_NAME##_available = true
#define FORMAT_USERVER_EXPECT_TRY(cmd) \
    try { \
        cmd; \
    } catch (const Error& error) { \
        EXPECT_EQ(error.Code(), ErrorCode::kExpected); \
    }
#define FORMAT_USERVER_BENCHMARK_ARGS ->Arg(2)->Arg(4)
#define IMPL_UTEST_FORMAT_USERVER(name) \
    TestLauncher<::testing::Test>::RunTest< \
        name>();                         \
    struct FormatUserverForceSemicolon

extern "C" {
#ifndef FORMAT_USERVER_CLANG
[[gnu::visibility("default")]] [[gnu::externally_visible]]
#endif
    int FormatUserverExternAttribute();
}

BENCHMARK_CAPTURE(FormatterBenchmark, Mode, kValue) FORMAT_USERVER_BENCHMARK_ARGS;
BENCHMARK_INSTANTIATE_TEMPLATE_F(FormatterBenchmark, Value, int);
BENCHMARK_DEFINE_TEMPLATE_F(FormatterBenchmark, Value)(benchmark::State& state) {
    UseBenchmarkState(state);
}
BENCHMARK_DEFINE_F(FormatterBenchmark, Inline)(benchmark::State& state) {
    UseBenchmarkState(state);
}

namespace format_userver_fixture {

template <typename Output>
USERVER_IMPL_FORCE_INLINE Output FormatUserverInline(Output output) {
    return output;
}

constexpr utils::StringLiteral kFormatUserverPrefixes[] = {
#ifdef FORMAT_USERVER_PREFIX
    FORMAT_USERVER_STRINGIZE(FORMAT_USERVER_PREFIX),
#endif
#ifdef FORMAT_USERVER_SOURCE_PREFIX
    FORMAT_USERVER_STRINGIZE(FORMAT_USERVER_SOURCE_PREFIX),
#endif
};

template <typename T>
concept FormatUserverConvertible = requires(T& value){FormatUserverConvert(value);
} &&
#if FORMAT_USERVER_OLD_LIB
    // Old libraries reject long double here.
    !std::same_as<T, long double>
#else
    true
#endif
;

template <typename Enum>
Flags<Enum> FormatUserverFlagsOr(Flags<Enum> lhs) {
    return Flags<Enum>{lhs} |= lhs;
}

namespace macro_boundary_fixture {}

class MockMethodFixture {
public:
    using typename Base::GetFunc;
    MOCK_METHOD(void, PreSendMessage, (Context&, (const CompletionStatus&)), (const, override));
};

class MacroFieldDeclarationHost {
public:
    MOCK_METHOD(void, SetValue, (std::string_view, std::string&&), (override));
};

UTEST_DEATH(FormatterMacroFixture, KeepsBody) {
    RunDeathTest();
}

UTEST_MT(FormatterMacroFixture, KeepsThreads, 2) {
    RunThreadedTest();
}

#if FORMAT_USERVER_DISABLED_THREADS
UTEST_MT(FormatterMacroFixture, DISABLED_ConditionalThreads, 2) {
#else
UTEST_MT(FormatterMacroFixture, ConditionalThreads, 2) {
#endif
    RunConditionalThreadedTest();
}

TYPED_UTEST_SUITE_P(FormatterTypedFixture);

TYPED_UTEST_P_MT(FormatterTypedFixture, KeepsTypedThreads, 2) {
    RunTypedThreadedTest<TypeParam>();
}

TYPED_UTEST_MT(FormatterTypedFixture, KeepsDirectTypedThreads, 2) {
    RunDirectTypedThreadedTest<TypeParam>();
}

INSTANTIATE_UTEST_SUITE_P(
    FormatterMacroFixture,
    testing::Values(true)
);

BENCHMARK_TEMPLATE(FormatterBenchmark, std::string)->Range(1, 8);

void DependentTemplateMemberCall(DependentStorage& storage) {
    storage.template Emplace(tag, MakeValue());
}

void DecltypeBracedSentinel(Iterator it) {
    for (; it != decltype(it){}; ++it) {}
}

void MacroConcatenatedString() {
    throw Error("prefix " FORMAT_USERVER_VERSION " suffix");
}

#if defined(FORMAT_USERVER_PLATFORM) && __has_include(<format_userver/header.hpp>)
void HasIncludeGuardedFunction() {
    UsePlatformHeader();
}
#else
void HasIncludeGuardedFallback() {
    UseFallbackHeader();
}
#endif

void QualifiedTemplateCompoundLiteral(Token token, Writer& writer) {
    WriteToStream(
        fixture::chaotic::Primitive<std::string, fixture::chaotic::MinLength<128>, fixture::chaotic::MaxLength<128>>{
            *token
        },
        writer
    );
}

auto BracedLambdaCapture(std::vector<int> inputs, CaptureSettings settings) {
    return [inputs{std::move(inputs)}, settings{std::move(settings)}]() mutable {
        return inputs.size() + settings.count;
    };
}

FORMAT_USERVER_NODEBUG_FUNC inline decltype(auto) PrefixedInlineFunction(Function&& function) {
    return function();
}

class PrefixedAttributeMethodFixture {
public:
    template <typename T>
    FORMAT_USERVER_PROTECTED_ATTR bool Compare(T& value) noexcept {
        return value.Compare();
    }
};

Metric* ContextualRefIdentifier(Storage& storage) {
    auto ref = storage.Find();
    if (ref) {
        return &ref->metric;
    }
    return nullptr;
}

extern template class ExplicitTemplateInstantiation<ExplicitOptions>;
template class ExplicitTemplateInstantiation<RuntimeOptions>;

extern int* ConditionalDeclarationSuffix(void)
#ifdef FORMAT_USERVER_THROW
    FORMAT_USERVER_THROW
#endif
;

void ConditionalLocalConstQualifier() {
#if FORMAT_USERVER_OPENSSL_HAS_CONST_SIGNATURE
const
#endif
    ASN1_BIT_STRING* signature = nullptr;
    UseSignature(signature);
}

struct MacroInitializerFixture {
    std::atomic_flag started FORMAT_USERVER_ATOMIC_INIT;
};

static const std::size_t gnu_attribute_value __attribute__((used)) = FormatterTraits::page_size();

constexpr int ConditionalExpressionFragment = kFirst |
#if FORMAT_USERVER_HAS_SECOND
    kSecond |
#endif
kThird;

bool ConditionalLogicalFragment(int error_code) {
    if (error_code == kWouldBlock
#if FORMAT_USERVER_HAS_DUPLICATE_WOULD_BLOCK
    || error_code == kAgain
#endif
    ) {
        return true;
    }
    return false;
}

bool ConditionalMultiLineLogicalFragment(Connection* conn) {
    if (conn->xactStatus != kInTransaction
#if FORMAT_USERVER_PIPELINE_STATUS
&& (conn->pipelineStatus == kPipelineOff ||
conn->asyncStatus == kAsyncIdle)
#endif
    ) {
        return true;
    }
    return false;
}

void PreprocessorSelectedIfHeader(Connection* conn) {
#if FORMAT_USERVER_PIPELINE_STATUS
if (conn->pipelineStatus == kPipelineOff)
#else
if (Flush(conn) < 0)
#endif
    goto sendFailed;
    sendFailed:;
}

void PreprocessorSelectedBracedIf(Connection* conn, std::string& status) {
#if FORMAT_USERVER_NEW_MONGO
if (HasReadableServer(conn)) {
#else
if (HasReadableServer(const_cast<Connection*>(conn))) {
#endif
status.append("Secondary AVAILABLE");
} else {
status.append("Secondary UNAVAILABLE");
}
}

bool ConditionalWholeCondition(int error_code) {
    if (
#if FORMAT_USERVER_USE_WOULD_BLOCK
error_code == kWouldBlock
#else
error_code == kAgain
#endif
    ) {
        return true;
    }
    return false;
}

void ConditionalArgumentFragment() {
    Use(
#ifdef FORMAT_USERVER_FAST_ARGUMENT
FastArgument(),
#else
SlowArgument(),
#endif
    "argument label");
    Open(
#ifdef FORMAT_USERVER_FLAG_A
kFlagA |
#endif
#ifdef FORMAT_USERVER_FLAG_B
kFlagB |
#endif
    kBaseFlag);
}

void DeclarationMacroArgument(Source& source) {
    UEXPECT_THROW([[maybe_unused]] auto bytes_read = source.ReadSome(kBuffer, kDeadline), IoTimeout);
}

void SplitConstDeclarationMacroArgument(Source& source) {
    EXPECT_THROW([[maybe_unused]] const auto
bytes_read = source.ReadSome(kBuffer, kDeadline), IoTimeout);
}

void ThrowExpressionMacroArgument() {
    UEXPECT_THROW(throw ErrorType(), ErrorType);
}

void PlainDeclarationMacroArgument() {
    UEXPECT_THROW(auto future = Client().SayHello(request), std::runtime_error);
    UEXPECT_NO_THROW(const auto stream = Client().ReadMany(request));
}

void StatementSequenceMacroArgument() {
    UEXPECT_THROW(crypto::SslCtx context = MakeContext();
static_cast<void>(UseContext(context)), IoException);
}

Value ConditionalThrowExpression(bool enabled) {
    return enabled ? throw ErrorType() : Value{};
}

void ThrowFoldExpression(ErrorContext context, std::size_t processed_bytes) {
    throw(IoCancelled(/*bytes_transferred =*/processed_bytes) << ... << context);
}

void QualifiedOperatorCall(Task& task) {
    task.TaskBase::operator=(Task{});
}

void CapitalizedHelperCall() {
    SetHttpProxy(target, channel_args, factory.GetAuthType(), proxy_address);
}

void ConditionalStreamingAssertion() {
#ifndef FORMAT_USERVER_ARCADIA
// Test flaps on external CI.
GTEST_SKIP()
#else
FAIL()
#endif
    << "failed to trigger failures";
}

void PreprocessorSelectedInitializer(DescriptorPool* descriptor_pool, std::string_view file_name) {
    const Descriptor* file_desc =
#if FORMAT_USERVER_PROTOBUF_GE_4022000
descriptor_pool->FindFileByName(file_name);
#else
descriptor_pool->FindFileByName(std::string{file_name});
#endif
    Use(file_desc);
}

auto PreprocessorSelectedListItem() {
    return TimestampToJsonFailureTestParam{TimestampMessageData{
        0,
        kMaxTimestampNanos + 1
    }, PrintErrorCode::kInvalidValue, "field1", {},
#if FORMAT_USERVER_PROTOBUF_GE_6033000
false
#else
true
#endif
    };
}

DateParts OperatorConversionCall(DatePartsParts ymd) {
    return {ymd.year().operator int(), ymd.month().operator unsigned int()};
}

template <typename T>
concept HasNonEmptyName = requires{requires !std::string_view{T::kName}
.empty();};

template <typename T>
struct DetectedBufferCategory : decltype(DetectBufferCategory<T>()) {};

template <
    typename RedisRequestType,
    typename... Args,
    typename M = RedisRequestType (storages::redis::Client::*)(Args..., const redis::CommandControl&)
>
HedgedRedisRequest<RedisRequestType> MakeHedgedRedisRequestAsync(M method, Args... args);

template <typename StringViews, typename DistanceFunc = std::size_t (*)(std::string_view, std::string_view)>
std::optional<std::string_view> GetNearestString(const StringViews& strings, DistanceFunc distance_func);

template <typename StorageTag>
void TemplateVariableAssignment(std::size_t alignment) {
    data_offset<StorageTag> += (alignment - (data_offset<StorageTag> % alignment)) % alignment;
    count<StorageTag>++;
}

class DeletedConversionOperator {
public:
    /*implicit*/ operator bool() const = delete;
};

void PreprocessorEndedConsequence(Status status, Handle& handle, Handle next_handle) {
#if FORMAT_USERVER_HAS_PIPELINING
if (status == Status::kSync) {
HandlePipelineSync();
} else if (status != Status::kAborted)
#endif
handle = std::move(next_handle);
}

void MacroCompoundArgument() {
    UEXPECT_THROW_MSG({
        GetConn()->SetParameter("invalid", "parameter", Scope::kSession);
        auto res = GetConn()->Execute("select 1");
    }, pg::AccessRuleViolation, "invalid parameter");
    UEXPECT_NO_THROW({
        const auto result = coll.Distinct("type", mongo::options::Comment("test distinct operation"));
        EXPECT_EQ(2, result.size());
    }
    );
}

void MacroTypedDeclarationArgument(Pool& pool) {
    UEXPECT_THROW(const pg::detail::ConnectionPtr conn2 = pool.Acquire(MakeDeadline()), pg::PoolError);
    ASSERT_THROW([[maybe_unused]] auto foo = value.foo(), proto_structs::OneofAccessError);
}

void MacroTrailingCommaArgument() {
    TEST_COMMAND("assert_matches('(running.*){1,4}', tasks_list_output)\n", test_in_coredump = True);
}

LockedChannelProxy<AMQP::Channel> GetChannel(engine::Deadline deadline) {
    return DoGetChannel(channel, deadline);
}

LockedChannelProxy<AmqpConnection::ReliableChannel> GetReliableChannel(engine::Deadline deadline) {
    return DoGetChannel(*reliable, deadline);
}

PostgresChaosProxy::PostgresChaosProxy(engine::TaskProcessor& task_processor) :
    task_processor_(task_processor),
    task_storage_() {}

PostgresChaosProxy::~PostgresChaosProxy() {}

ATTRIBUTE_NO_SANITIZE_UNDEFINED std::size_t AttributePrefixedFunction(const BoundsBlock& block, float value) noexcept {
    return block.Find(value);
}

template <typename T>
USERVER_IMPL_NODEBUG T NodebugPrefixedTemplate() {
    return T{};
}

template <IsRange T>
using IteratorType USERVER_IMPL_NODEBUG = decltype(begin(std::declval<T&>()));

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

FORMAT_USERVER_ALWAYS_INLINE_SIMD std::size_t PrefixMacroFunction(const BoundsBlock& block, float value) noexcept {
    return block.Find(value);
}

const char* ConditionalStringLiteral() {
    return "prefix "
#if FORMAT_USERVER_USE_UTC
"UTC "
#else
"GMT "
#endif
    "suffix";
}

Value DependentTypenameBraced(Payload payload) {
    return typename Value::Builder{payload.value}.ExtractValue();
}

Value DependentTypenameCall(Item item) {
    return typename Value::Builder(T{item});
}

auto DecltypeValueInitialization(Func& func) {
    return decltype(func())();
}

using MemberFunctionPointerArray = std::array<void (FieldSubparser::*)(), Count>;

using GenericPrepareUnaryCall = std::unique_ptr<grpc::ClientAsyncResponseReader<grpc::ByteBuffer>>(
    grpc::GenericStub::*
)(grpc::ClientContext*, const grpc::string&);
using AuthCheckerFactoryFactory = utils::UniqueRef<AuthCheckerFactoryBase>(*)(const components::ComponentContext&);

PrepareUnaryCallProxy(
    GenericPrepareUnaryCall,
    const grpc::string&
) -> PrepareUnaryCallProxy<grpc::GenericStub, grpc::ByteBuffer, grpc::ByteBuffer>;

Data& operator*() & FORMAT_USERVER_LIFETIME_BOUND {
    return data_;
}

using std::chrono::duration, std::chrono::nanoseconds;

enum class [[nodiscard]] FormatStatus : bool {
    kNo = false,
    kYes = true,
};

enum CurlNamespaceStatus {
    kOptional = CURL_FORMAT_USERVER_NAMESPACE kOptionalValue,
};

class PreprocessorSpecifierFixture {
public:
#if FORMAT_USERVER_USE_CONSTEXPR
    // Older compilers keep this path constexpr.
    constexpr
#else
    consteval
#endif
    PreprocessorSpecifierFixture(const char* value) noexcept : value{value} {}

private:
    const char* value;
};

}  // namespace format_userver_fixture

#endif  // FORMAT_USERVER_FIXTURE_HPP
