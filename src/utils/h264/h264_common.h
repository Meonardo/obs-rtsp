#ifndef COMMON_VIDEO_H264_H264_COMMON_H_
#define COMMON_VIDEO_H264_H264_COMMON_H_

#include "src/utils/video_utils.h"

#include <optional>

// this file is heavily brorrowed from chromium project
// please see: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/common_video/h264/h264_common.h

namespace utils::h264 {
// The size of a full NALU start sequence {0 0 0 1}, used for the first NALU
// of an access unit, and for SPS and PPS blocks.
const size_t kNaluLongStartSequenceSize = 4;

// The size of a shortened NALU start sequence {0 0 1}, that may be used if
// not the first NALU of an access unit or an SPS or PPS block.
const size_t kNaluShortStartSequenceSize = 3;

// The size of the NALU type byte (1).
const size_t kNaluTypeSize = 1;

enum NaluType : uint8_t {
	kSlice = 1,
	kIdr = 5,
	kSei = 6,
	kSps = 7,
	kPps = 8,
	kAud = 9,
	kEndOfSequence = 10,
	kEndOfStream = 11,
	kFiller = 12,
	kPrefix = 14,
	kStapA = 24,
	kFuA = 28
};

enum SliceType : uint8_t { kP = 0, kB = 1, kI = 2, kSp = 3, kSi = 4 };

// Returns a vector of the NALU indices in the given buffer.
std::vector<video::NaluIndex> FindNaluIndices(const uint8_t* buffer, size_t buffer_size);

// Get the NAL type from the header byte immediately following start sequence.
NaluType ParseNaluType(uint8_t data);

// Methods for parsing and writing RBSP. See section 7.4.1 of the H264 spec.
//
// The following sequences are illegal, and need to be escaped when encoding:
// 00 00 00 -> 00 00 03 00
// 00 00 01 -> 00 00 03 01
// 00 00 02 -> 00 00 03 02
// And things in the source that look like the emulation byte pattern (00 00 03)
// need to have an extra emulation byte added, so it's removed when decoding:
// 00 00 03 -> 00 00 03 03
//
// Decoding is simply a matter of finding any 00 00 03 sequence and removing
// the 03 emulation byte.

// Parse the given data and remove any emulation byte escaping.
std::vector<uint8_t> ParseRbsp(const uint8_t* data, size_t length);

// Representation of a SPS NALU.
struct SpsNalu {
	SpsNalu() = default;
	SpsNalu(const SpsNalu&) = default;
	~SpsNalu() = default;

	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t delta_pic_order_always_zero_flag = 0;
	uint32_t separate_colour_plane_flag = 0;
	uint32_t frame_mbs_only_flag = 0;
	uint32_t log2_max_frame_num = 4;         // Smallest valid value.
	uint32_t log2_max_pic_order_cnt_lsb = 4; // Smallest valid value.
	uint32_t pic_order_cnt_type = 0;
	uint32_t max_num_ref_frames = 0;
	uint32_t vui_params_present = 0;
	uint32_t id = 0;
};

// Parse the given buffer data and return a SpsNalu struct.
std::optional<SpsNalu> ParseSps(const std::vector<uint8_t>& data);
} // namespace utils::h264

#endif // COMMON_VIDEO_H264_H264_COMMON_H_
