#pragma once

#include <vector>
#include <stdexcept>
#include <cstdint>

namespace utils::video {

struct NaluIndex {
	// Start index of NALU, including start sequence.
	size_t start_offset;
	// Start index of NALU payload, typically type header.
	size_t payload_start_offset;
	// Length of NALU payload, in bytes, counting from payload_start_offset.
	size_t payload_size;
};

// A simple bitstream reader
class BitstreamReader {
public:
	BitstreamReader(const std::vector<uint8_t>& data) : data_(data), bit_position_(0) {}

	uint32_t ReadBits(int numBits) {
		uint32_t value = 0;
		for (int i = 0; i < numBits; i++) {
			value <<= 1;
			value |= ReadBit() ? 1 : 0;
		}
		return value;
	}

	bool ReadBit() {
		if (bit_position_ >= data_.size() * 8) {
			throw std::runtime_error("End of stream");
		}
		bool bit = data_[bit_position_ / 8] & (0x80 >> (bit_position_ % 8));
		bit_position_++;
		return bit;
	}

private:
	const std::vector<uint8_t>& data_;
	size_t bit_position_;
};

// A simple Exp-Golomb code reader
class ExponentialGolombReader {
public:
	ExponentialGolombReader(BitstreamReader& bitstream) : bitstream_(bitstream) {}

	uint32_t ReadUE() {
		int leadingZeroBits = -1;
		for (bool bit = false; !bit; leadingZeroBits++) { bit = bitstream_.ReadBit(); }
		return ((1 << leadingZeroBits) - 1) + bitstream_.ReadBits(leadingZeroBits);
	}

	int32_t ReadSE() {
		uint32_t unsigned_val = ReadUE();
		if (unsigned_val % 2 == 0) {
			return -static_cast<int>(unsigned_val / 2);
		} else {
			return (unsigned_val + 1) / 2;
		}
	}

  uint32_t ReadBits(int numBits) {
    return bitstream_.ReadBits(numBits);
  }

  bool ReadBit() {
    return bitstream_.ReadBit();
  }

private:
	BitstreamReader& bitstream_;
};

} // namespace utils::video
