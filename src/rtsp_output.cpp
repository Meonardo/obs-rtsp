#include "rtsp_output.h"

#include <util/threading.h>

#define MAX_CODECS 1
const char* audio_codecs[MAX_CODECS] = {"aac"};
const char* video_codecs[MAX_CODECS] = {"h264"};

RtspOutput::RtspOutput(obs_data_t* settings, obs_output_t* output)
  : output_(output),
    settings_(settings),
    server_(nullptr) {
	Start();
}

RtspOutput::~RtspOutput() {
	Stop(false);
	if (server_ != nullptr) {
		delete server_;
	}
}

bool RtspOutput::Start() {
	std::lock_guard<std::mutex> gurad(start_mutex_);

	if (!obs_output_can_begin_data_capture(output_, 0))
		return false;
	if (!obs_output_initialize_encoders(output_, 0))
		return false;

	if (server_ == nullptr) {
    server_ = new output::RtspServer();
	}
  if (running_.load()) {
    return false;
  }
	
	if (start_thread_.joinable()) {
		start_thread_.join();
	}
	start_thread_ = std::thread(&RtspOutput::StartThread, this);

	return true;
}

bool RtspOutput::Stop(bool signal) {
	std::lock_guard<std::mutex> guard(start_mutex_);
  if (server_ == nullptr)
    return false;

	obs_output_signal_stop(output_, OBS_OUTPUT_SUCCESS);
	running_.store(false);
	
	return server_->Stop();
}

void RtspOutput::Data(struct encoder_packet* packet) {
  if (!running_.load())
    return;

	if (packet->type == OBS_ENCODER_VIDEO) {
		if (server_ != nullptr) {
			server_->Data(packet);
		}
	}
}

size_t RtspOutput::GetTotalBytes() {
	return 0;
}

int RtspOutput::GetConnectTime() {
	return 0;
}

void RtspOutput::StartThread() {
	os_set_thread_name("rtsp_output_thread");

  if (!server_->Start()) {
    delete server_;
    server_ = nullptr;
    return;
  }

  obs_output_begin_data_capture(output_, 0);
  running_.store(true);
}

void register_rtsp_output() {
	struct obs_output_info info = {};

	info.id = "rtsp_output";
	info.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED | OBS_OUTPUT_SERVICE;
	info.get_name = [](void*) -> const char* {
		return "RTSP Output";
	};
	info.create = [](obs_data_t* settings, obs_output_t* output) -> void* {
		return new RtspOutput(settings, output);
	};
	info.destroy = [](void* priv_data) {
		delete static_cast<RtspOutput*>(priv_data);
	};
	info.start = [](void* priv_data) -> bool {
		return static_cast<RtspOutput*>(priv_data)->Start();
	};
	info.stop = [](void* priv_data, uint64_t) {
		static_cast<RtspOutput*>(priv_data)->Stop();
	};
	info.encoded_packet = [](void* priv_data, struct encoder_packet* packet) {
		static_cast<RtspOutput*>(priv_data)->Data(packet);
	};
	info.get_defaults = [](obs_data_t*) {
	};
	info.get_properties = [](void*) -> obs_properties_t* {
		return obs_properties_create();
	};
	info.get_total_bytes = [](void* priv_data) -> uint64_t {
		return (uint64_t) static_cast<RtspOutput*>(priv_data)->GetTotalBytes();
	};
	info.get_connect_time_ms = [](void* priv_data) -> int {
		return static_cast<RtspOutput*>(priv_data)->GetConnectTime();
	};
	info.encoded_video_codecs = "h264";
	info.encoded_audio_codecs = "aac";
	info.protocols = "RTSP";

	obs_register_output(&info);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RtspService::RtspService(obs_data_t* settings, obs_service_t* service) {
	Update(settings);
}

void RtspService::Update(obs_data_t* settings) {
	username_ = obs_data_get_string(settings, "username");
	credential_ = obs_data_get_string(settings, "credential");
	port_ = (uint16_t)obs_data_get_int(settings, "port");
}

obs_properties_t* RtspService::Properties() {
	obs_properties_t* ppts = obs_properties_create();

	obs_properties_add_text(ppts, "username", "username to connect the RTSP server",
				OBS_TEXT_DEFAULT);
	obs_properties_add_text(ppts, "credential", "password to connect the RTSP server",
				OBS_TEXT_PASSWORD);

	return ppts;
}

void RtspService::ApplyEncoderSettings(obs_data_t* video_settings, obs_data_t*) {
	// For now, ensure maximum compatibility with webrtc peers
	if (video_settings) {
		obs_data_set_int(video_settings, "bf", 0);
		obs_data_set_string(video_settings, "rate_control", "CBR");
		obs_data_set_bool(video_settings, "repeat_headers", true);
	}
}

const char* RtspService::GetConnectInfo(enum obs_service_connect_info type) {
	switch (type) {
	case OBS_SERVICE_CONNECT_INFO_USERNAME: return username_.c_str();
	case OBS_SERVICE_CONNECT_INFO_PASSWORD: return credential_.c_str();
	default: return nullptr;
	}
}

bool RtspService::CanTryToConnect() {
	return true;
}

void register_rtsp_service() {
	struct obs_service_info info = {};

	info.id = "rtsp_custom";
	info.get_name = [](void*) -> const char* {
		return "RTSP server";
	};
	info.create = [](obs_data_t* settings, obs_service_t* service) -> void* {
		return new RtspService(settings, service);
	};
	info.destroy = [](void* priv_data) {
		delete static_cast<RtspService*>(priv_data);
	};
	info.update = [](void* priv_data, obs_data_t* settings) {
		static_cast<RtspService*>(priv_data)->Update(settings);
	};
	info.get_properties = [](void*) -> obs_properties_t* {
		return RtspService::Properties();
	};
	info.get_protocol = [](void*) -> const char* {
		return "RTSP";
	};
	info.get_url = [](void* priv_data) -> const char* {
		return "0.0.0.0";
	};
	info.get_output_type = [](void*) -> const char* {
		return "rtsp_output";
	};
	info.apply_encoder_settings = [](void*, obs_data_t* video_settings,
					 obs_data_t* audio_settings) {
		RtspService::ApplyEncoderSettings(video_settings, audio_settings);
	};
	info.get_supported_video_codecs = [](void*) -> const char** {
		return video_codecs;
	};
	info.get_supported_audio_codecs = [](void*) -> const char** {
		return audio_codecs;
	};
	info.can_try_to_connect = [](void* priv_data) -> bool {
		return static_cast<RtspService*>(priv_data)->CanTryToConnect();
	};
	info.get_connect_info = [](void* priv_data, uint32_t type) -> const char* {
		return static_cast<RtspService*>(priv_data)->GetConnectInfo(
		  (enum obs_service_connect_info)type);
	};
	obs_register_service(&info);
}
