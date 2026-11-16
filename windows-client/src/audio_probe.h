#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <array>

class QAudioSource;
class QIODevice;
class QMediaDevices;
class QTimer;

// A local input-level meter only. Call its methods on its QObject thread.
// No microphone is opened until start(); samples are never saved or played.
class AudioProbe final : public QObject
{
    Q_OBJECT

public:
    explicit AudioProbe(QObject *parent = nullptr);
    ~AudioProbe() override;

    QList<QAudioDevice> inputDevices() const;
    QList<QAudioDevice> outputDevices() const;
    bool start(const QByteArray &deviceId = QByteArray());
    void stop();
    bool isRunning() const;

signals:
    // Linear RMS over the most recent interval, clamped to [0, 1], at <= 10 Hz.
    void levelChanged(float level);
    void errorOccurred(const QString &message);
    void runningChanged(bool running);
    void devicesChanged();

private:
    void drainInput();
    void publishLevel();
    void stopCapture(bool notify);
    void fail(const QString &message);

    QMediaDevices *m_devices = nullptr;
    QTimer *m_levelTimer = nullptr;
    QPointer<QAudioSource> m_source;
    QPointer<QIODevice> m_input;
    QAudioFormat m_format;
    QByteArray m_deviceId;
    std::array<char, sizeof(float)> m_partialSample{};
    qsizetype m_partialSize = 0;
    double m_sumSquares = 0.0;
    quint64 m_sampleCount = 0;
    quint64 m_session = 0;
    bool m_running = false;
    bool m_drainScheduled = false;
};
