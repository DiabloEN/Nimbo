#pragma once
#include <QObject>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QCapturableWindow>

struct MediaOptions {
    QString url, token;
    QHash<QString,int> participantVolumes;
    quint32 applicationPid = 0; // Root of the process tree excluded from screen audio.
    QByteArray inputId, outputId;
    bool synthetic = false;
    bool annotationData = false;
    int videoMode = 0; // 0 off, 1 generated pattern, 2 camera, 3 primary screen
    bool hardwareVideo = false;
    bool blur=false,noise=true,echo=true,agc=true;
    int blurRadius=12;QString noiseEngine="gtcrn";
    QByteArray cameraId;
    QString screenName;
    QCapturableWindow window;
};

class VoiceEvents : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void videoModeChanged(bool share,int mode, const QString& error);
    void localConnectionQualityChanged(const QString& quality);
    void statusChanged(const QString& status);
    void connectedChanged(bool connected);
    void failed(const QString& message);
    void audioWarning(const QString& message);
    void statistics(quint64 sentAudio, quint64 receivedAudio, quint64 receivedVideo, double receivedRms);
    void videoReceived(const QImage& image);
    void participantFrame(QString member, bool screen, const QImage& frame);
    void effectFailed();
    void screenAudioChanged(bool enabled, const QString& error);
    void microphoneMuted(bool muted);
    void audioDeviceSelected(bool input,const QByteArray& id,const QString& error);
    void localSpeakingChanged(bool speaking);
    void callRoster(const QJsonArray& people);
    void encoderReported(const QString& implementation, bool powerEfficient);
};
