#include <obs-module.h>

#include "src/rtsp_source.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-rtsp", "en-US")
MODULE_EXPORT const char* obs_module_description(void) {
	return "OBS rtsp module";
}

bool obs_module_load() {
	register_rtsp_source();

	return true;
}
