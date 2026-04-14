#pragma once

#include <optional>

bool IsFiniteDouble(double value);
double FiniteOr(double value, double fallback);
double FiniteNonNegativeOr(double value, double fallback = 0.0);
double ClampFinite(double value, double minimum, double maximum, double fallback = 0.0);
std::optional<double> FiniteOptional(std::optional<double> value);
