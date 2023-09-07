#include "rtsp_source.h"

#include <regex>
#include <string>
#include <tuple>

namespace utils {
// extract username, password and rtsp url from rtsp url
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

RtspSource::RtspSource(obs_data_t* settings, obs_source_t* source)
  : settings_(settings),
    source_(source),
    rtsp_url_(""),
    client_(nullptr) {
	auto url = obs_data_get_string(settings, "url");
	rtsp_url_ = url;
	blog(LOG_INFO, "play rtsp source url: %s", url);
}

RtspSource::~RtspSource() {
	if (client_ != nullptr) {
		delete client_;
		client_ = nullptr;
	}
}

void RtspSource::Update(obs_data_t* settings) {}

void RtspSource::GetDefaults(obs_data_t* settings) {
	obs_data_set_default_string(settings, "url", "rtsp://");
	obs_data_set_default_bool(settings, "stop_on_hide", true);
	obs_data_set_default_bool(settings, "restart_on_error", true);
	obs_data_set_default_int(settings, "restart_timeout", 20);
	obs_data_set_default_bool(settings, "block_video", false);
	obs_data_set_default_bool(settings, "block_audio", false);
	obs_data_set_default_bool(settings, "drop_video", false);
	obs_data_set_default_bool(settings, "drop_audio", false);
	obs_data_set_default_bool(settings, "clear_on_end", true);
}

obs_properties* RtspSource::GetProperties() {
	obs_properties_t* props = obs_properties_create();

	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_property_t* prop = obs_properties_add_text(props, "url", "RTSP URL", OBS_TEXT_DEFAULT);
	obs_property_set_long_description(prop, "Specify the RTSP URL to play.");

	obs_properties_add_bool(props, "restart_on_error", "Try to restart after pipeline encountered an error");
	obs_properties_add_int(props, "restart_timeout", "Error timeout seconds", 0, 20, 1);
	obs_properties_add_bool(props, "stop_on_hide", "Stop pipeline when hidden");
	obs_properties_add_bool(props, "clear_on_end", "Clear image data after end-of-stream or error");
	obs_properties_add_bool(props, "block_video", "Disable video sink buffer");
	obs_properties_add_bool(props, "drop_video", "Drop video when sink is not fast enough");
	obs_properties_add_bool(props, "block_audio", "Disable audio sink buffer");
	obs_properties_add_bool(props, "drop_audio", "Drop audio when sink is not fast enough");

	obs_properties_add_button2(
	  props, "apply", "Apply",
	  [](obs_properties_t* props, obs_property_t* property, void* pri_data) -> bool {
		  auto source = static_cast<RtspSource*>(pri_data);
		  return source->OnApplyBtnClicked(props, property);
	  },
	  this);

	return props;
}

int64_t RtspSource::GetTime() {
	return 0;
}

int64_t RtspSource::GetDuration() {
	return 0;
}

enum obs_media_state RtspSource::GetState() {
	return OBS_MEDIA_STATE_NONE;
}

uint32_t RtspSource::GetWidth() {
	if (client_ == nullptr) {
		return 1920;
	}
	return client_->GetWidth();
}

uint32_t RtspSource::GetHeight() {
	if (client_ == nullptr)
		return 1080;
	return client_->GetHeight();
}

void RtspSource::PlayPause(bool pause) {}

void RtspSource::Restart() {}

void RtspSource::Stop() {}

void RtspSource::SetTime(int64_t ms) {}

void RtspSource::Hide() {}

void RtspSource::Show() {}

bool RtspSource::OnApplyBtnClicked(obs_properties_t* props, obs_property_t* property) {
	std::string url = obs_data_get_string(settings_, "url");
	if (url.empty()) {
		blog(LOG_ERROR, "RTSP url is empty");
		return false;
	}

	auto [username, password, rtsp] = utils::ExtractRtspUrl(url);
	if (rtsp.empty()) {
		blog(LOG_ERROR, "Current RTSP url(%s) is invalidate", url.c_str());
		return false;
	}

	rtsp_url_ = rtsp;
	blog(LOG_INFO, "play rtsp source url: %s", rtsp_url_.c_str());

	if (client_) {
		delete client_;
		client_ = nullptr;
	}

	uint64_t timeout = obs_data_get_int(settings_, "restart_timeout");
	std::map<std::string, std::string> opts;
	opts["timeout"] = std::to_string(timeout);

	// create rtsp client and start playing the video
	client_ = new source::RtspClient(rtsp_url_, opts, this);

	return true;
}

// override methods
void RtspSource::OnSessionStarted(const char* id, const char* media, const char* codec, const char* sdp) {
	blog(LOG_INFO, "RTSP session started");
}

void RtspSource::OnSessionStopped(const char* msg) {
	blog(LOG_INFO, "RTSP session stopped, message: %s", msg);
}

void RtspSource::OnError(const char* msg) {
	blog(LOG_INFO, "RTSP session error, message: %s", msg);
}

void RtspSource::OnData(unsigned char* buffer, ssize_t size, struct timeval time) {
	if (buffer == nullptr || size == 0) {
		return;
	}

	// decode h264 frame here

	obs_source_frame* frame = obs_source_frame_create(VIDEO_FORMAT_I420, 1920, 1080);
	if (frame == nullptr) {
		return;
	}

	obs_source_output_video(source_, frame);
	obs_source_frame_destroy(frame);
}

void register_rtsp_source() {
	struct obs_source_info info = {};

	info.id = "rtsp_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE |
			    OBS_SOURCE_CONTROLLABLE_MEDIA;
	info.get_name = [](void*) -> const char* {
		return "RTSP Source";
	};
	info.create = [](obs_data_t* settings, obs_source_t* source) -> void* {
		return new RtspSource(settings, source);
	};
	info.destroy = [](void* priv_data) {
		delete static_cast<RtspSource*>(priv_data);
	};
	info.get_defaults = [](obs_data_t* s) {
		RtspSource::GetDefaults(s);
	};
	info.get_properties = [](void* priv_data) -> obs_properties_t* {
		return static_cast<RtspSource*>(priv_data)->GetProperties();
	};
	info.update = [](void* priv_data, obs_data_t* settings) {
		static_cast<RtspSource*>(priv_data)->Update(settings);
	};
	info.show = [](void* priv_data) {
		static_cast<RtspSource*>(priv_data)->Show();
	};
	info.hide = [](void* priv_data) {
		static_cast<RtspSource*>(priv_data)->Hide();
	};
	info.get_width = [](void* priv_data) -> uint32_t {
		return static_cast<RtspSource*>(priv_data)->GetWidth();
	};
	info.get_height = [](void* priv_data) -> uint32_t {
		return static_cast<RtspSource*>(priv_data)->GetHeight();
	};
	info.media_play_pause = [](void* priv_data, bool pause) {
		static_cast<RtspSource*>(priv_data)->PlayPause(pause);
	};
	info.media_restart = [](void* priv_data) {
		static_cast<RtspSource*>(priv_data)->Restart();
	};
	info.media_stop = [](void* priv_data) {
		static_cast<RtspSource*>(priv_data)->Stop();
	};
	info.media_get_state = [](void* priv_data) -> enum obs_media_state {
		return static_cast<RtspSource*>(priv_data)->GetState();
	};
	info.media_get_time = [](void* priv_data) -> int64_t {
		return static_cast<RtspSource*>(priv_data)->GetTime();
	};
	info.media_get_duration = [](void* priv_data) -> int64_t {
		return static_cast<RtspSource*>(priv_data)->GetDuration();
	};
	info.media_set_time = [](void* priv_data, int64_t ms) {
		static_cast<RtspSource*>(priv_data)->SetTime(ms);
	};

	obs_register_source(&info);
}
