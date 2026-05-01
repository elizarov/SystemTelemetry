#include "config/color_expression.h"

#include <cmath>
#include <limits>
#include <sstream>
#include <vector>

#include "util/strings.h"

namespace {

double ParseDoubleOrDefault(const std::string& value, double fallback) {
    try {
        size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        return consumed == value.size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

std::string FormatDouble(double value) {
    std::ostringstream stream;
    stream.precision(12);
    stream << value;
    std::string text = stream.str();
    if (const size_t dot = text.find('.'); dot != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

std::string FormatAlphaByte(unsigned int alpha) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string text = "0x00";
    text[2] = kHex[(alpha >> 4) & 0x0Fu];
    text[3] = kHex[alpha & 0x0Fu];
    return text;
}

}  // namespace

std::optional<unsigned int> ParseColorExpressionAlphaByte(const std::string& text) {
    const std::string value = Trim(text);
    try {
        size_t consumed = 0;
        const int base = value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0 ? 16 : 10;
        const unsigned long parsed = std::stoul(value, &consumed, base);
        if (consumed == value.size() && parsed <= 0xFFul) {
            return static_cast<unsigned int>(parsed);
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<ColorExpression> ParseColorExpression(const std::string& text) {
    const std::string source = Trim(text);
    const size_t open = source.find('(');
    if (open == std::string::npos) {
        if (source.empty()) {
            return std::nullopt;
        }
        return ColorExpression{source};
    }
    if (source.empty() || source.back() != ')') {
        return std::nullopt;
    }

    ColorExpression parsed;
    parsed.base = Trim(source.substr(0, open));
    if (parsed.base.empty()) {
        return std::nullopt;
    }

    const std::vector<std::string> options = SplitTrimmed(source.substr(open + 1, source.size() - open - 2), ',');
    for (const std::string& option : options) {
        const size_t colon = option.find(':');
        if (colon == std::string::npos) {
            return std::nullopt;
        }
        const std::string name = Trim(option.substr(0, colon));
        const std::string value = Trim(option.substr(colon + 1));
        if (name == "rotate_hue") {
            parsed.rotateHue = ParseDoubleOrDefault(value, std::numeric_limits<double>::quiet_NaN());
            if (!std::isfinite(*parsed.rotateHue)) {
                return std::nullopt;
            }
        } else if (name == "mix") {
            const std::vector<std::string> parts = SplitTrimmed(value, ' ');
            if (parts.size() != 2) {
                return std::nullopt;
            }
            const double amount = ParseDoubleOrDefault(parts[1], std::numeric_limits<double>::quiet_NaN());
            if (!std::isfinite(amount) || amount < 0.0 || amount > 1.0) {
                return std::nullopt;
            }
            parsed.mix = ColorMixExpression{parts[0], amount};
        } else if (name == "alpha") {
            parsed.alpha = ParseColorExpressionAlphaByte(value);
            if (!parsed.alpha.has_value()) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    return parsed;
}

std::string FormatColorExpression(const ColorExpression& expression) {
    std::vector<std::string> options;
    if (expression.rotateHue.has_value()) {
        options.push_back("rotate_hue: " + FormatDouble(*expression.rotateHue));
    }
    if (expression.mix.has_value()) {
        options.push_back("mix: " + expression.mix->target + " " + FormatDouble(expression.mix->amount));
    }
    if (expression.alpha.has_value()) {
        options.push_back("alpha: " + FormatAlphaByte(*expression.alpha));
    }
    if (options.empty()) {
        return expression.base;
    }

    std::string text = expression.base + "(";
    for (size_t i = 0; i < options.size(); ++i) {
        if (i > 0) {
            text += ", ";
        }
        text += options[i];
    }
    text += ")";
    return text;
}
