#include "h264_common.h"

namespace utils::h264 {
constexpr uint8_t kNaluTypeMask = 0x1F;
constexpr int kScalingDeltaMin = -128;
constexpr int kScaldingDeltaMax = 127;

std::vector<video::NaluIndex> FindNaluIndices(const uint8_t* buffer, size_t buffer_size) {
	// This is sorta like Boyer-Moore, but with only the first optimization step:
	// given a 3-byte sequence we're looking at, if the 3rd byte isn't 1 or 0,
	// skip ahead to the next 3-byte sequence. 0s and 1s are relatively rare, so
	// this will skip the majority of reads/checks.
	std::vector<video::NaluIndex> sequences;
	if (buffer_size < kNaluShortStartSequenceSize)
		return sequences;

	static_assert(kNaluShortStartSequenceSize >= 2,
		      "kNaluShortStartSequenceSize must be larger or equals to 2");
	const size_t end = buffer_size - kNaluShortStartSequenceSize;
	for (size_t i = 0; i < end;) {
		if (buffer[i + 2] > 1) {
			i += 3;
		} else if (buffer[i + 2] == 1) {
			if (buffer[i + 1] == 0 && buffer[i] == 0) {
				// We found a start sequence, now check if it was a 3 of 4 byte one.
				video::NaluIndex index = {i, i + 3, 0};
				if (index.start_offset > 0 && buffer[index.start_offset - 1] == 0)
					--index.start_offset;

				// Update length of previous entry.
				auto it = sequences.rbegin();
				if (it != sequences.rend())
					it->payload_size =
					  index.start_offset - it->payload_start_offset;

				sequences.push_back(index);
			}

			i += 3;
		} else {
			++i;
		}
	}

	// Update length of last entry, if any.
	auto it = sequences.rbegin();
	if (it != sequences.rend())
		it->payload_size = buffer_size - it->payload_start_offset;

	return sequences;
}

NaluType ParseNaluType(uint8_t data) {
	return static_cast<NaluType>(data & kNaluTypeMask);
}

std::vector<uint8_t> ParseRbsp(const uint8_t* data, size_t length) {
	std::vector<uint8_t> out;
	out.reserve(length);

	for (size_t i = 0; i < length;) {
		// Be careful about over/underflow here. byte_length_ - 3 can underflow, and
		// i + 3 can overflow, but byte_length_ - i can't, because i < byte_length_
		// above, and that expression will produce the number of bytes left in
		// the stream including the byte at i.
		if (length - i >= 3 && !data[i] && !data[i + 1] && data[i + 2] == 3) {
			// Two rbsp bytes.
			out.push_back(data[i++]);
			out.push_back(data[i++]);
			// Skip the emulation byte.
			i++;
		} else {
			// Single rbsp byte.
			out.push_back(data[i++]);
		}
	}
	return out;
}

std::optional<SpsNalu> ParseSps(const std::vector<uint8_t>& data) {
	video::BitstreamReader bitstream(data);
	video::ExponentialGolombReader reader(bitstream);

	SpsNalu sps;
	if (reader.ReadBits(1) != 0) { // forbidden_zero_bit not 0
		return std::optional<SpsNalu>();
	}

	if (reader.ReadBits(2) != 3) { // nal_ref_idc not 3
		return std::optional<SpsNalu>();
	}

	if (reader.ReadBits(5) != 7) {
		return std::optional<SpsNalu>(); // nal_unit_type not 7, its not a SPS NAL unit
	}
	// chroma_format_idc will be ChromaArrayType if separate_colour_plane_flag is
	// 0. It defaults to 1, when not specified.
	uint32_t chroma_format_idc = 1;

	// profile_idc: u(8). We need it to determine if we need to read/skip chroma
	// formats.
	uint32_t profile_idc = reader.ReadBits(8);

	// constraint_set0_flag through constraint_set5_flag + reserved_zero_2bits
	// 1 bit each for the flags + 2 bits + 8 bits for level_idc = 16 bits.
	reader.ReadBits(16);

	// level_idc
	// seq_parameter_set_id: ue(v)
	sps.id = reader.ReadUE();
	sps.separate_colour_plane_flag = 0;

	if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 ||
	    profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
	    profile_idc == 128 || profile_idc == 138 || profile_idc == 139 || profile_idc == 134) {
		uint32_t chroma_format_idc = reader.ReadUE();
		if (chroma_format_idc == 3) {
			// separate_colour_plane_flag: u(1)
			sps.separate_colour_plane_flag = reader.ReadBit();
		}
		reader.ReadUE();        // bit_depth_luma_minus8
		reader.ReadUE();        // bit_depth_chroma_minus8
		reader.ReadBits(1);     // qpprime_y_zero_transform_bypass_flag
		if (reader.ReadBit()) { // seq_scaling_matrix_present_flag
			// Process the scaling lists just enough to be able to properly
			// skip over them, so we can still read the resolution on streams
			// where this is included.
			int scaling_list_count = (chroma_format_idc == 3 ? 12 : 8);
			for (int i = 0; i < scaling_list_count; ++i) {
				// seq_scaling_list_present_flag[i]
				if (reader.ReadBit()) {
					int last_scale = 8;
					int next_scale = 8;
					int size_of_scaling_list = i < 6 ? 16 : 64;
					for (int j = 0; j < size_of_scaling_list; j++) {
						if (next_scale != 0) {
							// delta_scale: se(v)
							int delta_scale = reader.ReadSE();
							if (delta_scale < kScalingDeltaMin ||
							    delta_scale > kScaldingDeltaMax) {
								return std::optional<SpsNalu>();
							}
							next_scale =
							  (last_scale + delta_scale + 256) % 256;
						}
						if (next_scale != 0)
							last_scale = next_scale;
					}
				}
			}
		}
	}

	// log2_max_frame_num and log2_max_pic_order_cnt_lsb are used with
	// BitstreamReader::ReadBits, which can read at most 64 bits at a time. We
	// also have to avoid overflow when adding 4 to the on-wire golomb value,
	// e.g., for evil input data, ReadExponentialGolomb might return 0xfffc.
	const uint32_t kMaxLog2Minus4 = 32 - 4;
	auto log2_max_frame_num_minus4 = reader.ReadUE(); // log2_max_frame_num_minus4
	if (log2_max_frame_num_minus4 > kMaxLog2Minus4) {
		return std::optional<SpsNalu>();
	}
	sps.log2_max_frame_num = log2_max_frame_num_minus4 + 4;

	// pic_order_cnt_type: ue(v)
	sps.pic_order_cnt_type = reader.ReadUE();
	if (sps.pic_order_cnt_type == 0) {
		// log2_max_pic_order_cnt_lsb_minus4: ue(v)
		uint32_t log2_max_pic_order_cnt_lsb_minus4 = reader.ReadUE();
		if (log2_max_pic_order_cnt_lsb_minus4 > kMaxLog2Minus4) {
			return std::optional<SpsNalu>();
		}
		sps.log2_max_pic_order_cnt_lsb = log2_max_pic_order_cnt_lsb_minus4 + 4;
	} else if (sps.pic_order_cnt_type == 1) {
		// delta_pic_order_always_zero_flag: u(1)
		sps.delta_pic_order_always_zero_flag = reader.ReadBit();
		// offset_for_non_ref_pic: se(v)
		reader.ReadSE();
		// offset_for_top_to_bottom_field: se(v)
		reader.ReadSE();
		// num_ref_frames_in_pic_order_cnt_cycle: ue(v)
		auto num_ref_frames_in_pic_order_cnt_cycle = reader.ReadUE();
		for (size_t i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; ++i) {
			// offset_for_ref_frame[i]: se(v)
			reader.ReadSE();
		}
	}

	// max_num_ref_frames: ue(v)
	sps.max_num_ref_frames = reader.ReadUE();
	// gaps_in_frame_num_value_allowed_flag: u(1)
	reader.ReadBits(1);

	//
	// IMPORTANT ONES! Now we're getting to resolution. First we read the pic
	// width/height in macroblocks (16x16), which gives us the base resolution,
	// and then we continue on until we hit the frame crop offsets, which are used
	// to signify resolutions that aren't multiples of 16.
	//
	// pic_width_in_mbs_minus1: ue(v)
	sps.width = (reader.ReadUE() + 1) * 16;
	// pic_height_in_map_units_minus1: ue(v)
	auto pic_height_in_map_units_minus1 = reader.ReadUE();
	// frame_mbs_only_flag: u(1)
	sps.frame_mbs_only_flag = reader.ReadBit();
	if (!sps.frame_mbs_only_flag) {
		// mb_adaptive_frame_field_flag: u(1)
		reader.ReadBits(1);
	}
	sps.height = 16 * (2 - sps.frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1);
	// direct_8x8_inference_flag: u(1)
	reader.ReadBits(1);

	//
	// MORE IMPORTANT ONES! Now we're at the frame crop information.
	//
	uint32_t frame_crop_left_offset = 0;
	uint32_t frame_crop_right_offset = 0;
	uint32_t frame_crop_top_offset = 0;
	uint32_t frame_crop_bottom_offset = 0;
	// frame_cropping_flag: u(1)
	auto frame_cropping_flag = reader.ReadBit();
	if (frame_cropping_flag) {
		// frame_crop_left_offset: ue(v)
		frame_crop_left_offset = reader.ReadUE();
		// frame_crop_right_offset: ue(v)
		frame_crop_right_offset = reader.ReadUE();
		// frame_crop_top_offset: ue(v)
		frame_crop_top_offset = reader.ReadUE();
		// frame_crop_bottom_offset: ue(v)
		frame_crop_bottom_offset = reader.ReadUE();
	}

	// vui_parameters_present_flag: u(1)
	auto vui_parameters_present_flag = reader.ReadBit();
	if (!vui_parameters_present_flag) {
		return std::optional<SpsNalu>();
	}

	// Figure out the crop units in pixels. That's based on the chroma format's
	// sampling, which is indicated by chroma_format_idc.
	if (sps.separate_colour_plane_flag || chroma_format_idc == 0) {
		frame_crop_bottom_offset *= (2 - sps.frame_mbs_only_flag);
		frame_crop_top_offset *= (2 - sps.frame_mbs_only_flag);
	} else if (!sps.separate_colour_plane_flag && chroma_format_idc > 0) {
		// Width multipliers for formats 1 (4:2:0) and 2 (4:2:2).
		if (chroma_format_idc == 1 || chroma_format_idc == 2) {
			frame_crop_left_offset *= 2;
			frame_crop_right_offset *= 2;
		}
		// Height multipliers for format 1 (4:2:0).
		if (chroma_format_idc == 1) {
			frame_crop_top_offset *= 2;
			frame_crop_bottom_offset *= 2;
		}
	}
	// Subtract the crop for each dimension.
	sps.width -= (frame_crop_left_offset + frame_crop_right_offset);
	sps.height -= (frame_crop_top_offset + frame_crop_bottom_offset);

	return std::optional<SpsNalu>(sps);
}

} // namespace utils::h264
