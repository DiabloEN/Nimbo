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
    explicit CaptureEncoder(std::shared_ptr<livekit::EncodedVideoSource> source);
    ~CaptureEncoder();
    void submit(const QImage& rgba);
private:
    void run();
    std::shared_ptr<livekit::EncodedVideoSource> source_;
    std::mutex mutex_;
    std::condition_variable wake_;
    QImage next_;
    std::exception_ptr error_;
    bool stopping_ = false;
    std::thread worker_;
};
