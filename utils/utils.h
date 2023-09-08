#pragma once

#include <string>
#include <tuple>

namespace utils {
std::tuple<std::string, std::string, std::string> ExtractRtspUrl(const std::string& url);
}
