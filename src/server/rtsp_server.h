#pragma once

#include <stdint.h>
#include <thread>

// forward declarations
class RTSPServer;
class BasicUsageEnvironment;
class ServerMediaSession;
struct encoder_packet;

namespace output::source {
class RtspAudioSource;
class RtspVideoSource;
} // namespace output::source

namespace output {
class RtspServer {
public:
	RtspServer(uint16_t port = 8554);
	~RtspServer();
	// copy & move are deleted
	RtspServer(const RtspServer&) = delete;
	RtspServer(RtspServer&&) noexcept = delete;

	bool Start();
	bool Stop();
	void Data(struct encoder_packet* packet);
	size_t GetTotalBytes();
	int GetConnectTime();

  // static void AfterPlayingVideo(void* data);

private:
	RTSPServer* server_;
	ServerMediaSession* sms_;
  BasicUsageEnvironment* env_;
	uint16_t port_; // default port is 8554
  std::thread server_thread_;

	// sources
	source::RtspAudioSource* audio_source_;
	source::RtspVideoSource* video_source_;

  // void CreateVideoSource();

  void ServerThread();
};
} // namespace output
