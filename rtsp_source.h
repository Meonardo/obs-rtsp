#pragma once

#include "client/rtsp_client.h"
#include <string>

class RtspSource : public source::RTSPClientObserver {
public:
	RtspSource(obs_data_t* settings, obs_source_t* source);
	~RtspSource();

	// obs-source related functions begin
	static void GetDefaults(obs_data_t* settings);

	obs_properties_t* GetProperties();
	void Update(obs_data_t* settings);
	void Show();
	void Hide();
	enum obs_media_state GetState();
	int64_t GetTime();
	int64_t GetDuration();
	void PlayPause(bool pause);
	void Stop();
	void Restart();
	void SetTime(int64_t ms);
	uint32_t GetWidth();
	uint32_t GetHeight();
	// obs-source related functions end

	// the `Apply` button events from properties setting window(UI)
	bool OnApplyBtnClicked(obs_properties_t* props,
			       obs_property_t* property);

	// overrides begin
	virtual void OnSessionStarted(const char* id, const char* media,
				      const char* codec,
				      const char* sdp) override;
	virtual void OnSessionStopped(const char* msg) override;
	virtual void OnData(unsigned char* buffer, ssize_t size,
			    struct timeval presentationTime) override;
	virtual void OnError(const char* msg) override;
	// overrides end

private:
	obs_source_t* source_;
	obs_data_t* settings_;

	std::string rtsp_url_;
	source::RtspClient* client_;
};

void register_rtsp_source();
