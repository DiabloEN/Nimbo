#pragma once
#include "interface_text.h"
#include <QSaveFile>
#include "media_preview.h"
#include "api.h"
#include "ui.h"
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#include <QAudioOutput>
#include "attachment_playback.h"
#include <array>
#include <complex>
#include <numbers>

class AudioTimeline final : public QSlider {
public:
    AudioTimeline():QSlider(Qt::Horizontal){
        setFixedHeight(34);setRange(0,0);setSingleStep(100);setCursor(Qt::PointingHandCursor);
        for(int i=0;i<64;++i)bars[i]=16+(i*17)%28;
        clock.start();
    }
    void samples(const QAudioBuffer& buffer){
        if(!isVisible()||!buffer.isValid()||!animateSpectrum)return;
        const auto format=buffer.format();
        if(format.sampleFormat()!=QAudioFormat::Float || format.channelCount()!=1)return;
        const auto* data=buffer.constData<float>();
        for(int i=0;i<buffer.frameCount();++i){ring[index]=data[i];index=(index+1)%size;}
        if(clock.elapsed()<45)return;
        clock.restart();
        std::array<std::complex<double>,size> bins;
        for(int i=0;i<size;++i){
            double phase=2*std::numbers::pi*i/size;
            bins[i]=ring[(index+i)%size]*(.42-.5*std::cos(phase)+.08*std::cos(2*phase));
        }
        for(int i=1,j=0;i<size;++i){
            int bit=size/2;
            while(j&bit){j^=bit;bit/=2;}
            j^=bit;if(i<j)std::swap(bins[i],bins[j]);
        }
        for(int span=2;span<=size;span*=2){
            const auto step=std::polar(1.,-2*std::numbers::pi/span);
            for(int i=0;i<size;i+=span){
                std::complex<double> w=1;
                for(int j=0;j<span/2;++j){
                    auto a=bins[i+j],b=w*bins[i+j+span/2];
                    bins[i+j]=a+b;bins[i+j+span/2]=a-b;w*=step;
                }
            }
        }
        for(int i=0;i<size/2;++i)magnitude[i]=.65*magnitude[i]+.35*std::abs(bins[i])/size;
        const double rate=format.sampleRate(),ratio=qMin(14000.,rate/2)/70.;
        for(int i=0;i<64;++i){
            int band=i*23%64;
            double low=70*std::pow(ratio,band/64.),high=70*std::pow(ratio,(band+1)/64.);
            int first=qBound(1,int(low*size/rate),size/2-1),last=qBound(first,int(std::ceil(high*size/rate)),size/2-1);
            double peak=*std::max_element(magnitude.begin()+first,magnitude.begin()+last+1);
            double weighted=20*std::log10(qMax(1.e-12,peak))+qMax(0.,3*std::log2(low/200));
            double target=7+71*std::pow(qBound(0.,(weighted+75)/65,1.),.8);
            bars[i]=std::round((bars[i]+(target-bars[i])*(target>bars[i]?.55:.22))*10.)/10.;
        }
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);
        for(int i=0;i<64;++i){
            const qreal x=i*(width()-2)/63.,h=bars[i]*height()/100.;
            p.setBrush(maximum()>0 && x<width()*double(value())/maximum()?QColor("#d2d8e2"):QColor("#62656b"));
            p.drawRoundedRect(QRectF(x,height()-h,2,h),1,1);
        }
        if(hasFocus()&&keyboardFocus){p.setPen(QPen(QColor("#d2d8e2"),2));p.setBrush(Qt::NoBrush);p.drawRoundedRect(rect().adjusted(1,1,-1,-1),4,4);}
    }
    void focusInEvent(QFocusEvent* e) override{
        keyboardFocus=e->reason()==Qt::TabFocusReason||e->reason()==Qt::BacktabFocusReason||e->reason()==Qt::ShortcutFocusReason;
        QSlider::focusInEvent(e);update();
    }
    void keyPressEvent(QKeyEvent* e) override{keyboardFocus=true;QSlider::keyPressEvent(e);update();}
    void mousePressEvent(QMouseEvent* e) override{if(e->button()==Qt::LeftButton){keyboardFocus=false;update();setFocus(Qt::MouseFocusReason);setSliderDown(true);seek(e);}}
    void mouseMoveEvent(QMouseEvent* e) override{if(isSliderDown())seek(e);}
    void mouseReleaseEvent(QMouseEvent* e) override{if(isSliderDown()){seek(e);setSliderDown(false);}}
private:
    void seek(QMouseEvent* e){setValue(qRound(maximum()*qBound(0.,e->position().x()/qMax(1,width()),1.)));}
    static constexpr int size=2048;
    std::array<float,size> ring{};std::array<double,size/2> magnitude{};std::array<double,64> bars{};
    const bool animateSpectrum=Ui::animationsEnabled();
    int index=0;bool keyboardFocus=false;QElapsedTimer clock;
};

class AudioAttachment final : public QWidget {
public:
    AudioAttachment(ServerApi& api,QString fileName,QString route,std::function<void()> save):
        api(api),name(std::move(fileName)),route(std::move(route)){
        setObjectName("audioAttachment");setAccessibleName(name);setAttribute(Qt::WA_StyledBackground);
        setMaximumWidth(480);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);setFixedHeight(110);
        setStyleSheet("QWidget#audioAttachment{background:#1b1d1d;border-radius:20px}");
        auto* row=new QHBoxLayout(this);row->setContentsMargins(16,14,16,14);row->setSpacing(14);
        play=new QPushButton;play->setAccessibleName(UiText::Play()+name);play->setFixedSize(42,42);play->setIconSize({22,22});play->setCursor(Qt::PointingHandCursor);
        play->setStyleSheet("QPushButton{background:#dce5e3;border:0;border-radius:21px;padding:0} QPushButton:hover{background:#f2f8f6} QPushButton:focus{border:1px solid #999da5}");
        play->setEnabled(false);play->setIcon(symbol(false));row->addWidget(play);
        auto* main=new QVBoxLayout;main->setSpacing(0);row->addLayout(main,1);
        auto* title=new QHBoxLayout;main->addLayout(title);
        fileLabel=Ui::text(name,"color:#e5e8e7;font-size:12px");fileLabel->setToolTip(name);fileLabel->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);title->addWidget(fileLabel,1);
        auto* download=Ui::action("download",UiText::Download()+name,24);download->setIconSize({16,16});download->setStyleSheet("QPushButton{padding:0;background:transparent;border:0} QPushButton:hover{background:#252626;border-radius:6px}");
        download->setHoverIcons(Ui::tintedIcon("download",QColor("#8d9994")),Ui::tintedIcon("download",QColor("#e1f5eb")));title->addWidget(download);
        connect(download,&QPushButton::clicked,download,std::move(save));main->addSpacing(6);
        timeline=new AudioTimeline;timeline->setAccessibleName(UiText::PlaybackPosition()+name);main->addWidget(timeline);main->addSpacing(5);
        auto* time=new QHBoxLayout;main->addLayout(time);
        elapsed=Ui::text("0:00","font-size:10px;color:#999da5");total=Ui::text("0:00","font-size:10px;color:#999da5");
        time->addWidget(elapsed);time->addStretch();time->addWidget(total);
        connect(play,&QPushButton::clicked,this,[this]{
            if(wantsPlayback){suspendPlayback();return;}
            if(active && active!=this)active->suspendPlayback();
            active=this;
            wantsPlayback=true;
            if(!localFile.isEmpty())openPlayer();
            else if(!files)load();
        });
        connect(timeline,&QSlider::valueChanged,this,[this](int position){
            resumePosition=position;
            elapsed->setText(timeText(position));
            if(player && wantsPlayback)player->setPosition(position);
        });
    }
    QSize sizeHint() const override{return {480,110};}
    ~AudioAttachment() override{releasePlayer();}
    QString key;bool largeOriginal=false;
protected:
    void paintEvent(QPaintEvent* event) override{
        QWidget::paintEvent(event);
        if(!requested){requested=true;QTimer::singleShot(0,this,[this]{load();});}
    }
    void resizeEvent(QResizeEvent* event) override{
        QWidget::resizeEvent(event);fileLabel->setText(fileLabel->fontMetrics().elidedText(name,Qt::ElideRight,qMax(1,width()-128)));
    }
private:
    static QIcon symbol(bool pause){
        QPixmap pm(44,44);pm.fill(Qt::transparent);QPainter p(&pm);p.setRenderHint(QPainter::Antialiasing);p.scale(44./24,44./24);p.setPen(Qt::NoPen);p.setBrush(QColor("#1b2421"));
        if(pause){p.drawRect(QRectF(6,4,4,16));p.drawRect(QRectF(14,4,4,16));}
        else p.drawPolygon(QPolygonF{{8,4},{8,20},{20,12}});
        p.end();return QIcon(pm);
    }
    static QString timeText(qint64 ms){const auto sec=qMax<qint64>(0,ms)/1000;return QString("%1:%2").arg(sec/60).arg(sec%60,2,10,QChar('0'));}
    void failed(){play->setEnabled(false);total->setText(UiText::PlaybackFailed());}
    void load(){
        if(!wantsPlayback){
            const auto meta=api.durationPath(route);
            if(!meta.isEmpty()){
                QFile cached(meta);
                if(cached.open(QIODevice::ReadOnly) && cached.size()<=12){
                    const int ms=cached.readAll().toInt();
                    if(ms>0){total->setText(timeText(ms));timeline->setMaximum(ms);play->setEnabled(true);return;}
                }
            }
        }
        if(files)return;
        if(largeOriginal&&!wantsPlayback){play->setEnabled(true);return;}
        files=std::make_shared<QTemporaryDir>();if(!files->isValid()){failed();return;}
        const QString ext=QFileInfo(name).suffix().toLower(),path=files->filePath("audio."+ext);
        api.download(route,path,[this,guard=QPointer<AudioAttachment>(this),keep=files,path,ext](QString error){
            if(!guard)return;
            QFile f(path);if(!error.isEmpty()||!f.open(QIODevice::ReadOnly)){failed();return;}
            const QByteArray h=f.read(16);f.close();
            const bool valid=ext=="mp3"?(h.startsWith("ID3")||(h.size()>1&&uchar(h[0])==255&&(uchar(h[1])&224)==224)):
                ext=="wav"?(h.startsWith("RIFF")&&h.mid(8,4)=="WAVE"):ext=="flac"?h.startsWith("fLaC"):ext=="m4a"?h.mid(4,4)=="ftyp":h.startsWith("OggS");
            if(!valid){failed();return;}
            localFile=path;
            play->setEnabled(true);
            if(wantsPlayback){openPlayer();return;}
            MediaPreviewQueue::request(this,path,{},[this](qint64 duration,QImage){
                if(duration>0){
                    total->setText(timeText(duration));timeline->setMaximum(int(qMin<qint64>(INT_MAX,duration)));
                    const auto meta=api.durationPath(route);
                    if(!meta.isEmpty()){
                        QSaveFile file(meta);const auto data=QByteArray::number(duration);
                        if(file.open(QIODevice::WriteOnly) && file.write(data)==data.size() && file.commit())api.trimMedia();
                    }
                }
            });
        },wantsPlayback?1536LL*1048576:20LL*1048576,this);
    }
    void releasePlayer(){
        if(!player)return;
        disconnect(player.get(),nullptr,this,nullptr);
        player.reset();
    }
    void suspendPlayback(bool ended=false){
        if(player && wantsPlayback && !ended)resumePosition=player->position();
        if(ended)resumePosition=0;
        wantsPlayback=false;
        if(active==this)active.clear();
        releasePlayer();
        QSignalBlocker block(timeline);
        timeline->setValue(int(resumePosition));
        elapsed->setText(timeText(resumePosition));
        play->setIcon(symbol(false));
        play->setAccessibleName(UiText::Play()+name);
    }
    void openPlayer(){
        player=std::make_unique<AttachmentPlayback>(this);
        player->progress=[this](int ms,int duration){
            QSignalBlocker block(timeline);
            if(duration>0){total->setText(timeText(duration));timeline->setMaximum(duration);}
            if(!timeline->isSliderDown())timeline->setValue(ms);
            elapsed->setText(timeText(ms));
        };
        player->samples=[this](QByteArray bytes){
            QAudioFormat format;format.setSampleFormat(QAudioFormat::Float);format.setChannelCount(1);format.setSampleRate(48000);
            timeline->samples(QAudioBuffer(bytes,format));
        };
        player->stopped=[this](bool error){suspendPlayback(true);if(error)failed();};
        play->setAccessibleName(UiText::Pause()+name);play->setIcon(symbol(true));
        player->start(localFile,{},int(resumePosition),100);
    }
    inline static QPointer<AudioAttachment> active;
    QString localFile;qint64 resumePosition=0;bool wantsPlayback=false;
    ServerApi& api;QString name,route;QPushButton* play;QLabel *fileLabel,*elapsed,*total;AudioTimeline* timeline;bool requested=false;
    std::shared_ptr<QTemporaryDir> files;std::unique_ptr<AttachmentPlayback> player;
};
