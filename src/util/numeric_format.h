#pragma once

#include <string>

std::string FormatDoubleGeneral(double value, int precision = 6);
std::string FormatDoubleFixed(double value, int precision);
std::string FormatDoubleFixedTrimmed(double value, int precision);
