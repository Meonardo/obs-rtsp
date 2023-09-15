#pragma once

#include <stdint.h>

class RTSPServer;
class Environment;

namespace output {
class RtspServer {
public:
	RtspServer(uint16_t port);
	~RtspServer();

	bool Start();
	bool Stop();
	void Data(struct encoder_packet* packet);
	size_t GetTotalBytes();
	int GetConnectTime();

private:
	RTSPServer* server_;
  Environment* env_;
  uint16_t port_; // default port is 8554
};
} // namespace output
