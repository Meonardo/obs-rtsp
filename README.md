# OBS RTSP source & output
- Directly add RTSP camera stream as an obs-source, no other external dependencies needed(but still require FFmpeg);
- Make obs-studio as a RTSP server(can only pull a/v stream from it).

## Motivation
1. there is a delay issue with `Media Source` in OBS, even I set the `network buffering` to zero;
2. `obs-gstreamer` is great plugin, I can add RTSP URI as a source and play it with an ideal delay, but I have to include tons of the gstreamer lib dependencies, it's pain;

## Status
- Video:
  - [x] H.264;
  - [x] H.265;
- Audio:
  - WIP
- RTSP server:
  - WIP  
