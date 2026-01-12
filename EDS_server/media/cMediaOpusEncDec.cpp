#include "cMediaOpusEncDec.h"

namespace Sys {
    namespace Media {

        cMediaOpusEncoder::cMediaOpusEncoder(int sampleRate, int channels)
            : m_sr(sampleRate), m_ch(channels)
        {
            int error;
            m_encoder = opus_encoder_create(m_sr, m_ch, OPUS_APPLICATION_VOIP, &error);
            if (error != OPUS_OK) m_encoder = nullptr;

            if (m_encoder) {
                opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(24000));
                opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(5));
                opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));
                opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(10));
                opus_encoder_ctl(m_encoder, OPUS_SET_DTX(1));
            }
        }

        cMediaOpusEncoder::~cMediaOpusEncoder()
        {
            if (m_encoder) opus_encoder_destroy(m_encoder);
        }

        std::vector<uint8_t> cMediaOpusEncoder::fnEncode(const std::vector<int16_t>& interleavedPcm, int frames)
        {
            if (!m_encoder) return {};
            if (frames <= 0) return {};
            if ((int)interleavedPcm.size() < frames * m_ch) return {};

            std::vector<uint8_t> out(4000);

            // frames = frames per channel
            int len = opus_encode(m_encoder, interleavedPcm.data(), frames, out.data(), (opus_int32)out.size());
            if (len < 0) return {};

            out.resize((size_t)len);
            return out;
        }

        cMediaOpusDecoder::cMediaOpusDecoder(int sampleRate, int channels)
            : m_sr(sampleRate), m_ch(channels)
        {
            int error;
            m_decoder = opus_decoder_create(m_sr, m_ch, &error);
            if (error != OPUS_OK) m_decoder = nullptr;
        }

        cMediaOpusDecoder::~cMediaOpusDecoder()
        {
            if (m_decoder) opus_decoder_destroy(m_decoder);
        }

        std::vector<int16_t> cMediaOpusDecoder::fnDecode(const std::vector<uint8_t>& data, int maxFrames)
        {
            if (!m_decoder) return {};
            if (data.empty()) return {};

            std::vector<int16_t> pcm((size_t)maxFrames * (size_t)m_ch);

            // maxFrames = frames per channel
            int frames = opus_decode(m_decoder, data.data(), (opus_int32)data.size(),
                pcm.data(), maxFrames, 0);
            if (frames < 0) return {};

            pcm.resize((size_t)frames * (size_t)m_ch);
            return pcm;
        }

    } // Media
} // Sys
