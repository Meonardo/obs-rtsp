#include "rtsp_server.h"

#include "liveMedia.hh"
#include "environment.h"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"

#include <obs.h>
#include <util/threading.h>

namespace output::source {
// Custom FramedSource subclass for OBS integration
class OBSFramedSource : public FramedSource {
public:
	static OBSFramedSource* createNew(UsageEnvironment& env) {
		return new OBSFramedSource(env);
	}

	void Feed(struct encoder_packet* packet) {
		delete[] encoded_data_;
		encoded_data_ = new unsigned char[packet->size];
		memcpy(encoded_data_, packet->data, packet->size);
		encoded_data_size_ = packet->size;
	}

private:
	OBSFramedSource(UsageEnvironment& env)
	  : FramedSource(env),
	    encoded_data_(nullptr),
	    encoded_data_size_(0) {}

	virtual ~OBSFramedSource() { delete[] encoded_data_; }

	void deliverFrame() {}

protected:
	virtual void doGetNextFrame() {
		if (encoded_data_ == nullptr) {
			handleClosure(this);
			return;
		}

		// Deliver the data here:
		// Set the 'presentation time':
		struct timeval presentation_time;
		gettimeofday(&presentation_time, nullptr);
		// Deliver the data here:
		if (isCurrentlyAwaitingData()) {
			// Normal case: The reader of this source has asked for the data
			// (using "FramedSource::getNextFrame()").
			// Deliver a data frame:
			if (encoded_data_size_ > fMaxSize) {
				fFrameSize = fMaxSize;
				fNumTruncatedBytes = encoded_data_size_ - fMaxSize;
			} else {
				fFrameSize = encoded_data_size_;
			}
			memmove(fTo, encoded_data_, fFrameSize);

			fDurationInMicroseconds = 0;
			fPresentationTime = presentation_time;
			fNumTruncatedBytes = 0;
			// Tell the reader that the data is now available:
			FramedSource::afterGetting(this);
		} else {
			// Abnormal case: The reader of this source has *not* asked for the
			// data.
			// Do nothing.
		}
	}

private:
	// OBS encoded video data buffer
	unsigned char* encoded_data_;
	unsigned int encoded_data_size_;
};

/// <summary>
/// Customized video source from OBS output
/// </summary>
class RtspVideoSource {
public:
	RtspVideoSource()
	  : env_(nullptr),
	    source_(nullptr),
	    sink_(nullptr),
	    rtcp_(nullptr),
	    obs_source_(nullptr) {
		// Begin by setting up our usage environment:
		TaskScheduler* scheduler = BasicTaskScheduler::createNew();
		env_ = BasicUsageEnvironment::createNew(*scheduler);
		// Create 'groupsocks' for RTP and RTCP:
		struct sockaddr_storage dst_address = {0};
		dst_address.ss_family = AF_INET;
		((struct sockaddr_in&)dst_address).sin_addr.s_addr =
		  chooseRandomIPv4SSMAddress(*env_);

		const unsigned short rtp_port_num = 18888;
		const unsigned short rtcp_port_num = rtp_port_num + 1;
		const unsigned char ttl = 255;
		const Port rtp_port(rtp_port_num);
		const Port rtcp_port(rtcp_port_num);
		Groupsock rtp_groupsock(*env_, dst_address, rtp_port, ttl);
		rtp_groupsock.multicastSendOnly();
		Groupsock rtcp_groupsock(*env_, dst_address, rtcp_port, ttl);
		rtcp_groupsock.multicastSendOnly();

		// Create a 'H264 Video RTP' sink from the RTP 'groupsock':
		OutPacketBuffer::maxSize = 100000;
		sink_ = H264VideoRTPSink::createNew(*env_, &rtp_groupsock, 96);

		// Create (and start) a 'RTCP instance' for this RTP sink:
		const unsigned estimated_session_bandwidth = 500; // in kbps; for RTCP b/w share
		const unsigned max_cname_len = 100;
		unsigned char CNAME[max_cname_len + 1];
		gethostname((char*)CNAME, max_cname_len);
		CNAME[max_cname_len] = '\0'; // just in case

		rtcp_ = RTCPInstance::createNew(*env_, &rtcp_groupsock, estimated_session_bandwidth,
						CNAME, sink_, nullptr /* we're a server */, true);
		// Note: This starts RTCP running automatically
	}
	~RtspVideoSource() {
		Stop();
		env_->reclaim();
	}

	UsageEnvironment* GetEnv() const { return env_; }

	bool Play(ServerMediaSession* sms) {
		if (source_ != nullptr) {
			blog(LOG_INFO, "already playing");
			return false;
		}
		// Add to the media session:
		bool ret =
		  sms->addSubsession(PassiveServerMediaSubsession::createNew(*sink_, rtcp_));
		if (!ret) {
			blog(LOG_ERROR, "Add to media session failed");
			return false;
		}
		// Create a framer for the Video Elementary Stream:
		obs_source_ = OBSFramedSource::createNew(*env_);
		source_ = H264VideoStreamFramer::createNew(*env_, obs_source_);
		// Start playing the sink:
		ret = sink_->startPlaying(*source_, AfterPlaying, this);
		if (!ret) {
			blog(LOG_ERROR, "start playing failed");
		}
		return ret;
	}

	void Stop() {
		if (sink_ != nullptr)
			sink_->stopPlaying();
		if (source_ != nullptr)
			Medium::close(source_);
	}

	void Feed(struct encoder_packet* packet) { obs_source_->Feed(packet); }

	static void AfterPlaying(void* data) {
		auto source = static_cast<RtspVideoSource*>(data);
		source->Stop();
	}

private:
	H264VideoStreamFramer* source_;
	RTPSink* sink_;
	RTCPInstance* rtcp_;
	UsageEnvironment* env_;

	OBSFramedSource* obs_source_;
};

/// <summary>
/// Customized audio source from OBS output
/// </summary>
class RtspAudioSource : public FramedSource {
public:
	static RtspAudioSource* createNew(UsageEnvironment& env) {
		return new RtspAudioSource(env);
	}
	virtual ~RtspAudioSource() {}

	virtual void doGetNextFrame() override {}

protected:
	RtspAudioSource(UsageEnvironment& env) : FramedSource(env) {}
};
} // namespace output::source

namespace output {
RtspServer::RtspServer(uint16_t port)
  : server_(nullptr),
    sms_(nullptr),
    env_(nullptr),
    port_(port),
    audio_source_(nullptr),
    video_source_(nullptr) {
	if (port == 0) {
		port = 8554;
	}
}

RtspServer::~RtspServer() {
	Stop();
}

bool RtspServer::Start() {
	if (server_ != nullptr) {
		return false;
	}

	env_ = new Environment();
	server_ = RTSPServer::createNew(*env_, port_, nullptr);
	if (server_ == nullptr) {
		blog(LOG_ERROR, "failed to create RTSP server");
		return false;
	}

	sms_ = ServerMediaSession::createNew(*env_, "obs_live", "Live stream from OBS rtsp plugin",
					     "live stream");
	if (sms_ == nullptr) {
		blog(LOG_ERROR, "failed to create RTSP server media session");
		return false;
	}
	server_->addServerMediaSession(sms_);

	// create video source
	video_source_ = new source::RtspVideoSource();
	if (!video_source_->Play(sms_)) {
		blog(LOG_ERROR, "failed to play video source");
		delete video_source_;
    video_source_ = nullptr;
		return false;
	}

	server_thread_ = std::thread(&RtspServer::ServerThread, this);

	blog(LOG_INFO, "Play this stream using the URL: ");
	if (weHaveAnIPv4Address(*env_)) {
		auto url = server_->ipv4rtspURL(sms_);
		blog(LOG_INFO, "%s", url);
		delete[] url;
	}

	return true;
}

bool RtspServer::Stop() {
	// release a/v sources
	if (audio_source_ != nullptr) {
		delete audio_source_;
		audio_source_ = nullptr;
	}
	if (video_source_ != nullptr) {
		delete video_source_;
		video_source_ = nullptr;
	}
	// close server
	if (server_ != nullptr) {
		Medium::close(server_);
		server_ = nullptr;
	}
	// release media session
	if (sms_ != nullptr) {
		Medium::close(sms_);
		sms_ = nullptr;
	}
	// detach thread
	if (server_thread_.joinable())
		server_thread_.join();
	if (env_ != nullptr) {
		delete env_;
		env_ = nullptr;
	}

	return true;
}

void RtspServer::ServerThread() {
	os_set_thread_name("rtsp_server_thread");
	env_->mainloop();
}

void RtspServer::Data(struct encoder_packet* packet) {
	if (video_source_ != nullptr) {
		video_source_->Feed(packet);
	}
}

size_t RtspServer::GetTotalBytes() {
	return 0;
}

int RtspServer::GetConnectTime() {
	return 0;
}

} // namespace output
