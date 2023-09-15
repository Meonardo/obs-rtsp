cmake_minimum_required(VERSION 3.22...3.25)

option(ENABLE_RTSP "Enable RTSP source support" ON)
if(NOT ENABLE_RTSP)
  target_disable(obs-rtsp)
  return()
endif()

find_package(
  FFmpeg REQUIRED
  COMPONENTS avcodec
             avfilter
             avdevice
             avutil
             swscale
             avformat
             swresample)

add_library(obs-rtsp MODULE)
add_library(OBS::rtsp ALIAS obs-rtsp)

# live555
add_subdirectory(deps/live555)
set(LIVE555 deps/live555/live)

target_include_directories(
  obs-rtsp
  PRIVATE

  # live555 includes
  ${LIVE555}/groupsock/include 
  ${LIVE555}/liveMedia/include 
  ${LIVE555}/UsageEnvironment/include 
  ${LIVE555}/BasicUsageEnvironment/include
  ${LIVE555}/../include
)

target_sources(
  obs-rtsp
  PRIVATE

  obs-rtsp.cpp

  # source
  src/rtsp_source.h
  src/rtsp_source.cpp

  # output
  src/rtsp_output.h
  src/rtsp_output.cpp

  # utils
  src/utils/utils.h
  src/utils/utils.cpp
  src/utils/video_utils.h
  src/utils/h264/h264_common.h
  src/utils/h264/h264_common.cpp
  src/utils/h265/h265_common.h
  src/utils/h265/h265_common.cpp

  # server
  src/server/rtsp_server.h
  src/server/rtsp_server.cpp

  # client
  src/client/rtsp_client.h
  src/client/rtsp_client.cpp
)

target_link_libraries(
  obs-rtsp
  PRIVATE

  OBS::libobs
  liblive555helper

  # ffmpeg
  FFmpeg::avcodec
  FFmpeg::avfilter
  FFmpeg::avformat
  FFmpeg::avdevice
  FFmpeg::avutil
  FFmpeg::swscale
  FFmpeg::swresample
)

set_target_properties_obs(obs-rtsp PROPERTIES FOLDER plugins/obs-rtsp PREFIX "")
