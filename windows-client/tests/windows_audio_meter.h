#pragma once

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>

// Read only the playback level of the supplied test PID. No PCM is captured.
inline int measureProcessOutput(DWORD pid, const QString& report) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IMMDeviceEnumerator> devices;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&devices)))) return 2;
    ComPtr<IMMDevice> device;
    if (FAILED(devices->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) return 2;
    ComPtr<IAudioSessionManager2> manager;
    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(manager.GetAddressOf())))) return 2;
    QFile ready(report + ".ready");
    if (!ready.open(QIODevice::WriteOnly)) return 2;
    ready.close();
    QElapsedTimer timer;
    timer.start();
    float peak = 0;
    while (timer.elapsed() < 2200) {
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(manager->GetSessionEnumerator(&sessions))) return 2;
        int count = 0;
        sessions->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            ComPtr<IAudioSessionControl> session;
            ComPtr<IAudioSessionControl2> identified;
            ComPtr<IAudioMeterInformation> meter;
            DWORD source = 0;
            if (FAILED(sessions->GetSession(i, &session)) || FAILED(session.As(&identified)) ||
                FAILED(identified->GetProcessId(&source)) || source != pid || FAILED(session.As(&meter))) continue;
            float sample = 0;
            if (SUCCEEDED(meter->GetPeakValue(&sample))) peak = qMax(peak, sample);
        }
        QThread::msleep(12);
    }
    QFile result(report);
    if (!result.open(QIODevice::WriteOnly)) return 2;
    result.write(QByteArray::number(peak));
    return peak > 0.0001f ? 0 : 1;
}
