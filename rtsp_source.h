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

class Decoder {
public:
	Decoder(bool video, bool require_hw, const std::string& codec);
	~Decoder();
	Decoder(const Decoder&) = delete;
	Decoder(const Decoder&&) noexcept = delete;

	bool Avaiable() const { return codec_ctx_ != nullptr; }
	bool HardwareDecoderAvailable() const { return hw_decoder_available_; }

	bool Init();
	void Destory();

	bool Decode(unsigned char* buffer, ssize_t size, timeval time, obs_source_frame* frame,
		    obs_source_audio* audio);

private:
	bool video_; // audio or video
	std::string codec_name_;
	AVCodecContext* codec_ctx_;
	const AVCodec* codec_;
	AVFrame* in_frame_;
	AVFrame* sw_frame_;

	// packet
	AVPacket* pkt_;

	// hardware codec related
	bool require_hw_;
	bool hw_decoder_available_;
	AVBufferRef* hw_ctx_;
	AVPixelFormat hw_format_;
	AVFrame* hw_frame_;

	// obs video frame properties
	video_format video_format_;
	video_colorspace color_space_;

	void InitHardwareDecoder(const AVCodec* codec);
	bool HardwareFormatTypeAvailable(const AVCodec* c, AVHWDeviceType type);
	bool DecodePacket(unsigned char* buffer, ssize_t size);
};

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
	void Stop();
	// obs-source related functions end

	// the `Apply` button events from properties setting window(UI)
	bool OnApplyBtnClicked(obs_properties_t* props, obs_property_t* property);

	// overrides begin
	virtual bool OnSessionStarted(bool video, const char* codec) override;
	virtual void OnSessionStopped(const char* msg) override;
	virtual void OnData(unsigned char* buffer, ssize_t size, timeval time, bool video) override;
	virtual void OnError(const char* msg) override;
	// overrides end

private:
	obs_source_t* source_;
	obs_data_t* settings_;

	std::string rtsp_url_;
	source::RtspClient* client_;

	// decoders
	Decoder* video_decoder_;
	Decoder* audio_decoder_;
	AVFormatContext* fmt_ctx_;
	bool hw_decode_;

	// configures
	bool video_disabled_; // only receive audio, defalut is false
	bool audio_disabled_; // only receive video, defalut is false

	// obs source properties
	obs_source_frame obs_frame_;
	obs_media_state media_state_;
	obs_source_audio obs_audio_frame_;

	bool InitFFmpeg(const char* codec, bool video);
	void DestoryFFmpeg();
	bool PrepareToPlay();
};

void register_rtsp_source();
