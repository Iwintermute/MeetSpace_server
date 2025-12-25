#pragma once
#include <functional>
#include <vector>
#include <cstdint>
#include "../include/portaudio/portaudio.h"
#include <mutex>

namespace Sys {
    namespace Media {

        class cMediaAudioDevice {
        public:
            using tOnAudioFrame = std::function<void(const std::vector<int16_t>& samples)>;

            cMediaAudioDevice();
            ~cMediaAudioDevice();

            void fnStartCapture();
            void fnStopCapture();
            void fnSetCallback(tOnAudioFrame cb) { m_fnOnAudioFrame = std::move(cb); }

            // Äëÿ ëîêàëüíîãî ïðîñëóøèâàíèÿ
            void fnStartPlayback();
            void fnStopPlayback();
            void fnPlay(const std::vector<int16_t>& samples);

        private:
            static int paCaptureCallback(const void* input, void*, unsigned long frameCount,
                const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData);
            static int paPlaybackCallback(const void*, void* outputBuffer, unsigned long frameCount,
                const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData);

            tOnAudioFrame m_fnOnAudioFrame;
            bool m_bCapturing;
            bool m_bPlaying;
            PaStream* m_captureStream;
            PaStream* m_playbackStream;

            std::mutex m_playbackMutex;
            std::vector<int16_t> m_playbackBuffer;
        };

    } // namespace Media
} // namespace Sys
