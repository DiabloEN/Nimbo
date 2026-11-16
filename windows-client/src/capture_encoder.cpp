#include "capture_encoder.h"
#include <livekit/encoded_video_source.h>
#include <livekit/video_frame.h>
#include <wels/codec_api.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <numeric>
#include <stdexcept>

namespace {
constexpr int maxBitrate = 3000000;
struct EncoderDeleter {
    void operator()(ISVCEncoder* encoder) const {
        encoder->Uninitialize();
        WelsDestroySVCEncoder(encoder);
    }
};
using Encoder = std::unique_ptr<ISVCEncoder, EncoderDeleter>;

Encoder openEncoder(QSize size, int bitrate) {
    ISVCEncoder* raw = nullptr;
    if (WelsCreateSVCEncoder(&raw) != 0) throw std::runtime_error("Cannot create H.264 encoder");
    Encoder encoder(raw);
    SEncParamBase config{};
    config.iUsageType = CAMERA_VIDEO_REAL_TIME;
    config.iPicWidth = size.width();
    config.iPicHeight = size.height();
    config.iTargetBitrate = bitrate;
    config.iRCMode = RC_BITRATE_MODE;
    config.fMaxFrameRate = 30;
    if (encoder->Initialize(&config) != 0) throw std::runtime_error("Cannot initialize H.264 encoder");
    int period = 60;
    encoder->SetOption(ENCODER_OPTION_IDR_INTERVAL, &period);
    return encoder;
}

bool encode(ISVCEncoder& encoder, const QImage& image, livekit::EncodedVideoSource& destination) {
    const int w = image.width(), h = image.height();
    auto rgba = livekit::VideoFrame::create(w, h, livekit::VideoBufferType::RGBA);
    for (int y = 0; y < h; ++y) std::memcpy(rgba.data() + y*w*4, image.constScanLine(y), w*4);
    auto i420 = rgba.convert(livekit::VideoBufferType::I420);
    SSourcePicture picture{};
    picture.iColorFormat = videoFormatI420;
    picture.iPicWidth = w;
    picture.iPicHeight = h;
    const auto planes = i420.planeInfos();
    if (planes.size() != 3) throw std::runtime_error("Unexpected I420 layout");
    for (int i = 0; i < 3; ++i) {
        picture.pData[i] = reinterpret_cast<unsigned char*>(planes[i].data_ptr);
        picture.iStride[i] = int(planes[i].stride);
    }
    picture.uiTimeStamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    SFrameBSInfo info{};
    if (encoder.EncodeFrame(&picture, &info) != 0) throw std::runtime_error("H.264 frame encoding failed");
    if (!info.iFrameSizeInBytes) return false;
    livekit::EncodedVideoSource::Frame frame;
    frame.width = w;
    frame.height = h;
    frame.is_keyframe = info.eFrameType == videoFrameTypeIDR;
    frame.data.reserve(info.iFrameSizeInBytes);
    for (int i = 0; i < info.iLayerNum; ++i) {
        const auto& layer = info.sLayerInfo[i];
        const auto size = std::accumulate(layer.pNalLengthInByte, layer.pNalLengthInByte + layer.iNalCount, 0);
        frame.data.insert(frame.data.end(), layer.pBsBuf, layer.pBsBuf + size);
    }
    return destination.captureFrame(frame);
}
}

CaptureEncoder::CaptureEncoder(std::shared_ptr<livekit::EncodedVideoSource> source)
    : source_(std::move(source)), worker_(&CaptureEncoder::run, this) {}

CaptureEncoder::~CaptureEncoder() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        next_ = {};
    }
    wake_.notify_one();
    worker_.join();
}

void CaptureEncoder::submit(const QImage& rgba) {
    {
        std::lock_guard lock(mutex_);
        if (error_) std::rethrow_exception(error_);
        next_ = rgba;
    }
    wake_.notify_one();
}

void CaptureEncoder::run() {
    try {
        Encoder encoder;
        QSize size;
        int bitrate = maxBitrate;
        bool needKeyframe = true;
        for (;;) {
            QImage image;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&]{ return stopping_ || !next_.isNull(); });
                if (stopping_) return;
                image.swap(next_);
            }
            const auto feedback = source_->takeFeedback();
            if (feedback.rate_control && feedback.rate_control->target_bitrate_bps) {
                const int target = int(std::clamp<std::uint64_t>(feedback.rate_control->target_bitrate_bps, 16000, maxBitrate));
                if (target != bitrate && encoder) {
                    SBitrateInfo rate{SPATIAL_LAYER_ALL, target};
                    encoder->SetOption(ENCODER_OPTION_BITRATE, &rate);
                }
                bitrate = target;
            }
            if (image.size() != size) {
                encoder.reset();
                size = image.size();
                encoder = openEncoder(size, bitrate);
                needKeyframe = true;
            }
            needKeyframe |= feedback.keyframe_requested;
            if (needKeyframe) encoder->ForceIntraFrame(true);
            if (encode(*encoder, image, *source_)) needKeyframe = false;
        }
    } catch (...) {
        std::lock_guard lock(mutex_);
        error_ = std::current_exception();
    }
}
