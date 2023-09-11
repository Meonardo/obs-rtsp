#pragma once

#include <string>
#include <tuple>

namespace utils {
std::tuple<std::string, std::string, std::string> ExtractRtspUrl(const std::string& url);

namespace string {
std::string ToLower(const std::string& source);
std::string ToUpper(const std::string& source);
} // namespace string

} // namespace utils
