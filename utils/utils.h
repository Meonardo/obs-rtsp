#pragma once

#include <string>
#include <tuple>
#include <vector>

namespace utils {
std::tuple<std::string, std::string, std::string> ExtractRtspUrl(const std::string& url);

namespace string {
std::string ToLower(const std::string source);
std::string ToUpper(const std::string source);
void SeperateStringBy(char token, std::string source, std::vector<std::string>& result);
} // namespace string

} // namespace utils
