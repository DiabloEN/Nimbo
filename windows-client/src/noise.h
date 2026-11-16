#pragma once
#include <memory>
#include <cstdint>
#include <vector>
class VoiceDenoiser {
public:
    explicit VoiceDenoiser(bool gtcrn);~VoiceDenoiser();
    void filter(std::vector<std::int16_t>& pcm);
private:
    struct Engine;std::unique_ptr<Engine> state;
};
