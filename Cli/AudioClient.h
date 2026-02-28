#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <opus/opus.h>
#include <portaudio.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <mutex>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;

class AudioClient {
private:
    PaStream* paStream;
    OpusEncoder* encoder;
    OpusDecoder* decoder;
    websocket::stream<beast::tcp_stream>& ws;
    
    std::mutex audio_mutex;
    std::vector<unsigned char> encode_buffer;
    std::vector<float> audio_buffer;
    
    const int SAMPLE_RATE = 48000;
    const int CHANNELS = 1;
    const int FRAME_SIZE = 960;  // 20ms at 48kHz
    const int OPUS_APPLICATION = OPUS_APPLICATION_VOIP;
    
public:
    AudioClient(websocket::stream<beast::tcp_stream>& websocket) 
        : ws(websocket), paStream(nullptr), encoder(nullptr), decoder(nullptr) {
        
        encode_buffer.resize(1275);  // Max Opus packet size
        audio_buffer.resize(FRAME_SIZE * CHANNELS);
    }
    
    bool init();
    void startCapture() {
        Pa_OpenDefaultStream(&paStream,
            CHANNELS,          // input channels
            0,                 // output channels
            paFloat32,         // sample format
            SAMPLE_RATE,
            FRAME_SIZE,        // frames per buffer
            audioCallback,
            this);
        
        Pa_StartStream(paStream);
    }
    
    static int audioCallback(const void* input, void* output,
                             unsigned long frameCount,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void* userData);
    
    void stop();
};