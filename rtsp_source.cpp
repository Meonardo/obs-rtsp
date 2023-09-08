#include "rtsp_source.h"

#include "utils/utils.h"

#ifdef av_err2str
#undef av_err2str
av_always_inline std::string av_err2string(int errnum) {
	char str[AV_ERROR_MAX_STRING_SIZE];
	return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
}
#define av_err2str(err) av_err2string(err).c_str()
#endif // av_err2str

static inline video_format convert_pixel_format(int f) {
	switch (f) {
	case AV_PIX_FMT_NONE: return VIDEO_FORMAT_NONE;
	case AV_PIX_FMT_YUV420P: return VIDEO_FORMAT_I420;
	case AV_PIX_FMT_YUYV422: return VIDEO_FORMAT_YUY2;
	case AV_PIX_FMT_YUV422P: return VIDEO_FORMAT_I422;
	case AV_PIX_FMT_YUV422P10LE: return VIDEO_FORMAT_I210;
	case AV_PIX_FMT_YUV444P: return VIDEO_FORMAT_I444;
	case AV_PIX_FMT_YUV444P12LE: return VIDEO_FORMAT_I412;
	case AV_PIX_FMT_UYVY422: return VIDEO_FORMAT_UYVY;
	case AV_PIX_FMT_YVYU422: return VIDEO_FORMAT_YVYU;
	case AV_PIX_FMT_NV12: return VIDEO_FORMAT_NV12;
	case AV_PIX_FMT_RGBA: return VIDEO_FORMAT_RGBA;
	case AV_PIX_FMT_BGRA: return VIDEO_FORMAT_BGRA;
	case AV_PIX_FMT_YUVA420P: return VIDEO_FORMAT_I40A;
	case AV_PIX_FMT_YUV420P10LE: return VIDEO_FORMAT_I010;
	case AV_PIX_FMT_YUVA422P: return VIDEO_FORMAT_I42A;
	case AV_PIX_FMT_YUVA444P: return VIDEO_FORMAT_YUVA;
#if LIBAVUTIL_BUILD >= AV_VERSION_INT(56, 31, 100)
	case AV_PIX_FMT_YUVA444P12LE: return VIDEO_FORMAT_YA2L;
#endif
	case AV_PIX_FMT_BGR0: return VIDEO_FORMAT_BGRX;
	case AV_PIX_FMT_P010LE: return VIDEO_FORMAT_P010;
	default:;
	}

	return VIDEO_FORMAT_NONE;
}

static inline audio_format convert_sample_format(int f) {
	switch (f) {
	case AV_SAMPLE_FMT_U8: return AUDIO_FORMAT_U8BIT;
	case AV_SAMPLE_FMT_S16: return AUDIO_FORMAT_16BIT;
	case AV_SAMPLE_FMT_S32: return AUDIO_FORMAT_32BIT;
	case AV_SAMPLE_FMT_FLT: return AUDIO_FORMAT_FLOAT;
	case AV_SAMPLE_FMT_U8P: return AUDIO_FORMAT_U8BIT_PLANAR;
	case AV_SAMPLE_FMT_S16P: return AUDIO_FORMAT_16BIT_PLANAR;
	case AV_SAMPLE_FMT_S32P: return AUDIO_FORMAT_32BIT_PLANAR;
	case AV_SAMPLE_FMT_FLTP: return AUDIO_FORMAT_FLOAT_PLANAR;
	default:;
	}

	return AUDIO_FORMAT_UNKNOWN;
}

static inline video_colorspace convert_color_space(AVColorSpace s, AVColorTransferCharacteristic trc,
						   AVColorPrimaries color_primaries) {
	switch (s) {
	case AVCOL_SPC_BT709: return (trc == AVCOL_TRC_IEC61966_2_1) ? VIDEO_CS_SRGB : VIDEO_CS_709;
	case AVCOL_SPC_FCC:
	case AVCOL_SPC_BT470BG:
	case AVCOL_SPC_SMPTE170M:
	case AVCOL_SPC_SMPTE240M: return VIDEO_CS_601;
	case AVCOL_SPC_BT2020_NCL: return (trc == AVCOL_TRC_ARIB_STD_B67) ? VIDEO_CS_2100_HLG : VIDEO_CS_2100_PQ;
	default:
		return (color_primaries == AVCOL_PRI_BT2020)
			 ? ((trc == AVCOL_TRC_ARIB_STD_B67) ? VIDEO_CS_2100_HLG : VIDEO_CS_2100_PQ)
			 : VIDEO_CS_DEFAULT;
	}
}

static inline video_range_type convert_color_range(AVColorRange r) {
	return r == AVCOL_RANGE_JPEG ? VIDEO_RANGE_FULL : VIDEO_RANGE_DEFAULT;
}

enum AVHWDeviceType hw_priority[] = {
  AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2, AV_HWDEVICE_TYPE_CUDA,         AV_HWDEVICE_TYPE_VAAPI,
  AV_HWDEVICE_TYPE_VDPAU,   AV_HWDEVICE_TYPE_QSV,   AV_HWDEVICE_TYPE_VIDEOTOOLBOX, AV_HWDEVICE_TYPE_NONE,
};

RtspSource::RtspSource(obs_data_t* settings, obs_source_t* source)
  : settings_(settings),
    source_(source),
    rtsp_url_(""),
    client_(nullptr),
    fmt_ctx_(nullptr),
    in_frame_(nullptr),
    sw_frame_(nullptr),
    hw_frame_(nullptr),
    pkt_(nullptr),
    codec_ctx_(nullptr),
    hw_ctx_(nullptr),
    codec_(nullptr) {
	auto url = obs_data_get_string(settings, "url");
	rtsp_url_ = url;
	blog(LOG_INFO, "play rtsp source url: %s", url);
}

RtspSource::~RtspSource() {
	if (client_ != nullptr) {
		delete client_;
		client_ = nullptr;
	}
}

void RtspSource::Update(obs_data_t* settings) {
	std::string url = obs_data_get_string(settings_, "url");
	if (url != rtsp_url_) {
		if (url.empty()) {
			blog(LOG_ERROR, "RTSP url is empty");
			return;
		}

		auto [username, password, rtsp] = utils::ExtractRtspUrl(url);
		if (rtsp.empty()) {
			blog(LOG_ERROR, "Current RTSP url(%s) is invalidate", url.c_str());
			return;
		}

		rtsp_url_ = rtsp;
		blog(LOG_INFO, "play rtsp source url: %s", rtsp_url_.c_str());

		if (client_) {
			delete client_;
			client_ = nullptr;
		}

		uint64_t timeout = obs_data_get_int(settings_, "restart_timeout");
		std::map<std::string, std::string> opts;
		opts["timeout"] = std::to_string(timeout);

		// create rtsp client and start playing the video
		client_ = new source::RtspClient(rtsp_url_, opts, this);
	}
}

void RtspSource::GetDefaults(obs_data_t* settings) {
	obs_data_set_default_string(settings, "url", "rtsp://");
	obs_data_set_default_bool(settings, "stop_on_hide", true);
	obs_data_set_default_bool(settings, "restart_on_error", true);
	obs_data_set_default_int(settings, "restart_timeout", 20);
	obs_data_set_default_bool(settings, "block_video", false);
	obs_data_set_default_bool(settings, "block_audio", false);
	obs_data_set_default_bool(settings, "hw_decode", true);
}

obs_properties* RtspSource::GetProperties() {
	obs_properties_t* props = obs_properties_create();

	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_property_t* prop = obs_properties_add_text(props, "url", "RTSP URL", OBS_TEXT_DEFAULT);
	obs_property_set_long_description(prop, "Specify the RTSP URL to play.");

	obs_properties_add_bool(props, "restart_on_error", "Try to restart after pipeline encountered an error");
	obs_properties_add_int(props, "restart_timeout", "Error timeout seconds", 0, 20, 1);
	obs_properties_add_bool(props, "stop_on_hide", "Stop pipeline when hidden");
	obs_properties_add_bool(props, "block_video", "Disable video sink buffer");
	obs_properties_add_bool(props, "block_audio", "Disable audio sink buffer");
	obs_properties_add_bool(props, "hw_decode", "Use hardware decode");

	obs_properties_add_button2(
	  props, "apply", "Apply",
	  [](obs_properties_t* props, obs_property_t* property, void* pri_data) -> bool {
		  auto source = static_cast<RtspSource*>(pri_data);
		  return source->OnApplyBtnClicked(props, property);
	  },
	  this);

	return props;
}

int64_t RtspSource::GetTime() {
	return 0;
}

int64_t RtspSource::GetDuration() {
	return 0;
}

enum obs_media_state RtspSource::GetState() {
	return OBS_MEDIA_STATE_NONE;
}

uint32_t RtspSource::GetWidth() {
	if (client_ == nullptr) {
		return 1920;
	}
	return client_->GetWidth();
}

uint32_t RtspSource::GetHeight() {
	if (client_ == nullptr)
		return 1080;
	return client_->GetHeight();
}

void RtspSource::PlayPause(bool pause) {}

void RtspSource::Restart() {}

void RtspSource::Stop() {}

void RtspSource::SetTime(int64_t ms) {}

void RtspSource::Hide() {}

void RtspSource::Show() {}

bool RtspSource::OnApplyBtnClicked(obs_properties_t* props, obs_property_t* property) {
	std::string url = obs_data_get_string(settings_, "url");
	if (url.empty()) {
		blog(LOG_ERROR, "RTSP url is empty");
		return false;
	}

	auto [username, password, rtsp] = utils::ExtractRtspUrl(url);
	if (rtsp.empty()) {
		blog(LOG_ERROR, "Current RTSP url(%s) is invalidate", url.c_str());
		return false;
	}

	rtsp_url_ = rtsp;
	blog(LOG_INFO, "play rtsp source url: %s", rtsp_url_.c_str());

	if (client_) {
		delete client_;
		client_ = nullptr;
	}

	uint64_t timeout = obs_data_get_int(settings_, "restart_timeout");
	std::map<std::string, std::string> opts;
	opts["timeout"] = std::to_string(timeout);

	// create rtsp client and start playing the video
	client_ = new source::RtspClient(rtsp_url_, opts, this);

	return true;
}

// override methods
void RtspSource::OnSessionStarted(const char* id, const char* media, const char* codec, const char* sdp) {
	blog(LOG_INFO, "RTSP session started");
	if (strcmp(media, "video") == 0) {
		bool ret = InitFFmpegFormat(codec, true, true);
		if (!ret) {
			blog(LOG_ERROR, "Init ffmpeg format failed");
			return;
		}
	}
}

void RtspSource::OnSessionStopped(const char* msg) {
	blog(LOG_INFO, "RTSP session stopped, message: %s", msg);
}

void RtspSource::OnError(const char* msg) {
	blog(LOG_INFO, "RTSP session error, message: %s", msg);
}

void RtspSource::OnData(unsigned char* buffer, ssize_t size, struct timeval time) {
	if (buffer == nullptr || size == 0) {
		return;
	}

	// decode h264 frame here
	if (fmt_ctx_ == nullptr || codec_ctx_ == nullptr) {
		return;
	}

	// create packet here
	pkt_->data = buffer;
	pkt_->size = size;

	// send the packet to decoder
	auto ret = avcodec_send_packet(codec_ctx_, pkt_);
	if (ret < 0) {
		blog(LOG_ERROR, "error sending a packet for decoding, error: %s\n", av_err2str(ret));
		av_packet_unref(pkt_);
		return;
	}

	// receive the decoded frame
	ret = avcodec_receive_frame(codec_ctx_, hw_frame_);
	if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
		return;
	} else if (ret < 0) {
		blog(LOG_ERROR, "error during decoding, error: %s\n", av_err2str(ret));
		av_packet_unref(pkt_);
		return;
	}

	hw_frame_->format = AV_PIX_FMT_YUV420P;
	obs_source_frame* frame = obs_source_frame_create(VIDEO_FORMAT_I420, hw_frame_->width, hw_frame_->height);
	if (frame == nullptr) {
		return;
	}

	bool flip = hw_frame_->linesize[0] < 0 && hw_frame_->linesize[1] == 0;
	for (size_t i = 0; i < MAX_AV_PLANES; i++) {
		frame->data[i] = hw_frame_->data[i];
		frame->linesize[i] = abs(hw_frame_->linesize[i]);
	}
	if (flip)
		frame->data[0] -= frame->linesize[0] * ((size_t)hw_frame_->height - 1);

	frame->format = convert_pixel_format(hw_frame_->format);

	switch (hw_frame_->color_trc) {
	case AVCOL_TRC_BT709:
	case AVCOL_TRC_GAMMA22:
	case AVCOL_TRC_GAMMA28:
	case AVCOL_TRC_SMPTE170M:
	case AVCOL_TRC_SMPTE240M:
	case AVCOL_TRC_IEC61966_2_1: frame->trc = VIDEO_TRC_SRGB; break;
	case AVCOL_TRC_SMPTE2084: frame->trc = VIDEO_TRC_PQ; break;
	case AVCOL_TRC_ARIB_STD_B67: frame->trc = VIDEO_TRC_HLG; break;
	default: frame->trc = VIDEO_TRC_DEFAULT;
	}

	frame->flip = flip;

	obs_source_output_video(source_, frame);
	obs_source_frame_destroy(frame);
}

bool RtspSource::InitFFmpegFormat(const char* codec, bool video, bool hw_decode) {
	// init format context
	fmt_ctx_ = avformat_alloc_context();
	if (fmt_ctx_ == nullptr) {
		blog(LOG_ERROR, "AVFormatContext init failed");
		return false;
	}
	// find the target codec and init codec context
	codec_ = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (codec_ == nullptr) {
		blog(LOG_ERROR, "AVCodec init failed");
		return false;
	}
	codec_ctx_ = avcodec_alloc_context3(codec_);
	if (codec_ctx_ == nullptr) {
		blog(LOG_ERROR, "AVCodecContext init failed");
		return false;
	}

	if (hw_decode) { // init hardware decoder
		InitHardwareDecoder(codec_);
	}

	// open codec context
	if (avcodec_open2(codec_ctx_, codec_, NULL) < 0) {
		blog(LOG_ERROR, "AVCodecContext open failed");

		avcodec_free_context(&codec_ctx_);
		return false;
	}

	// init frame
	if (hw_decode) {
		hw_frame_ = av_frame_alloc();
		if (hw_frame_ == nullptr) {
			blog(LOG_ERROR, "AVFrame init failed(hardware)");
			avcodec_free_context(&codec_ctx_);
			return false;
		}
	} else {
		sw_frame_ = av_frame_alloc();
		if (sw_frame_ == nullptr) {
			blog(LOG_ERROR, "AVFrame init failed(software)");
			avcodec_free_context(&codec_ctx_);
			return false;
		}
	}

	// init packet
	pkt_ = av_packet_alloc();

	return true;
}

bool RtspSource::HardwareFormatTypeAvailable(const AVCodec* c, AVHWDeviceType type) {
	for (int i = 0;; i++) {
		const AVCodecHWConfig* config = avcodec_get_hw_config(c, i);
		if (!config) {
			break;
		}

		if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == type) {
			hw_format_ = config->pix_fmt;
			return true;
		}
	}

	return false;
}

void RtspSource::InitHardwareDecoder(const AVCodec* codec) {
	enum AVHWDeviceType* priority = hw_priority;
	AVBufferRef* hw_ctx = NULL;

	while (*priority != AV_HWDEVICE_TYPE_NONE) {
		if (HardwareFormatTypeAvailable(codec, *priority)) {
			int ret = av_hwdevice_ctx_create(&hw_ctx, *priority, NULL, NULL, 0);
			if (ret == 0)
				break;
		}

		priority++;
	}

	if (hw_ctx) {
		codec_ctx_->hw_device_ctx = av_buffer_ref(hw_ctx);
		codec_ctx_->opaque = this;
		hw_ctx_ = hw_ctx;
		hw_decoder_available_ = true;
	}
}

void register_rtsp_source() {
	struct obs_source_info info = {};

	info.id = "rtsp_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE |
			    OBS_SOURCE_CONTROLLABLE_MEDIA;
	info.get_name = [](void*) -> const char* {
		return "RTSP Source";
	};
	info.create = [](obs_data_t* settings, obs_source_t* source) -> void* {
		return new RtspSource(settings, source);
	};
	info.destroy = [](void* priv_data) {
		delete static_cast<RtspSource*>(priv_data);
	};
	info.get_defaults = [](obs_data_t* s) {
		RtspSource::GetDefaults(s);
	};
	info.get_properties = [](void* priv_data) -> obs_properties_t* {
		return static_cast<RtspSource*>(priv_data)->GetProperties();
	};
	info.update = [](void* priv_data, obs_data_t* settings) {
		static_cast<RtspSource*>(priv_data)->Update(settings);
	};
	info.show = [](void* priv_data) {
		static_cast<RtspSource*>(priv_data)->Show();
	};
	info.hide = [](void* priv_data) {
		static_cast<RtspSource*>(priv_data)->Hide();
	};
	info.get_width = [](void* priv_data) -> uint32_t {
		return static_cast<RtspSource*>(priv_data)->GetWidth();
	};
	info.get_height = [](void* priv_data) -> uint32_t {
		return static_cast<RtspSource*>(priv_data)->GetHeight();
	};
	info.media_play_pause = [](void* priv_data, bool pause) {
		static_cast<RtspSource*>(priv_data)->PlayPause(pause);
	};
	info.media_restart = [](void* priv_data) {
		static_cast<RtspSource*>(priv_data)->Restart();
	};
	info.media_stop = [](void* priv_data) {
		static_cast<RtspSource*>(priv_data)->Stop();
	};
	info.media_get_state = [](void* priv_data) -> enum obs_media_state {
		return static_cast<RtspSource*>(priv_data)->GetState();
	};
	info.media_get_time = [](void* priv_data) -> int64_t {
		return static_cast<RtspSource*>(priv_data)->GetTime();
	};
	info.media_get_duration = [](void* priv_data) -> int64_t {
		return static_cast<RtspSource*>(priv_data)->GetDuration();
	};
	info.media_set_time = [](void* priv_data, int64_t ms) {
		static_cast<RtspSource*>(priv_data)->SetTime(ms);
	};

	obs_register_source(&info);
}
