#pragma once

#include <thread>
#include <vector>
#include <string>
#include <unordered_map>

#include "rtspconnectionclient.h"

namespace source {

class RTSPClient : public RTSPConnection::Callback {
public:
	RTSPClient(const std::string &uri,
		   const std::map<std::string, std::string> &opts);

	virtual ~RTSPClient();

	static RTSPClient *Create(const std::string &uri);

	// start RTSP connection
	void Start();
	// stop RTSP connection
	void Stop();
	// check if the RTSP is running
	bool IsRunning();
	// the thread is capturing RTSP stream
	void CaptureThread();

	virtual bool onNewSession(const char *id, const char *media,
				  const char *codec, const char *sdp) override;
	virtual bool onData(const char *id, unsigned char *buffer, ssize_t size,
			    struct timeval presentationTime) override;
	virtual void onError(RTSPConnection &connection,
			     const char *message) override;
	virtual void onConnectionTimeout(RTSPConnection &connection) override;
	virtual void onDataTimeout(RTSPConnection &connection) override;

private:
	char stop_;
	Environment env_;

protected:
	RTSPConnection client_;

private:
	std::thread capture_thread_;
	std::map<std::string, std::string> codec_;
	std::vector<uint8_t> cfg_;
	// video resolution info
	size_t width_ = 1920;
	size_t height_ = 1080;

	void ProcessBuffer(const char *id, unsigned char *buffer, ssize_t size,
			   struct timeval presentationTime);
};

} // namespace source
