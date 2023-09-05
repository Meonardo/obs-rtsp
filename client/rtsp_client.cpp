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
} // namespace utils::h264

namespace source {
RtspClient::RtspClient(const std::string& uri,
		       const std::map<std::string, std::string>& opts,
		       RTSPClientObserver* observer)
  : stop_(0),
    client_(env_, this, uri.c_str(), opts, 2),
    observer_(observer) {
	this->Start();
}

RtspClient::~RtspClient() {
	this->Stop();
}

bool RtspClient::IsRunning() {
	return (stop_ == 0);
}

uint32_t RtspClient::GetWidth() const {
	return width_;
}

uint32_t RtspClient::GetHeight() const {
	return height_;
}

void RtspClient::CaptureThread() {
	SetThreadDescription(GetCurrentThread(), L"rtsp_capture_thread");
	env_.mainloop();
}

void RtspClient::Start() {
	blog(LOG_INFO, "RTSP client started");
	capture_thread_ = std::thread(&RtspClient::CaptureThread, this);
}

void RtspClient::Stop() {
	blog(LOG_INFO, "RTSP client stopped");
	env_.stop();
	capture_thread_.join();
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
				/*size_t w, h;
        bool ret = libwebrtc::RTCUtils::
                ParseH264SizeInfoFromSPSNALU(
                        buffer +
                                index.payload_start_offset,
                        index.payload_size, &w,
                        &h);
        if (ret) {
                width_ = w;
                height_ = h;

                blog(LOG_INFO,
                     "Parse video resolution, width: %d, height: %d",
                     w, h);
        }*/

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
