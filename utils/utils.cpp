#include "utils.h"

#include <regex>

namespace utils {
std::tuple<std::string, std::string, std::string> ExtractRtspUrl(const std::string& url) {
	std::regex pattern(
	  "^(rtsp|rtsps):\\/\\/(?:([a-zA-Z0-9]+):([a-zA-Z0-9]+)@)?([a-zA-Z0-9.-]+)(?::([0-9]+))?\\/(.+)$");
	std::smatch matches;
	if (std::regex_search(url, matches, pattern)) {
		std::string rtspUri = matches[1].str() + "://" + matches[4].str();
		if (matches[5].matched) {
			rtspUri += ":" + matches[5].str();
		}
		rtspUri += "/" + matches[6].str();
		if (matches[2].matched && matches[3].matched) {
			return std::make_tuple(matches[2].str(), matches[3].str(), rtspUri);
		} else {
			return std::make_tuple("", "", rtspUri);
		}
	}
	return std::make_tuple("", "", "");
}
} // namespace utils
