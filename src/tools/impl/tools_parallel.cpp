#include "tools/impl/tools_parallel.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <system_error>

bool
    ParseToolConcurrency(std::string_view value, size_t& concurrency, std::string& error, std::string_view optionName)
{
    if (value.empty()) {
        error = std::string(optionName) + " requires a value";
        return false;
    }

    unsigned long long parsed = 0;
    const char* first = value.data();
    const char* last = value.data() + value.size();
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    const bool outOfRange = parsed > static_cast<unsigned long long>(std::numeric_limits<size_t>::max());
    if (result.ec != std::errc() || result.ptr != last || parsed == 0 || outOfRange) {
        error = std::string(optionName) + " requires a positive integer";
        return false;
    }
    concurrency = static_cast<size_t>(parsed);
    return true;
}

size_t DefaultToolConcurrency() {
    const unsigned int hardware = std::thread::hardware_concurrency();
    return hardware == 0 ? 4 : static_cast<size_t>(hardware);
}

size_t EffectiveToolConcurrency(size_t workSize, size_t requestedConcurrency) {
    if (workSize == 0) {
        return 0;
    }
    const size_t concurrency =
        requestedConcurrency == kToolAutoConcurrency ? DefaultToolConcurrency() : requestedConcurrency;
    return std::min(workSize, std::max<size_t>(1, concurrency));
}
