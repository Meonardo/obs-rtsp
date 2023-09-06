#include "rtsp_client.h"

namespace utils::h264 {
// The size of a full NALU start sequence {0 0 0 1}, used for the first NALU
// of an access unit, and for SPS and PPS blocks.
const size_t kNaluLongStartSequenceSize = 4;

// The size of a shortened NALU start sequence {0 0 1}, that may be used if
// not the first NALU of an access unit or an SPS or PPS block.
const size_t kNaluShortStartSequenceSize = 3;

// The size of the NALU type byte (1).
const size_t kNaluTypeSize = 1;

const uint8_t kNaluTypeMask = 0x1F;

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

struct NaluIndex {
	// Start index of NALU, including start sequence.
	size_t start_offset;
	// Start index of NALU payload, typically type header.
	size_t payload_start_offset;
	// Length of NALU payload, in bytes, counting from payload_start_offset.
	size_t payload_size;
};

// Returns a vector of the NALU indices in the given buffer.
std::vector<NaluIndex> FindNaluIndices(const uint8_t* buffer,
				       size_t buffer_size) {
	// This is sorta like Boyer-Moore, but with only the first optimization step:
	// given a 3-byte sequence we're looking at, if the 3rd byte isn't 1 or 0,
	// skip ahead to the next 3-byte sequence. 0s and 1s are relatively rare, so
	// this will skip the majority of reads/checks.
	std::vector<NaluIndex> sequences;
	if (buffer_size < kNaluShortStartSequenceSize)
		return sequences;

	static_assert(
	  kNaluShortStartSequenceSize >= 2,
	  "kNaluShortStartSequenceSize must be larger or equals to 2");
	const size_t end = buffer_size - kNaluShortStartSequenceSize;
	for (size_t i = 0; i < end;) {
		if (buffer[i + 2] > 1) {
			i += 3;
		} else if (buffer[i + 2] == 1) {
			if (buffer[i + 1] == 0 && buffer[i] == 0) {
				// We found a start sequence, now check if it was a 3 of 4 byte one.
				NaluIndex index = {i, i + 3, 0};
				if (index.start_offset > 0 &&
				    buffer[index.start_offset - 1] == 0)
					--index.start_offset;

				// Update length of previous entry.
				auto it = sequences.rbegin();
				if (it != sequences.rend())
					it->payload_size =
					  index.start_offset -
					  it->payload_start_offset;

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

// Get the NAL type from the header byte immediately following start sequence.
NaluType ParseNaluType(uint8_t data) {
	return static_cast<NaluType>(data & kNaluTypeMask);
}

// Utility function to decode unsigned exponential Golomb-coded values
uint32_t uev_decode(const uint8_t* data, size_t dataSize, size_t& offset) {
	uint32_t leadingZeros = 0;
	while (offset < dataSize * 8 && !data[offset / 8] &&
	       !(data[offset / 8 + 1] & (1 << (7 - offset % 8)))) {
		++leadingZeros;
		++offset;
	}

	if (offset >= dataSize * 8) {
		blog(LOG_ERROR,
		     "Error decoding exponential Golomb - coded value");
		return 0;
	}

	uint32_t codeNum = 0;
	for (int32_t i = leadingZeros; i >= 0; --i) {
		if (offset >= dataSize * 8) {
			blog(LOG_ERROR,
			     "Error decoding exponential Golomb-coded value");
			return 0;
		}
		codeNum |= ((data[offset / 8] >> (7 - offset % 8)) & 1) << i;
		++offset;
	}

	return (1 << leadingZeros) - 1 + codeNum;
}

// Function to extract video resolution from SPS NALU buffer
void GetVideoResolution(const uint8_t* data, size_t dataSize, int& width,
			int& height) {
	// Check for NALU start code (0x00000001 or 0x000001)
	size_t startIndex = 0;
	if (dataSize >= 3 && data[0] == 0x00 && data[1] == 0x00 &&
	    (data[2] == 0x01 || (data[2] == 0x00 && data[3] == 0x01)))
		startIndex = (data[2] == 0x01) ? 3 : 4;

	// Find the SPS NALU header
	size_t spsHeaderStartIndex = startIndex;
	while (spsHeaderStartIndex < dataSize - 3 &&
	       !(data[spsHeaderStartIndex] == 0x00 &&
		 data[spsHeaderStartIndex + 1] == 0x00 &&
		 (data[spsHeaderStartIndex + 2] == 0x01 ||
		  (data[spsHeaderStartIndex + 2] == 0x00 &&
		   data[spsHeaderStartIndex + 3] == 0x01)))) {
		++spsHeaderStartIndex;
	}

	// Make sure SPS NALU is found and has correct type
	if (spsHeaderStartIndex >= dataSize - 3 ||
	    data[spsHeaderStartIndex + 2] != 0x07) {
		blog(LOG_ERROR, "Invalid SPS NALU");
		return;
	}

	// Parse SPS payload to extract resolution information
	size_t offset = spsHeaderStartIndex + 4; // Start after NALU header
	uint32_t picWidthInMbsMinus1 = 0, picHeightInMapUnitsMinus1 = 0;
	uint32_t frameCropLeftOffset = 0, frameCropRightOffset = 0,
		 frameCropTopOffset = 0, frameCropBottomOffset = 0;

	// SPS parameters: profile_idc, constraint_set_flags, etc. (skip)
	offset += 2;

	// Get pic_width_in_mbs_minus1 and pic_height_in_map_units_minus1
	picWidthInMbsMinus1 = uev_decode(data, dataSize, offset);
	picHeightInMapUnitsMinus1 = uev_decode(data, dataSize, offset);

	// Skip frame_mbs_only_flag, direct_8x8_inference_flag, etc.
	offset += 5;

	// Check for frame cropping information present
	if (data[offset++] == 0x01) {
		// Get frame cropping offsets
		frameCropLeftOffset = uev_decode(data, dataSize, offset);
		frameCropRightOffset = uev_decode(data, dataSize, offset);
		frameCropTopOffset = uev_decode(data, dataSize, offset);
		frameCropBottomOffset = uev_decode(data, dataSize, offset);
	}

	// Calculate video resolution
	width = (picWidthInMbsMinus1 + 1) * 16 - frameCropLeftOffset * 2 -
		frameCropRightOffset * 2;
	height = (picHeightInMapUnitsMinus1 + 1) * 16 - frameCropTopOffset * 2 -
		 frameCropBottomOffset * 2;
}
} // namespace utils::h264

namespace source {
RtspClient::RtspClient(const std::string& uri,
		       const std::map<std::string, std::string>& opts,
		       RTSPClientObserver* observer)
  : observer_(observer),
    env_(nullptr),
    client_(nullptr),
    uri_(uri),
    opts_(opts) {
	Start();
}

RtspClient::~RtspClient() {
	Stop();
}

bool RtspClient::IsRunning() {
	return (client_ != nullptr);
}

uint32_t RtspClient::GetWidth() const {
	return width_;
}

uint32_t RtspClient::GetHeight() const {
	return height_;
}

void RtspClient::CaptureThread() {
	SetThreadDescription(GetCurrentThread(), L"rtsp_capture_thread");
	env_->mainloop();
}

void RtspClient::Start() {
	if (client_ != nullptr) {
		return;
	}

	env_ = new Environment;
	client_ = new RTSPConnection(*env_, this, uri_.c_str(), opts_, 2);
	capture_thread_ = std::thread(&RtspClient::CaptureThread, this);

	blog(LOG_INFO, "RTSP client started");
}

void RtspClient::Stop() {
	if (env_ != nullptr) {
		env_->stop();
	}
	capture_thread_.join();

	if (client_ != nullptr) {
		delete client_;
		client_ = nullptr;
	}
	if (env_ != nullptr) {
		delete env_;
		env_ = nullptr;
	}
	blog(LOG_INFO, "RTSP client stopped");
}

bool RtspClient::onNewSession(const char* id, const char* media,
			      const char* codec, const char* sdp) {
	bool success = false;
	if (strcmp(media, "video") == 0) {
		blog(
		  LOG_INFO,
		  "New session created: id: %s, media: %s, codec: %s, sdp: %s",
		  id, media, codec, sdp);

		if ((strcmp(codec, "H264") == 0)) { // only support H.264 codec
			codec_[id] = codec;
			success = true;
		}
	}
	return success;
}

bool RtspClient::onData(const char* id, unsigned char* buffer, ssize_t size,
			struct timeval presentationTime) {
	ProcessBuffer(id, buffer, size, presentationTime);
	return true;
}

void RtspClient::onError(RTSPConnection& connection, const char* message) {
	blog(LOG_ERROR, "RTSP client error : %s", message);
	observer_->OnError(message);
}

void RtspClient::onConnectionTimeout(RTSPConnection& connection) {
	blog(LOG_INFO, "RTSP client connect timeout");
	observer_->OnSessionStopped("timeout");
}

void RtspClient::onDataTimeout(RTSPConnection& connection) {
	blog(LOG_INFO, "RTSP client data timeout");
	observer_->OnSessionStopped("timeout");
}

void RtspClient::ProcessBuffer(const char* id, unsigned char* buffer,
			       ssize_t size, struct timeval presentationTime) {
	std::string codec = codec_[id];
	if (codec == "H264") {
		std::vector<utils::h264::NaluIndex> indexes =
		  utils::h264::FindNaluIndices(buffer, size);

		for (const utils::h264::NaluIndex& index : indexes) {
			utils::h264::NaluType nalu_type =
			  utils::h264::ParseNaluType(
			    buffer[index.payload_start_offset]);
			if (nalu_type == utils::h264::NaluType::kSps) {
				// blog(LOG_INFO, "SPS NALU");

				cfg_.clear();
				cfg_.insert(cfg_.end(),
					    buffer + index.start_offset,
					    buffer + index.payload_size +
					      index.payload_start_offset -
					      index.start_offset);
				int w = 0, h = 0;
				utils::h264::GetVideoResolution(
				  buffer + index.payload_start_offset,
				  index.payload_size, w, h);
				if (width_ && height_) {
					width_ = w;
					height_ = h;

					blog(
					  LOG_INFO,
					  "Parse video resolution, width: %d, height: %d",
					  w, h);
				}
			} else if (nalu_type == utils::h264::NaluType::kPps) {
				// blog(LOG_INFO, "PPS NALU");

				cfg_.insert(cfg_.end(),
					    buffer + index.start_offset,
					    buffer + index.payload_size +
					      index.payload_start_offset -
					      index.start_offset);
			} else if (nalu_type == utils::h264::NaluType::kSei) {
				// blog(LOG_INFO, "SEI NALU");
			} else {
				bool keyframe = false;
				std::vector<uint8_t> content;
				if (nalu_type == utils::h264::NaluType::kIdr) {
					keyframe = true;
					// blog(LOG_INFO, "IDR NALU");

					content.insert(content.end(),
						       cfg_.begin(),
						       cfg_.end());
				}
				// else {
				//	blog(LOG_DEBUG, "NALU type: %d",
				//	     nalu_type);
				// }

				content.insert(content.end(),
					       buffer + index.start_offset,
					       buffer + index.payload_size +
						 index.payload_start_offset -
						 index.start_offset);

				observer_->OnData(content.data(),
						  content.size(),
						  presentationTime);
			}
		}
	}
}

} // namespace source
