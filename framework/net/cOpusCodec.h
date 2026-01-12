#pragma once
#include <opus.h>
#include <vector>
#include <cstdint>

class cOpusCodec {
public:
    bool init(int sampleRate = 48000, int channels = 1);
    void shutdown();

    std::vector<uint8_t> encode(const int16_t* pcm, int frameSamples);
    std::vector<int16_t> decode(const uint8_t* data, int len, int maxFrameSamples = 960);

private:
    OpusEncoder* m_enc{ nullptr };
    OpusDecoder* m_dec{ nullptr };
    int m_sr{ 48000 };
    int m_ch{ 1 };
};
