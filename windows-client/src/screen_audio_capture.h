#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <audioclientactivationparams.h>
#include <wrl/implements.h>
#include <wrl/client.h>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <cstring>

// Windows calls this agile completion object from an audio-service thread.
class LoopbackActivation final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
public:
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    ~LoopbackActivation(){if(ready)CloseHandle(ready);}
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override {
        SetEvent(ready);
        return S_OK;
    }
};

// Bind capture to a process tree. Never fall back to a whole endpoint mix.
class ScreenAudioCapture final {
public:
    ScreenAudioCapture(DWORD process, PROCESS_LOOPBACK_MODE mode){
        const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(apartment)&&apartment!=RPC_E_CHANGED_MODE)check(apartment);
        apartmentOwner=SUCCEEDED(apartment);
        try{
            AUDIOCLIENT_ACTIVATION_PARAMS params{};
            params.ActivationType=AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
            if (!process) throw std::runtime_error("Missing audio target");
            params.ProcessLoopbackParams.TargetProcessId=process;
            params.ProcessLoopbackParams.ProcessLoopbackMode=mode;
            PROPVARIANT property{};
            property.vt=VT_BLOB;
            property.blob.cbSize=sizeof(params);
            property.blob.pBlobData=reinterpret_cast<BYTE*>(&params);
            auto callback=Microsoft::WRL::Make<LoopbackActivation>();
            if(!callback || !callback->ready)check(E_OUTOFMEMORY);
            Microsoft::WRL::ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
            check(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,__uuidof(IAudioClient),
                                              &property,callback.Get(),&operation));
            if(WaitForSingleObject(callback->ready,5000)!=WAIT_OBJECT_0)check(E_FAIL);
            HRESULT activated=E_FAIL;
            Microsoft::WRL::ComPtr<IUnknown> unknown;
            check(operation->GetActivateResult(&activated,&unknown));
            check(activated);check(unknown.As(&client));
            WAVEFORMATEX pcm{};pcm.wFormatTag=WAVE_FORMAT_PCM;pcm.nChannels=2;pcm.nSamplesPerSec=48000;
            pcm.wBitsPerSample=16;pcm.nBlockAlign=4;pcm.nAvgBytesPerSec=192000;
            check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK|
                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                1000000,0,&pcm,nullptr));
            check(client->GetService(IID_PPV_ARGS(&capture)));check(client->Start());
        }catch(...){close();throw;}
    }
    // LiveKit direct capture accepts exactly 10 ms of interleaved stereo per call.
    template<class Submit>
    void submitBlocks(Submit submit){
        auto packet=read();
        remainder.insert(remainder.end(),packet.begin(),packet.end());
        constexpr size_t block=480*2;
        size_t sent=0;
        while(remainder.size()-sent>=block){
            submit(std::vector<int16_t>(remainder.begin()+sent,remainder.begin()+sent+block));
            sent+=block;
        }
        remainder.erase(remainder.begin(),remainder.begin()+sent);
    }
    ScreenAudioCapture(const ScreenAudioCapture&)=delete;
    ~ScreenAudioCapture(){close();}
    std::vector<int16_t> read(){
        std::vector<int16_t> pcm;
        for(unsigned packets=0;packets<12;++packets){
            UINT32 available=0;check(capture->GetNextPacketSize(&available));if(!available)break;
            BYTE* bytes=nullptr;UINT32 frames=0;DWORD flags=0;
            check(capture->GetBuffer(&bytes,&frames,&flags,nullptr,nullptr));
            const auto first=pcm.size();
            try{
                pcm.resize(first+frames*2);
                if(bytes && !(flags&AUDCLNT_BUFFERFLAGS_SILENT))std::memcpy(pcm.data()+first,bytes,frames*4);
            }catch(...){capture->ReleaseBuffer(frames);throw;}
            check(capture->ReleaseBuffer(frames));
        }
        return pcm;
    }
private:
    static void check(HRESULT result){if(FAILED(result))throw std::runtime_error("Windows playback capture unavailable");}
    void close(){
        if(client)client->Stop();
        capture.Reset();client.Reset();if(apartmentOwner){CoUninitialize();apartmentOwner=false;}
    }
    Microsoft::WRL::ComPtr<IAudioClient> client;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> capture;
    std::vector<int16_t> remainder;
    bool apartmentOwner=false;
};
