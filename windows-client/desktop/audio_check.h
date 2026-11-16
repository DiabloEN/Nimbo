#pragma once
#include "interface_text.h"
#include "noise.h"
#include "audio_pcm.h"
#include "sound_check_phrase.h"
#include "ui.h"
#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QMediaDevices>
#include <cmath>
#include <numbers>

class AudioCheck final : public QWidget {
public:
    AudioCheck(QByteArray in,QByteArray out,bool inCall,std::function<QString()> engine):inputId(in),outputId(out),locked(inCall),engine(std::move(engine)){
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,8,0,0);auto* row=new QHBoxLayout;layout->addLayout(row);
        setStyleSheet("QPushButton{background:#202020;color:#ededed;border:0;border-radius:9px;font-size:12px;padding:0 16px;height:38px} QPushButton:disabled{color:#737373} QPushButton:hover:enabled{background:#303030}");
        mic=new QPushButton(UiText::TestMicrophone());tone=new QPushButton(UiText::TestSound());row->addWidget(mic);row->addWidget(tone);row->addStretch();
        level=new QProgressBar;level->setRange(0,100);level->setTextVisible(false);level->setFixedHeight(6);level->setAccessibleName(UiText::MicrophoneLevel());level->hide();layout->addWidget(level);
        if(inCall)layout->addWidget(Ui::text(UiText::LeaveTheVoiceChannelToListenTo(),"font-size:12px;color:#999"));
        note=Ui::text("","font-size:12px;color:#aaa");note->hide();layout->addWidget(note);
        deadline.setSingleShot(true);connect(&deadline,&QTimer::timeout,this,[this]{stop();});
        connect(mic,&QPushButton::clicked,this,[this]{start(true);});connect(tone,&QPushButton::clicked,this,[this]{start(false);});mic->setEnabled(!locked);
    }
    void devices(QByteArray in,QByteArray out){stop();inputId=in;outputId=out;}
    ~AudioCheck()override{stop();}
private:
    QByteArray inputId,outputId;bool locked;std::function<QString()> engine;QTimer deadline;
    QPushButton *mic,*tone;QLabel* note;QProgressBar* level;std::unique_ptr<QAudioSource> source;std::unique_ptr<QAudioSink> sink;
    std::unique_ptr<VoiceDenoiser> denoiser;QBuffer phrase;QByteArray carry;
    static QAudioDevice device(bool input,const QByteArray& id){
        const auto devices=input?QMediaDevices::audioInputs():QMediaDevices::audioOutputs();
        for(const auto& d:devices)if(d.id()==id)return d;
        return input?QMediaDevices::defaultAudioInput():QMediaDevices::defaultAudioOutput();
    }
    void stop(){deadline.stop();if(source)source->stop();source.reset();if(sink)sink->stop();sink.reset();denoiser.reset();phrase.close();phrase.setData({});carry.clear();level->hide();note->hide();
        mic->setText(UiText::TestMicrophone());mic->setEnabled(!locked);tone->setText(UiText::TestSound());tone->setEnabled(true);}
    void fail(){stop();note->setText(UiText::DeviceUnavailableCheckYourSelectedMicrophoneOr());note->show();}
    static QByteArray melody(const QAudioFormat& format){
        const auto samples=soundCheckPhrase(format.sampleRate());
        if(format.sampleFormat()==QAudioFormat::Int16 && format.channelCount()==1)
            return QByteArray(reinterpret_cast<const char*>(samples.data()),qsizetype(samples.size()*sizeof(int16_t)));
        QByteArray pcm(format.bytesForFrames(int(samples.size())),Qt::Uninitialized);
        char* dest=pcm.data();
        for(auto sample:samples){
            for(int channel=0;channel<format.channelCount();++channel){
                AudioPcm::encode(dest,sample/32768.f,format.sampleFormat());
                dest+=format.bytesPerSample();
            }
        }
        return pcm;
    }
    void start(bool microphone){
        if(sink){stop();return;}
        QAudioFormat format;format.setSampleRate(48000);format.setChannelCount(1);format.setSampleFormat(QAudioFormat::Int16);
        auto out=device(false,outputId),in=device(true,inputId);
        const auto playback=out.isFormatSupported(format)?format:out.preferredFormat();
        const auto capture=in.isFormatSupported(format)?format:in.preferredFormat();
        auto supported=[](const QAudioFormat& f){return f.isValid() && f.sampleRate()%100==0 && f.channelCount()<=8;};
        if(out.isNull() || !supported(playback) || (microphone && (in.isNull() || !supported(capture)))){fail();return;}
        sink=std::make_unique<QAudioSink>(out,playback);
        if(qEnvironmentVariableIsSet("NIMBO_QA_SILENT_OUTPUT"))sink->setVolume(0);
        if(!microphone){phrase.setData(melody(playback));phrase.open(QIODevice::ReadOnly);sink->start(&phrase);if(sink->error()!=QtAudio::NoError){fail();return;}
            mic->setEnabled(false);tone->setText(UiText::StopSound());deadline.start(3000);return;}
        auto* destination=sink->start();source=std::make_unique<QAudioSource>(in,capture);auto* input=source->start();
        if(!input||!destination){fail();return;}
        auto selected=engine();
        try{if(selected=="gtcrn"||selected=="rnnoise")denoiser=std::make_unique<VoiceDenoiser>(selected=="gtcrn");}catch(...){fail();return;}
        connect(input,&QIODevice::readyRead,this,[this,input,destination,capture,playback]{
            carry+=input->readAll();
            const auto bytes=capture.bytesForFrames(capture.sampleRate()/100);
            while(carry.size()>=bytes){
                auto samples=AudioPcm::captureBlock(carry.constData(),capture);
                carry.remove(0,bytes);
                if(denoiser)denoiser->filter(samples);
                float mono[480];double power=0;
                for(int i=0;i<480;++i){mono[i]=samples[i]/32768.f;power+=mono[i]*mono[i];}
                level->setValue(qMin(100,int(std::sqrt(power/480)*300)));
                const auto pcm=AudioPcm::playbackBlock(mono,playback);
                destination->write(pcm);
            }
        });
        level->show();tone->setEnabled(false);mic->setText(UiText::StopTest());note->setText(UiText::YouCanHearYourMicrophoneTheTest());note->show();deadline.start(10000);
    }
};

