#include "audio_probe.h"

#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr qsizetype readChunkBytes = 16 * 1024;
constexpr int maxChunksPerDrain = 4;

double normalizedSample(const char *data, QAudioFormat::SampleFormat format)
{
    // Qt PCM uses native byte order. memcpy also handles unaligned buffers.
    switch (format) {
    case QAudioFormat::UInt8:
        return (static_cast<unsigned char>(*data) - 128) / 128.0;
    case QAudioFormat::Int16: {
        qint16 sample;
        std::memcpy(&sample, data, sizeof(sample));
        return sample / 32768.0;
    }
    case QAudioFormat::Int32: {
        qint32 sample;
        std::memcpy(&sample, data, sizeof(sample));
        return sample / 2147483648.0;
    }
    case QAudioFormat::Float: {
        float sample;
        std::memcpy(&sample, data, sizeof(sample));
        return std::isfinite(sample) ? std::clamp<double>(sample, -1.0, 1.0) : 0.0;
    }
    default:
        return 0.0;
    }
}

QString captureError(QtAudio::Error error)
{
    switch (error) {
    case QtAudio::OpenError:
        return AudioProbe::tr("Не удалось открыть микрофон. Проверьте устройство и разрешение на доступ к микрофону в Windows.");
    case QtAudio::IOError:
        return AudioProbe::tr("Ошибка чтения микрофона. Проверьте подключение устройства.");
    case QtAudio::UnderrunError:
        return AudioProbe::tr("Поток микрофона прервался. Попробуйте запустить проверку снова.");
    default:
        return AudioProbe::tr("Произошла ошибка захвата микрофона.");
    }
}

} // namespace

AudioProbe::AudioProbe(QObject *parent)
    : QObject(parent),
      m_devices(new QMediaDevices(this)),
      m_levelTimer(new QTimer(this))
{
    m_levelTimer->setInterval(100);
    m_levelTimer->setTimerType(Qt::PreciseTimer);
    connect(m_levelTimer, &QTimer::timeout, this, &AudioProbe::publishLevel);

    connect(m_devices, &QMediaDevices::audioInputsChanged, this, [this] {
        const QPointer<AudioProbe> guard(this);
        if (m_running) {
            const auto devices = inputDevices();
            const bool present = std::any_of(devices.cbegin(), devices.cend(),
                [this](const QAudioDevice &device) { return device.id() == m_deviceId; });
            if (!present)
                fail(tr("Микрофон отключён. Подключите устройство и запустите проверку снова."));
        }
        if (guard)
            emit devicesChanged();
    });
    connect(m_devices, &QMediaDevices::audioOutputsChanged, this, &AudioProbe::devicesChanged);
}

AudioProbe::~AudioProbe()
{
    stopCapture(false);
}

QList<QAudioDevice> AudioProbe::inputDevices() const
{
    return QMediaDevices::audioInputs();
}

QList<QAudioDevice> AudioProbe::outputDevices() const
{
    return QMediaDevices::audioOutputs();
}

bool AudioProbe::start(const QByteArray &deviceId)
{
    const QPointer<AudioProbe> guard(this);
    stop();
    if (!guard)
        return false;

    QAudioDevice device;
    if (deviceId.isEmpty()) {
        device = QMediaDevices::defaultAudioInput();
    } else {
        const auto devices = inputDevices();
        const auto it = std::find_if(devices.cbegin(), devices.cend(),
            [&deviceId](const QAudioDevice &candidate) { return candidate.id() == deviceId; });
        if (it != devices.cend())
            device = *it;
    }

    if (device.isNull()) {
        emit errorOccurred(tr("Микрофон не найден. Выберите доступное устройство ввода."));
        return false;
    }

    m_format = device.preferredFormat();
    const auto sampleFormat = m_format.sampleFormat();
    const bool knownFormat = sampleFormat == QAudioFormat::UInt8
        || sampleFormat == QAudioFormat::Int16 || sampleFormat == QAudioFormat::Int32
        || sampleFormat == QAudioFormat::Float;
    if (!m_format.isValid() || !knownFormat || !device.isFormatSupported(m_format)) {
        emit errorOccurred(tr("Формат выбранного микрофона не поддерживается проверкой."));
        return false;
    }

    m_deviceId = device.id();
    m_source = new QAudioSource(device, m_format, this);
    m_source->setBufferSize(m_format.bytesForDuration(100000));
    const auto session = ++m_session;
    const QPointer<QAudioSource> source = m_source;
    connect(m_source.data(), &QAudioSource::stateChanged, this, [this, source, session](QtAudio::State) {
        // Handle state changes after start() has returned. Ignore old sessions.
        if (session != m_session || !source || source != m_source)
            return;
        if (source->error() != QtAudio::NoError) {
            fail(captureError(source->error()));
        } else if (m_running && source->state() == QtAudio::StoppedState) {
            fail(tr("Захват микрофона остановлен. Запустите проверку снова."));
        }
    }, Qt::QueuedConnection);

    m_input = m_source->start();
    if (!m_input || m_source->error() != QtAudio::NoError
        || m_source->state() == QtAudio::StoppedState) {
        const auto message = captureError(m_source->error());
        stopCapture(false);
        emit errorOccurred(message);
        return false;
    }

    connect(m_input.data(), &QIODevice::readyRead, this, &AudioProbe::drainInput);
    m_running = true;
    m_levelTimer->start();
    emit runningChanged(true);
    if (!guard)
        return false;
    // Data can already be available before readyRead is connected.
    if (m_running)
        drainInput();
    return guard && m_running;
}

void AudioProbe::stop()
{
    stopCapture(true);
}

bool AudioProbe::isRunning() const
{
    return m_running;
}

void AudioProbe::stopCapture(bool notify)
{
    ++m_session;
    m_levelTimer->stop();
    const bool wasRunning = m_running;
    m_running = false;
    m_drainScheduled = false;

    if (m_input)
        disconnect(m_input.data(), nullptr, this, nullptr);
    m_input = nullptr;
    if (m_source) {
        auto *source = m_source.data();
        m_source = nullptr;
        disconnect(source, nullptr, this, nullptr);
        source->stop();
        source->deleteLater();
    }

    m_deviceId.clear();
    m_partialSample.fill(0);
    m_partialSize = 0;
    m_sumSquares = 0.0;
    m_sampleCount = 0;
    // The UI clears its meter on runningChanged(false). Level emissions remain
    // confined to the 100 ms timer, including rapid stop/start interactions.
    if (notify && wasRunning)
        emit runningChanged(false);
}

void AudioProbe::fail(const QString &message)
{
    const QPointer<AudioProbe> guard(this);
    stop();
    if (guard)
        emit errorOccurred(message);
}

void AudioProbe::drainInput()
{
    if (!m_running || !m_input)
        return;

    std::array<char, readChunkBytes + sizeof(float)> buffer;
    const auto sampleBytes = m_format.bytesPerSample();
    for (int chunk = 0; chunk < maxChunksPerDrain && m_input->bytesAvailable() > 0; ++chunk) {
        std::memcpy(buffer.data(), m_partialSample.data(), static_cast<size_t>(m_partialSize));
        const auto count = m_input->read(buffer.data() + m_partialSize, readChunkBytes);
        if (count < 0) {
            fail(tr("Не удалось прочитать данные микрофона."));
            return;
        }
        if (count == 0)
            break;

        const auto total = count + m_partialSize;
        const auto complete = total - total % sampleBytes;
        for (qint64 offset = 0; offset < complete; offset += sampleBytes) {
            const auto sample = normalizedSample(buffer.data() + offset, m_format.sampleFormat());
            m_sumSquares += sample * sample;
            ++m_sampleCount;
        }
        m_partialSize = static_cast<qsizetype>(total - complete);
        std::memcpy(m_partialSample.data(), buffer.data() + complete, static_cast<size_t>(m_partialSize));
    }

    // Bound work per event; retain at most one incomplete sample, not a recording.
    if (m_input->bytesAvailable() > 0 && !m_drainScheduled) {
        m_drainScheduled = true;
        const auto session = m_session;
        QTimer::singleShot(0, this, [this, session] {
            if (session != m_session)
                return;
            m_drainScheduled = false;
            drainInput();
        });
    }
}

void AudioProbe::publishLevel()
{
    if (!m_running)
        return;
    const auto rms = m_sampleCount > 0 ? std::sqrt(m_sumSquares / m_sampleCount) : 0.0;
    m_sumSquares = 0.0;
    m_sampleCount = 0;
    emit levelChanged(static_cast<float>(std::clamp(rms, 0.0, 1.0)));
}
