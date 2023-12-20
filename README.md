# OBS RTSP source & output
- Directly add RTSP camera stream as an obs-source, no other external dependencies needed(but still requires FFmpeg);
- Make obs-studio as a RTSP server(can only pull video(H.264 codec) stream from it).

## Motivation
1. there is a delay issue with `Media Source` in OBS, even I set the `network buffering` to zero;
2. `obs-gstreamer` is a great [plugin](https://github.com/fzwoch/obs-gstreamer), I can add RTSP URI as a source and play it with an ideal delay, but I have to include lots of the gstreamer lib dependencies, it's pain;

## Status
- Video:
  - [x] H.264;
  - [x] H.265;
- Audio:
  - [x] AAC
  - [ ] more tests
- RTSP server:
  - WIP  
- Issue:
  - decode audio & video in the same thread

## Credit
- `liblive555helper` is based on [mpromonet/live555helper](https://github.com/mpromonet/live555helper);
