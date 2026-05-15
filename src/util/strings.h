#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

std::string ToLower(std::string value);
std::string Trim(std::string_view value);
std::vector<std::string> SplitTrimmed(std::string_view value, char delimiter);
std::vector<std::string> SplitTrimmedPreservingEmpty(std::string_view value, char delimiter);
std::string CollapseAsciiWhitespace(std::string_view value);
bool ContainsInsensitive(const std::string& value, const std::string& needle);
bool EqualsInsensitive(const std::string& left, const std::string& right);
std::string JoinNames(const std::vector<std::string>& names);
size_t StableStringHash(std::string_view value);
void SortStrings(std::vector<std::string>& values);
void SortUniqueStrings(std::vector<std::string>& values);
