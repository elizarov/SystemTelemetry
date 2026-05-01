#pragma once

#include <optional>
#include <string>

struct ColorMixExpression {
    std::string target;
    double amount = 0.0;

    bool operator==(const ColorMixExpression& other) const = default;
};

struct ColorExpression {
    std::string base;
    std::optional<double> rotateHue;
    std::optional<ColorMixExpression> mix;
    std::optional<unsigned int> alpha;

    bool operator==(const ColorExpression& other) const = default;
};

std::optional<unsigned int> ParseColorExpressionAlphaByte(const std::string& text);
std::optional<ColorExpression> ParseColorExpression(const std::string& text);
std::string FormatColorExpression(const ColorExpression& expression);
