#include "rtsp_client.h"

#include <iostream>

#include "Base64.hh"
#include "utils/h264/h264_common.h"
#include "utils/h265/h265_common.h"
#include "utils/utils.h"

#include <util/threading.h>

namespace source {
RtspClient::RtspClient(const std::string& uri, const std::map<std::string, std::string>& opts,
		       RTSPClientObserver* observer)
  : observer_(observer),
    env_(nullptr),
    client_(nullptr),
    uri_(uri),
    opts_(opts) {
	Start();
}

RtspClient::~RtspClient() {
	Stop();
}

bool RtspClient::IsRunning() {
	return (client_ != nullptr);
}

uint32_t RtspClient::GetWidth() const {
	return width_;
}

uint32_t RtspClient::GetHeight() const {
	return height_;
}

void RtspClient::CaptureThread() {
  os_set_thread_name("rtsp_capture_thread");
	env_->mainloop();
}

void RtspClient::Start() {
	if (client_ != nullptr) {
		return;
	}

	env_ = new Environment;
	client_ = new RTSPConnection(*env_, this, uri_.c_str(), opts_, 2);
	capture_thread_ = std::thread(&RtspClient::CaptureThread, this);

	blog(LOG_INFO, "RTSP client started");
}

void RtspClient::Stop() {
	if (env_ != nullptr) {
		env_->stop();
	}
	capture_thread_.join();

	if (client_ != nullptr) {
		delete client_;
		client_ = nullptr;
	}
	if (env_ != nullptr) {
		delete env_;
		env_ = nullptr;
	}
	blog(LOG_INFO, "RTSP client stopped");
}

bool RtspClient::onNewSession(const char* id, const char* media, const char* codec,
			      const char* sdp) {
	blog(LOG_INFO, "New session created: id: %s, media: %s, codec: %s, sdp: %s", id, media,
	     codec, sdp);

	media_ids_[id] = media;
	bool video = strcmp(media, "video") == 0;
	bool audio = strcmp(media, "audio") == 0;
	if (video) {
		// try to retrieve video resolution from sdp (useless!)
		if (strcmp(codec, "h264") == 0 || strcmp(codec, "H264") == 0) {
			auto sps_base64 = client_->getFmtpSpropParametersSets();
			blog(LOG_INFO, "sps in base64: %s", sps_base64);
			if (strlen(sps_base64)) {
				unsigned result_size = 0;
				unsigned char* sps = base64Decode(sps_base64, result_size, true);

				std::vector<uint8_t> sps_nalu_data(sps, sps + result_size);
				auto sps_nalu = utils::h264::ParseSps(sps_nalu_data);
				if (sps_nalu.has_value()) {
					width_ = sps_nalu->width;
					height_ = sps_nalu->height;
				} else {
					blog(LOG_ERROR, "Can not parse video resolution info");
				}
			}
		} else if (strcmp(codec, "h265") == 0 || strcmp(codec, "H265") == 0) {
			auto sps_base64 = client_->getFmtpSpropsps();
			blog(LOG_INFO, "sps in base64: %s", sps_base64);
			if (strlen(sps_base64)) {
				unsigned result_size = 0;
				unsigned char* sps = base64Decode(sps_base64, result_size, true);

				std::vector<uint8_t> sps_nalu_data(sps, sps + result_size);
				auto sps_nalu = utils::h265::ParseSps(sps_nalu_data);
				if (sps_nalu.has_value()) {
					width_ = sps_nalu->width;
					height_ = sps_nalu->height;
				} else {
					blog(LOG_ERROR, "Can not parse video resolution info");
				}
			}
		}

		return observer_->OnVideoSessionStarted(codec, width_, height_);
	}
	if (audio) {
		// parse sdp to extract freq and channel
		std::string codec_name = utils::string::ToLower(codec);
		auto fmt = utils::string::ToLower(sdp);
		size_t pos = fmt.find(codec_name);

		int rate = 0;
		int channels = 2;

		if (pos != std::string::npos) {
			fmt.erase(0, pos + strlen(codec));
			fmt.erase(fmt.find_first_of(" \r\n"));
			std::vector<std::string> results;
			utils::string::SeperateStringBy('/', fmt, results);
			if (results.size() > 2) {
				rate = std::stoi(results[1]);
				channels = std::stoi(results[2]);
			}
		}

		return observer_->OnAudioSessionStarted(codec, rate, channels);
	}

	// any other session is not support
	blog(LOG_ERROR, "not a/v stream, do not support it!");
	return false;
}

bool RtspClient::onData(const char* id, unsigned char* buffer, ssize_t size,
			struct timeval presentationTime) {
	ProcessBuffer(id, buffer, size, presentationTime);
	return true;
}

void RtspClient::onError(RTSPConnection& connection, const char* message) {
	blog(LOG_ERROR, "RTSP client error : %s", message);
	observer_->OnError(message);
}

void RtspClient::onConnectionTimeout(RTSPConnection& connection) {
	blog(LOG_INFO, "RTSP client connect timeout");
	observer_->OnSessionStopped("timeout");
}

void RtspClient::onDataTimeout(RTSPConnection& connection) {
	blog(LOG_INFO, "RTSP client data timeout");
	observer_->OnSessionStopped("timeout");
}

void RtspClient::ProcessBuffer(const char* id, unsigned char* buffer, ssize_t size,
			       timeval presentationTime) {
	std::string& media = media_ids_[id];
	bool video = media == "video";
	observer_->OnData(buffer, size, presentationTime, video);

	//std::vector<utils::h264::NaluIndex> indexes = utils::h264::FindNaluIndices(buffer, size);

	//for (const utils::h264::NaluIndex& index : indexes) {
	//	utils::h264::NaluType nalu_type =
	//	  utils::h264::ParseNaluType(buffer[index.payload_start_offset]);
	//	if (nalu_type == utils::h264::NaluType::kSps) {
	//		// blog(LOG_INFO, "SPS NALU");

	//		cfg_.clear();
	//		cfg_.insert(cfg_.end(), buffer + index.start_offset,
	//			    buffer + index.payload_size + index.payload_start_offset -
	//			      index.start_offset);

	//	} else if (nalu_type == utils::h264::NaluType::kPps) {
	//		// blog(LOG_INFO, "PPS NALU");

	//		cfg_.insert(cfg_.end(), buffer + index.start_offset,
	//			    buffer + index.payload_size + index.payload_start_offset -
	//			      index.start_offset);
	//	} else if (nalu_type == utils::h264::NaluType::kSei) {
	//		// blog(LOG_INFO, "SEI NALU");
	//	} else {
	//		std::vector<uint8_t> content;
	//		if (nalu_type == utils::h264::NaluType::kIdr) {
	//			// blog(LOG_INFO, "IDR NALU");

	//			content.insert(content.end(), cfg_.begin(), cfg_.end());
	//		}
	//		// else {
	//		//	blog(LOG_DEBUG, "NALU type: %d",
	//		//	     nalu_type);
	//		// }

	//		content.insert(content.end(), buffer + index.start_offset,
	//			       buffer + index.payload_size + index.payload_start_offset -
	//				 index.start_offset);

	//		observer_->OnData(content.data(), content.size(), presentationTime);
	//	}
	//}
}

} // namespace source
