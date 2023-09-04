cmake_minimum_required(VERSION 3.22...3.25)

option(ENABLE_RTSP "Enable WebRTC Output support" ON)
if(NOT ENABLE_RTSP)
  target_disable(obs-rtsp)
  return()
endif()

# live555
add_subdirectory(deps/live555)
set(LIVE555 deps/live555/live)

add_library(obs-rtsp MODULE)
add_library(OBS::rtsp ALIAS obs-rtsp)

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
  rtsp_source.h
  rtsp_source.cpp

  # server

  # client
  client/rtsp_client.h
  client/rtsp_client.cpp
)

target_link_libraries(
  obs-rtsp
  PRIVATE

  OBS::libobs
)

set_target_properties_obs(obs-rtsp PROPERTIES FOLDER plugins PREFIX "")
