#include "rtsp_server.h"

#include "liveMedia.hh"
#include "environment.h"
#include "BasicUsageEnvironment.hh"

#include <obs.h>

namespace output {
RtspServer::RtspServer(uint16_t port) : server_(nullptr), env_(nullptr), port_(port) {
	if (port == 0) {
		port = 8554;
	}
}

RtspServer::~RtspServer() {
	Stop();
}

bool RtspServer::Start() {
	if (server_ != nullptr) {
		return;
	}
	env_ = new Environment;
	server_ = RTSPServer::createNew(*env_, port_, nullptr);
  if (server_ == nullptr) {
    blog(LOG_ERROR, "failed to create RTSP server");
    return false;
  }

	return true;
}

bool RtspServer::Stop() {
  if (server_ != nullptr) {
    Medium::close(server_);
    server_ = nullptr;
  }

	return true;
}

void RtspServer::Data(struct encoder_packet* packet) {}

size_t RtspServer::GetTotalBytes() {
	return 0;
}

int RtspServer::GetConnectTime() {
	return 0;
}

} // namespace output
