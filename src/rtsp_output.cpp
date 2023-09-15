#include "rtsp_output.h"

RtspOutput::RtspOutput(obs_data_t* settings, obs_output_t* output)
  : output_(output),
    settings_(settings) {}

RtspOutput::~RtspOutput() {}

bool RtspOutput::Start() {
	return true;
}

bool RtspOutput::Stop(bool signal) {
	return true;
}

void RtspOutput::Data(struct encoder_packet* packet) {}

size_t RtspOutput::GetTotalBytes() {
	return 0;
}

int RtspOutput::GetConnectTime() {
	return 0;
}

void register_rtsp_output() {
	struct obs_output_info info = {};

	info.id = "rtsp_output";
	info.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED | OBS_OUTPUT_SERVICE;
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
