#pragma once
#include <vector>
#include <cstdint>
#include "../include/opus/opus.h"


namespace Sys {
    namespace Media {

        class cMediaOpusDecoder {
        public:
            cMediaOpusDecoder(int sampleRate = 48000, int channels = 1);
            ~cMediaOpusDecoder();

            std::vector<int16_t> fnDecode(const std::vector<uint8_t>& data);

        private:
            OpusDecoder* m_decoder;
        };

        class cMediaOpusEncoder {
        public:
            cMediaOpusEncoder(int sampleRate = 48000, int channels = 1);
            ~cMediaOpusEncoder();

            std::vector<uint8_t> fnEncode(const std::vector<int16_t>& pcm);

        private:
            OpusEncoder* m_encoder;
        };

    } // namespace Media
} // namespace Sys
