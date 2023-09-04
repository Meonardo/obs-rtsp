#include "rtsp_source.h"

RtspSource::RtspSource(obs_data_t *settings, obs_source_t *source)
	: settings(settings),
	  source(source)
{
}

RtspSource::~RtspSource() {}

void RtspSource::Update(obs_data_t *settings) {}

void RtspSource::GetDefaults(obs_data_t *settings) {}

obs_properties *RtspSource::GetProperties()
{
	return nullptr;
}

int64_t RtspSource::GetTime()
{
	return 0;
}

int64_t RtspSource::GetDuration()
{
	return 0;
}

enum obs_media_state RtspSource::GetState()
{
	return OBS_MEDIA_STATE_NONE;
}

uint32_t RtspSource::GetWidth()
{
	return 0;
}

uint32_t RtspSource::GetHeight()
{
	return 0;
}

void RtspSource::PlayPause(bool pause) {}

void RtspSource::Restart() {}

void RtspSource::Stop() {}

void RtspSource::SetTime(int64_t ms) {}

void RtspSource::Hide() {}

void RtspSource::Show() {}

void register_rtsp_source()
{
	struct obs_source_info info = {};

	info.id = "rtsp_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO |
			    OBS_SOURCE_DO_NOT_DUPLICATE |
			    OBS_SOURCE_CONTROLLABLE_MEDIA;
	info.get_name = [](void *) -> const char * {
		return "obs-rtsp-source";
	};
	info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
		return new RtspSource(settings, source);
	};
	info.destroy = [](void *priv_data) {
		delete static_cast<RtspSource *>(priv_data);
	};
	info.get_defaults = [](obs_data_t *s) {
		RtspSource::GetDefaults(s);
	};
	info.get_properties = [](void *priv_data) -> obs_properties_t * {
		return static_cast<RtspSource *>(priv_data)->GetProperties();
	};
	info.update = [](void *priv_data, obs_data_t *settings) {
		static_cast<RtspSource *>(priv_data)->Update(settings);
	};
	info.show = [](void *priv_data) {
		static_cast<RtspSource *>(priv_data)->Show();
	};
	info.hide = [](void *priv_data) {
		static_cast<RtspSource *>(priv_data)->Hide();
	};
	info.get_width = [](void *priv_data) -> uint32_t {
		return static_cast<RtspSource *>(priv_data)->GetWidth();
	};
	info.get_height = [](void *priv_data) -> uint32_t {
		return static_cast<RtspSource *>(priv_data)->GetHeight();
	};
	info.media_play_pause = [](void *priv_data, bool pause) {
		static_cast<RtspSource *>(priv_data)->PlayPause(pause);
	};
	info.media_restart = [](void *priv_data) {
		static_cast<RtspSource *>(priv_data)->Restart();
	};
	info.media_stop = [](void *priv_data) {
		static_cast<RtspSource *>(priv_data)->Stop();
	};
	info.media_get_state = [](void *priv_data) -> enum obs_media_state {
		return static_cast<RtspSource *>(priv_data)->GetState();
	};
	info.media_get_time = [](void *priv_data) -> int64_t {
		return static_cast<RtspSource *>(priv_data)->GetTime();
	};
	info.media_get_duration = [](void *priv_data) -> int64_t {
		return static_cast<RtspSource *>(priv_data)->GetDuration();
	};
	info.media_set_time = [](void *priv_data, int64_t ms) {
		static_cast<RtspSource *>(priv_data)->SetTime(ms);
	};

	obs_register_source(&info);
}
