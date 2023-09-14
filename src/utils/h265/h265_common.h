#ifndef COMMON_VIDEO_H265_H265_COMMON_H_
#define COMMON_VIDEO_H265_H265_COMMON_H_

#include <memory>
#include <vector>
#include <optional>

#include "src/utils/video_utils.h"

namespace utils::h265 {
// The size of a full NALU start sequence {0 0 0 1}, used for the first NALU
// of an access unit, and for SPS and PPS blocks.
const size_t kNaluLongStartSequenceSize = 4;

// The size of a shortened NALU start sequence {0 0 1}, that may be used if
// not the first NALU of an access unit or an SPS or PPS block.
const size_t kNaluShortStartSequenceSize = 3;

// The size of the NALU type byte (2).
const size_t kNaluTypeSize = 2;

enum NaluType : uint8_t {
	kTrailN = 0,
	kTrailR = 1,
	kTsaN = 2,
	kTsaR = 3,
	kStsaN = 4,
	kStsaR = 5,
	kRadlN = 6,
	kRadlR = 7,
	kBlaWLp = 16,
	kBlaWRadl = 17,
	kBlaNLp = 18,
	kIdrWRadl = 19,
	kIdrNLp = 20,
	kCra = 21,
	kRsvIrapVcl23 = 23,
	kVps = 32,
	kSps = 33,
	kPps = 34,
	kAud = 35,
	kPrefixSei = 39,
	kSuffixSei = 40,
	kAP = 48,
	kFU = 49
};

enum SliceType : uint8_t { kB = 0, kP = 1, kI = 2 };

// Returns a vector of the NALU indices in the given buffer.
std::vector<video::NaluIndex> FindNaluIndices(const uint8_t* buffer, size_t buffer_size);

// Get the NAL type from the header byte immediately following start sequence.
NaluType ParseNaluType(uint8_t data);

// Methods for parsing and writing RBSP. See section 7.4.2 of the H265 spec.
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

uint32_t Log2(uint32_t value);

struct ShortTermRefPicSet {
	ShortTermRefPicSet() = default;
	~ShortTermRefPicSet() = default;

	uint32_t inter_ref_pic_set_prediction_flag = 0;
	std::vector<uint32_t> used_by_curr_pic_flag;
	std::vector<uint32_t> use_delta_flag;
	uint32_t num_negative_pics = 0;
	uint32_t num_positive_pics = 0;
	std::vector<uint32_t> delta_poc_s0_minus1;
	std::vector<uint32_t> used_by_curr_pic_s0_flag;
	std::vector<uint32_t> delta_poc_s1_minus1;
	std::vector<uint32_t> used_by_curr_pic_s1_flag;
};

// The parsed state of the SPS. Only some select values are stored.
// Add more as they are actually needed.
struct SpsNalu {
	SpsNalu() = default;
	~SpsNalu() = default;

	uint32_t sps_max_sub_layers_minus1;
	uint32_t chroma_format_idc = 0;
	uint32_t separate_colour_plane_flag = 0;
	uint32_t pic_width_in_luma_samples = 0;
	uint32_t pic_height_in_luma_samples = 0;
	uint32_t log2_max_pic_order_cnt_lsb_minus4 = 0;
	std::vector<uint32_t> sps_max_dec_pic_buffering_minus1;
	uint32_t log2_min_luma_coding_block_size_minus3 = 0;
	uint32_t log2_diff_max_min_luma_coding_block_size = 0;
	uint32_t sample_adaptive_offset_enabled_flag = 0;
	uint32_t num_short_term_ref_pic_sets = 0;
	std::vector<ShortTermRefPicSet> short_term_ref_pic_set;
	uint32_t long_term_ref_pics_present_flag = 0;
	uint32_t num_long_term_ref_pics_sps = 0;
	std::vector<uint32_t> used_by_curr_pic_lt_sps_flag;
	uint32_t sps_temporal_mvp_enabled_flag = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t id = 0;
	uint32_t vps_id = 0;
};

bool ParseScalingListData(video::ExponentialGolombReader& reader);
std::optional<ShortTermRefPicSet>
ParseShortTermRefPicSet(uint32_t st_rps_idx, uint32_t num_short_term_ref_pic_sets,
			const std::vector<ShortTermRefPicSet>& ref_pic_sets, SpsNalu& sps,
			video::ExponentialGolombReader& reader);

// Parse the given SPS NALU buffer and return the parsed SpsNalu.
std::optional<h265::SpsNalu> ParseSps(const std::vector<uint8_t>& buffer);

} // namespace utils::h265

#endif // COMMON_VIDEO_H265_H265_COMMON_H_
