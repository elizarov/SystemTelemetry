#include "snapshot_dump.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <string_view>

namespace {

constexpr char kDumpFormatVersion[] = "system_telemetry_snapshot_v3";

std::string TrimAsciiWhitespace(const std::string& value) {
    const size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string EscapeString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
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

void WriteLine(std::ostream& output, const std::string& key, const std::string& value) {
    output << key << '=' << value << '\n';
}

void WriteString(std::ostream& output, const std::string& key, const std::string& value) {
    WriteLine(output, key, '"' + EscapeString(value) + '"');
}

void WriteBool(std::ostream& output, const std::string& key, bool value) {
    WriteLine(output, key, value ? "true" : "false");
}

void WriteDouble(std::ostream& output, const std::string& key, double value, int precision = 6) {
    char buffer[64];
    sprintf_s(buffer, "%.*f", precision, value);
    WriteLine(output, key, buffer);
}

template <typename T>
void WriteInteger(std::ostream& output, const std::string& key, T value) {
    WriteLine(output, key, std::to_string(static_cast<long long>(value)));
}

void WriteOptionalDouble(std::ostream& output, const std::string& key, const std::optional<double>& value, int precision = 6) {
    if (!value.has_value()) {
        WriteLine(output, key, "null");
        return;
    }
    WriteDouble(output, key, *value, precision);
}

template <typename T>
void WriteOptionalInteger(std::ostream& output, const std::string& key, const std::optional<T>& value) {
    if (!value.has_value()) {
        WriteLine(output, key, "null");
        return;
    }
    WriteInteger(output, key, *value);
}

void WriteDoubleArray(std::ostream& output, const std::string& key, const std::vector<double>& values) {
    output << key << "=[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            output << ',';
        }
        char buffer[64];
        sprintf_s(buffer, "%.6f", values[i]);
        output << buffer;
    }
    output << "]\n";
}

void WriteStringList(std::ostream& output, const std::string& prefix, const std::vector<std::string>& values) {
    WriteInteger(output, prefix + ".count", values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        WriteString(output, prefix + "." + std::to_string(i) + ".name", values[i]);
    }
}

void WriteNamedScalarMetrics(std::ostream& output, const std::string& prefix, const std::vector<NamedScalarMetric>& metrics) {
    WriteInteger(output, prefix + ".count", metrics.size());
    for (size_t i = 0; i < metrics.size(); ++i) {
        const std::string metricPrefix = prefix + "." + std::to_string(i);
        WriteString(output, metricPrefix + ".name", metrics[i].name);
        WriteOptionalDouble(output, metricPrefix + ".value", metrics[i].metric.value, 6);
        WriteString(output, metricPrefix + ".unit", metrics[i].metric.unit);
    }
}

void WriteMetricHistories(std::ostream& output, const std::string& prefix, const std::vector<MetricHistorySeries>& histories) {
    WriteInteger(output, prefix + ".count", histories.size());
    for (size_t i = 0; i < histories.size(); ++i) {
        const std::string historyPrefix = prefix + "." + std::to_string(i);
        WriteString(output, historyPrefix + ".metric_ref", histories[i].metricRef);
        WriteDoubleArray(output, historyPrefix + ".samples", histories[i].samples);
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

bool ParseBool(const std::string& text, bool& value) {
    if (text == "true") {
        value = true;
        return true;
    }
    if (text == "false") {
        value = false;
        return true;
    }
    return false;
}

bool ParseDoubleArray(const std::string& text, std::vector<double>& values) {
    if (text.size() < 2 || text.front() != '[' || text.back() != ']') {
        return false;
    }
    values.clear();
    const std::string body = TrimAsciiWhitespace(text.substr(1, text.size() - 2));
    if (body.empty()) {
        return true;
    }

    size_t start = 0;
    while (start < body.size()) {
        size_t comma = body.find(',', start);
        std::string token = TrimAsciiWhitespace(body.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
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

bool TryGetValue(const std::map<std::string, std::string>& values, const std::string& key, std::string& value) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return false;
    }
    value = it->second;
    return true;
}

bool LoadString(const std::map<std::string, std::string>& values, const std::string& key, std::string& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    if (!UnescapeQuotedString(text, field)) {
        if (error != nullptr) {
            *error = "Invalid quoted string for key: " + key;
        }
        return false;
    }
    return true;
}

bool LoadBool(const std::map<std::string, std::string>& values, const std::string& key, bool& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    if (!ParseBool(text, field)) {
        if (error != nullptr) {
            *error = "Invalid boolean for key: " + key;
        }
        return false;
    }
    return true;
}

bool LoadDouble(const std::map<std::string, std::string>& values, const std::string& key, double& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    if (!ParseStrictDouble(text, field)) {
        if (error != nullptr) {
            *error = "Invalid number for key: " + key;
        }
        return false;
    }
    return true;
}

template <typename T>
bool LoadUnsigned(const std::map<std::string, std::string>& values, const std::string& key, T& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    unsigned long long parsed = 0;
    if (!ParseStrictUnsigned(text, parsed) || parsed > static_cast<unsigned long long>((std::numeric_limits<T>::max)())) {
        if (error != nullptr) {
            *error = "Invalid integer for key: " + key;
        }
        return false;
    }
    field = static_cast<T>(parsed);
    return true;
}

bool LoadOptionalDouble(const std::map<std::string, std::string>& values, const std::string& key,
    std::optional<double>& field, std::string* error) {
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
        if (error != nullptr) {
            *error = "Invalid optional number for key: " + key;
        }
        return false;
    }
    field = parsed;
    return true;
}

template <typename T>
bool LoadOptionalUnsigned(const std::map<std::string, std::string>& values, const std::string& key,
    std::optional<T>& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    if (text == "null") {
        field = std::nullopt;
        return true;
    }
    unsigned long long parsed = 0;
    if (!ParseStrictUnsigned(text, parsed) || parsed > static_cast<unsigned long long>((std::numeric_limits<T>::max)())) {
        if (error != nullptr) {
            *error = "Invalid optional integer for key: " + key;
        }
        return false;
    }
    field = static_cast<T>(parsed);
    return true;
}

bool LoadDoubleArrayField(const std::map<std::string, std::string>& values, const std::string& key,
    std::vector<double>& field, std::string* error) {
    std::string text;
    if (!TryGetValue(values, key, text)) {
        return true;
    }
    if (!ParseDoubleArray(text, field)) {
        if (error != nullptr) {
            *error = "Invalid number array for key: " + key;
        }
        return false;
    }
    return true;
}

bool LoadStringList(const std::map<std::string, std::string>& values, const std::string& prefix,
    std::vector<std::string>& field, std::string* error) {
    size_t count = 0;
    if (!LoadUnsigned(values, prefix + ".count", count, error)) {
        return false;
    }

    field.clear();
    field.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        std::string value;
        if (!LoadString(values, prefix + "." + std::to_string(i) + ".name", value, error)) {
            return false;
        }
        field.push_back(std::move(value));
    }
    return true;
}

bool LoadNamedScalarMetrics(const std::map<std::string, std::string>& values, const std::string& prefix,
    std::vector<NamedScalarMetric>& field, std::string* error) {
    size_t count = 0;
    if (!LoadUnsigned(values, prefix + ".count", count, error)) {
        return false;
    }

    field.clear();
    field.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        NamedScalarMetric metric;
        const std::string metricPrefix = prefix + "." + std::to_string(i);
        if (!LoadString(values, metricPrefix + ".name", metric.name, error) ||
            !LoadOptionalDouble(values, metricPrefix + ".value", metric.metric.value, error) ||
            !LoadString(values, metricPrefix + ".unit", metric.metric.unit, error)) {
            return false;
        }
        field.push_back(std::move(metric));
    }
    return true;
}

bool LoadMetricHistories(const std::map<std::string, std::string>& values, const std::string& prefix,
    std::vector<MetricHistorySeries>& field, std::string* error) {
    size_t count = 0;
    if (!LoadUnsigned(values, prefix + ".count", count, error)) {
        return false;
    }

    field.clear();
    field.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        MetricHistorySeries history;
        const std::string historyPrefix = prefix + "." + std::to_string(i);
        if (!LoadString(values, historyPrefix + ".metric_ref", history.metricRef, error) ||
            !LoadDoubleArrayField(values, historyPrefix + ".samples", history.samples, error)) {
            return false;
        }
        field.push_back(std::move(history));
    }
    return true;
}

}  // namespace

bool WriteTelemetryDump(std::ostream& output, const TelemetryDump& dump) {
    WriteLine(output, "format", kDumpFormatVersion);

    WriteString(output, "cpu.name", dump.snapshot.cpu.name);
    WriteDouble(output, "cpu.load_percent", dump.snapshot.cpu.loadPercent, 6);
    WriteOptionalDouble(output, "cpu.clock.value", dump.snapshot.cpu.clock.value, 6);
    WriteString(output, "cpu.clock.unit", dump.snapshot.cpu.clock.unit);
    WriteDouble(output, "cpu.memory.used_gb", dump.snapshot.cpu.memory.usedGb, 6);
    WriteDouble(output, "cpu.memory.total_gb", dump.snapshot.cpu.memory.totalGb, 6);
    WriteNamedScalarMetrics(output, "board.temperatures", dump.snapshot.boardTemperatures);
    WriteNamedScalarMetrics(output, "board.fans", dump.snapshot.boardFans);
    WriteMetricHistories(output, "metric_histories", dump.snapshot.metricHistories);

    WriteString(output, "gpu.name", dump.snapshot.gpu.name);
    WriteDouble(output, "gpu.load_percent", dump.snapshot.gpu.loadPercent, 6);
    WriteOptionalDouble(output, "gpu.temperature.value", dump.snapshot.gpu.temperature.value, 6);
    WriteString(output, "gpu.temperature.unit", dump.snapshot.gpu.temperature.unit);
    WriteOptionalDouble(output, "gpu.clock.value", dump.snapshot.gpu.clock.value, 6);
    WriteString(output, "gpu.clock.unit", dump.snapshot.gpu.clock.unit);
    WriteOptionalDouble(output, "gpu.fan.value", dump.snapshot.gpu.fan.value, 6);
    WriteString(output, "gpu.fan.unit", dump.snapshot.gpu.fan.unit);
    WriteDouble(output, "gpu.vram.used_gb", dump.snapshot.gpu.vram.usedGb, 6);
    WriteDouble(output, "gpu.vram.total_gb", dump.snapshot.gpu.vram.totalGb, 6);

    WriteString(output, "gpu_provider.name", dump.gpuProvider.providerName);
    WriteString(output, "gpu_provider.diagnostics", dump.gpuProvider.diagnostics);
    WriteBool(output, "gpu_provider.available", dump.gpuProvider.available);

    WriteString(output, "board_provider.name", dump.boardProvider.providerName);
    WriteString(output, "board_provider.diagnostics", dump.boardProvider.diagnostics);
    WriteBool(output, "board_provider.available", dump.boardProvider.available);
    WriteString(output, "board_provider.board_manufacturer", dump.boardProvider.boardManufacturer);
    WriteString(output, "board_provider.board_product", dump.boardProvider.boardProduct);
    WriteString(output, "board_provider.chip_name", dump.boardProvider.chipName);
    WriteString(output, "board_provider.controller_type", dump.boardProvider.controllerType);
    WriteString(output, "board_provider.driver_library", dump.boardProvider.driverLibrary);
    WriteStringList(output, "board_provider.requested_fans", dump.boardProvider.requestedFanNames);
    WriteStringList(output, "board_provider.requested_temperatures", dump.boardProvider.requestedTemperatureNames);
    WriteNamedScalarMetrics(output, "board_provider.fans", dump.boardProvider.fans);
    WriteNamedScalarMetrics(output, "board_provider.temperatures", dump.boardProvider.temperatures);

    WriteString(output, "network.adapter_name", dump.snapshot.network.adapterName);
    WriteDouble(output, "network.upload_mbps", dump.snapshot.network.uploadMbps, 6);
    WriteDouble(output, "network.download_mbps", dump.snapshot.network.downloadMbps, 6);
    WriteString(output, "network.ip_address", dump.snapshot.network.ipAddress);
    WriteDoubleArray(output, "network.upload_history", dump.snapshot.network.uploadHistory);
    WriteDoubleArray(output, "network.download_history", dump.snapshot.network.downloadHistory);

    WriteDouble(output, "storage.read_mbps", dump.snapshot.storage.readMbps, 6);
    WriteDouble(output, "storage.write_mbps", dump.snapshot.storage.writeMbps, 6);
    WriteDoubleArray(output, "storage.read_history", dump.snapshot.storage.readHistory);
    WriteDoubleArray(output, "storage.write_history", dump.snapshot.storage.writeHistory);

    WriteInteger(output, "drives.count", dump.snapshot.drives.size());
    for (size_t i = 0; i < dump.snapshot.drives.size(); ++i) {
        const std::string prefix = "drives." + std::to_string(i);
        WriteString(output, prefix + ".label", dump.snapshot.drives[i].label);
        WriteDouble(output, prefix + ".used_percent", dump.snapshot.drives[i].usedPercent, 6);
        WriteDouble(output, prefix + ".free_gb", dump.snapshot.drives[i].freeGb, 6);
    }

    WriteInteger(output, "time.year", dump.snapshot.now.wYear);
    WriteInteger(output, "time.month", dump.snapshot.now.wMonth);
    WriteInteger(output, "time.day", dump.snapshot.now.wDay);
    WriteInteger(output, "time.hour", dump.snapshot.now.wHour);
    WriteInteger(output, "time.minute", dump.snapshot.now.wMinute);
    WriteInteger(output, "time.second", dump.snapshot.now.wSecond);
    WriteInteger(output, "time.milliseconds", dump.snapshot.now.wMilliseconds);
    output.flush();
    return output.good();
}

bool LoadTelemetryDump(std::istream& input, TelemetryDump& dump, std::string* error) {
    TelemetryDump parsed;

    std::map<std::string, std::string> values;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string trimmed = TrimAsciiWhitespace(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            if (error != nullptr) {
                *error = "Missing '=' on line " + std::to_string(lineNumber);
            }
            return false;
        }
        values[TrimAsciiWhitespace(trimmed.substr(0, equals))] = TrimAsciiWhitespace(trimmed.substr(equals + 1));
    }

    std::string format;
    if (!TryGetValue(values, "format", format) || format != kDumpFormatVersion) {
        if (error != nullptr) {
            *error = "Unsupported or missing dump format.";
        }
        return false;
    }

    if (!LoadString(values, "cpu.name", parsed.snapshot.cpu.name, error) ||
        !LoadDouble(values, "cpu.load_percent", parsed.snapshot.cpu.loadPercent, error) ||
        !LoadOptionalDouble(values, "cpu.clock.value", parsed.snapshot.cpu.clock.value, error) ||
        !LoadString(values, "cpu.clock.unit", parsed.snapshot.cpu.clock.unit, error) ||
        !LoadDouble(values, "cpu.memory.used_gb", parsed.snapshot.cpu.memory.usedGb, error) ||
        !LoadDouble(values, "cpu.memory.total_gb", parsed.snapshot.cpu.memory.totalGb, error) ||
        !LoadNamedScalarMetrics(values, "board.temperatures", parsed.snapshot.boardTemperatures, error) ||
        !LoadNamedScalarMetrics(values, "board.fans", parsed.snapshot.boardFans, error) ||
        !LoadMetricHistories(values, "metric_histories", parsed.snapshot.metricHistories, error) ||
        !LoadString(values, "gpu.name", parsed.snapshot.gpu.name, error) ||
        !LoadDouble(values, "gpu.load_percent", parsed.snapshot.gpu.loadPercent, error) ||
        !LoadOptionalDouble(values, "gpu.temperature.value", parsed.snapshot.gpu.temperature.value, error) ||
        !LoadString(values, "gpu.temperature.unit", parsed.snapshot.gpu.temperature.unit, error) ||
        !LoadOptionalDouble(values, "gpu.clock.value", parsed.snapshot.gpu.clock.value, error) ||
        !LoadString(values, "gpu.clock.unit", parsed.snapshot.gpu.clock.unit, error) ||
        !LoadOptionalDouble(values, "gpu.fan.value", parsed.snapshot.gpu.fan.value, error) ||
        !LoadString(values, "gpu.fan.unit", parsed.snapshot.gpu.fan.unit, error) ||
        !LoadDouble(values, "gpu.vram.used_gb", parsed.snapshot.gpu.vram.usedGb, error) ||
        !LoadDouble(values, "gpu.vram.total_gb", parsed.snapshot.gpu.vram.totalGb, error) ||
        !LoadString(values, "gpu_provider.name", parsed.gpuProvider.providerName, error) ||
        !LoadString(values, "gpu_provider.diagnostics", parsed.gpuProvider.diagnostics, error) ||
        !LoadBool(values, "gpu_provider.available", parsed.gpuProvider.available, error) ||
        !LoadString(values, "board_provider.name", parsed.boardProvider.providerName, error) ||
        !LoadString(values, "board_provider.diagnostics", parsed.boardProvider.diagnostics, error) ||
        !LoadBool(values, "board_provider.available", parsed.boardProvider.available, error) ||
        !LoadString(values, "board_provider.board_manufacturer", parsed.boardProvider.boardManufacturer, error) ||
        !LoadString(values, "board_provider.board_product", parsed.boardProvider.boardProduct, error) ||
        !LoadString(values, "board_provider.chip_name", parsed.boardProvider.chipName, error) ||
        !LoadString(values, "board_provider.controller_type", parsed.boardProvider.controllerType, error) ||
        !LoadString(values, "board_provider.driver_library", parsed.boardProvider.driverLibrary, error) ||
        !LoadStringList(values, "board_provider.requested_fans", parsed.boardProvider.requestedFanNames, error) ||
        !LoadStringList(values, "board_provider.requested_temperatures", parsed.boardProvider.requestedTemperatureNames, error) ||
        !LoadNamedScalarMetrics(values, "board_provider.fans", parsed.boardProvider.fans, error) ||
        !LoadNamedScalarMetrics(values, "board_provider.temperatures", parsed.boardProvider.temperatures, error) ||
        !LoadString(values, "network.adapter_name", parsed.snapshot.network.adapterName, error) ||
        !LoadDouble(values, "network.upload_mbps", parsed.snapshot.network.uploadMbps, error) ||
        !LoadDouble(values, "network.download_mbps", parsed.snapshot.network.downloadMbps, error) ||
        !LoadString(values, "network.ip_address", parsed.snapshot.network.ipAddress, error) ||
        !LoadDoubleArrayField(values, "network.upload_history", parsed.snapshot.network.uploadHistory, error) ||
        !LoadDoubleArrayField(values, "network.download_history", parsed.snapshot.network.downloadHistory, error) ||
        !LoadDouble(values, "storage.read_mbps", parsed.snapshot.storage.readMbps, error) ||
        !LoadDouble(values, "storage.write_mbps", parsed.snapshot.storage.writeMbps, error) ||
        !LoadDoubleArrayField(values, "storage.read_history", parsed.snapshot.storage.readHistory, error) ||
        !LoadDoubleArrayField(values, "storage.write_history", parsed.snapshot.storage.writeHistory, error)) {
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
        const std::string prefix = "drives." + std::to_string(i);
        if (!LoadString(values, prefix + ".label", drive.label, error) ||
            !LoadDouble(values, prefix + ".used_percent", drive.usedPercent, error) ||
            !LoadDouble(values, prefix + ".free_gb", drive.freeGb, error)) {
            return false;
        }
        parsed.snapshot.drives.push_back(std::move(drive));
    }

    if (!LoadUnsigned(values, "time.year", parsed.snapshot.now.wYear, error) ||
        !LoadUnsigned(values, "time.month", parsed.snapshot.now.wMonth, error) ||
        !LoadUnsigned(values, "time.day", parsed.snapshot.now.wDay, error) ||
        !LoadUnsigned(values, "time.hour", parsed.snapshot.now.wHour, error) ||
        !LoadUnsigned(values, "time.minute", parsed.snapshot.now.wMinute, error) ||
        !LoadUnsigned(values, "time.second", parsed.snapshot.now.wSecond, error) ||
        !LoadUnsigned(values, "time.milliseconds", parsed.snapshot.now.wMilliseconds, error)) {
        return false;
    }

    dump = std::move(parsed);
    return true;
}
