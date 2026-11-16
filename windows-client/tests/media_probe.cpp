#include "media_session.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QJsonObject>
#include <QTimer>
#include <algorithm>

// Audio is synthetic. Physical video capture is opt-in for the loopback cycle test.
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOptions({{"invite", "Connection file", "path"}, {"report", "Result JSON", "path"},
        {"seconds", "Duration", "seconds", "15"}, {"video", "Generated video"}, {"hardware", "Hardware encoder request"},
        {"capture-cycles-at", "Switch the camera and primary screen twice starting at this UTC epoch in ms", "epoch"},
        {"observe-cycles-at", "Check received camera, screen and audio during the same capture windows", "epoch"}});
    parser.process(app);
    QFile file(parser.value("invite"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return 2;
    auto invite = QJsonDocument::fromJson(file.readAll()).object();
    MediaOptions options; options.url = invite.value("url").toString(); options.token = invite.value("token").toString();
    options.synthetic = true; options.videoMode = parser.isSet("video") ? 1 : 0; options.hardwareVideo = parser.isSet("hardware");
    quint64 tx = 0, rx = 0, video = 0; double peakRms = 0; bool connected = false, failed = false;
    QString encoder; bool powerEfficient = false;
    MediaSession session;
    const bool capture = parser.isSet("capture-cycles-at");
    const bool observe = parser.isSet("observe-cycles-at");
    if (capture && observe) return 2;
    QJsonArray cycles;
    bool cycleCheck = true;
    if (capture || observe) {
        const auto epoch = parser.value(capture ? "capture-cycles-at" : "observe-cycles-at").toLongLong();
        const auto delay = epoch - QDateTime::currentMSecsSinceEpoch();
        if (delay < 0 || delay > 30000) return 2;
        struct Counts { int frames = 0; QSize size; };
        auto camera = std::make_shared<Counts>();
        auto screen = std::make_shared<Counts>();
        QObject::connect(&session, &MediaSession::participantFrame, &app,
            [camera,screen](const QString& peer, bool share, const QImage& image) {
                if (peer.isEmpty() || image.isNull()) return;
                auto& counts = share ? *screen : *camera;
                ++counts.frames;
                counts.size = image.size();
            });
        for (int index = 0; index < 2; ++index) {
            const int start = int(delay) + 11500 * index;
            if (capture) {
                QTimer::singleShot(start, &app, [&] {
                    session.changeVideo(false, 2, {}, {}, {});
                    session.changeVideo(true, 3, {}, {}, {});
                });
                QTimer::singleShot(start + 7300, &app, [&] {
                    session.changeVideo(true, 0, {}, {}, {});
                    session.changeVideo(false, 0, {}, {}, {});
                });
            } else {
                auto audioBaseline = std::make_shared<quint64>();
                QTimer::singleShot(start + 2200, &app, [&,camera,screen,audioBaseline] {
                    *camera = {}; *screen = {}; *audioBaseline = rx;
                });
                QTimer::singleShot(start + 7150, &app, [&,camera,screen,audioBaseline] {
                    const auto audio = rx - *audioBaseline;
                    cycles.append(QJsonObject{{"cameraFrames", camera->frames}, {"cameraWidth", camera->size.width()},
                        {"cameraHeight", camera->size.height()}, {"screenFrames", screen->frames},
                        {"screenWidth", screen->size.width()}, {"screenHeight", screen->size.height()}, {"audioFrames", qint64(audio)}});
                    cycleCheck &= camera->frames > 5 && screen->frames > 5 && audio > 100;
                });
            }
        }
    }
    QObject::connect(&session, &MediaSession::encoderReported, &app, [&](const QString& value, bool efficient) { encoder = value; powerEfficient = efficient; });
    QObject::connect(&session, &MediaSession::connectedChanged, &app, [&](bool value) { connected = connected || value; });
    QObject::connect(&session, &MediaSession::failed, &app, [&](const QString&) { failed = true; app.quit(); });
    QObject::connect(&session, &MediaSession::statistics, &app, [&](quint64 t, quint64 r, quint64 v, double rms) {
        tx = t; rx = r; video = v; peakRms = std::max(peakRms, rms);
    });
    QTimer::singleShot(std::clamp(parser.value("seconds").toInt(), 3, 120) * 1000, &app, &QCoreApplication::quit);
    session.start(options); app.exec();
    const bool passed = connected && !failed && tx > 100 && rx > 100 && peakRms > .005 && (!options.videoMode || video > 10)
        && (!observe || (cycles.size() == 2 && cycleCheck));
    QJsonObject result{{"passed", passed}, {"connected", connected}, {"failed", failed}, {"sentAudio", qint64(tx)},
        {"receivedAudio", qint64(rx)}, {"receivedVideo", qint64(video)}, {"peakReceivedRms", peakRms},
        {"synthetic", true}, {"hardwareRequested", options.hardwareVideo}, {"encoder", encoder}, {"powerEfficientEncoder", powerEfficient}};
    QFile report(parser.value("report"));
    if (observe) result.insert("cycles", cycles);
    if (!report.open(QIODevice::WriteOnly) || report.write(QJsonDocument(result).toJson()) < 0) return 2;
    return passed ? 0 : 1;
}
