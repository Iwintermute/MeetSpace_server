#include "cOpusCodec.h"

bool cOpusCodec::init(int sampleRate, int channels)
{
    m_sr = sampleRate;
    m_ch = channels;

    int err = 0;
    m_enc = opus_encoder_create(m_sr, m_ch, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) return false;

    m_dec = opus_decoder_create(m_sr, m_ch, &err);
    if (err != OPUS_OK) return false;

    return true;
}

void cOpusCodec::shutdown()
{
    if (m_enc) opus_encoder_destroy(m_enc), m_enc = nullptr;
    if (m_dec) opus_decoder_destroy(m_dec), m_dec = nullptr;
}

std::vector<uint8_t> cOpusCodec::encode(const int16_t* pcm, int frameSamples)
{
    std::vector<uint8_t> out(4000);
    if (!m_enc) return {};

    int n = opus_encode(m_enc, pcm, frameSamples, out.data(), (opus_int32)out.size());
    if (n <= 0) return {};
    out.resize(n);
    return out;
}

std::vector<int16_t> cOpusCodec::decode(const uint8_t* data, int len, int maxFrameSamples)
{
    std::vector<int16_t> pcm(maxFrameSamples * m_ch);
    if (!m_dec) return {};

    int n = opus_decode(m_dec, data, (opus_int32)len, pcm.data(), maxFrameSamples, 0);
    if (n <= 0) return {};
    pcm.resize(n * m_ch);
    return pcm;
}
