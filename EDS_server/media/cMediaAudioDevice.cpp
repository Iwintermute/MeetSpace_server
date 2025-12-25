
#include "cMediaAudioDevice.h"
#include <iostream>
#include <algorithm>

namespace Sys {
    namespace Media {

        int cMediaAudioDevice::paCaptureCallback(const void* input, void*, unsigned long frameCount,
            const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData)
        {
            auto* device = static_cast<cMediaAudioDevice*>(userData);
            if (device->m_fnOnAudioFrame && input) {
                const int16_t* in = static_cast<const int16_t*>(input);
                std::vector<int16_t> samples(in, in + frameCount);

                // Callback для обработки/отправки
                device->m_fnOnAudioFrame(samples);

                // Локальный вывод (самослушание)
                device->fnPlay(samples);

                std::cout << "[Audio] Captured " << samples.size() * sizeof(int16_t)
                    << " bytes" << std::endl;
            }
            return paContinue;
        }

        int cMediaAudioDevice::paPlaybackCallback(const void*, void* outputBuffer, unsigned long frameCount,
            const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData)
        {
            auto* device = static_cast<cMediaAudioDevice*>(userData);
            int16_t* out = static_cast<int16_t*>(outputBuffer);

            std::lock_guard<std::mutex> lg(device->m_playbackMutex);
            size_t toCopy = std::min(static_cast<size_t>(frameCount), device->m_playbackBuffer.size());
            std::copy(device->m_playbackBuffer.begin(), device->m_playbackBuffer.begin() + toCopy, out);

            // Остальное заполняем нулями
            std::fill(out + toCopy, out + frameCount, 0);

            if (toCopy > 0)
                device->m_playbackBuffer.erase(device->m_playbackBuffer.begin(), device->m_playbackBuffer.begin() + toCopy);

            return paContinue;
        }

        cMediaAudioDevice::cMediaAudioDevice()
            : m_bCapturing(false), m_bPlaying(false), m_captureStream(nullptr), m_playbackStream(nullptr)
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

            // Стартуем поток воспроизведения для самослушания (тест)
            if (!m_bPlaying) fnStartPlayback();

            Pa_OpenDefaultStream(&m_captureStream, 1, 0, paInt16, 48000, 480, paCaptureCallback, this);
            Pa_StartStream(m_captureStream);
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
            Pa_OpenDefaultStream(&m_playbackStream, 0, 1, paInt16, 48000, 480, paPlaybackCallback, this);
            Pa_StartStream(m_playbackStream);
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

        void cMediaAudioDevice::fnPlay(const std::vector<int16_t>& samples)
        {
            std::lock_guard<std::mutex> lg(m_playbackMutex);
            m_playbackBuffer.insert(m_playbackBuffer.end(), samples.begin(), samples.end());
        }

    } // namespace Media
} // namespace Sys
