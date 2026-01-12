#pragma once
#include <functional>
#include <vector>
#include <cstdint>
#include <portaudio.h>
#include <mutex>

namespace Sys {
    namespace Media {

        class cMediaAudioDevice {
        public:
            using tOnAudioFrame = std::function<void(const std::vector<int16_t>& interleavedSamples, int frames, int channels)>;

            cMediaAudioDevice(int sampleRate = 48000, int channels = 1, int framesPerBuffer = 480);
            ~cMediaAudioDevice();

            void fnStartCapture();
            void fnStopCapture();

            void fnSetCallback(tOnAudioFrame cb) { m_fnOnAudioFrame = std::move(cb); }

            void fnStartPlayback();
            void fnStopPlayback();

            void fnPlaySamples(const std::vector<int16_t>& interleavedSamples);

            int sampleRate() const { return m_sr; }
            int channels() const { return m_ch; }
            int framesPerBuffer() const { return m_frames; }

        private:
            static int paCaptureCallback(const void* input, void*, unsigned long frameCount,
                const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData);

            static int paPlaybackCallback(const void*, void* outputBuffer, unsigned long frameCount,
                const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData);

        private:
            tOnAudioFrame m_fnOnAudioFrame;

            bool m_bCapturing{ false };
            bool m_bPlaying{ false };

            int m_sr{ 48000 };
            int m_ch{ 1 };
            int m_frames{ 480 };

            PaStream* m_captureStream{ nullptr };
            PaStream* m_playbackStream{ nullptr };

            std::mutex m_playbackMutex;
            std::vector<int16_t> m_playbackBuffer; // interleaved samples
        };

    } // Media
} // Sys
