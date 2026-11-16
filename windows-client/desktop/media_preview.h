#pragma once
#include <QCoreApplication>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <private/qvideoframeconverter_p.h>
#include <QImage>
#include <QPointer>
#include <QQueue>
#include <QTimer>
#include <functional>

class MediaPreviewQueue final : public QObject {
public:
    using Result=std::function<void(qint64,QImage)>;
    static void request(QObject* card,QString path,QSize bounds,Result result){
        static QPointer<MediaPreviewQueue> queue;
        if(!queue)queue=new MediaPreviewQueue(qApp);
        queue->jobs.enqueue({card,std::move(path),bounds,std::move(result)});
        queue->startNext();
    }
private:
    using QObject::QObject;
    struct Job {QPointer<QObject> card;QString path;QSize bounds;Result result;};
    QQueue<Job> jobs;
    QMediaPlayer* active=nullptr;
    void startNext(){
        if(active)return;
        while(!jobs.isEmpty() && !jobs.head().card)jobs.dequeue();
        if(jobs.isEmpty())return;
        auto job=jobs.dequeue();
        auto* player=new QMediaPlayer(this);
        active=player;
        auto finish=[this,job,player](QImage image={}){
            if(active!=player || player->property("finished").toBool())return;
            player->setProperty("finished",true);
            const auto duration=player->duration();
            player->deleteLater();
            if(job.card)job.result(duration,std::move(image));
        };
        connect(player,&QObject::destroyed,this,[this]{active=nullptr;QTimer::singleShot(0,this,[this]{startNext();});});
        connect(job.card,&QObject::destroyed,player,&QObject::deleteLater);
        if(!job.bounds.isEmpty()){
            auto* sink=new QVideoSink(player);
            player->setVideoSink(sink);
            connect(sink,&QVideoSink::videoFrameChanged,player,[finish,bounds=job.bounds](const QVideoFrame& frame){
                if(!frame.isValid())return;
                auto image=qImageFromVideoFrame(frame,true);
                if(!image.isNull())finish(image.scaled(bounds,Qt::KeepAspectRatio,Qt::SmoothTransformation));
            });
        }
        connect(player,&QMediaPlayer::mediaStatusChanged,player,[player,finish,video=!job.bounds.isEmpty()](auto status){
            if(status==QMediaPlayer::LoadedMedia){if(video)player->play();else finish();}
            if(status==QMediaPlayer::InvalidMedia || status==QMediaPlayer::EndOfMedia)finish();
        });
        QTimer::singleShot(8000,player,[finish]{finish();});
        player->setSource(QUrl::fromLocalFile(job.path));
    }
};
