#include "cAudioIO.h"
#include <algorithm>
#include <cstring>

bool cAudioIO::init(int sampleRate, int channels, int frameSamples)
{
    m_sr = sampleRate;
    m_ch = channels;
    m_frame = frameSamples;

    if (Pa_Initialize() != paNoError) return false;
    return true;
}

void cAudioIO::shutdown()
{
    stopCapture();
    stopPlayback();
    Pa_Terminate();
}

bool cAudioIO::startCapture(tOnFrame cb)
{
    m_onFrame = std::move(cb);

    if (Pa_OpenDefaultStream(&m_in, m_ch, 0, paInt16, m_sr, m_frame, &cAudioIO::inCb, this) != paNoError)
        return false;
    if (Pa_StartStream(m_in) != paNoError) return false;
    return true;
}

void cAudioIO::stopCapture()
{
    if (!m_in) return;
    Pa_StopStream(m_in);
    Pa_CloseStream(m_in);
    m_in = nullptr;
}

bool cAudioIO::startPlayback()
{
    if (Pa_OpenDefaultStream(&m_out, 0, m_ch, paInt16, m_sr, m_frame, &cAudioIO::outCb, this) != paNoError)
        return false;
    if (Pa_StartStream(m_out) != paNoError) return false;
    return true;
}

void cAudioIO::stopPlayback()
{
    if (!m_out) return;
    Pa_StopStream(m_out);
    Pa_CloseStream(m_out);
    m_out = nullptr;
}

void cAudioIO::pushToPlayback(const int16_t* pcm, int samples)
{
    std::lock_guard<std::mutex> lg(m_pbMtx);
    m_pb.insert(m_pb.end(), pcm, pcm + samples);
}

int cAudioIO::inCb(const void* input, void*, unsigned long frameCount,
    const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData)
{
    auto* self = static_cast<cAudioIO*>(userData);
    if (!input || !self->m_onFrame) return paContinue;

    const int16_t* pcm = static_cast<const int16_t*>(input);
    self->m_onFrame(pcm, (int)frameCount);
    return paContinue;
}

int cAudioIO::outCb(const void*, void* output, unsigned long frameCount,
    const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData)
{
    auto* self = static_cast<cAudioIO*>(userData);
    int16_t* out = static_cast<int16_t*>(output);

    std::lock_guard<std::mutex> lg(self->m_pbMtx);

    const size_t need = frameCount * self->m_ch;
    size_t n = std::min(need, self->m_pb.size());
    std::copy(self->m_pb.begin(), self->m_pb.begin() + n, out);
    if (n < need) std::fill(out + n, out + need, 0);

    self->m_pb.erase(self->m_pb.begin(), self->m_pb.begin() + n);
    return paContinue;
}
