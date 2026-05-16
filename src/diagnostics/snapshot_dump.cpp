#include "diagnostics/snapshot_dump.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <utility>

#include "util/text_format.h"

namespace {

constexpr char kDumpFormatVersion[] = "casedash_snapshot_v12";

enum class DumpFieldKind : std::uint8_t {
    String,
    Double,
    OptionalDouble,
    ScalarUnit,
    ScalarIssue,
    SystemTimeWord,
};

struct DumpFieldDescriptor {
    const char* key = "";
    std::uint32_t offset = 0;
    DumpFieldKind kind = DumpFieldKind::String;
};

using DumpValues = std::vector<std::pair<std::string, std::string>>;

#define DUMP_FIELD(key, kind, field)                                                                                   \
    DumpFieldDescriptor {                                                                                              \
        key, static_cast<std::uint32_t>(offsetof(TelemetryDump, field)), kind                                          \
    }

// Size: fixed metadata avoids function-local vector construction/destruction code.
constexpr DumpFieldDescriptor kFlatDumpFields[] = {
    DUMP_FIELD("cpu.name", DumpFieldKind::String, snapshot.cpu.name),
    DUMP_FIELD("cpu.load_percent", DumpFieldKind::Double, snapshot.cpu.loadPercent),
    DUMP_FIELD("cpu.clock.value", DumpFieldKind::OptionalDouble, snapshot.cpu.clock.value),
    DUMP_FIELD("cpu.clock.unit", DumpFieldKind::ScalarUnit, snapshot.cpu.clock.unit),
    DUMP_FIELD("cpu.memory.used_gb", DumpFieldKind::Double, snapshot.cpu.memory.usedGb),
    DUMP_FIELD("cpu.memory.total_gb", DumpFieldKind::Double, snapshot.cpu.memory.totalGb),
    DUMP_FIELD("gpu.name", DumpFieldKind::String, snapshot.gpu.name),
    DUMP_FIELD("gpu.load_percent", DumpFieldKind::Double, snapshot.gpu.loadPercent),
    DUMP_FIELD("gpu.temperature.value", DumpFieldKind::OptionalDouble, snapshot.gpu.temperature.value),
    DUMP_FIELD("gpu.temperature.unit", DumpFieldKind::ScalarUnit, snapshot.gpu.temperature.unit),
    DUMP_FIELD("gpu.clock.value", DumpFieldKind::OptionalDouble, snapshot.gpu.clock.value),
    DUMP_FIELD("gpu.clock.unit", DumpFieldKind::ScalarUnit, snapshot.gpu.clock.unit),
    DUMP_FIELD("gpu.fan.value", DumpFieldKind::OptionalDouble, snapshot.gpu.fan.value),
    DUMP_FIELD("gpu.fan.unit", DumpFieldKind::ScalarUnit, snapshot.gpu.fan.unit),
    DUMP_FIELD("gpu.fps.value", DumpFieldKind::OptionalDouble, snapshot.gpu.fps.value),
    DUMP_FIELD("gpu.fps.unit", DumpFieldKind::ScalarUnit, snapshot.gpu.fps.unit),
    DUMP_FIELD("gpu.fps.issue", DumpFieldKind::ScalarIssue, snapshot.gpu.fps.issue),
    DUMP_FIELD("gpu.fps.app_name", DumpFieldKind::String, snapshot.gpu.fpsAppName),
    DUMP_FIELD("gpu.vram.used_gb", DumpFieldKind::Double, snapshot.gpu.vram.usedGb),
    DUMP_FIELD("gpu.vram.total_gb", DumpFieldKind::Double, snapshot.gpu.vram.totalGb),
    DUMP_FIELD("network.adapter_name", DumpFieldKind::String, snapshot.network.adapterName),
    DUMP_FIELD("network.upload_mbps", DumpFieldKind::Double, snapshot.network.uploadMbps),
    DUMP_FIELD("network.download_mbps", DumpFieldKind::Double, snapshot.network.downloadMbps),
    DUMP_FIELD("network.ip_address", DumpFieldKind::String, snapshot.network.ipAddress),
    DUMP_FIELD("storage.read_mbps", DumpFieldKind::Double, snapshot.storage.readMbps),
    DUMP_FIELD("storage.write_mbps", DumpFieldKind::Double, snapshot.storage.writeMbps),
    DUMP_FIELD("time.year", DumpFieldKind::SystemTimeWord, snapshot.now.wYear),
    DUMP_FIELD("time.month", DumpFieldKind::SystemTimeWord, snapshot.now.wMonth),
    DUMP_FIELD("time.day", DumpFieldKind::SystemTimeWord, snapshot.now.wDay),
    DUMP_FIELD("time.hour", DumpFieldKind::SystemTimeWord, snapshot.now.wHour),
    DUMP_FIELD("time.minute", DumpFieldKind::SystemTimeWord, snapshot.now.wMinute),
    DUMP_FIELD("time.second", DumpFieldKind::SystemTimeWord, snapshot.now.wSecond),
    DUMP_FIELD("time.milliseconds", DumpFieldKind::SystemTimeWord, snapshot.now.wMilliseconds),
};
constexpr size_t kFlatDumpFieldCount = sizeof(kFlatDumpFields) / sizeof(kFlatDumpFields[0]);

#undef DUMP_FIELD

template <typename Field> Field& DumpField(TelemetryDump& dump, const DumpFieldDescriptor& field) {
    return *reinterpret_cast<Field*>(reinterpret_cast<char*>(&dump) + field.offset);
}

template <typename Field> const Field& DumpField(const TelemetryDump& dump, const DumpFieldDescriptor& field) {
    return *reinterpret_cast<const Field*>(reinterpret_cast<const char*>(&dump) + field.offset);
}

std::string TrimDumpWhitespace(const std::string& value) {
    const size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string DumpKey(std::string_view prefix, const char* suffix) {
    return FormatText("%.*s%s", static_cast<int>(prefix.size()), prefix.data(), suffix);
}

std::string EscapeString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\':
                AppendFormat(escaped, "\\\\");
                break;
            case '"':
                AppendFormat(escaped, "\\\"");
                break;
            case '\n':
                AppendFormat(escaped, "\\n");
                break;
            case '\r':
                AppendFormat(escaped, "\\r");
                break;
            case '\t':
                AppendFormat(escaped, "\\t");
                break;
            default:
                escaped.push_back(static_cast<char>(ch));
                break;
        }
    }
    return escaped;
}

bool UnescapeQuotedString(const std::string& text, std::string& value) {
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') {
        return false;
    }

    value.clear();
    value.reserve(text.size() - 2);
    bool escaped = false;
    for (size_t i = 1; i + 1 < text.size(); ++i) {
        const char ch = text[i];
        if (escaped) {
            switch (ch) {
                case '\\':
                    value.push_back('\\');
                    break;
                case '"':
                    value.push_back('"');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    return false;
            }
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        value.push_back(ch);
    }
    return !escaped;
}

bool WriteDumpText(std::FILE* output, std::string_view text) {
    return output != nullptr && fwrite(text.data(), 1, text.size(), output) == text.size();
}

bool WriteLine(std::string& output, const std::string& key, const std::string& value) {
    AppendFormat(output, "%s=%s\n", key.c_str(), value.c_str());
    return true;
}

void WriteString(std::string& output, const std::string& key, const std::string& value) {
    WriteLine(output, key, FormatText("\"%s\"", EscapeString(value).c_str()));
}

void WriteDouble(std::string& output, const std::string& key, double value, int precision = 6) {
    WriteLine(output, key, FormatText("%.*f", precision, value));
}

template <typename T> void WriteInteger(std::string& output, const std::string& key, T value) {
    WriteLine(output, key, FormatText("%lld", static_cast<long long>(value)));
}

void WriteOptionalDouble(
    std::string& output, const std::string& key, const std::optional<double>& value, int precision = 6) {
    if (!value.has_value()) {
        WriteLine(output, key, "null");
        return;
    }
    WriteDouble(output, key, *value, precision);
}

void WriteDoubleArray(std::string& output, const std::string& key, const std::vector<double>& values) {
    AppendFormat(output, "%s=[", key.c_str());
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            AppendFormat(output, ",");
        }
        AppendFormat(output, "%.6f", values[i]);
    }
    AppendFormat(output, "]\n");
}

std::string_view ScalarMetricUnitDumpText(ScalarMetricUnit unit) {
    return EnumToString(unit);
}

bool ParseDumpScalarMetricUnit(std::string_view text, ScalarMetricUnit& unit) {
    return TryEnumFromString(text, unit);
}

void WriteScalarMetricUnit(std::string& output, const std::string& key, ScalarMetricUnit unit) {
    WriteString(output, key, std::string(ScalarMetricUnitDumpText(unit)));
}

void WriteScalarMetricIssue(std::string& output, const std::string& key, ScalarMetricIssue issue) {
    WriteString(output, key, std::string(EnumToString(issue)));
}

void WriteFlatDumpFields(std::string& output, const TelemetryDump& dump, size_t begin, size_t end) {
    end = (std::min)(end, kFlatDumpFieldCount);
    for (size_t i = begin; i < end; ++i) {
        const DumpFieldDescriptor& field = kFlatDumpFields[i];
        const std::string key(field.key);
        switch (field.kind) {
            case DumpFieldKind::String:
                WriteString(output, key, DumpField<std::string>(dump, field));
                break;
            case DumpFieldKind::Double:
                WriteDouble(output, key, DumpField<double>(dump, field), 6);
                break;
            case DumpFieldKind::OptionalDouble:
                WriteOptionalDouble(output, key, DumpField<std::optional<double>>(dump, field), 6);
                break;
            case DumpFieldKind::ScalarUnit:
                WriteScalarMetricUnit(output, key, DumpField<ScalarMetricUnit>(dump, field));
                break;
            case DumpFieldKind::ScalarIssue:
                WriteScalarMetricIssue(output, key, DumpField<ScalarMetricIssue>(dump, field));
                break;
            case DumpFieldKind::SystemTimeWord:
                WriteInteger(output, key, DumpField<WORD>(dump, field));
                break;
        }
    }
}

void WriteNamedScalarMetrics(
    std::string& output, const std::string& prefix, const std::vector<NamedScalarMetric>& metrics) {
    WriteInteger(output, DumpKey(prefix, ".count"), metrics.size());
    for (size_t i = 0; i < metrics.size(); ++i) {
        const std::string metricPrefix = FormatText("%s.%zu", prefix.c_str(), i);
        WriteString(output, DumpKey(metricPrefix, ".name"), metrics[i].name);
        WriteOptionalDouble(output, DumpKey(metricPrefix, ".value"), metrics[i].metric.value, 6);
        WriteScalarMetricUnit(output, DumpKey(metricPrefix, ".unit"), metrics[i].metric.unit);
    }
}

void WriteRetainedHistories(
    std::string& output, const std::string& prefix, const std::vector<RetainedHistorySeries>& histories) {
    WriteInteger(output, DumpKey(prefix, ".count"), histories.size());
    for (size_t i = 0; i < histories.size(); ++i) {
        const std::string historyPrefix = FormatText("%s.%zu", prefix.c_str(), i);
        WriteString(output, DumpKey(historyPrefix, ".series_ref"), histories[i].seriesRef);
        WriteDoubleArray(output, DumpKey(historyPrefix, ".samples"), histories[i].samples);
        WriteDoubleArray(
            output, DumpKey(historyPrefix, ".throughput_live_samples"), histories[i].throughputLiveSamples);
        WriteDouble(output, DumpKey(historyPrefix, ".throughput_bucket_total"), histories[i].throughputBucketTotal, 6);
        WriteInteger(output,
            DumpKey(historyPrefix, ".throughput_bucket_sample_count"),
            histories[i].throughputBucketSampleCount);
    }
}

bool ParseStrictDouble(const std::string& text, double& value) {
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        return false;
    }
    value = parsed;
    return true;
}

bool ParseStrictUnsigned(const std::string& text, unsigned long long& value) {
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        return false;
    }
    value = parsed;
    return true;
}

bool ParseDoubleArray(const std::string& text, std::vector<double>& values) {
    if (text.size() < 2 || text.front() != '[' || text.back() != ']') {
        return false;
    }
    values.clear();
    const std::string body = TrimDumpWhitespace(text.substr(1, text.size() - 2));
    if (body.empty()) {
        return true;
    }

    size_t start = 0;
    while (start < body.size()) {
        size_t comma = body.find(',', start);
        std::string token =
            TrimDumpWhitespace(body.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        double value = 0.0;
        if (!ParseStrictDouble(token, value)) {
            return false;
        }
        values.push_back(value);
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return true;
}

bool TryGetValue(const DumpValues& values, const std::string& key, std::string& value) {
    for (const auto& item : values) {
        if (item.first == key) {
            value = item.second;
            return true;
        }
    }
    return false;
}

void SetDumpValue(DumpValues& values, std::string key, std::string value) {
    for (auto& item : values) {
        if (item.first == key) {
            item.second = std::move(value);
            return;
        }
    }
    values.emplace_back(std::move(key), std::move(value));
}

void SetInvalidKeyError(std::string* error, const char* valueKind, const std::string& key) {
    if (error != nullptr) {
        AssignFormat(*error, "Invalid %s for key: %s", valueKind, key.c_str());
    }
}

bool LoadString(const DumpValues& values, const std::string& key, std::string& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    if (!UnescapeQuotedString(text, field)) {
        SetInvalidKeyError(error, "quoted string", key);
        return false;
    }
    return true;
}

bool LoadDouble(const DumpValues& values, const std::string& key, double& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    if (!ParseStrictDouble(text, field)) {
        SetInvalidKeyError(error, "number", key);
        return false;
    }
    return true;
}

template <typename T>
bool LoadUnsigned(const DumpValues& values, const std::string& key, T& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    unsigned long long parsed = 0;
    if (!ParseStrictUnsigned(text, parsed) ||
        parsed > static_cast<unsigned long long>((std::numeric_limits<T>::max)())) {
        SetInvalidKeyError(error, "integer", key);
        return false;
    }
    field = static_cast<T>(parsed);
    return true;
}

bool LoadOptionalDouble(
    const DumpValues& values, const std::string& key, std::optional<double>& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    if (text == "null") {
        field = std::nullopt;
        return true;
    }
    double parsed = 0.0;
    if (!ParseStrictDouble(text, parsed)) {
        SetInvalidKeyError(error, "optional number", key);
        return false;
    }
    field = parsed;
    return true;
}

bool LoadScalarMetricUnit(
    const DumpValues& values, const std::string& key, ScalarMetricUnit& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    std::string parsed;
    if (!UnescapeQuotedString(text, parsed) || !ParseDumpScalarMetricUnit(parsed, field)) {
        SetInvalidKeyError(error, "scalar unit", key);
        return false;
    }
    return true;
}

bool LoadScalarMetricIssue(
    const DumpValues& values, const std::string& key, ScalarMetricIssue& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    std::string parsed;
    if (!UnescapeQuotedString(text, parsed) || !TryEnumFromString(parsed, field)) {
        SetInvalidKeyError(error, "scalar issue", key);
        return false;
    }
    return true;
}

bool LoadDoubleArrayField(
    const DumpValues& values, const std::string& key, std::vector<double>& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    if (!ParseDoubleArray(text, field)) {
        SetInvalidKeyError(error, "number array", key);
        return false;
    }
    return true;
}

bool LoadFlatDumpFields(const DumpValues& values, TelemetryDump& dump, size_t begin, size_t end, std::string* error) {
    end = (std::min)(end, kFlatDumpFieldCount);
    for (size_t i = begin; i < end; ++i) {
        const DumpFieldDescriptor& field = kFlatDumpFields[i];
        const std::string key(field.key);
        switch (field.kind) {
            case DumpFieldKind::String:
                if (!LoadString(values, key, DumpField<std::string>(dump, field), error)) {
                    return false;
                }
                break;
            case DumpFieldKind::Double:
                if (!LoadDouble(values, key, DumpField<double>(dump, field), error)) {
                    return false;
                }
                break;
            case DumpFieldKind::OptionalDouble:
                if (!LoadOptionalDouble(values, key, DumpField<std::optional<double>>(dump, field), error)) {
                    return false;
                }
                break;
            case DumpFieldKind::ScalarUnit:
                if (!LoadScalarMetricUnit(values, key, DumpField<ScalarMetricUnit>(dump, field), error)) {
                    return false;
                }
                break;
            case DumpFieldKind::ScalarIssue:
                if (!LoadScalarMetricIssue(values, key, DumpField<ScalarMetricIssue>(dump, field), error)) {
                    return false;
                }
                break;
            case DumpFieldKind::SystemTimeWord:
                if (!LoadUnsigned(values, key, DumpField<WORD>(dump, field), error)) {
                    return false;
                }
                break;
        }
    }
    return true;
}

bool LoadNamedScalarMetrics(
    const DumpValues& values, const std::string& prefix, std::vector<NamedScalarMetric>& field, std::string* error) {
    size_t count = 0;
    if (!LoadUnsigned(values, DumpKey(prefix, ".count"), count, error)) {
        return false;
    }

    field.clear();
    field.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        NamedScalarMetric metric;
        const std::string metricPrefix = FormatText("%s.%zu", prefix.c_str(), i);
        if (!LoadString(values, DumpKey(metricPrefix, ".name"), metric.name, error) ||
            !LoadOptionalDouble(values, DumpKey(metricPrefix, ".value"), metric.metric.value, error) ||
            !LoadScalarMetricUnit(values, DumpKey(metricPrefix, ".unit"), metric.metric.unit, error)) {
            return false;
        }
        field.push_back(std::move(metric));
    }
    return true;
}

bool LoadRetainedHistories(const DumpValues& values,
    const std::string& prefix,
    std::vector<RetainedHistorySeries>& field,
    std::string* error) {
    size_t count = 0;
    if (!LoadUnsigned(values, DumpKey(prefix, ".count"), count, error)) {
        return false;
    }

    field.clear();
    field.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RetainedHistorySeries history;
        const std::string historyPrefix = FormatText("%s.%zu", prefix.c_str(), i);
        if (!LoadString(values, DumpKey(historyPrefix, ".series_ref"), history.seriesRef, error) ||
            !LoadDoubleArrayField(values, DumpKey(historyPrefix, ".samples"), history.samples, error) ||
            !LoadDoubleArrayField(
                values, DumpKey(historyPrefix, ".throughput_live_samples"), history.throughputLiveSamples, error) ||
            !LoadDouble(
                values, DumpKey(historyPrefix, ".throughput_bucket_total"), history.throughputBucketTotal, error) ||
            !LoadUnsigned(values,
                DumpKey(historyPrefix, ".throughput_bucket_sample_count"),
                history.throughputBucketSampleCount,
                error)) {
            return false;
        }
        field.push_back(std::move(history));
    }
    return true;
}

}  // namespace

bool WriteTelemetryDumpText(std::string& output, const TelemetryDump& dump) {
    output.clear();
    WriteLine(output, "format", kDumpFormatVersion);

    WriteFlatDumpFields(output, dump, 0, 6);
    WriteNamedScalarMetrics(output, "board.temperatures", dump.snapshot.boardTemperatures);
    WriteNamedScalarMetrics(output, "board.fans", dump.snapshot.boardFans);
    WriteRetainedHistories(output, "retained_histories", dump.snapshot.retainedHistories);
    WriteFlatDumpFields(output, dump, 6, 26);

    WriteInteger(output, "drives.count", dump.snapshot.drives.size());
    for (size_t i = 0; i < dump.snapshot.drives.size(); ++i) {
        const std::string prefix = FormatText("drives.%zu", i);
        WriteString(output, DumpKey(prefix, ".label"), dump.snapshot.drives[i].label);
        WriteDouble(output, DumpKey(prefix, ".used_percent"), dump.snapshot.drives[i].usedPercent, 6);
        WriteDouble(output, DumpKey(prefix, ".free_gb"), dump.snapshot.drives[i].freeGb, 6);
        WriteDouble(output, DumpKey(prefix, ".read_mbps"), dump.snapshot.drives[i].readMbps, 6);
        WriteDouble(output, DumpKey(prefix, ".write_mbps"), dump.snapshot.drives[i].writeMbps, 6);
    }

    WriteFlatDumpFields(output, dump, 26, kFlatDumpFieldCount);
    return true;
}

bool WriteTelemetryDump(std::FILE* output, const TelemetryDump& dump) {
    std::string text;
    if (!WriteTelemetryDumpText(text, dump)) {
        return false;
    }
    return WriteDumpText(output, text) && fflush(output) == 0;
}

bool LoadTelemetryDump(std::string_view input, TelemetryDump& dump, std::string* error) {
    TelemetryDump parsed;

    DumpValues values;
    size_t lineNumber = 0;
    size_t lineStart = 0;
    while (lineStart <= input.size()) {
        size_t lineEnd = input.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = input.size();
        }
        std::string line(input.substr(lineStart, lineEnd - lineStart));
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string trimmed = TrimDumpWhitespace(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            if (lineEnd == input.size()) {
                break;
            }
            lineStart = lineEnd + 1;
            continue;
        }
        const size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            if (error != nullptr) {
                *error = FormatText("Missing '=' on line %zu", lineNumber);
            }
            return false;
        }
        SetDumpValue(
            values, TrimDumpWhitespace(trimmed.substr(0, equals)), TrimDumpWhitespace(trimmed.substr(equals + 1)));
        if (lineEnd == input.size()) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    std::string format;
    if (!TryGetValue(values, "format", format) || format != kDumpFormatVersion) {
        if (error != nullptr) {
            *error = "Unsupported or missing dump format.";
        }
        return false;
    }

    if (!LoadFlatDumpFields(values, parsed, 0, 6, error) ||
        !LoadNamedScalarMetrics(values, "board.temperatures", parsed.snapshot.boardTemperatures, error) ||
        !LoadNamedScalarMetrics(values, "board.fans", parsed.snapshot.boardFans, error) ||
        !LoadRetainedHistories(values, "retained_histories", parsed.snapshot.retainedHistories, error) ||
        !LoadFlatDumpFields(values, parsed, 6, 26, error)) {
        return false;
    }

    size_t driveCount = 0;
    if (!LoadUnsigned(values, "drives.count", driveCount, error)) {
        return false;
    }
    parsed.snapshot.drives.clear();
    parsed.snapshot.drives.reserve(driveCount);
    for (size_t i = 0; i < driveCount; ++i) {
        DriveInfo drive;
        const std::string prefix = FormatText("drives.%zu", i);
        if (!LoadString(values, DumpKey(prefix, ".label"), drive.label, error) ||
            !LoadDouble(values, DumpKey(prefix, ".used_percent"), drive.usedPercent, error) ||
            !LoadDouble(values, DumpKey(prefix, ".free_gb"), drive.freeGb, error) ||
            !LoadDouble(values, DumpKey(prefix, ".read_mbps"), drive.readMbps, error) ||
            !LoadDouble(values, DumpKey(prefix, ".write_mbps"), drive.writeMbps, error)) {
            return false;
        }
        parsed.snapshot.drives.push_back(std::move(drive));
    }

    if (!LoadFlatDumpFields(values, parsed, 26, kFlatDumpFieldCount, error)) {
        return false;
    }

    dump = std::move(parsed);
    return true;
}
