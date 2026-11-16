#pragma once
#include "voice_events.h"
#include <QThread>
#include <QMap>
#include <mutex>

class MediaWorker;
class MediaSession final : public VoiceEvents {
    Q_OBJECT
public:
    explicit MediaSession(QObject* parent = nullptr);
    ~MediaSession() override;
    void start(const MediaOptions& options);
    void stop();
    void setMuted(bool muted);
    void selectAudioDevice(bool input,const QByteArray& id);
    void muteStreamPlayback(QString peer,bool mute);
    void setVolume(QString identity,int percent);
    void setBlur(bool on,int radius);
    void setProcessing(bool noise,bool echo,bool agc,QString engine);
    void setVideoHeight(int height);
    void setScreenAudio(bool enabled);
    void changeVideo(bool share, int mode, const QByteArray& cameraId, const QString& screenName, const QCapturableWindow& window);
private:
    friend class MediaWorker;
    friend class NativeDesktopTests;
    QObject* workerContext() const;
    void postFrame(const QString& member,bool screen,const QImage& frame);
    std::mutex previewMutex_;
    QMap<QPair<QString,bool>,QImage> pendingPreviews_;
    QThread thread_;
    MediaWorker* worker_;
};
