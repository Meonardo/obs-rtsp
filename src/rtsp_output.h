#pragma once

#include <obs-module.h>
#include "src/server/rtsp_server.h"

#include <string>
#include <atomic>
#include <thread>
#include <mutex>

class RtspOutput {
public:
	RtspOutput(obs_data_t* settings, obs_output_t* output);
	~RtspOutput();

	// obs output related functions begin
	bool Start();
	bool Stop(bool signal = true);
	void Data(struct encoder_packet* packet);
	size_t GetTotalBytes();
	int GetConnectTime();
	// obs output related functions end

private:
	obs_output_t* output_;
	obs_data_t* settings_;

	output::RtspServer* server_;

  std::atomic<bool> running_;
  std::mutex start_mutex_;
  std::thread start_thread_;

  void StartThread();
};

void register_rtsp_output();

class RtspService {
public:
	RtspService(obs_data_t* settings, obs_service_t* service);
	~RtspService() = default;

	// obs service related functions begin
	void Update(obs_data_t* settings);

	static obs_properties_t* Properties();
	static void ApplyEncoderSettings(obs_data_t* video_settings, obs_data_t* audio_settings);
	bool CanTryToConnect();
	const char* GetConnectInfo(enum obs_service_connect_info type);
	// obs service related functions end

private:
	std::string username_;
	std::string credential_;
	uint16_t port_;
};

void register_rtsp_service();
