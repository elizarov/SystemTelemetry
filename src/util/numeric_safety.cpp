#include "util/numeric_safety.h"

#include <algorithm>
#include <cmath>

bool IsFiniteDouble(double value) {
    return std::isfinite(value);
}

double FiniteOr(double value, double fallback) {
    return IsFiniteDouble(value) ? value : fallback;
}

double FiniteNonNegativeOr(double value, double fallback) {
    const double safeFallback = IsFiniteDouble(fallback) ? fallback : 0.0;
    return IsFiniteDouble(value) && value >= 0.0 ? value : (std::max)(0.0, safeFallback);
}

double ClampFinite(double value, double minimum, double maximum, double fallback) {
    const double safeFallback = std::clamp(FiniteOr(fallback, minimum), minimum, maximum);
    if (!IsFiniteDouble(value)) {
        return safeFallback;
    }
    return std::clamp(value, minimum, maximum);
}

std::optional<double> FiniteOptional(std::optional<double> value) {
    if (!value.has_value() || !IsFiniteDouble(*value)) {
        return std::nullopt;
    }
    return value;
}
