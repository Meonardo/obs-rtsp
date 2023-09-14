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

constexpr AVHWDeviceType hw_priority[] = {
  AV_HWDEVICE_TYPE_D3D11VA,      AV_HWDEVICE_TYPE_DXVA2, AV_HWDEVICE_TYPE_CUDA,
  AV_HWDEVICE_TYPE_VAAPI,        AV_HWDEVICE_TYPE_VDPAU, AV_HWDEVICE_TYPE_QSV,
  AV_HWDEVICE_TYPE_VIDEOTOOLBOX, AV_HWDEVICE_TYPE_NONE,
};

static inline video_format convert_pixel_format(int f) {
	switch (f) {
	case AV_PIX_FMT_NONE: return VIDEO_FORMAT_NONE;
	case AV_PIX_FMT_YUV420P: return VIDEO_FORMAT_I420;
	case AV_PIX_FMT_YUVJ420P: return VIDEO_FORMAT_I420;
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

static inline enum speaker_layout convert_speaker_layout(uint8_t channels) {
	switch (channels) {
	case 0: return SPEAKERS_UNKNOWN;
	case 1: return SPEAKERS_MONO;
	case 2: return SPEAKERS_STEREO;
	case 3: return SPEAKERS_2POINT1;
	case 4: return SPEAKERS_4POINT0;
	case 5: return SPEAKERS_4POINT1;
	case 6: return SPEAKERS_5POINT1;
	case 8: return SPEAKERS_7POINT1;
	default: return SPEAKERS_UNKNOWN;
	}
}

static inline video_colorspace convert_color_space(AVColorSpace s,
						   AVColorTransferCharacteristic trc,
						   AVColorPrimaries color_primaries) {
	switch (s) {
	case AVCOL_SPC_BT709: return (trc == AVCOL_TRC_IEC61966_2_1) ? VIDEO_CS_SRGB : VIDEO_CS_709;
	case AVCOL_SPC_FCC:
	case AVCOL_SPC_BT470BG:
	case AVCOL_SPC_SMPTE170M:
	case AVCOL_SPC_SMPTE240M: return VIDEO_CS_601;
	case AVCOL_SPC_BT2020_NCL:
		return (trc == AVCOL_TRC_ARIB_STD_B67) ? VIDEO_CS_2100_HLG : VIDEO_CS_2100_PQ;
	default:
		return (color_primaries == AVCOL_PRI_BT2020)
			 ? ((trc == AVCOL_TRC_ARIB_STD_B67) ? VIDEO_CS_2100_HLG : VIDEO_CS_2100_PQ)
			 : VIDEO_CS_DEFAULT;
	}
}

static inline video_range_type convert_color_range(AVColorRange r) {
	return r == AVCOL_RANGE_JPEG ? VIDEO_RANGE_FULL : VIDEO_RANGE_DEFAULT;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

Decoder::Decoder(bool video, bool require_hw, const std::string& codec_name)
  : video_(video),
    codec_name_(codec_name),
    codec_ctx_(nullptr),
    codec_(nullptr),
    in_frame_(nullptr),
    sw_frame_(nullptr),
    pkt_(nullptr),
    require_hw_(require_hw),
    hw_decoder_available_(false),
    hw_ctx_(nullptr),
    hw_format_(AV_PIX_FMT_NONE),
    hw_frame_(nullptr),
    video_format_(VIDEO_FORMAT_NONE),
    color_space_(VIDEO_CS_DEFAULT) {}

Decoder::~Decoder() {
	Destory();
}

bool Decoder::Init(int rate, int channels) {
	if (video_) {
		codec_ = avcodec_find_decoder_by_name(codec_name_.c_str());
		if (codec_ == nullptr) {
			if (codec_name_ ==
			    "h265") { // compare with the codec name again if not found
				codec_ = avcodec_find_decoder(AV_CODEC_ID_HEVC);
				if (codec_ == nullptr) {
					blog(LOG_ERROR, "AVCodec init failed");
					return false;
				}
			} else {
				blog(LOG_ERROR, "AVCodec init failed");
				return false;
			}
		}
	} else {
		if (codec_name_ ==
		    "mpeg4-generic") { // usually its aac (I can not get a correct way to get audio codec name)
			codec_ = avcodec_find_decoder(AV_CODEC_ID_AAC);
			if (codec_ == nullptr) {
				blog(LOG_ERROR, "AVCodec(aac) init failed");
				return false;
			}
		} else {
			codec_ = avcodec_find_decoder_by_name(codec_name_.c_str());
			if (codec_ == nullptr) {
				blog(LOG_ERROR, "AVCodec(%s) init failed", codec_name_.c_str());
				return false;
			}
		}
	}

	codec_ctx_ = avcodec_alloc_context3(codec_);
	if (codec_ctx_ == nullptr) {
		blog(LOG_ERROR, "AVCodecContext init failed");
		return false;
	}

  // audio configures
	if (!video_) {
    codec_ctx_->channels = channels;
		codec_ctx_->sample_rate = rate;
	}

	if (require_hw_) { // init hardware decoder if necessary
		InitHardwareDecoder(codec_);
	}

	// open codec context
	if (avcodec_open2(codec_ctx_, codec_, NULL) < 0) {
		blog(LOG_ERROR, "AVCodecContext open failed");

		avcodec_free_context(&codec_ctx_);
		return false;
	}

	// init frames
	sw_frame_ = av_frame_alloc();
	if (sw_frame_ == nullptr) {
		blog(LOG_ERROR, "AVFrame init failed(software)");
		avcodec_free_context(&codec_ctx_);
		return false;
	}

	in_frame_ = sw_frame_;
	if (require_hw_ && hw_decoder_available_) { // init hardware frame if necessary
		hw_frame_ = av_frame_alloc();
		if (hw_frame_ == nullptr) {
			blog(LOG_ERROR, "AVFrame init failed(hardware)");
			avcodec_free_context(&codec_ctx_);
			return false;
		}
		in_frame_ = hw_frame_;
	}

	// init packet
	pkt_ = av_packet_alloc();

	return true;
}

void Decoder::Destory() {
	if (codec_ctx_ != nullptr) {
		avcodec_free_context(&codec_ctx_);
		codec_ctx_ = nullptr;
	}

	if (sw_frame_ != nullptr) {
		av_frame_unref(sw_frame_);
		av_frame_free(&sw_frame_);
		sw_frame_ = nullptr;
	}
	if (hw_frame_ != nullptr) {
		av_frame_unref(hw_frame_);
		av_frame_free(&hw_frame_);
		hw_frame_ = nullptr;
	}
	in_frame_ = nullptr;
	hw_decoder_available_ = false;

	if (pkt_ != nullptr) {
		av_packet_free(&pkt_);
		pkt_ = nullptr;
	}

	if (hw_ctx_ != nullptr) {
		av_buffer_unref(&hw_ctx_);
		hw_ctx_ = nullptr;
	}
}

bool Decoder::Decode(unsigned char* buffer, ssize_t size, timeval time, obs_source_frame* frame,
		     obs_source_audio* audio) {
	// decode packet
	if (!DecodePacket(buffer, size)) {
		blog(LOG_INFO, "decode failed, buffer size: %u", size);
		return false;
	}

	// check if need use hardware decoder
	if (hw_decoder_available_) {
		auto ret = av_hwframe_transfer_data(sw_frame_, hw_frame_, 0);
		if (ret != 0) {
			blog(LOG_ERROR,
			     "error transfer data from hw frame to sw frame, error: %s\n",
			     av_err2str(ret));
			return false;
		}

		if (video_) {
			sw_frame_->color_range = hw_frame_->color_range;
			sw_frame_->color_primaries = hw_frame_->color_primaries;
			sw_frame_->color_trc = hw_frame_->color_trc;
			sw_frame_->colorspace = hw_frame_->colorspace;
		}
	}

	if (video_) {
		auto format = convert_pixel_format(sw_frame_->format);
		if (format == VIDEO_FORMAT_NONE) {
			blog(LOG_ERROR, "video format is none?");
			return false;
		}

		for (size_t i = 0; i < MAX_AV_PLANES; i++) {
			frame->data[i] = sw_frame_->data[i];
			frame->linesize[i] = abs(sw_frame_->linesize[i]);
		}

		frame->format = format;
		frame->width = sw_frame_->width;
		frame->height = sw_frame_->height;
		frame->timestamp = time.tv_sec;
		frame->flip = false;
		frame->max_luminance = 0;

		auto color_space = convert_color_space(sw_frame_->colorspace, sw_frame_->color_trc,
						       sw_frame_->color_primaries);
		auto color_range = convert_color_range(sw_frame_->color_range);
		frame->full_range = color_range;

		if (color_space != color_space_ || format != video_format_) {
			bool success = video_format_get_parameters_for_format(
			  color_space, color_range, format, frame->color_matrix,
			  frame->color_range_min, frame->color_range_max);
			color_space_ = color_space;
			video_format_ = format;
			if (!success) {
				frame->format = VIDEO_FORMAT_NONE;
				blog(LOG_ERROR, "video format is none?");
				return false;
			}
		}
		if (frame->format == VIDEO_FORMAT_NONE) {
			blog(LOG_ERROR, "video format is none?");
			return false;
		}

		switch (sw_frame_->color_trc) {
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

		return true;
	} else {
		int channels;
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(59, 19, 100)
		channels = sw_frame_->channels;
#else
		channels = sw_frame_->ch_layout.nb_channels;
#endif
		for (size_t i = 0; i < MAX_AV_PLANES; i++) audio->data[i] = sw_frame_->data[i];

		audio->samples_per_sec = sw_frame_->sample_rate;
		audio->speakers = convert_speaker_layout(channels);
		audio->format = convert_sample_format(sw_frame_->format);
		audio->frames = sw_frame_->nb_samples;
		audio->timestamp = time.tv_sec;

		if (audio->format == AUDIO_FORMAT_UNKNOWN)
			return false;

		return true;
	}
}

bool Decoder::DecodePacket(unsigned char* buffer, ssize_t size) {
	if (buffer == nullptr || size == 0) {
		return false;
	}
	if (codec_ctx_ == nullptr) {
		return false;
	}

	pkt_->data = buffer;
	pkt_->size = (int)size;

	// send the packet to decoder
	auto ret = avcodec_send_packet(codec_ctx_, pkt_);
	if (ret < 0) {
		blog(LOG_INFO, "sending a packet for decoding failed, error: %s", av_err2str(ret));
		av_packet_unref(pkt_);
		return false;
	}

	// receive the decoded frame
	ret = avcodec_receive_frame(codec_ctx_, in_frame_);
	if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
		return false;
	} else if (ret < 0) {
		blog(LOG_INFO, "decoding failed, error: %s\n", av_err2str(ret));
		av_packet_unref(pkt_);
		return false;
	}

	return true;
}

bool Decoder::HardwareFormatTypeAvailable(const AVCodec* c, AVHWDeviceType type) {
	for (int i = 0;; i++) {
		const AVCodecHWConfig* config = avcodec_get_hw_config(c, i);
		if (!config) {
			break;
		}

		if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
		    config->device_type == type) {
			hw_format_ = config->pix_fmt;
			return true;
		}
	}

	return false;
}

void Decoder::InitHardwareDecoder(const AVCodec* codec) {
	const AVHWDeviceType* priority = hw_priority;
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

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

RtspSource::RtspSource(obs_data_t* settings, obs_source_t* source)
  : settings_(settings),
    source_(source),
    rtsp_url_(""),
    client_(nullptr),
    video_decoder_(nullptr),
    audio_decoder_(nullptr),
    fmt_ctx_(nullptr),
    hw_decode_(false),
    video_disabled_(false),
    audio_disabled_(false) {
	auto url = obs_data_get_string(settings, "url");
	rtsp_url_ = url;
	media_state_ = OBS_MEDIA_STATE_NONE;

	blog(LOG_INFO, "play rtsp source url: %s", url);

	// try to play the RTSP stream
	PrepareToPlay();
}

RtspSource::~RtspSource() {
	if (client_ != nullptr) {
		delete client_;
		client_ = nullptr;
	}

	// release ffmpeg stuff
	DestoryFFmpeg();
}

void RtspSource::Update(obs_data_t* settings) {
	bool need_restart = false;

	std::string url = obs_data_get_string(settings_, "url");
	bool hw_decode = obs_data_get_bool(settings_, "hw_decode");
	bool disable_video = obs_data_get_bool(settings_, "block_video");
	bool disable_audio = obs_data_get_bool(settings_, "block_audio");

	if (url != rtsp_url_) // url changed
		need_restart = true;
	if (hw_decode != hw_decode_) // hw decode changed
		need_restart = true;
	if (disable_audio != audio_disabled_) // audio disabled changed
		need_restart = true;
	if (disable_video != video_disabled_) // video disabled changed
		need_restart = true;

	if (need_restart)
		PrepareToPlay();
}

void RtspSource::GetDefaults(obs_data_t* settings) {
	obs_data_set_default_string(settings, "url", "rtsp://");
	obs_data_set_default_bool(settings, "stop_on_hide", true);
	obs_data_set_default_int(settings, "restart_timeout", 20);
	obs_data_set_default_bool(settings, "block_video", false);
	obs_data_set_default_bool(settings, "block_audio", false);
	obs_data_set_default_bool(settings, "hw_decode", false);
}

obs_properties* RtspSource::GetProperties() {
	obs_properties_t* props = obs_properties_create();

	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_property_t* prop = obs_properties_add_text(props, "url", "RTSP URL", OBS_TEXT_DEFAULT);
	obs_property_set_long_description(prop, "Specify the RTSP URL to play");

	obs_properties_add_int(props, "restart_timeout", "Error timeout seconds", 5, 20, 1);
	obs_properties_add_bool(props, "stop_on_hide", "Stop playing when hidden");
	obs_properties_add_bool(props, "block_video", "Disable video");
	obs_properties_add_bool(props, "block_audio", "Disable audio");
	obs_properties_add_bool(props, "hw_decode", "Use hardware decode if possible");

	obs_properties_add_button2(
	  props, "apply", "Apply",
	  [](obs_properties_t* props, obs_property_t* property, void* pri_data) -> bool {
		  auto source = static_cast<RtspSource*>(pri_data);
		  return source->OnApplyBtnClicked(props, property);
	  },
	  this);

	return props;
}

enum obs_media_state RtspSource::GetState() {
	return media_state_;
}

void RtspSource::Stop() {
	media_state_ = OBS_MEDIA_STATE_STOPPED;

	if (client_) { // delete the rtsp client(will stop receive RTSP stream)
		delete client_;
		client_ = nullptr;
	}

	// release FFmpeg stuff include context & decoders
	DestoryFFmpeg();
}

void RtspSource::Hide() {
	auto stop_on_hide = obs_data_get_bool(settings_, "stop_on_hide");
	if (stop_on_hide) {
		Stop();
	}
}

void RtspSource::Show() {
	auto stop_on_hide = obs_data_get_bool(settings_, "stop_on_hide");
	if (stop_on_hide) {
		PrepareToPlay();
	}
}

bool RtspSource::OnApplyBtnClicked(obs_properties_t* props, obs_property_t* property) {
	return PrepareToPlay();
}

bool RtspSource::PrepareToPlay() {
	// check RTSP URL is valid
	std::string url = obs_data_get_string(settings_, "url");
	if (url.empty()) {
		blog(LOG_ERROR, "RTSP url is empty");
		return false;
	}

	auto [username, password, rtsp] = utils::ExtractRtspUrl(url);
	if (rtsp.empty()) {
		blog(LOG_ERROR, "Current RTSP url(%s) is invalid", url.c_str());
		return false;
	}

	rtsp_url_ = rtsp;
	blog(LOG_INFO, "play rtsp source url: %s", rtsp_url_.c_str());

	// stop the already running session
	Stop();

	uint64_t timeout = obs_data_get_int(settings_, "restart_timeout");
	std::map<std::string, std::string> opts;
	opts["timeout"] = std::to_string(timeout);

	// save the configures
	hw_decode_ = obs_data_get_bool(settings_, "hw_decode");
	video_disabled_ = obs_data_get_bool(settings_, "block_video");
	audio_disabled_ = obs_data_get_bool(settings_, "block_audio");

	// create rtsp client and start playing the a/v
	client_ = new source::RtspClient(rtsp_url_, opts, this);

	return true;
}

// override methods
bool RtspSource::OnVideoSessionStarted(const char* codec, int width, int height) {
	blog(LOG_INFO, "RTSP video session started");
	if (video_disabled_) { // nothing to play with
		blog(LOG_INFO, "no media source enabled");
		return false;
	}
	bool ret = InitFFmpeg();
	if (!ret) {
		blog(LOG_ERROR, "Init ffmpeg format failed");
		return false;
	}

  // init decoders
  auto codec_name = utils::string::ToLower(codec);
  bool hw_decode = obs_data_get_bool(settings_, "hw_decode");
	if (video_decoder_ == nullptr) {
		video_decoder_ = new Decoder(true, hw_decode, codec_name);
	}

	media_state_ = OBS_MEDIA_STATE_PLAYING;

	return video_decoder_->Init();
}

bool RtspSource::OnAudioSessionStarted(const char* codec, int rate, int channels) {
	blog(LOG_INFO, "RTSP audio session started");
	if (audio_disabled_) { // nothing to play with
		blog(LOG_INFO, "no media source enabled");
		return false;
	}
	bool ret = InitFFmpeg();
	if (!ret) {
		blog(LOG_ERROR, "Init ffmpeg format failed");
		return false;
	}

  // init decoders
  auto codec_name = utils::string::ToLower(codec);
  bool hw_decode = obs_data_get_bool(settings_, "hw_decode");
	if (audio_decoder_ == nullptr) {
		audio_decoder_ = new Decoder(false, hw_decode, codec_name);
	}

	media_state_ = OBS_MEDIA_STATE_PLAYING;

	return audio_decoder_->Init(rate, channels);
}

void RtspSource::OnSessionStopped(const char* msg) {
	blog(LOG_INFO, "RTSP session stopped, message: %s", msg);
	media_state_ = OBS_MEDIA_STATE_STOPPED;
}

void RtspSource::OnError(const char* msg) {
	blog(LOG_INFO, "RTSP session error, message: %s", msg);
	media_state_ = OBS_MEDIA_STATE_STOPPED;
}

void RtspSource::OnData(unsigned char* buffer, ssize_t size, timeval time, bool video) {
	if (video) {
		if (video_decoder_->Decode(buffer, size, time, &obs_frame_, nullptr)) {
			// send to obs
			obs_source_output_video(source_, &obs_frame_);
		}
	} else {
		struct obs_source_audio audio = {0};
		if (audio_decoder_->Decode(buffer, size, time, nullptr, &audio)) {
			// send to obs
			obs_source_output_audio(source_, &audio);
		}
	}
}

void RtspSource::DestoryFFmpeg() {
	if (fmt_ctx_ != nullptr) {
		avformat_free_context(fmt_ctx_);
		fmt_ctx_ = nullptr;
	}

	if (video_decoder_ != nullptr) {
		delete video_decoder_;
		video_decoder_ = nullptr;
	}
	if (audio_decoder_ != nullptr) {
		delete audio_decoder_;
		audio_decoder_ = nullptr;
	}
}

bool RtspSource::InitFFmpeg() {
	// init format context
	if (fmt_ctx_ == nullptr) {
		fmt_ctx_ = avformat_alloc_context();
		if (fmt_ctx_ == nullptr) {
			blog(LOG_ERROR, "AVFormatContext init failed");
			return false;
		}
	}

	return true;
}

void register_rtsp_source() {
	struct obs_source_info info = {};

	info.id = "rtsp_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
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
	info.media_stop = [](void* priv_data) {
		static_cast<RtspSource*>(priv_data)->Stop();
	};
	info.media_get_state = [](void* priv_data) -> enum obs_media_state {
		return static_cast<RtspSource*>(priv_data)->GetState();
	};
	info.icon_type = OBS_ICON_TYPE_MEDIA;

	obs_register_source(&info);
}
