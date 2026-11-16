#pragma once
#include "interface_text.h"
#include "media_preview.h"
#include "api.h"
#include "ui.h"
#include <QTemporaryDir>
#include <QFile>
#include <QSaveFile>
#include <QImageReader>
#include <QAudioOutput>
#include "attachment_playback.h"
#include <QVideoSink>
#include <QVideoFrame>
#include <QMediaMetaData>

// Keep the authenticated source alive for the lifetime of the decoder.
class VideoAttachment final : public QWidget {
public:
    VideoAttachment(ServerApi& api,QString route,QString name,bool compact,bool expanded=false)
        :api(api),route(std::move(route)),name(std::move(name)),compact(compact),expanded(expanded) {
        setObjectName("videoAttachment");setAccessibleName(this->name);setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);setAttribute(Qt::WA_Hover);
        if(!expanded)setFixedSize(targetSize({16,9}));
        controls=new QWidget(this);controls->setStyleSheet("background:#202020;color:white");
        auto* actions=new QHBoxLayout(controls);actions->setContentsMargins(5,2,5,2);actions->setSpacing(5);
        play=new QPushButton("▶");play->setAccessibleName(UiText::Play2());play->setFixedSize(30,28);play->setStyleSheet("border:0;padding:0");
        actions->addWidget(play);position=Ui::text("0:00","font-size:11px");actions->addWidget(position);
        seek=new QSlider(Qt::Horizontal);seek->setAccessibleName(UiText::VideoPosition());seek->setRange(0,0);actions->addWidget(seek,1);
        mute=Ui::action("sound",UiText::Mute(),28);actions->addWidget(mute);
        volume=new QSlider(Qt::Horizontal);volume->setRange(0,100);volume->setValue(100);volume->setFixedWidth(45);volume->setAccessibleName(UiText::Volume());actions->addWidget(volume);
        if(!compact){
            auto* save=Ui::action("download",UiText::DownloadVideo(),28);
            actions->addWidget(save);
            connect(save,&QPushButton::clicked,this,[this]{if(download)download();});
        }
        controls->setVisible(!compact);
        if(!compact&&!expanded){expand=Ui::action("expand",UiText::ExpandVideo(),32);expand->setParent(this);expand->hide();connect(expand,&QPushButton::clicked,this,[this]{open();});}
        connect(play,&QPushButton::clicked,this,[this]{toggle();});
        connect(seek,&QSlider::sliderMoved,this,[this](int value){resumePosition=value;if(player)player->setPosition(value);});
        connect(mute,&QPushButton::clicked,this,[this]{
            muted=!muted;if(player)player->setVolume(muted?0:volume->value());mute->setIcon(Ui::icon(muted?"sound-off":"sound"));
            mute->setAccessibleName(muted?UiText::Unmute():UiText::Mute());
        });
        connect(volume,&QSlider::valueChanged,this,[this](int value){if(player)player->setVolume(muted?0:value);});
    }
    ~VideoAttachment() override {
        releaseDecoder();
    }
    QString key;bool largeOriginal=false;
    std::function<void()> enlarge,geometryChanged,download;
    void pause(){
        if(!userPlayback)return;
        if(player)resumePosition=player->position();
        userPlayback=false;
        releaseDecoder();
        if(!expanded && !frame.isNull())frame=frame.scaled(targetSize(frame.size()),Qt::KeepAspectRatio,Qt::SmoothTransformation);
        showPlaying(false);
    }
    void open(){pause();if(enlarge)enlarge();}
    void requestPlay(){if(!userPlayback)toggle();}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
        QPainterPath clip;clip.addRoundedRect(rect(),expanded?0:12,expanded?0:12);painter.setClipPath(clip);
        painter.fillRect(rect(),QColor("#111"));
        if(!frame.isNull()){
            const auto size=frame.size().scaled(this->size(),Qt::KeepAspectRatio);
            QRect area(QPoint(),size);area.moveCenter(rect().center());painter.drawImage(area,frame);
        }
        if(!error.isEmpty()){painter.setPen(Qt::white);painter.drawText(rect().adjusted(8,4,-8,-4),Qt::AlignCenter|Qt::TextWordWrap,error);}
        else if(userPlayback && !player){
            painter.setPen(Qt::white);painter.drawText(rect(),Qt::AlignCenter,UiText::LoadingVideo());
        }else if(compact){
            painter.translate(rect().center());painter.setPen(Qt::NoPen);painter.setBrush(QColor(0,0,0,150));painter.drawEllipse(QPointF(),19,19);
            painter.setBrush(Qt::white);QPolygonF triangle{{-5,-8},{-5,8},{8,0}};painter.drawPolygon(triangle);
        }
        if(!started){started=true;QTimer::singleShot(0,this,[this]{if(!files && frame.isNull())load();});}
    }
    void resizeEvent(QResizeEvent*) override {
        controls->setGeometry(0,qMax(0,height()-32),width(),32);
        if(expand)expand->move(width()-40,8);
    }
    void enterEvent(QEnterEvent*) override{if(expand)expand->show();}
    void leaveEvent(QEvent*) override{if(expand&&!hasFocus())expand->hide();}
    void focusInEvent(QFocusEvent*) override{if(expand)expand->show();}
    void focusOutEvent(QFocusEvent*) override{if(expand&&!underMouse())expand->hide();}
    void mousePressEvent(QMouseEvent* e) override{
        if(e->button()==Qt::LeftButton){e->accept();return;}
        QWidget::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override{if(e->button()==Qt::LeftButton && rect().contains(e->position().toPoint())){if(!error.isEmpty()){if(download)download();}else if(compact)open();else toggle();}}
    void mouseDoubleClickEvent(QMouseEvent*) override{if(!expanded)open();}
    void keyPressEvent(QKeyEvent* e) override {
        if(e->key()==Qt::Key_Space||e->key()==Qt::Key_Return){if(compact)open();else toggle();}
        else QWidget::keyPressEvent(e);
    }
private:
    QSize targetSize(QSize aspect)const{
        const QSize bounds=compact?QSize(240,120):QSize(480,280);
        return aspect.scaled(bounds,Qt::KeepAspectRatio).expandedTo(QSize(1,1));
    }
    void toggle(){
        if(userPlayback){pause();return;}
        userPlayback=true;
        update();
        if(!localFile.isEmpty())openDecoder();
        else if(started && !files)load();
    }
    void releaseDecoder(){player.reset();}
    void showPlaying(bool playing){
        play->setText(playing?"Ⅱ":"▶");
        play->setAccessibleName(playing?UiText::Pause2():UiText::Play2());
    }
    void fail(QString text){error=std::move(text);controls->hide();if(expand)expand->hide();update();}
    void load(){
        const auto storedPoster=api.posterPath(route);
        if(!userPlayback && !storedPoster.isEmpty()){
            QImageReader reader(storedPoster,"PNG");
            const auto size=reader.size();
            if(size.isValid() && size.width()<=1920 && size.height()<=1120){
                auto image=reader.read();
                if(!image.isNull()){const auto duration=image.text("duration").toLongLong();showPoster(duration,std::move(image));return;}
            }
        }
        if(largeOriginal && !userPlayback)return;
        files=std::make_shared<QTemporaryDir>();
        if(!files->isValid()){fail(UiText::CouldNotOpenThePreviewYouCan());return;}
        const auto ext=QFileInfo(name).suffix().toLower();
        const auto path=files->filePath("attachment."+ext);
        api.download(route,path,[this,guard=QPointer<VideoAttachment>(this),keep=files,path,ext](QString error){
            if(!guard)return;
            QFile file(path);
            if(!error.isEmpty() || !file.open(QIODevice::ReadOnly)){fail(UiText::CouldNotOpenThePreviewYouCan());return;}
            const auto bytes=file.read(16);file.close();
            const bool valid=(ext=="mp4"&&bytes.mid(4,4)=="ftyp")||(ext=="webm"&&bytes.startsWith(QByteArray::fromHex("1a45dfa3")))||(ext=="ogv"&&bytes.startsWith("OggS"));
            if(!valid){fail(UiText::CouldNotOpenThePreviewYouCan());return;}
            localFile=path;
            if(userPlayback){openDecoder();return;}
            const auto bounds=compact?QSize(240,120):QSize(480,280);
            MediaPreviewQueue::request(this,path,bounds*devicePixelRatioF(),[this](qint64 duration,QImage poster){
                if(userPlayback || player || !frame.isNull())return;
                if(poster.isNull())return;
                const auto target=api.posterPath(route);
                if(!target.isEmpty()){
                    poster.setText("duration",QString::number(duration));
                    QSaveFile file(target);
                    if(file.open(QIODevice::WriteOnly) && poster.save(&file,"PNG") && file.commit())api.trimMedia();
                }
                showPoster(duration,std::move(poster));
            });
        },userPlayback?1536LL*1048576:20LL*1048576,this);
    }
    void showPoster(qint64 duration,QImage image){
        frame=std::move(image);setProperty("posterReady",true);
        seek->setRange(0,int(std::clamp<qint64>(duration,0,INT_MAX)));
        if(!expanded){setFixedSize(targetSize(frame.size()));if(geometryChanged)geometryChanged();}
        update();
    }
    void openDecoder(){
        player=std::make_unique<AttachmentPlayback>(this);
        player->frame=[this](QImage image){
            frame=std::move(image);setProperty("posterReady",true);
            if(!expanded && size()!=targetSize(frame.size())){
                setFixedSize(targetSize(frame.size()));if(geometryChanged)geometryChanged();
            }
            update();
        };
        player->progress=[this](int ms,int duration){
            seek->setMaximum(duration);
            if(!seek->isSliderDown())seek->setValue(ms);
            position->setText(QString("%1:%2").arg(ms/60000).arg(ms/1000%60,2,10,QChar('0')));
        };
        player->stopped=[this](bool error){
            pause();resumePosition=0;seek->setValue(0);position->setText("0:00");
            if(error)fail(UiText::CouldNotOpenThePreviewYouCan());
        };
        const QSize bounds=expanded?QSize(1920,1080):QSize(480,280)*qBound(1.,devicePixelRatioF(),3.);
        player->start(localFile,bounds,int(resumePosition),muted?0:volume->value());
        showPlaying(true);
    }
    ServerApi& api;QString route,name,error;bool compact,expanded,started=false,userPlayback=false;
    std::shared_ptr<QTemporaryDir> files;QString localFile;bool muted=false;qint64 resumePosition=0;
   QImage frame;std::unique_ptr<AttachmentPlayback> player;
    QWidget* controls;QPushButton *play,*mute,*expand=nullptr;QSlider *volume,*seek;QLabel* position;
};
