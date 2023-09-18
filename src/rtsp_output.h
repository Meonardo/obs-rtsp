#pragma once

#include <obs-module.h>

#include "src/server/rtsp_server.h"

class RtspOutput {
public:
	RtspOutput(obs_data_t* settings, obs_output_t* output);
	~RtspOutput();

  // obs output related functions begin
  bool Start();
  bool Stop(bool signal = true);
  void Data(struct encoder_packet* packet);
  size_t GetTotalBytes();
  int GetConnectTime();
  // obs output related functions end

private:
	obs_output_t* output_;
	obs_data_t* settings_;

  output::RtspServer* server_;
};

void register_rtsp_output();
