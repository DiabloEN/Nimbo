#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

// Same seven notes and envelopes as the web client's "Проверить звук".
inline std::vector<std::int16_t> soundCheckPhrase(int rate){
    constexpr std::array tones{392.,493.88,587.33,659.25,587.33,493.88,392.};
    std::vector<std::int16_t> pcm(3*rate);
    for(size_t i=0;i<tones.size();++i){
        const int start=int((.06+i*.34)*rate);
        const double duration=i+1==tones.size()?.80:.42;
        for(int n=0;n<int(duration*rate);++n){
            const double t=double(n)/rate,phase=2*std::numbers::pi*tones[i]*t;
            double sample=0;
            for(int h:{1,2}){
                const double gain=h==1?.23:.045;
                const double envelope=t<.025?gain*t/.025:gain*std::pow(.001/gain,(t-.025)/(duration-.025));
                sample+=envelope*std::sin(h*phase);
            }
            pcm[start+n]+=std::int16_t(sample*32767);
        }
    }
    return pcm;
}
