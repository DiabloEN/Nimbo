#include "noise.h"
#include "gtcrn.h"
#include "rnnoise.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <cmath>
#include <mutex>
#include <stdexcept>

struct w2c_env{w2c_rnnoise* instance;};
struct w2c_a{w2c_gtcrn* instance;};
namespace {
uint32_t grow(wasm_rt_memory_t* m,uint32_t size){if(size<=m->size)return 1;if(size>64*1024*1024)return 0;return wasm_rt_grow_memory(m,(size-m->size+65535)/65536)!=UINT64_MAX;}
}
extern "C" {
void w2c_env_0x5F_assert_fail(w2c_env*,u32,u32,u32,u32){throw std::runtime_error("RNNoise assertion");}
u32 w2c_env_emscripten_memcpy_big(w2c_env* env,u32 dest,u32 src,u32 len){auto* m=w2c_rnnoise_memory(env->instance);if(uint64_t(dest)+len>m->size||uint64_t(src)+len>m->size)throw std::runtime_error("RNNoise bounds");std::memmove(m->data+dest,m->data+src,len);return dest;}
u32 w2c_env_emscripten_resize_heap(w2c_env* env,u32 size){return grow(w2c_rnnoise_memory(env->instance),size);}
void w2c_a_a(w2c_a*,u32,u32,u32,u32){throw std::runtime_error("GTCRN assertion");}
u32 w2c_a_b(w2c_a* a,u32 size){return grow(w2c_gtcrn_c(a->instance),size);}
}
struct VoiceDenoiser::Engine {
    w2c_gtcrn g{};w2c_rnnoise r{};w2c_env re{&r};w2c_a ge{&g};bool gt;
    u32 handle=0,in=0,out=0;std::deque<float> feed,ready;size_t n;
    Engine(bool useGt):gt(useGt),n(gt?768:480){
        static std::once_flag init;std::call_once(init,[]{wasm_rt_init();});
        if(gt){wasm2c_gtcrn_instantiate(&g,&ge);w2c_gtcrn_d(&g);handle=w2c_gtcrn_e(&g);in=w2c_gtcrn_k(&g,u32(n*4));out=w2c_gtcrn_k(&g,u32(n*4));ready.resize(n-1);}
        else{wasm2c_rnnoise_instantiate(&r,&re);w2c_rnnoise_0x5F_wasm_call_ctors(&r);handle=w2c_rnnoise_rnnoise_create(&r,0);in=w2c_rnnoise_malloc(&r,u32(n*4));out=w2c_rnnoise_malloc(&r,u32(n*4));}
        if(!handle||!in||!out)throw std::runtime_error("Denoiser allocation");
    }
    ~Engine(){if(gt)wasm2c_gtcrn_free(&g);else wasm2c_rnnoise_free(&r);}
    void process(std::vector<int16_t>& samples){
        for(auto x:samples)feed.push_back(gt?x/32768.f:float(x));
        while(feed.size()>=n){
            auto mem=gt?w2c_gtcrn_c(&g):w2c_rnnoise_memory(&r);auto p=reinterpret_cast<float*>(mem->data+in);for(size_t i=0;i<n;++i){p[i]=feed.front();feed.pop_front();}
            if(gt)w2c_gtcrn_j(&g,handle,in,out);else w2c_rnnoise_rnnoise_process_frame(&r,handle,out,in);
            auto dst=reinterpret_cast<const float*>(mem->data+out);for(size_t i=0;i<n;++i)ready.push_back(gt?dst[i]*32768:dst[i]);
        }
        for(auto& x:samples){if(ready.empty()){x=0;continue;}x=int16_t(std::clamp(ready.front(),-32768.f,32767.f));ready.pop_front();}
    }
};
VoiceDenoiser::VoiceDenoiser(bool gtcrn):state(std::make_unique<Engine>(gtcrn)){}
VoiceDenoiser::~VoiceDenoiser()=default;
void VoiceDenoiser::filter(std::vector<int16_t>& samples){state->process(samples);}
