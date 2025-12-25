#include "cMediaOpusEncDec.h"
#include <iostream>

namespace Sys {
    namespace Media {

        cMediaOpusEncoder::cMediaOpusEncoder(int sampleRate, int channels) {
            int error;
            m_encoder = opus_encoder_create(sampleRate, channels, OPUS_APPLICATION_VOIP, &error);
            if (error != OPUS_OK) m_encoder = nullptr;
        }

        cMediaOpusEncoder::~cMediaOpusEncoder() {
            if (m_encoder) opus_encoder_destroy(m_encoder);
        }

        std::vector<uint8_t> cMediaOpusEncoder::fnEncode(const std::vector<int16_t>& pcm) {
            std::vector<uint8_t> out(4000);
            if (!m_encoder) return {};
            int len = opus_encode(m_encoder, pcm.data(), static_cast<int>(pcm.size()), out.data(), static_cast<opus_int32>(out.size()));
            if (len < 0) return {};
            out.resize(len);
            return out;
        }

        cMediaOpusDecoder::cMediaOpusDecoder(int sampleRate, int channels) {
            int error;
            m_decoder = opus_decoder_create(sampleRate, channels, &error);
            if (error != OPUS_OK) m_decoder = nullptr;
        }

        cMediaOpusDecoder::~cMediaOpusDecoder() {
            if (m_decoder) opus_decoder_destroy(m_decoder);
        }

        std::vector<int16_t> cMediaOpusDecoder::fnDecode(const std::vector<uint8_t>& data) {
            std::vector<int16_t> pcm(480 * 2);
            if (!m_decoder) return {};
            int len = opus_decode(m_decoder, data.data(), static_cast<opus_int32>(data.size()), pcm.data(), static_cast<int>(pcm.size()), 0);
            if (len < 0) return {};
            pcm.resize(len);
            return pcm;
        }

    } // namespace Media
} // namespace Sys
