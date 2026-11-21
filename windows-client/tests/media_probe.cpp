#include "media_session.h"
#include "quality_window.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QJsonObject>
#include <QTimer>
#include <QWindowCapture>
#include <algorithm>

// Audio is synthetic. Physical video capture is opt-in for the loopback cycle test.
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOptions({{"invite", "Connection file", "path"}, {"report", "Result JSON", "path"},
        {"seconds", "Duration", "seconds", "15"}, {"video", "Generated video"}, {"hardware", "Hardware encoder request"},
        {"ink-role","Exercise screen drawing in a local call","host|viewer"},
        {"quality-window","Share the named synthetic QA window on loopback","title"},
        {"quality-scene","Display a generated texture and HUD from a test directory","directory"},
        {"raw-preview","Save the first synthetic QA frame submitted to the encoder","path"},
        {"capture-cycles-at", "Switch the camera and primary screen twice starting at this UTC epoch in ms", "epoch"},
        {"observe-cycles-at", "Check received camera, screen and audio during the same capture windows", "epoch"}});
    parser.process(app);
    if(parser.isSet("quality-scene")){
        QualityWindow scene(QDir(parser.value("quality-scene")));
        if(!scene.valid())return 2;
        scene.show();return app.exec();
    }
    QFile file(parser.value("invite"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return 2;
    auto invite = QJsonDocument::fromJson(file.readAll()).object();
    MediaOptions options; options.url = invite.value("url").toString(); options.token = invite.value("token").toString();
    options.synthetic = true; options.videoMode = parser.isSet("video") ? 1 : 0; options.hardwareVideo = parser.isSet("hardware");
    quint64 tx = 0, rx = 0, video = 0; double peakRms = 0; bool connected = false, failed = false;
    QString encoder; bool powerEfficient = false;
    MediaSession session;
    const auto qualityWindow=parser.value("quality-window");
    bool hasPreview=false;
    if(!qualityWindow.isEmpty()){
        if(QUrl(options.url).host()!="127.0.0.1" || !qualityWindow.startsWith("Nimbo QA "))return 2;
        QObject::connect(&session,&VoiceEvents::connectedChanged,&app,[&](bool online){
            if(!online)return;
            for(const auto& window:QWindowCapture::capturableWindows())if(window.description()==qualityWindow){
                session.changeVideo(true,4,{},{},window);return;
            }
            failed=true;app.quit();
        });
        QObject::connect(&session,&VoiceEvents::participantFrame,&app,[&](QString peer,bool share,const QImage& frame){
            if(!hasPreview && peer.isEmpty() && share && !frame.isNull())
                hasPreview=frame.save(parser.value("raw-preview"));
        });
    }
    const auto inkRole=parser.value("ink-role");
    options.annotationData=!inkRole.isEmpty();
    const auto claims=QJsonDocument::fromJson(QByteArray::fromBase64(options.token.section('.',1,1).toLatin1(),QByteArray::Base64UrlEncoding)).object();
    const auto identity=claims["sub"].toString();
    QString hostId,epoch;int inkPhase=0,inkRounds=0;
    QSet<QString> seenBoards;
    auto stroke=[&](double y){
        return QJsonObject{{"session",epoch},{"type","stroke"},{"color","#eeeeee"},{"id",QUuid::createUuid().toString()},
            {"points",QJsonArray{QJsonArray{.25,y},QJsonArray{.75,y}}}};
    };
    if(options.annotationData){
        QObject::connect(&session,&MediaSession::connectedChanged,&app,[&](bool on){
            if(on && inkRole=="host")session.changeVideo(true,1,{},{},{});
        });
        QObject::connect(&session,&MediaSession::inkSession,&app,[&](QString author,QString key,bool allowed){
            if(key.isEmpty())return;
            const bool localBoard=inkRole=="host" && author.isEmpty();
            const bool watchedBoard=inkRole=="viewer" && !author.isEmpty();
            if(!allowed)return;
            if((!localBoard && !watchedBoard) || seenBoards.contains(key))return;
            seenBoards.insert(key);epoch=key;inkPhase=0;
            if(localBoard)session.sendInk(identity,stroke(.3));
            else hostId=author;
        });
        QObject::connect(&session,&MediaSession::localInkChanged,&app,[&](const ScreenInk::Strokes& strokes){
            if(inkRole!="host")return;
            if(inkPhase==0 && strokes.size()==1)inkPhase=1;
            if(inkPhase==1 && strokes.size()==2 && strokes.last().author!=identity)inkPhase=2;
            if(inkPhase==2 && strokes.size()==1 && strokes.first().author==identity){
                inkPhase=3;
                QTimer::singleShot(1500,&app,[&]{session.sendInk(identity,{{"type","clear"},{"session",epoch}});});
            }
            if(inkPhase==3 && strokes.isEmpty()){
                inkPhase=4;
                if(++inkRounds<5)QTimer::singleShot(1250,&app,[&]{
                    session.changeVideo(true,0,{},{},{});
                    QTimer::singleShot(300,&app,[&]{session.changeVideo(true,1,{},{},{});});
                });
            }
        });
        QObject::connect(&session,&MediaSession::participantFrame,&app,[&](QString peer,bool share,const QImage& frame){
            if(inkRole!="viewer" || !share || peer.isEmpty() || frame.isNull())return;
            if(epoch.isEmpty()){if(hostId.isEmpty()){hostId=peer;session.sendInk(peer,{{"type","query"}});}return;}
            auto bright=[&](double y){
                auto c=frame.pixelColor(frame.width()/2,qRound(frame.height()*y));
                return std::min({c.red(),c.green(),c.blue()})>207;
            };
            const bool first=bright(.3),second=bright(.7);
            if(!inkPhase && first){inkPhase=1;session.sendInk(hostId,stroke(.7));}
            else if(inkPhase==1 && first && second){inkPhase=2;session.sendInk(hostId,{{"type","clear"},{"session",epoch},{"author",hostId}});}
            else if(inkPhase==2 && first && !second)inkPhase=3;
            else if(inkPhase==3 && !first && !second){inkPhase=4;++inkRounds;}
        });
    }
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
    const bool passed = connected && !failed && tx > 100 && (qualityWindow.isEmpty()?(rx > 100 && peakRms > .005):hasPreview) && (!options.videoMode || video > 10)
        && (!observe || (cycles.size() == 2 && cycleCheck)) && (inkRole.isEmpty() || (inkPhase==4 && inkRounds==5));
    QJsonObject result{{"passed", passed}, {"connected", connected}, {"failed", failed}, {"sentAudio", qint64(tx)},
        {"receivedAudio", qint64(rx)}, {"receivedVideo", qint64(video)}, {"peakReceivedRms", peakRms},
        {"synthetic", true}, {"hardwareRequested", options.hardwareVideo}, {"encoder", encoder}, {"powerEfficientEncoder", powerEfficient}};
    QFile report(parser.value("report"));
    if(!inkRole.isEmpty()){result.insert("inkPhase",inkPhase);result.insert("inkRounds",inkRounds);}
    if (observe) result.insert("cycles", cycles);
    if (!report.open(QIODevice::WriteOnly) || report.write(QJsonDocument(result).toJson()) < 0) return 2;
    return passed ? 0 : 1;
}
