#pragma once
#include <QAudioBufferOutput>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <private/qvideoframeconverter_p.h>
#include <QImage>
#include <functional>

class AttachmentPlayback final : public QObject {
public:
    explicit AttachmentPlayback(QObject* parent) : QObject(parent) {
        player.setObjectName("attachmentPlayback");
        player.setParent(this);
        player.setAudioOutput(&output);
        output.setMuted(qEnvironmentVariableIsSet("NIMBO_QA_SILENT_OUTPUT"));
        connect(&player,&QMediaPlayer::positionChanged,this,[this]{report();});
        connect(&player,&QMediaPlayer::durationChanged,this,[this]{report();});
        connect(&player,&QMediaPlayer::mediaStatusChanged,this,[this](auto status){
            if(status==QMediaPlayer::LoadedMedia && !started){
                started=true;
                player.setPosition(initialPosition);
                player.play();
            }
            if(status==QMediaPlayer::EndOfMedia || status==QMediaPlayer::InvalidMedia)
                finish(status==QMediaPlayer::InvalidMedia);
        });
        connect(&player,&QMediaPlayer::errorOccurred,this,[this]{finish(true);});
    }
    ~AttachmentPlayback() override {
        disconnect(&player,nullptr,this,nullptr);
        if(video)disconnect(video,nullptr,this,nullptr);
        if(spectrum)disconnect(spectrum,nullptr,this,nullptr);
        player.stop();
    }
    std::function<void(QImage)> frame;
    std::function<void(QByteArray)> samples;
    std::function<void(int,int)> progress;
    std::function<void(bool)> stopped;
    void start(const QString& path,QSize bounds,int position,int volume){
        initialPosition=position;
        setVolume(volume);
        if(!bounds.isEmpty()){
            video=new QVideoSink(this);
            player.setVideoSink(video);
            connect(video,&QVideoSink::videoFrameChanged,this,[this,bounds](const QVideoFrame& incoming){
                if(!incoming.isValid() || !frame)return;
                auto image=qImageFromVideoFrame(incoming,true);
                if(image.width()>bounds.width() || image.height()>bounds.height())
                    image=image.scaled(bounds,Qt::KeepAspectRatio,Qt::SmoothTransformation);
                if(!image.isNull())frame(std::move(image));
            });
        }else{
            QAudioFormat format;
            format.setSampleFormat(QAudioFormat::Float);
            format.setSampleRate(48000);
            format.setChannelCount(1);
            spectrum=new QAudioBufferOutput(format,this);
            player.setAudioBufferOutput(spectrum);
            connect(spectrum,&QAudioBufferOutput::audioBufferReceived,this,[this,format](const QAudioBuffer& buffer){
                if(!samples || !buffer.isValid() || buffer.format()!=format)return;
                const int count=qMin(buffer.frameCount(),2048);
                const auto* tail=buffer.constData<float>()+buffer.frameCount()-count;
                samples(QByteArray(reinterpret_cast<const char*>(tail),count*sizeof(float)));
            });
        }
        player.setSource(QUrl::fromLocalFile(path));
    }
    int position()const{return started?int(player.position()):initialPosition;}
    void setPosition(int ms){initialPosition=ms;if(started)player.setPosition(ms);}
    void setVolume(int value){output.setVolume(qBound(0,value,100)/100.f);}
private:
    void report(){if(started && progress)progress(int(player.position()),int(player.duration()));}
    void finish(bool error){
        if(done)return;
        done=true;
        QTimer::singleShot(0,this,[this,error]{auto notify=stopped;if(notify)notify(error);});
    }
    QAudioOutput output;
    QMediaPlayer player;
    QVideoSink* video=nullptr;
    QAudioBufferOutput* spectrum=nullptr;
    int initialPosition=0;
    bool started=false,done=false;
};
