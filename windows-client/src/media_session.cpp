#include "interface_text.h"
#include "audio_pcm.h"
#include "sound_check_phrase.h"
#include <QPointer>
#include "media_session.h"
#include "background.h"
#include "screen_audio_capture.h"
#include "noise.h"
#include <livekit/audio_processing_module.h>
#include <QAudioSource>
#include <QAudioSink>
#include <QCamera>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QScreenCapture>
#include <QScreen>
#include <QWindowCapture>
#include <QTimer>
#include <QUrl>
#include <QVideoSink>
#include <private/qvideoframeconverter_p.h>
#include <private/qcapturablewindow_p.h>
#include <QJsonObject>
#include <QJsonDocument>
#include <livekit/livekit.h>
#include <livekit/room.h>
#include <livekit/room_delegate.h>
#include <livekit/local_participant.h>
#include <livekit/remote_participant.h>
#include <livekit/remote_track_publication.h>
#include <livekit/local_video_track.h>
#include <livekit/local_audio_track.h>
#include <livekit/audio_source.h>
#include <livekit/video_source.h>
#include <livekit/encoded_video_source.h>
#include "capture_encoder.h"
#include <livekit/stats.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>

namespace {
constexpr int rate = 48000, samples = 480;
std::mutex sdkMutex;
int sdkUsers = 0;
QAudioDevice chooseDevice(const QList<QAudioDevice>& devices, const QByteArray& id, const QAudioDevice& fallback) {
    if (id.isEmpty()) return fallback;
    for (const auto& device : devices) if (device.id() == id) return device;
    throw std::runtime_error("Selected audio device is no longer available");
}
QAudioFormat chooseFormat(const QAudioDevice& device,bool input) {
    if (device.isNull()) throw std::runtime_error("No audio device available");
    QAudioFormat format;
    format.setSampleRate(rate); format.setChannelCount(input?1:2); format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) format = device.preferredFormat();
    if (!format.isValid() || format.sampleRate() % 100 != 0 || format.channelCount() > 8)
        throw std::runtime_error("Unsupported audio device format");
    return format;
}
}

struct OutgoingVideo {
    std::unique_ptr<CaptureEncoder> encoder;
    int mode=0;
    std::shared_ptr<livekit::VideoSource> source;
    std::shared_ptr<livekit::LocalVideoTrack> track;
    std::unique_ptr<QMediaCaptureSession> capture;
    QElapsedTimer clock;
    qint64 nextCaptureNs=0;
    QImage lastWindowFrame;
};

// SDK/network and device operations live on this worker, never on the UI thread.
class MediaWorker final : public QObject, public livekit::RoomDelegate {
public:
    explicit MediaWorker(MediaSession* owner) : owner_(owner) {}
    int activeEncoderQueues() const{return int(bool(cameraVideo_.encoder))+int(bool(shareVideo_.encoder));}
    void start(const MediaOptions& options, bool mayRetry = true) {
        stop(false); options_ = options; gains_=options.participantVolumes; muted_ = false; tx_ = rx_ = videoRx_ = 0; rms_ = 0; ticks_ = 0;
        ++generation_;
        QString failure=UiText::InvalidMediaServerAddressOrInvitation();
        bool connectionAttempt=false;
        try {
            const QUrl url(options.url);
            if ((url.scheme() != "ws" && url.scheme() != "wss") || url.host().isEmpty() ||
                !url.userInfo().isEmpty() || options.token.isEmpty() || options.token.size() > 16384)
                throw std::runtime_error("Invalid server URL or connection token");
            emit owner_->statusChanged(QString::fromUtf8(UiText::Connecting()));
            connectionAttempt=true;
            failure=UiText::CouldNotConnectToTheMediaServer();
            room_ = std::make_unique<livekit::Room>(); room_->setDelegate(this);
            livekit::RoomOptions config; config.join_retries = 0;
            config.connect_timeout = std::chrono::milliseconds(5000);
            if (!room_->connect(options.url.toStdString(), options.token.toStdString(), config))
                throw std::runtime_error("Could not connect to the room; check server and invitation expiry");
            auto participant = room_->localParticipant().lock();
            if (!participant) throw std::runtime_error("No local participant after connection");
            // Existing participants need not publish a track to appear in the room.
            for(const auto& handle:room_->remoteParticipants())
                if(const auto remote=handle.lock())rememberParticipant(*remote);
            failure=UiText::CouldNotPublishTheMicrophoneToThe();
            source_ = std::make_shared<livekit::AudioSource>(rate, 1, 0);
            audioTrack_ = participant->publishAudioTrack("microphone", source_, livekit::TrackSource::SOURCE_MICROPHONE);
            if (!audioTrack_) throw std::runtime_error("Audio track publication failed");
            tick_ = new QTimer(this); tick_->setTimerType(Qt::PreciseTimer); tick_->setInterval(10);
            connect(tick_, &QTimer::timeout, this, [this] { tick(); });
            QStringList warnings;
            if (!options.synthetic){
                connectionAttempt=false;
                failure=UiText::CouldNotConfigureMicrophoneProcessing();
                processing(options.noise,options.echo,options.agc,options.noiseEngine);
                try{openAudioDevice(true,options.inputId,false);}
                catch(const std::exception&){
                    audioTrack_->mute();muted_=true;
                    warnings.append(UiText::MicrophoneUnavailableChooseAMicrophoneInSound());
                }
                try{openAudioDevice(false,options.outputId,false);}
                catch(const std::exception&){warnings.append(UiText::AudioOutputUnavailableChooseSpeakersInSound());}
                devices_=new QMediaDevices(this);
                outputRetry_=new QTimer(devices_);
                outputRetry_->setSingleShot(true);
                connect(outputRetry_,&QTimer::timeout,this,[this]{refreshOutput();});
                connect(devices_,&QMediaDevices::audioOutputsChanged,this,[this]{
                    outputDelay_=250;
                    outputRetry_->start(0);
                });
                if(!output_)outputRetry_->start(0);
            }
            if(options.videoMode)startVideo(options.videoMode>=3,options.videoMode);
            tick_->start();
            emit owner_->connectedChanged(true);
            if(muted_)emit owner_->microphoneMuted(true);
            if(!warnings.isEmpty())emit owner_->audioWarning(warnings.join(" "));
            // Seed the UI before the first periodic roster update so an early arrival plays its cue.
            QJsonArray initialRoster;
            { std::lock_guard guard(mutex_); for(const auto& peer:peers_)initialRoster.append(peer); rosterDirty_=false; }
            emit owner_->callRoster(initialRoster);
            emit owner_->statusChanged(QString::fromUtf8(UiText::ConnectedLocalMediaTest()));
        } catch (const std::exception&) {
            // Do not expose SDK exception text: URLs/tokens can be embedded in it.
            stop(false);
            if(connectionAttempt && mayRetry){
                const auto attempt=generation_.load();
                QTimer::singleShot(250,this,[this,attempt,options]{
                    if(generation_==attempt)start(options,false);
                });
            }else{
                emit owner_->failed(failure);
            }
        }
    }
    void moveRoom(const QString& url,const QString& token,bool annotations){
        if(!room_ || !tick_)return;
        try{
            // Keep captures, DSP, encoders and their sources. Only the RTC publications belong to a room.
            ++generation_;
            room_->setDelegate(nullptr);room_->disconnect();room_.reset();
            encoderStats_={};
            for(auto* video:{&cameraVideo_,&shareVideo_}){
                video->track.reset();
                if(!video->mode)video->source.reset();
            }
            audioTrack_.reset();screenAudioTrack_.reset();
            {std::lock_guard lock(mutex_);incoming_.clear();frames_.clear();peers_.clear();sdkSpeakers_.clear();lastSpeech_.clear();mutedStreams_.clear();rosterDirty_=true;}
            room_=std::make_unique<livekit::Room>();room_->setDelegate(this);
            livekit::RoomOptions config;config.join_retries=0;config.connect_timeout=std::chrono::milliseconds(5000);
            if(!room_->connect(url.toStdString(),token.toStdString(),config))throw std::runtime_error("move");
            auto local=room_->localParticipant().lock();
            if(!local)throw std::runtime_error("move");
            audioTrack_=livekit::LocalAudioTrack::createLocalAudioTrack("microphone",source_);
            if(muted_)audioTrack_->mute();
            livekit::TrackPublishOptions mic;mic.source=livekit::TrackSource::SOURCE_MICROPHONE;
            local->publishTrack(audioTrack_,mic);
            if(cameraVideo_.mode)publishVideo(cameraVideo_,false);
            if(shareVideo_.mode)publishVideo(shareVideo_,true);
            if(loopback_){
                screenAudioTrack_=local->publishAudioTrack("screen-audio",screenAudioSource_,livekit::TrackSource::SOURCE_SCREENSHARE_AUDIO);
                if(!screenAudioTrack_)throw std::runtime_error("screen audio");
            }
            options_.url=url;options_.token=token;options_.annotationData=annotations;
            for(auto handle:room_->remoteParticipants())if(auto remote=handle.lock())rememberParticipant(*remote);
            emit owner_->roomMoved();
            if(!board_.session.isEmpty())announceInk({});
        }catch(const std::exception&){
            stop(false);emit owner_->failed(UiText::VoiceMoveFailed());
        }
    }
    void stop(bool notify = true) {
        ++generation_;
        resetSpeaking();
        if (tick_) { tick_->stop(); delete tick_; tick_ = nullptr; }
        releaseVideo(false,false);releaseVideo(true,false);
        if (input_) { disconnect(input_,nullptr,this,nullptr);disconnect(inputIo_,nullptr,this,nullptr);input_->stop(); delete input_; input_ = nullptr; }
        delete devices_;devices_=nullptr;outputRetry_=nullptr;outputDelay_=250;
        closeOutput();
        inputIo_ = outputIo_ = nullptr; pending_.clear();processor_.reset();denoiser_.reset();effect_.reset();
        if (room_) { room_->setDelegate(nullptr); room_->disconnect(); room_.reset(); }
        audioTrack_.reset();source_.reset();
        encoderStats_ = {};
        { std::lock_guard guard(mutex_); incoming_.clear(); frames_.clear(); peers_.clear(); gains_.clear();mutedStreams_.clear();lastSpeech_.clear();sdkSpeakers_.clear();rosterDirty_=true; }
        if (notify) {
            emit owner_->connectedChanged(false);
            emit owner_->statusChanged(QString::fromUtf8(UiText::Disconnected()));
        }
    }
    void mute(bool value) {
        if(!value && !options_.synthetic && !input_ && audioTrack_){
            try{openAudioDevice(true,options_.inputId,false);}
            catch(const std::exception&){
                emit owner_->microphoneMuted(true);emit owner_->audioWarning(UiText::MicrophoneUnavailableChooseOneInSoundSettings());return;
            }
        }
        muted_ = value;
        if(value)resetSpeaking();
        if (!audioTrack_) return;
        try {
            if (value) audioTrack_->mute(); else audioTrack_->unmute();
        } catch (const std::exception&) {
            stop();
            emit owner_->failed(QString::fromUtf8(UiText::CouldNotChangeMicrophoneStateTheCall()));
        }
    }
    void muteStream(QString peer,bool mute){std::lock_guard guard(mutex_);if(mute)mutedStreams_.insert(peer);else mutedStreams_.remove(peer);}
    void volume(QString id,int percent){std::lock_guard guard(mutex_);gains_[id]=qBound(0,percent,200);}
    void blur(bool on,int radius){options_.blur=on;options_.blurRadius=qBound(1,radius,60);if(!on)effect_.reset();}
    void processing(bool noise,bool echo,bool agc,QString engine){
        if(noise&&engine!="browser"){if(!denoiser_||options_.noiseEngine!=engine)denoiser_=std::make_unique<VoiceDenoiser>(engine=="gtcrn");}else denoiser_.reset();options_.noiseEngine=engine;
        options_.noise=noise;options_.echo=echo;options_.agc=agc;
        if(!noise&&!echo&&!agc){processor_.reset();return;}
        livekit::AudioProcessingModule::Options o;o.noise_suppression=noise&&engine=="browser";o.echo_cancellation=echo;o.high_pass_filter=noise;o.auto_gain_control=agc;
        auto next=std::make_unique<livekit::AudioProcessingModule>(o);if(echo)next->setStreamDelayMs(50);processor_=std::move(next);
    }
    void setScreenAudio(bool enabled){
        stopScreenAudio(true);
        if(!enabled || !room_ || shareVideo_.mode!=3){emit owner_->screenAudioChanged(false,{});return;}
        try{
            loopback_=std::make_unique<ScreenAudioCapture>(options_.applicationPid);
            auto peer=room_->localParticipant().lock();if(!peer)throw std::runtime_error("Room unavailable");
            screenAudioSource_=std::make_shared<livekit::AudioSource>(48000,2,0);
            screenAudioTrack_=peer->publishAudioTrack("screen-audio",screenAudioSource_,livekit::TrackSource::SOURCE_SCREENSHARE_AUDIO);
            if(!screenAudioTrack_)throw std::runtime_error("Publication failed");
            emit owner_->screenAudioChanged(true,{});
        }catch(const std::exception&){stopScreenAudio(true);emit owner_->screenAudioChanged(false,UiText::ScreenSharingContinuesWithoutAudioCapturingOther());}
    }
    void selectAudioDevice(bool input,const QByteArray& id){
        if(!room_)return;
        auto& applied=input?options_.inputId:options_.outputId;
        try{
            if(!options_.synthetic || !id.isEmpty())openAudioDevice(input,id,options_.synthetic);
            applied=id;
            emit owner_->audioDeviceSelected(input,id,{});
        }catch(const std::exception&){
            const auto reason=input?QString::fromUtf8(UiText::CouldNotSwitchMicrophonesCheckTheActive()):
                QString::fromUtf8(UiText::CouldNotSwitchOutputCheckTheSystem());
            emit owner_->audioDeviceSelected(input,applied,reason);
        }
    }
    void setVideoHeight(int height){height_=height==360||height==720?height:1080;}
    void sendInk(const QString& host,QJsonObject command){
        if(!room_)return;
        auto local=room_->localParticipant().lock();if(!local)return;
        const auto self=QString::fromStdString(local->identity());
        if(host==self){receiveInk(self,true,command);return;}
        if(options_.annotationData && !host.isEmpty())publishInk(host,command);
    }
    void changeVideo(bool share,int mode,QByteArray camera,QString screen,QCapturableWindow window){
        if(!room_)return;
        try{
            releaseVideo(share,true);
            if(share){options_.screenName=screen;options_.window=window;}else options_.cameraId=camera;
            if(mode)startVideo(share,mode);
            emit owner_->videoModeChanged(share,mode,{});
        }catch(const std::exception&){releaseVideo(share,true);emit owner_->videoModeChanged(share,0,UiText::VideoSourceUnavailable());}
    }
private:
    ScreenInk::Board board_;
    QImage cleanScreen_;
    std::atomic_int pendingInk_{0};
    void publishInk(const QString& destination,const QJsonObject& object){
        if(!options_.annotationData || !room_)return;
        const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);
        if(bytes.size()>12000)return;
        try{
            if(auto local=room_->localParticipant().lock()){
                std::vector<std::string> recipients;
                if(!destination.isEmpty())recipients.push_back(destination.toStdString());
                local->publishData({bytes.begin(),bytes.end()},true,recipients,ScreenInk::topic);
            }
        }catch(const std::exception&){emit owner_->audioWarning(UiText::InkSendFailed());}
    }
    void announceInk(const QString& destination){
        publishInk(destination,{{"type","ready"},{"session",board_.session},{"allowed",true}});
    }
    void receiveInk(const QString& author,bool local,const QJsonObject& command){
        const auto type=command["type"].toString();
        if((type=="ready" || type=="locked") && !local){
            const auto epoch=command["session"].toString();
            if(!QUuid::fromString(epoch).isNull())emit owner_->inkSession(author,epoch,type=="ready" && command["allowed"].toBool(true));
            return;
        }
        if(board_.session.isEmpty())return;
        if(type=="query"){announceInk(author);return;}
        if(!board_.apply(author,local,command))return;
        emit owner_->localInkChanged(board_.strokes);
        const auto& last=shareVideo_.mode==4?shareVideo_.lastWindowFrame:cleanScreen_;
        if(!last.isNull())sendVideo(last,true);
    }
    void onUserPacketReceived(livekit::Room&,const livekit::UserDataPacketEvent& event) override{
        if(event.topic=="nimbo.voice-move"){
            if(!event.participant && event.kind==livekit::DataPacketKind::Reliable && !event.data.empty() && event.data.size()<=128){
                const QByteArray bytes(reinterpret_cast<const char*>(event.data.data()),qsizetype(event.data.size()));
                if(QJsonDocument::fromJson(bytes).object()["check"].toBool())emit owner_->voiceControlHint();
            }
            return;
        }
        if(event.topic!=ScreenInk::topic || !event.participant || event.data.empty() || event.data.size()>12000)return;
        if(pendingInk_.fetch_add(1)>=32){--pendingInk_;return;}
        const auto sender=QString::fromStdString(event.participant->identity());
        const QByteArray bytes(reinterpret_cast<const char*>(event.data.data()),qsizetype(event.data.size()));
        const auto generation=generation_.load();
        QMetaObject::invokeMethod(this,[this,sender,bytes,generation]{
            --pendingInk_;
            if(generation==generation_ && room_ && options_.annotationData)
                receiveInk(sender,false,QJsonDocument::fromJson(bytes).object());
        },Qt::QueuedConnection);
    }
    void stopScreenAudio(bool unpublish){
        loopback_.reset();
        if(unpublish && screenAudioTrack_ && room_){
            try{if(auto peer=room_->localParticipant().lock())peer->unpublishTrack(screenAudioTrack_->sid());}catch(const std::exception&){}
        }
        screenAudioTrack_.reset();screenAudioSource_.reset();
    }
    void releaseVideo(bool share,bool stayInRoom){
        if(share && loopback_){stopScreenAudio(stayInRoom);emit owner_->screenAudioChanged(false,{});}
        auto& v=share?shareVideo_:cameraVideo_;
        if(share && !board_.session.isEmpty()){
            board_={};cleanScreen_={};emit owner_->localInkChanged({});
            emit owner_->inkSession({},{},false);
        }
        if(v.capture){
            if(auto* sink=v.capture->videoSink())disconnect(sink,nullptr,v.capture.get(),nullptr);
            if(auto* camera=v.capture->camera())camera->stop();
            if(auto* screen=v.capture->screenCapture())screen->stop();
            if(auto* window=v.capture->windowCapture())window->stop();
            v.capture->setVideoSink(nullptr);
            v.capture.reset();
        }
        v.encoder.reset();
        if(stayInRoom && v.mode && v.track && v.track->publication()){
            // WebRTC retains a sender for each publication until the room closes.
            // Keep one muted sender per source; the device and its frames are gone.
            try{
                v.track->mute();
            }catch(const std::exception&){
                // A disconnected or invalid sender cannot be reused on the next start.
                try{if(auto p=room_->localParticipant().lock())p->unpublishTrack(v.track->sid());}catch(const std::exception&){}
                v.track.reset();
                v.source.reset();
            }
        }else if(!stayInRoom || !v.track || !v.track->publication()){
            v.track.reset();v.source.reset();
        }
        if(v.mode&&stayInRoom)owner_->postFrame({},share,{});
        v.lastWindowFrame={};
        v.mode=0;
        encoderStats_={};
        if(!share)effect_.reset();
    }
    void openAudioDevice(bool input,const QByteArray& id,bool synthetic){
        const auto device=input?chooseDevice(QMediaDevices::audioInputs(),id,QMediaDevices::defaultAudioInput()):
                                chooseDevice(QMediaDevices::audioOutputs(),id,QMediaDevices::defaultAudioOutput());
        const auto format=chooseFormat(device,input);
        if(synthetic)return;
        // Keep the active stream until the new endpoint is open.
        if(input){
            auto next=std::make_unique<QAudioSource>(device,format,this);
            next->setBufferSize(format.bytesForDuration(100000));
            auto* io=next->start();
            if(!io || next->error()!=QtAudio::NoError)throw std::runtime_error("Cannot open microphone");
            if(input_){disconnect(input_,nullptr,this,nullptr);disconnect(inputIo_,nullptr,this,nullptr);input_->stop();delete input_;}
            input_=next.release();inputIo_=io;inFormat_=format;pending_.clear();resetSpeaking();
            connect(io,&QIODevice::readyRead,this,[this]{captureAudio();});
            connect(input_,&QAudioSource::stateChanged,input_,[this,source=input_](QtAudio::State state){
                if(source!=input_ || state!=QtAudio::StoppedState || source->error()==QtAudio::NoError)return;
                inputIo_=nullptr;
                input_->deleteLater();input_=nullptr;pending_.clear();
                mute(true);
                emit owner_->microphoneMuted(true);
                emit owner_->audioWarning(UiText::MicrophoneDisconnectedConnectADeviceAndUnmute());
            },Qt::QueuedConnection);
        }else{
            auto next=std::make_unique<QAudioSink>(device,format,this);
            next->setBufferSize(format.bytesForDuration(60000));
            next->setVolume(qEnvironmentVariableIsSet("NIMBO_QA_SILENT_OUTPUT")?0.f:1.f);
            auto* io=next->start();
            if(!io || next->error()!=QtAudio::NoError)throw std::runtime_error("Cannot open audio output");
            closeOutput();
            output_=next.release();outputIo_=io;outFormat_=format;outputId_=device.id();
            outputProgress_.start();
            connect(output_,&QAudioSink::stateChanged,this,[this](QtAudio::State state){
                if((state==QtAudio::StoppedState || state==QtAudio::SuspendedState) && outputRetry_)outputRetry_->start(0);
            });
        }
    }
    void closeOutput(){
        outputIo_=nullptr;
        outputId_.clear();outputProgress_.invalidate();
        if(!output_)return;
        disconnect(output_,nullptr,this,nullptr);
        output_->stop();
        delete output_;
        output_=nullptr;
    }
    void refreshOutput(bool stalled=false){
        const auto outputs=QMediaDevices::audioOutputs();
        const auto fallback=QMediaDevices::defaultAudioOutput();
        auto target=fallback;
        for(const auto& device:outputs){
            if(device.id()==options_.outputId){target=device;break;}
        }
        if(target.isNull()){closeOutput();return;}
        if(!stalled && output_ && outputId_==target.id() && outputIo_ &&
           (output_->state()==QtAudio::ActiveState || output_->state()==QtAudio::IdleState))return;
        try{
            openAudioDevice(false,target.id(),false);
            outputDelay_=250;
            // A fallback is temporary. Keep the selected headset so it is restored on replug.
        }catch(const std::exception&){
            if(output_ && (stalled || output_->state()==QtAudio::StoppedState))closeOutput();
            if(!fallback.isNull() && fallback.id()!=target.id() && (!output_ || !outputIo_ || outputId_!=fallback.id())){
                try{openAudioDevice(false,fallback.id(),false);}catch(const std::exception&){}
            }
            outputRetry_->start(outputDelay_);
            outputDelay_=qMin(outputDelay_*2,4000);
        }
    }
    void deviceFailed() {
        const auto gen = generation_.load();
        QTimer::singleShot(0, this, [this, gen] { if (gen != generation_) return; stop();
            emit owner_->failed(QString::fromUtf8(UiText::AudioDeviceDisconnectedOrUnavailableSelectA())); });
    }
    QElapsedTimer lastVoice_;
    bool speaking_=false;
    void resetSpeaking() {
        lastVoice_.invalidate();
        if(speaking_){speaking_=false;emit owner_->localSpeakingChanged(false);}
    }
    void sampleSpeaking(const std::vector<std::int16_t>& samples) {
        if(samples.empty())return;
        double energy=0;
        for(const auto sample:samples)energy+=double(sample)*sample;
        const double rms=std::sqrt(energy/samples.size())/32768.;
        if(rms>(speaking_?.007:.012))lastVoice_.start();
        const bool active=lastVoice_.isValid() && lastVoice_.elapsed()<160;
        if(speaking_!=active){speaking_=active;emit owner_->localSpeakingChanged(active);}
    }
    void sendFrame(std::vector<std::int16_t> data) {
        if (muted_) std::fill(data.begin(), data.end(), 0);
        try {
            if(denoiser_&&!muted_)denoiser_->filter(data);
            livekit::AudioFrame frame(std::move(data),rate,1,samples);
            if(processor_&&!muted_)processor_->processStream(frame);
            if(!muted_ && tx_.load()%5==0)sampleSpeaking(frame.data());
            source_->captureFrame(frame,100);
            ++tx_;
        }
        catch (const std::exception&) { deviceFailed(); }
    }
    void captureAudio() {
        if (!inputIo_ || !source_) return;
        pending_ += inputIo_->read(inFormat_.bytesForDuration(100000));
        const int frames = inFormat_.sampleRate() / 100;
        const int bytes = frames * inFormat_.bytesPerFrame();
        if (pending_.size() > bytes * 10) pending_.remove(0, pending_.size() - bytes * 10);
        int iterations = 0;
        while (pending_.size() >= bytes && iterations++ < 10) {
            auto pcm=AudioPcm::captureBlock(pending_.constData(),inFormat_);
            pending_.remove(0, bytes); sendFrame(std::move(pcm));
        }
    }
    void playAudio() {
        if (!outputIo_) return;
        // Some drivers leave a live sink with a permanently full buffer after reconnect.
        if(outputProgress_.isValid() && outputProgress_.elapsed()>2000){
            refreshOutput(true);
            if(!outputIo_)return;
        }
        const int frames = outFormat_.sampleRate() / 100;
        const int bytes = frames * outFormat_.bytesPerFrame();
        for (int chunk = 0; chunk < 3 && output_->bytesFree() >= bytes; ++chunk) {
            float mixed[samples]{};
            { std::lock_guard guard(mutex_);
                for (auto& [id, track] : incoming_) {
                    auto& buffer=track.buffer;
                    const int count = std::min(samples, int(buffer.size() / 2));
                    const int percent=gains_.value(track.member,100);
                    const float gain=track.screen?percent/100.f:AudioPcm::voiceGain(percent);
                    for (int i = 0; i < count; ++i) { qint16 v; std::memcpy(&v, buffer.constData() + 2 * i, 2); const float value=v/32768.f; mixed[i] += track.screen?gain*value:track.level.apply(value,gain); }
                    buffer.remove(0, count * 2);
                }
            }
            if(processor_&&options_.echo){std::vector<int16_t> ref(samples);for(int i=0;i<samples;++i)ref[i]=int16_t(std::clamp(mixed[i],-1.f,1.f)*32767);livekit::AudioFrame frame(std::move(ref),rate,1,samples);try{processor_->processReverseStream(frame);}catch(const std::exception&){deviceFailed();return;}}
            const auto data=AudioPcm::playbackBlock(mixed,outFormat_);
            const auto written=outputIo_->write(data);
            if(written>0)outputProgress_.restart();
            if(written<0){
                outputIo_=nullptr;
                if(outputRetry_ && !outputRetry_->isActive())outputRetry_->start(0);
            }
            if(written!=data.size())break;
        }
    }
    void publishVideo(OutgoingVideo& stream,bool share){
        stream.track=livekit::LocalVideoTrack::createLocalVideoTrack(share?"screen":"camera",stream.source);
        livekit::TrackPublishOptions publish;
        publish.source=share?livekit::TrackSource::SOURCE_SCREENSHARE:livekit::TrackSource::SOURCE_CAMERA;
        publish.video_encoder=options_.hardwareVideo?livekit::VideoEncoderBackend::Hardware:livekit::VideoEncoderBackend::PreEncoded;
        publish.video_codec=livekit::VideoCodec::H264;publish.simulcast=false;publish.video_encoding=livekit::VideoEncodingOptions{3000000,30};
        auto participant=room_->localParticipant().lock();if(!participant)throw std::runtime_error("No local participant");
        participant->publishTrack(stream.track,publish);
    }
    void startVideo(bool share,int mode){
        auto& stream=share?shareVideo_:cameraVideo_;stream.mode=mode;
        if(!stream.track){
            const int w = mode==1 ? 640 : 1920, h = mode==1 ? 360 : 1080;
            if (options_.hardwareVideo) stream.source = std::make_shared<livekit::VideoSource>(w,h);
            else stream.source = std::make_shared<livekit::EncodedVideoSource>(livekit::VideoCodec::H264,w,h);
            publishVideo(stream,share);
        }else{
            stream.track->unmute();
        }
        if (!options_.hardwareVideo) stream.encoder = std::make_unique<CaptureEncoder>(
            std::static_pointer_cast<livekit::EncodedVideoSource>(stream.source));
        if(share){
            board_.session=QUuid::createUuid().toString(QUuid::WithoutBraces);
            emit owner_->inkSession({},board_.session,true);
            announceInk({});
        }
        if(mode==1)return;
        stream.capture=std::make_unique<QMediaCaptureSession>();auto* capture=stream.capture.get();
        auto* sink=new QVideoSink(capture);capture->setVideoSink(sink);stream.clock.invalidate();stream.nextCaptureNs=0;
        struct CaptureMailbox {
            std::mutex mutex;
            QVideoFrame latest;
            bool scheduled=false;
        };
        auto mailbox=std::make_shared<CaptureMailbox>();
        connect(sink,&QVideoSink::videoFrameChanged,capture,[this,share,&stream,capture,mailbox](const QVideoFrame& incoming){
            if(!incoming.isValid())return;
            {
                std::lock_guard lock(mailbox->mutex);
                mailbox->latest=incoming;
                if(mailbox->scheduled)return;
                mailbox->scheduled=true;
            }
            QMetaObject::invokeMethod(capture,[this,share,&stream,mailbox]{
                QVideoFrame frame;
                {
                    std::lock_guard lock(mailbox->mutex);
                    frame.swap(mailbox->latest);
                    mailbox->scheduled=false;
                }
                if(!stream.source)return;
                // Follow a 30 Hz timeline. Camera-driver jitter must not discard every other frame.
                constexpr qint64 period=1000000000/30;
                const auto now=stream.clock.isValid()?stream.clock.nsecsElapsed():0;
                if(now+period/4<stream.nextCaptureNs)return;
                // Capture is consumed as CPU pixels by the RTC encoder and widget preview.
                const auto image=qImageFromVideoFrame(frame,true);
                if(image.isNull())return;
                if(!stream.clock.isValid())stream.clock.start();
                stream.nextCaptureNs=qMax(now+period/2,stream.nextCaptureNs+period);
                if(stream.mode==4)stream.lastWindowFrame=image;
                sendVideo(image,share);
            },Qt::QueuedConnection);
        },Qt::DirectConnection);
        auto failed=[this,share,guard=QPointer<QMediaCaptureSession>(capture)](auto,const QString& reason){
            QTimer::singleShot(0,this,[this,share,guard,reason]{
                if(!guard)return;
                releaseVideo(share,true);
                emit owner_->videoModeChanged(share,0,reason.isEmpty()?UiText::VideoSourceUnavailable():UiText::VideoSourceUnavailable2()+reason);
            });
        };
        QTimer::singleShot(12000,capture,[this,share,&stream]{
            if(stream.clock.isValid())return;
            QTimer::singleShot(0,this,[this,share,guard=QPointer<QMediaCaptureSession>(stream.capture.get())]{
                if(!guard)return;
                releaseVideo(share,true);
                emit owner_->videoModeChanged(share,0,UiText::CouldNotCaptureAFrameFromThe());
            });
        });
        if(!share){
            auto device=QMediaDevices::defaultVideoInput();if(!options_.cameraId.isEmpty()){
                device={};for(auto candidate:QMediaDevices::videoInputs())if(candidate.id()==options_.cameraId)device=candidate;
            }
            if(device.isNull())throw std::runtime_error("Camera unavailable");
            auto* camera=new QCamera(device,capture);
            QCameraFormat preferred;
            const auto formats=device.videoFormats();
            for(const auto& format:formats){
                if(format.maxFrameRate()<29 || format.resolution().height()>height_)continue;
                if(preferred.isNull() || format.resolution().width()>preferred.resolution().width())preferred=format;
            }
            if(!preferred.isNull())camera->setCameraFormat(preferred);
            capture->setCamera(camera);connect(camera,&QCamera::errorOccurred,capture,failed);camera->start();
        }else if(mode==4){
            if(!options_.window.isValid())throw std::runtime_error("Window unavailable");
            auto* window=new QWindowCapture(capture);window->setWindow(options_.window);capture->setWindowCapture(window);connect(window,&QWindowCapture::errorOccurred,capture,failed);window->start();
            // A static window may have painted before Windows Graphics Capture subscribed.
            auto requestPaint=[window]{
                const auto* selected=QCapturableWindowPrivate::handle(window->window());
                if(selected && window->window().isValid())
                    RedrawWindow(reinterpret_cast<HWND>(selected->id),nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN);
            };
            QTimer::singleShot(250,window,[requestPaint,&stream]{if(!stream.clock.isValid())requestPaint();});
            QTimer::singleShot(4000,window,[window,&stream,requestPaint]{
                if(!stream.clock.isValid() && window->isActive() && window->window().isValid()){
                    window->stop();window->start();
                    QTimer::singleShot(250,window,requestPaint);
                }
            });
            // Some windows only produce a frame when their contents change. A late
            // subscriber still needs an input frame when the encoder requests a keyframe.
            auto* refresh=new QTimer(window);
            refresh->setInterval(1000);
            connect(refresh,&QTimer::timeout,window,[this,window,&stream]{
                if(window->isActive() && window->window().isValid() && !stream.lastWindowFrame.isNull() && stream.clock.elapsed()-stream.nextCaptureNs/1000000>=900)
                    sendVideo(stream.lastWindowFrame,true);
            });
            refresh->start();
        }else{
            QScreen* selected=options_.screenName.isEmpty()?QGuiApplication::primaryScreen():nullptr;
            for(auto* screen:QGuiApplication::screens())if(screen->name()==options_.screenName)selected=screen;
            if(!selected)throw std::runtime_error("Screen unavailable");
            auto* source=new QScreenCapture(capture);source->setScreen(selected);capture->setScreenCapture(source);connect(source,&QScreenCapture::errorOccurred,capture,failed);source->start();
        }
    }
    void sendVideo(const QImage& raw,bool share){
        auto& stream=share?shareVideo_:cameraVideo_;if(!stream.source)return;
        try{
            QSize size=raw.size();if(size.height()>height_||size.width()>height_*16/9)size.scale(height_*16/9,height_,Qt::KeepAspectRatio);
            size.setWidth(qMax(2,size.width()&~1));size.setHeight(qMax(2,size.height()&~1));
            auto image=(size==raw.size()?raw:raw.scaled(size,Qt::IgnoreAspectRatio,Qt::FastTransformation)).convertToFormat(QImage::Format_RGBA8888);
            if(share && stream.mode!=3){
                if(stream.mode==1)cleanScreen_=image;
                if(!board_.strokes.isEmpty()){
                    QPainter composite(&image);ScreenInk::paint(composite,image.rect(),board_.strokes);
                }
            }
            if(!share&&stream.mode==2&&options_.blur){try{if(!effect_)effect_=std::make_unique<BackgroundBlur>();image=effect_->apply(image,options_.blurRadius);}catch(...){blur(false,options_.blurRadius);emit owner_->effectFailed();}}
            if(stream.mode!=1)owner_->postFrame({},share,image);
            if (stream.encoder) {
                stream.encoder->submit(image);
                return;
            }
            auto frame=livekit::VideoFrame::create(image.width(),image.height(),livekit::VideoBufferType::RGBA);
            for(int y=0;y<image.height();++y)std::memcpy(frame.data()+y*image.width()*4,image.constScanLine(y),image.width()*4);
            stream.source->captureFrame(frame);
        }catch(const std::exception&){releaseVideo(share,true);emit owner_->videoModeChanged(share,0,UiText::CouldNotSendVideo());}
    }
    void tick() {
        if(loopback_){
            try{
                loopback_->submitBlocks([this](std::vector<int16_t> pcm){
                    screenAudioSource_->captureFrame(livekit::AudioFrame(std::move(pcm),48000,2,480),100);
                });
            }catch(const std::exception&){stopScreenAudio(true);emit owner_->screenAudioChanged(false,UiText::SystemAudioUnavailable());}
        }
        if (options_.synthetic) {
            std::vector<std::int16_t> pcm(samples);
            static const auto phrase=soundCheckPhrase(rate);
            // A quiet phrase followed by a rest, also when a test call runs for minutes.
            const size_t offset=(tx_.load()%(6*rate/samples))*samples;
            if(offset+samples<=phrase.size()){
                for(int i=0;i<samples;++i)pcm[i]=phrase[offset+i]/3;
            }
            sendFrame(std::move(pcm));
        } else playAudio();
        if ((cameraVideo_.mode==1||shareVideo_.mode==1) && tx_ % 10 == 0) {
            QImage image(640, 360, QImage::Format_RGBA8888); image.fill(QColor::fromHsv(int(tx_ % 360), 180, 180)); if(cameraVideo_.mode==1)sendVideo(image,false);if(shareVideo_.mode==1)sendVideo(image,true);
        }
        if (++ticks_ % 50 == 0) {
            emit owner_->statistics(tx_, rx_, videoRx_, rms_);
            if (cameraVideo_.mode||shareVideo_.mode) {
                try {
                    if (encoderStats_.valid() && encoderStats_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                        for (const auto& item : encoderStats_.get()) {
                            if (const auto* outbound = std::get_if<livekit::RtcOutboundRtpStats>(&item.stats)) {
                                if (!outbound->outbound.encoder_implementation.empty())
                                    emit owner_->encoderReported(QString::fromStdString(outbound->outbound.encoder_implementation), outbound->outbound.power_efficient_encoder);
                            }
                        }
                    }
                    if (!encoderStats_.valid()) encoderStats_ = (cameraVideo_.mode?cameraVideo_.track:shareVideo_.track)->getStats();
                } catch (const std::exception&) { /* Stats must not interrupt a call. */ }
            }
        }
        {
            QMap<QPair<QString,bool>,QImage> images;QJsonArray people;bool changed;
            {
                std::lock_guard guard(mutex_);
                if(ticks_%5==0){
                    for(auto it=peers_.begin();it!=peers_.end();++it){
                        const auto last=lastSpeech_.constFind(it.key());
                        const bool speaking=it.value()["mic"].toBool() && (sdkSpeakers_.contains(it.key()) ||
                            (last!=lastSpeech_.cend() && last->elapsed()<220));
                        if(it.value()["speaking"].toBool()!=speaking){it.value()["speaking"]=speaking;rosterDirty_=true;}
                    }
                }
                images.swap(frames_);changed=rosterDirty_;rosterDirty_=false;if(changed)for(auto p:peers_)people.append(p);
            }
            if(changed)emit owner_->callRoster(people);
            for(auto it=images.cbegin();it!=images.cend();++it)owner_->postFrame(it.key().first,it.key().second,it.value());
        }
    }
    void rememberParticipant(const livekit::RemoteParticipant& participant){
        const auto identity=QString::fromStdString(participant.identity());
        std::lock_guard guard(mutex_);
        auto& person=peers_[identity];
        person["identity"]=identity;
        person["name"]=QString::fromStdString(participant.name());
        person["micKnown"]=true;
        rosterDirty_=true;
    }
    void onParticipantConnected(livekit::Room&,const livekit::ParticipantConnectedEvent& event) override{
        if(event.participant)rememberParticipant(*event.participant);
    }
    void setPublicationState(livekit::Participant* participant,const std::shared_ptr<livekit::TrackPublication>& publication,bool enabled){
        if(!participant||!publication)return;
        const auto source=publication->source();
        const char* key=nullptr;
        if(source==livekit::TrackSource::SOURCE_MICROPHONE)key="mic";
        if(source==livekit::TrackSource::SOURCE_CAMERA)key="camera";
        if(source==livekit::TrackSource::SOURCE_SCREENSHARE)key="screen";
        if(!key)return;
        if(dynamic_cast<livekit::LocalParticipant*>(participant)){
            const auto generation=generation_.load();
            QMetaObject::invokeMethod(this,[this,generation,source,enabled]{
                if(generation!=generation_ || !room_)return;
                if(source==livekit::TrackSource::SOURCE_MICROPHONE){
                    muted_=!enabled;
                    if(muted_)resetSpeaking();
                    emit owner_->microphoneMuted(muted_);
                }else if(!enabled){
                    const bool share=source==livekit::TrackSource::SOURCE_SCREENSHARE;
                    if((share?shareVideo_:cameraVideo_).mode)changeVideo(share,0,{},{},{});
                }
            },Qt::QueuedConnection);
            return;
        }
        std::lock_guard guard(mutex_);
        auto it=peers_.find(QString::fromStdString(participant->identity()));
        if(it!=peers_.end() && it.value()[key].toBool()!=enabled){
            it.value()[key]=enabled;rosterDirty_=true;
            if(!enabled && source!=livekit::TrackSource::SOURCE_MICROPHONE)
                frames_[{it.key(),source==livekit::TrackSource::SOURCE_SCREENSHARE}]={};
        }
    }
    void onTrackMuted(livekit::Room&,const livekit::TrackMutedEvent& event) override{
        setPublicationState(event.participant,event.publication,false);
    }
    void onTrackUnmuted(livekit::Room&,const livekit::TrackUnmutedEvent& event) override{
        setPublicationState(event.participant,event.publication,true);
    }
    void onTrackUnpublished(livekit::Room&,const livekit::TrackUnpublishedEvent& event) override{
        setPublicationState(event.participant,event.publication,false);
    }
    void onTrackSubscribed(livekit::Room& room, const livekit::TrackSubscribedEvent& event) override {
        if (!event.participant || !event.track || !event.publication) return;
        const auto identity = event.participant->identity(), name = event.publication->name();
        const auto kind = event.track->kind();
        auto peer=QString::fromStdString(identity);bool screen=event.publication->source()==livekit::TrackSource::SOURCE_SCREENSHARE;
        rememberParticipant(*event.participant);
        setPublicationState(event.participant,event.publication,!event.publication->muted());
        // The SDK starts its stream reader immediately after this callback returns.
        // Register synchronously here; a queued registration misses that transition.
        // These setters only register callbacks; they do not block on FFI operations.
            if (kind == livekit::TrackKind::KIND_AUDIO) {
                const bool screenAudio=event.publication->source()==livekit::TrackSource::SOURCE_SCREENSHARE_AUDIO;
                const auto streamKey=std::make_pair(identity,name);
                livekit::AudioStream::Options opts; opts.capacity = 10;
                room.setOnAudioFrameCallback(identity, name, [this, streamKey, screenAudio, peer](const livekit::AudioFrame& frame) {
                    if (frame.sampleRate() != rate || frame.numChannels() < 1 || frame.numChannels() > 8 || frame.data().size()%frame.numChannels()!=0) return;
                    double sum = 0; for (auto v : frame.data()) sum += double(v) * v;
                    const double level=frame.data().empty()?0:std::sqrt(sum/frame.data().size())/32768.;
                    rms_=level;++rx_;
                    std::lock_guard guard(mutex_);
                    if(!screenAudio && level>.003 && peers_.contains(peer))lastSpeech_[peer].start();
                    if(options_.synthetic)return;
                    if(screenAudio && mutedStreams_.contains(peer)){incoming_.erase(streamKey);return;}
                    if (!incoming_.contains(streamKey) && incoming_.size() >= 16) return;
                    auto& track=incoming_[streamKey];track.screen=screenAudio;track.member=peer.section('.',0,0);
                    auto& buffer=track.buffer;
                    const int channels=frame.numChannels();
                    if(channels==1)buffer.append(reinterpret_cast<const char*>(frame.data().data()),qsizetype(frame.data().size()*2));
                    else {
                        const auto offset=buffer.size();
                        buffer.resize(offset+qsizetype(frame.data().size()/channels)*2);
                        auto* dst=buffer.data()+offset;
                        for(size_t i=0;i<frame.data().size();i+=channels){
                            const auto value=AudioPcm::monoSample(frame.data().data()+i,channels);
                            std::memcpy(dst,&value,2);dst+=2;
                        }
                    }
                    if (buffer.size() > 19200) buffer.remove(0, buffer.size() - 19200);
                }, opts);
            } else if (kind == livekit::TrackKind::KIND_VIDEO) {
                livekit::VideoStream::Options opts; opts.capacity = 2;
                room.setOnVideoFrameCallback(identity, name, [this,peer,screen](const livekit::VideoFrame& frame, std::int64_t) {
                    ++videoRx_;
                    try {
                        std::optional<livekit::VideoFrame> converted;
                        const auto* rgba = &frame;
                        if (frame.type() != livekit::VideoBufferType::RGBA) { converted.emplace(frame.convert(livekit::VideoBufferType::RGBA)); rgba = &*converted; }
                        QImage image(rgba->data(), rgba->width(), rgba->height(), QImage::Format_RGBA8888);
                        std::lock_guard guard(mutex_);
                        if(peers_.value(peer).value(screen?"screen":"camera").toBool())frames_[{peer,screen}]=image.copy();
                    } catch (const std::exception&) {}
                }, opts);
            }
    }
    void onParticipantDisconnected(livekit::Room&, const livekit::ParticipantDisconnectedEvent& event) override {
        if (!event.participant) return;
        auto identity=event.participant->identity();auto peer=QString::fromStdString(identity);std::lock_guard guard(mutex_);std::erase_if(incoming_,[&](const auto& stream){return stream.first.first==identity;});peers_.remove(peer);lastSpeech_.remove(peer);sdkSpeakers_.remove(peer);rosterDirty_=true;frames_[{peer,false}]={};frames_[{peer,true}]={};
    }
    void onTrackUnsubscribed(livekit::Room&, const livekit::TrackUnsubscribedEvent& e) override{
        if(!e.participant||!e.publication)return;
        if(e.publication->kind()==livekit::TrackKind::KIND_AUDIO){
            std::lock_guard guard(mutex_);incoming_.erase({e.participant->identity(),e.publication->name()});return;
        }
        if(e.publication->kind()!=livekit::TrackKind::KIND_VIDEO)return;
        std::lock_guard guard(mutex_);frames_[{QString::fromStdString(e.participant->identity()),e.publication->source()==livekit::TrackSource::SOURCE_SCREENSHARE}]={};
    }
    void onActiveSpeakersChanged(livekit::Room&, const livekit::ActiveSpeakersChangedEvent& e) override {
        QSet<QString> active;for(auto p:e.speakers)if(p)active.insert(QString::fromStdString(p->identity()));
        std::lock_guard guard(mutex_);sdkSpeakers_=std::move(active);
    }
    void onConnectionQualityChanged(livekit::Room&,const livekit::ConnectionQualityChangedEvent& event) override{
        if(!dynamic_cast<livekit::LocalParticipant*>(event.participant))return;
        const auto generation=generation_.load();
        QMetaObject::invokeMethod(this,[this,generation,quality=event.quality]{
            if(generation!=generation_)return;
            QString state;
            if(quality==livekit::ConnectionQuality::Lost)state="lost";
            if(quality==livekit::ConnectionQuality::Poor)state="poor";
            emit owner_->localConnectionQualityChanged(state);
        },Qt::QueuedConnection);
    }
    void onDisconnected(livekit::Room& room, const livekit::DisconnectedEvent&) override {
        const auto gen = generation_.load();
        QMetaObject::invokeMethod(this, [this, gen, source=&room] { if (gen == generation_ && room_.get()==source) stop(); }, Qt::QueuedConnection);
    }
    void onReconnecting(livekit::Room&, const livekit::ReconnectingEvent&) override { emit owner_->statusChanged(QString::fromUtf8(UiText::Reconnecting())); }
    void onReconnected(livekit::Room&, const livekit::ReconnectedEvent&) override { emit owner_->statusChanged(QString::fromUtf8(UiText::ConnectionRestored())); }
    MediaSession* owner_; MediaOptions options_;
    std::unique_ptr<livekit::AudioProcessingModule> processor_;std::unique_ptr<BackgroundBlur> effect_;std::unique_ptr<VoiceDenoiser> denoiser_;
    std::unique_ptr<livekit::Room> room_;
    std::shared_ptr<livekit::AudioSource> source_;
    std::shared_ptr<livekit::LocalAudioTrack> audioTrack_;
    std::unique_ptr<ScreenAudioCapture> loopback_;
    std::shared_ptr<livekit::AudioSource> screenAudioSource_;
    std::shared_ptr<livekit::LocalAudioTrack> screenAudioTrack_;
    OutgoingVideo cameraVideo_,shareVideo_;int height_=1080;
    std::future<std::vector<livekit::RtcStats>> encoderStats_;
    QElapsedTimer outputProgress_;
    QAudioSource* input_ = nullptr; QAudioSink* output_ = nullptr;
    QPointer<QIODevice> inputIo_,outputIo_;
    QMediaDevices* devices_=nullptr;
    QTimer* outputRetry_=nullptr;
    QByteArray outputId_;
    int outputDelay_=250;
    QSet<QString> sdkSpeakers_;
    QHash<QString,QElapsedTimer> lastSpeech_;
    QTimer* tick_ = nullptr; QAudioFormat inFormat_, outFormat_; QByteArray pending_;
    struct IncomingTrack { QByteArray buffer; QString member; bool screen=false; AudioPcm::VoiceLevel level; };
    std::mutex mutex_; std::map<std::pair<std::string,std::string>, IncomingTrack> incoming_;QSet<QString> mutedStreams_;QHash<QString,int> gains_;QMap<QString,QJsonObject> peers_;bool rosterDirty_=true;QMap<QPair<QString,bool>,QImage> frames_;
    std::atomic<quint64> generation_{0}, tx_{0}, rx_{0}, videoRx_{0}; std::atomic<double> rms_{0};
    bool muted_ = false; int ticks_ = 0;
};

// Post at most one UI delivery, even while the main thread is busy resizing or opening a menu.
void MediaSession::postFrame(const QString& member,bool screen,const QImage& frame){
    {
        std::lock_guard lock(previewMutex_);
        const bool alreadyScheduled=!pendingPreviews_.isEmpty();
        pendingPreviews_.insert({member,screen},frame);
        if(alreadyScheduled)return;
    }
    QMetaObject::invokeMethod(this,[this]{
        decltype(pendingPreviews_) ready;
        {std::lock_guard lock(previewMutex_);ready.swap(pendingPreviews_);}
        for(auto it=ready.cbegin();it!=ready.cend();++it){
            emit participantFrame(it.key().first,it.key().second,it.value());
            if(!it.key().first.isEmpty() && !it.value().isNull())emit videoReceived(it.value());
        }
    },Qt::QueuedConnection);
}
MediaSession::MediaSession(QObject* parent) : VoiceEvents(parent), worker_(new MediaWorker(this)) {
    {
        std::lock_guard guard(sdkMutex);
        if(sdkUsers++==0){
            // One RTC session needs a small async I/O pool. The SDK otherwise
            // creates one Tokio worker per logical CPU, even after the call ends.
            if(!qEnvironmentVariableIsSet("TOKIO_WORKER_THREADS"))
                qputenv("TOKIO_WORKER_THREADS",QThread::idealThreadCount()==1?"1":"2");
            livekit::initialize(livekit::LogLevel::Warn);
        }
    }
    worker_->moveToThread(&thread_); connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater); thread_.start();
}
QObject* MediaSession::workerContext() const { return worker_; }
int MediaSession::activeEncoderQueues() const {
    int count=0;
    QMetaObject::invokeMethod(worker_,[this,&count]{count=worker_->activeEncoderQueues();},Qt::BlockingQueuedConnection);
    return count;
}
MediaSession::~MediaSession() {
    QMetaObject::invokeMethod(worker_, [this] { worker_->stop(false); }, Qt::BlockingQueuedConnection);
    thread_.quit(); thread_.wait();
    { std::lock_guard guard(sdkMutex); if (--sdkUsers == 0) livekit::shutdown(); }
}
void MediaSession::start(const MediaOptions& options) { QMetaObject::invokeMethod(worker_, [this, options] { worker_->start(options); }, Qt::QueuedConnection); }
void MediaSession::stop() { QMetaObject::invokeMethod(worker_, [this] { worker_->stop(); }, Qt::QueuedConnection); }
void MediaSession::muteStreamPlayback(QString peer,bool mute){QMetaObject::invokeMethod(worker_,[this,peer,mute]{worker_->muteStream(peer,mute);},Qt::QueuedConnection);}
void MediaSession::setVolume(QString id,int value){QMetaObject::invokeMethod(worker_,[this,id,value]{worker_->volume(id,value);},Qt::QueuedConnection);}
void MediaSession::setBlur(bool on,int radius){QMetaObject::invokeMethod(worker_,[this,on,radius]{worker_->blur(on,radius);},Qt::QueuedConnection);}
void MediaSession::setProcessing(bool n,bool e,bool a,QString engine){QMetaObject::invokeMethod(worker_,[this,n,e,a,engine]{try{worker_->processing(n,e,a,engine);}catch(const std::exception&){emit statusChanged(UiText::CouldNotEnableAudioProcessing());}},Qt::QueuedConnection);}
void MediaSession::selectAudioDevice(bool input,const QByteArray& id){
    QMetaObject::invokeMethod(worker_,[this,input,id]{worker_->selectAudioDevice(input,id);},Qt::QueuedConnection);
}
void MediaSession::moveRoom(QString url,QString token,bool annotations){
    QMetaObject::invokeMethod(worker_,[this,url,token,annotations]{worker_->moveRoom(url,token,annotations);},Qt::QueuedConnection);
}
void MediaSession::setMuted(bool muted) { QMetaObject::invokeMethod(worker_, [this, muted] { worker_->mute(muted); }, Qt::QueuedConnection); }
void MediaSession::setScreenAudio(bool enabled){QMetaObject::invokeMethod(worker_,[this,enabled]{worker_->setScreenAudio(enabled);},Qt::QueuedConnection);}
void MediaSession::sendInk(QString host,QJsonObject command){
    QMetaObject::invokeMethod(worker_,[this,host,command]{worker_->sendInk(host,command);},Qt::QueuedConnection);
}
void MediaSession::setVideoHeight(int height){QMetaObject::invokeMethod(worker_,[this,height]{worker_->setVideoHeight(height);},Qt::QueuedConnection);}
void MediaSession::changeVideo(bool share,int mode, const QByteArray& camera, const QString& screen, const QCapturableWindow& window) {
    QMetaObject::invokeMethod(worker_, [this,share,mode,camera,screen,window] { worker_->changeVideo(share,mode,camera,screen,window); }, Qt::QueuedConnection);
}
