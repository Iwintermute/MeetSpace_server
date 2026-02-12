#pragma once
#include <portaudio/portaudio.h>
#include <functional>
#include <vector>
#include <mutex>

class cAudioIO {
public:
    using tOnFrame = std::function<void(const int16_t* pcm, int frameSamples)>;

    bool init(int sampleRate = 48000, int channels = 1, int frameSamples = 480);
    void shutdown();

    bool startCapture(tOnFrame cb);
    void stopCapture();

    bool startPlayback();
    void stopPlayback();

    void pushToPlayback(const int16_t* pcm, int samples);

private:
    static int inCb(const void* input, void*, unsigned long frameCount,
        const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData);
    static int outCb(const void*, void* output, unsigned long frameCount,
        const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData);

private:
    int m_sr{ 48000 };
    int m_ch{ 1 };
    int m_frame{ 480 };

    PaStream* m_in{ nullptr };
    PaStream* m_out{ nullptr };

    tOnFrame m_onFrame;

    std::mutex m_pbMtx;
    std::vector<int16_t> m_pb;
};
