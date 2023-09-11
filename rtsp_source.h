#pragma once

#include "client/rtsp_client.h"
#include <string>

extern "C" {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4204)
#endif

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/mastering_display_metadata.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

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
	// obs-source related functions end

	// the `Apply` button events from properties setting window(UI)
	bool OnApplyBtnClicked(obs_properties_t* props, obs_property_t* property);

	// overrides begin
	virtual void OnSessionStarted(const char* id, const char* media, const char* codec,
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

	// FFmpeg
	AVFormatContext* fmt_ctx_;
	AVFrame* in_frame_;
	AVFrame* sw_frame_;
	AVFrame* hw_frame_;
	AVPacket* pkt_;
	AVCodecContext* codec_ctx_;
	AVBufferRef* hw_ctx_;
	const AVCodec* codec_;
	AVPixelFormat hw_format_;
	bool hw_decoder_available_;

	// obs frame properties
	obs_source_frame obs_frame_;
	obs_media_state media_state_;
	video_format video_format_;
	video_colorspace color_space_;

	bool InitFFmpeg(const char* codec, bool video);
	void DestoryFFmpeg();
	bool HardwareFormatTypeAvailable(const AVCodec* codec, AVHWDeviceType type);
	void InitHardwareDecoder(const AVCodec* codec);

  bool PrepareToPlay();
};

void register_rtsp_source();
