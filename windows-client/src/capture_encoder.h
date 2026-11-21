#pragma once
#include <QImage>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

namespace livekit { class EncodedVideoSource; }

// One worker per active capture. A slow encode replaces the pending frame.
class CaptureEncoder final {
public:
    static constexpr int bitrateLimit(bool screenShare) { return screenShare ? 8000000 : 3000000; }
    CaptureEncoder(std::shared_ptr<livekit::EncodedVideoSource> source, bool screenShare);
    ~CaptureEncoder();
    void submit(const QImage& rgba);
private:
    void run();
    std::shared_ptr<livekit::EncodedVideoSource> source_;
    const int bitrateLimit_;
    std::mutex mutex_;
    std::condition_variable wake_;
    QImage next_;
    std::exception_ptr error_;
    bool stopping_ = false;
    std::thread worker_;
};
