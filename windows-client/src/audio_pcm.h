#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>

namespace AudioPcm {
inline void encode(char* data, float value, QAudioFormat::SampleFormat format) {
    value = std::clamp(value, -1.f, 1.f);
    switch (format) {
    case QAudioFormat::UInt8: *data = static_cast<char>(std::clamp(int(value * 127 + 128), 0, 255)); break;
    case QAudioFormat::Int16: { auto v = static_cast<qint16>(value * 32767); std::memcpy(data, &v, 2); break; }
    case QAudioFormat::Int32: { auto v = static_cast<qint32>(value * 2147483647.0); std::memcpy(data, &v, 4); break; }
    case QAudioFormat::Float: std::memcpy(data, &value, 4); break;
    default: break;
    }
}
inline float decode(const char* data, QAudioFormat::SampleFormat format) {
    switch (format) {
    case QAudioFormat::UInt8: return (static_cast<unsigned char>(*data) - 128) / 128.f;
    case QAudioFormat::Int16: { qint16 value; std::memcpy(&value, data, 2); return value / 32768.f; }
    case QAudioFormat::Int32: { qint32 value; std::memcpy(&value, data, 4); return static_cast<float>(value / 2147483648.0); }
    case QAudioFormat::Float: { float value; std::memcpy(&value, data, 4); return std::isfinite(value) ? value : 0.f; }
    default: return 0.f;
    }
}

// Receive path at 48 kHz. Match the web participant slider and its per-track limiter.
inline float voiceGain(int percent) {
    return percent<=100 ? percent/50.f : 2.f*std::exp2((percent-100)/50.f);
}
struct VoiceLevel {
    float gain=-1.f, reduction=1.f;
    float apply(float sample,float target) {
        if(gain<0)gain=target;
        else gain+=(target-gain)*0.001387925f; // 15 ms
        const float input=sample*gain, magnitude=std::abs(input);
        constexpr float threshold=0.70794578f; // -3 dB, hard knee, 20:1
        const float wanted=magnitude<=threshold?1.f:std::pow(threshold/magnitude,0.95f);
        reduction+=(wanted-reduction)*(wanted<reduction?0.00692039f:0.000173596f); // 3 / 120 ms
        return input*reduction;
    }
};
inline int16_t monoSample(const int16_t* samples,int channels) {
    int32_t sum=0;
    for(int i=0;i<channels;++i)sum+=samples[i];
    return static_cast<int16_t>(sum/channels);
}

// One 10 ms block at the device rate maps to 480 mono samples for processing.
inline std::vector<int16_t> captureBlock(const char* data, const QAudioFormat& format) {
    const int frames=format.sampleRate()/100;
    std::vector<float> mono(frames);
    for (int i = 0; i < frames; ++i) for (int ch = 0; ch < format.channelCount(); ++ch)
        mono[i] += decode(data + i * format.bytesPerFrame() + ch * format.bytesPerSample(), format.sampleFormat()) / format.channelCount();
    std::vector<std::int16_t> pcm(480);
    for (int i = 0; i < 480; ++i) {
        const float pos = float(i) * frames / 480; const int lo = int(pos), hi = std::min(lo + 1, frames - 1);
        const float value = mono[lo] + (mono[hi] - mono[lo]) * (pos - lo);
        pcm[i] = static_cast<std::int16_t>(std::clamp(value, -1.f, 1.f) * 32767);
    }
    return pcm;
}

inline QByteArray playbackBlock(const float (&mixed)[480],const QAudioFormat& format){
    const int frames=format.sampleRate()/100;
    QByteArray data(format.bytesForFrames(frames), Qt::Uninitialized);
    for (int i = 0; i < frames; ++i) {
        const float pos = float(i) * 480 / frames; const int lo = int(pos), hi = std::min(lo + 1, 480 - 1);
        const float value = mixed[lo] + (mixed[hi] - mixed[lo]) * (pos - lo);
        for (int ch = 0; ch < format.channelCount(); ++ch)
            encode(data.data() + i * format.bytesPerFrame() + ch * format.bytesPerSample(), value, format.sampleFormat());
    }
    return data;
}
}
