#include <obs-module.h>

class RtspSource {
public:
	RtspSource(obs_data_t *settings, obs_source_t *source);
	~RtspSource();

	static void GetDefaults(obs_data_t *settings);

	obs_properties_t *GetProperties();
	void Update(obs_data_t *settings);
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

private:
	obs_source_t *source;
	obs_data_t *settings;
};

void register_rtsp_source();
