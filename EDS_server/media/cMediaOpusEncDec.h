#pragma once
#include <vector>
#include <cstdint>
#include <opus/opus.h>

namespace Sys {
    namespace Media {

        class cMediaOpusDecoder {
        public:
            cMediaOpusDecoder(int sampleRate = 48000, int channels = 1);
            ~cMediaOpusDecoder();

            // maxFrames = frames per channel
            std::vector<int16_t> fnDecode(const std::vector<uint8_t>& data, int maxFrames = 960);

        private:
            OpusDecoder* m_decoder{ nullptr };
            int m_sr{ 48000 };
            int m_ch{ 1 };
        };

        class cMediaOpusEncoder {
        public:
            cMediaOpusEncoder(int sampleRate = 48000, int channels = 1);
            ~cMediaOpusEncoder();
            std::vector<uint8_t> fnEncode(const std::vector<int16_t>& interleavedPcm) {
                if (m_ch <= 0) return {};
                int frames = (int)interleavedPcm.size() / m_ch;
                return fnEncode(interleavedPcm, frames);
            }
            // frames = frames per channel
            std::vector<uint8_t> fnEncode(const std::vector<int16_t>& interleavedPcm, int frames);

        private:
            OpusEncoder* m_encoder{ nullptr };
            int m_sr{ 48000 };
            int m_ch{ 1 };
        };

    } // Media
} // Sys
