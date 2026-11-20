#pragma once
#include "voice_events.h"
#include "screen_ink.h"
#include <QThread>
#include <QMap>
#include <mutex>
#include <atomic>

class MediaWorker;
class MediaSession final : public VoiceEvents {
    Q_OBJECT
public:
    explicit MediaSession(QObject* parent = nullptr);
    ~MediaSession() override;
    void start(const MediaOptions& options);
    void stop();
    void moveRoom(QString url,QString token,bool annotations);
    void setMuted(bool muted);
    void selectAudioDevice(bool input,const QByteArray& id);
    void muteStreamPlayback(QString peer,bool mute);
    void setVolume(QString identity,int percent);
    void setBlur(bool on,int radius);
    void setProcessing(bool noise,bool echo,bool agc,QString engine);
    void setVideoHeight(int height);
    void setScreenAudio(bool enabled);
    void sendInk(QString host,QJsonObject command);
signals:
    void voiceControlHint();
    void roomMoved();
    void inkSession(QString host,QString session,bool allowed);
    void localInkChanged(ScreenInk::Strokes strokes);
public:
    void changeVideo(bool share, int mode, const QByteArray& cameraId, const QString& screenName, const QCapturableWindow& window);
private:
    friend class MediaWorker;
    friend class NativeDesktopTests;
    QObject* workerContext() const;
    int activeEncoderQueues() const;
    int activeScreenAudioResources() const;
    quint64 audibleTestBlocks() const;
    std::atomic<quint64> audibleBlocks_{0};
    void postFrame(const QString& member,bool screen,const QImage& frame);
    std::mutex previewMutex_;
    QMap<QPair<QString,bool>,QImage> pendingPreviews_;
    QThread thread_;
    MediaWorker* worker_;
};
