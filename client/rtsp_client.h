#pragma once

#include <obs-module.h>

#include <thread>
#include <vector>
#include <string>
#include <map>
#include <mutex>

#include "rtspconnectionclient.h"

namespace source {
class RTSPClientObserver {
public:
	virtual ~RTSPClientObserver() = default;

	virtual void OnSessionStarted(const char* id, const char* media, const char* codec, const char* sdp) = 0;
	virtual void OnSessionStopped(const char* msg) = 0;
	virtual void OnData(unsigned char* buffer, ssize_t size, struct timeval presentationTime) = 0;
	virtual void OnError(const char* msg) = 0;
};

class RtspClient : public RTSPConnection::Callback {
public:
	RtspClient(const std::string& uri, const std::map<std::string, std::string>& opts,
		   RTSPClientObserver* observer);

	virtual ~RtspClient();

	// start RTSP connection
	void Start();
	// stop RTSP connection
	void Stop();
	// check if the RTSP is running
	bool IsRunning();
	// the thread is capturing RTSP stream
	void CaptureThread();

	// the video resolution info
	uint32_t GetWidth() const;
	uint32_t GetHeight() const;

	virtual bool onNewSession(const char* id, const char* media, const char* codec, const char* sdp) override;
	virtual bool onData(const char* id, unsigned char* buffer, ssize_t size,
			    struct timeval presentationTime) override;
	virtual void onError(RTSPConnection& connection, const char* message) override;
	virtual void onConnectionTimeout(RTSPConnection& connection) override;
	virtual void onDataTimeout(RTSPConnection& connection) override;

private:
	RTSPClientObserver* observer_;
	Environment* env_;
	RTSPConnection* client_;
	std::string uri_;
	std::map<std::string, std::string> opts_;
	std::thread capture_thread_;
	std::map<std::string, std::string> codec_;
	std::vector<uint8_t> cfg_;
	// video resolution info
	uint32_t width_ = 1920;
	uint32_t height_ = 1080;

	void ProcessBuffer(const char* id, unsigned char* buffer, ssize_t size, struct timeval presentationTime);
};

} // namespace source
