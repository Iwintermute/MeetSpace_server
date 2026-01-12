#include "cMediaAudioDevice.h"
#include <algorithm>
#include <iostream>

namespace Sys {
    namespace Media {

        int cMediaAudioDevice::paCaptureCallback(const void* input, void*, unsigned long frameCount,
            const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData)
        {
            auto* device = static_cast<cMediaAudioDevice*>(userData);
            if (!device || !input) return paContinue;

            const int16_t* in = static_cast<const int16_t*>(input);
            const size_t samples = (size_t)frameCount * (size_t)device->m_ch;

            std::vector<int16_t> buf;
            buf.assign(in, in + samples);

            if (device->m_fnOnAudioFrame) {
                device->m_fnOnAudioFrame(buf, (int)frameCount, device->m_ch);
            }

            // самопрослушивание если нужно
            // device->fnPlaySamples(buf);

            return paContinue;
        }

        int cMediaAudioDevice::paPlaybackCallback(const void*, void* outputBuffer, unsigned long frameCount,
            const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData)
        {
            auto* device = static_cast<cMediaAudioDevice*>(userData);
            int16_t* out = static_cast<int16_t*>(outputBuffer);

            const size_t needSamples = (size_t)frameCount * (size_t)device->m_ch;

            std::lock_guard<std::mutex> lg(device->m_playbackMutex);
            size_t n = std::min(needSamples, device->m_playbackBuffer.size());

            std::copy(device->m_playbackBuffer.begin(), device->m_playbackBuffer.begin() + n, out);
            if (n < needSamples) std::fill(out + n, out + needSamples, 0);

            device->m_playbackBuffer.erase(device->m_playbackBuffer.begin(), device->m_playbackBuffer.begin() + n);
            return paContinue;
        }

        cMediaAudioDevice::cMediaAudioDevice(int sampleRate, int channels, int framesPerBuffer)
            : m_sr(sampleRate), m_ch(channels), m_frames(framesPerBuffer)
        {
            Pa_Initialize();
        }

        cMediaAudioDevice::~cMediaAudioDevice()
        {
            fnStopCapture();
            fnStopPlayback();
            Pa_Terminate();
        }

        void cMediaAudioDevice::fnStartCapture()
        {
            if (m_bCapturing) return;

            if (Pa_OpenDefaultStream(&m_captureStream, m_ch, 0, paInt16, m_sr, m_frames, paCaptureCallback, this) != paNoError)
                return;

            if (Pa_StartStream(m_captureStream) != paNoError)
                return;

            m_bCapturing = true;
        }

        void cMediaAudioDevice::fnStopCapture()
        {
            if (!m_bCapturing || !m_captureStream) return;
            Pa_StopStream(m_captureStream);
            Pa_CloseStream(m_captureStream);
            m_captureStream = nullptr;
            m_bCapturing = false;
        }

        void cMediaAudioDevice::fnStartPlayback()
        {
            if (m_bPlaying) return;

            if (Pa_OpenDefaultStream(&m_playbackStream, 0, m_ch, paInt16, m_sr, m_frames, paPlaybackCallback, this) != paNoError)
                return;

            if (Pa_StartStream(m_playbackStream) != paNoError)
                return;

            m_bPlaying = true;
        }

        void cMediaAudioDevice::fnStopPlayback()
        {
            if (!m_bPlaying || !m_playbackStream) return;
            Pa_StopStream(m_playbackStream);
            Pa_CloseStream(m_playbackStream);
            m_playbackStream = nullptr;
            m_bPlaying = false;
        }

        void cMediaAudioDevice::fnPlaySamples(const std::vector<int16_t>& interleavedSamples)
        {
            std::lock_guard<std::mutex> lg(m_playbackMutex);
            m_playbackBuffer.insert(m_playbackBuffer.end(), interleavedSamples.begin(), interleavedSamples.end());
        }

    } // Media
} // Sys
