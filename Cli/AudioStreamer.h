#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <opus/opus.h>
#include <portaudio.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <chrono>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;

// Перечисление состояний стримера
enum class StreamerState {
    IDLE,
    INITIALIZING,
    CAPTURING,
    PLAYING,
    STREAMING,
    ERR,
    STOPPED
};

// Перечисление типов событий
enum class AudioEventType {
    DEVICE_LIST_CHANGED,
    STREAM_STARTED,
    STREAM_STOPPED,
    BUFFER_OVERFLOW,
    BUFFER_UNDERRUN,
    ENCODING_ERROR,
    DECODING_ERROR,
    DEVICE_ERROR,
    VOLUME_CHANGED
};

// Структура для аудио событий
struct AudioEvent {
    AudioEventType type;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    int data; // Дополнительные данные (например, уровень громкости)
};

// Структура для конфигурации аудио
struct AudioConfig {
    int sampleRate = 48000;
    int channels = 1;
    int frameSize = 960;  // 20ms at 48kHz
    int bitrate = 64000;   // 64 kbps
    int complexity = 10;   // Opus complexity (0-10)
    int application = OPUS_APPLICATION_VOIP;
    std::string inputDevice = "default";
    std::string outputDevice = "default";
    float volume = 1.0f;
    bool echoCancellation = false;
    bool noiseSuppression = false;
    bool autoGainControl = false;
};

// Структура для аудио пакета
struct AudioPacket {
    std::vector<uint8_t> data;
    uint32_t sequence;
    uint64_t timestamp;
    bool isKeyframe;
};

// Класс для стриминга аудио
class AudioStreamer {
private:
    // PortAudio
    PaStream* captureStream;
    PaStream* playbackStream;

    // Opus
    OpusEncoder* encoder;
    OpusDecoder* decoder;

    // WebSocket ссылка
    websocket::stream<beast::tcp_stream>& ws;

    // Конфигурация
    AudioConfig config;
    std::atomic<StreamerState> state;

    // Буферы
    std::vector<float> captureBuffer;
    std::vector<float> playbackBuffer;
    std::vector<unsigned char> encodeBuffer;
    std::vector<float> resampleBuffer;

    // Очереди для потоковой обработки
    std::queue<std::vector<float>> captureQueue;
    std::queue<AudioPacket> receiveQueue;
    std::mutex captureMutex;
    std::mutex receiveMutex;

    // Потоки
    std::unique_ptr<std::thread> encodeThread;
    std::unique_ptr<std::thread> decodeThread;
    std::unique_ptr<std::thread> jitterBufferThread;
    std::atomic<bool> threadsRunning;

    // Статистика
    std::atomic<uint32_t> packetsSent;
    std::atomic<uint32_t> packetsReceived;
    std::atomic<uint32_t> packetsLost;
    std::atomic<uint64_t> bytesSent;
    std::atomic<uint64_t> bytesReceived;
    std::atomic<float> currentJitter;
    std::atomic<uint32_t> sequenceNumber;

    // Jitter buffer
    std::map<uint32_t, AudioPacket> jitterBuffer;
    std::mutex jitterMutex;
    uint32_t lastPlayedSequence;
    static constexpr size_t MAX_JITTER_SIZE = 100;
    static constexpr size_t JITTER_TARGET = 3; // Целевой размер jitter buffer в пакетах

    // Callbacks
    std::function<void(const AudioEvent&)> eventCallback;
    std::function<void(const std::vector<float>&)> audioProcessCallback;

    // Статические callback функции для PortAudio
    static int captureCallback(const void* input, void* output,
        unsigned long frameCount,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData);

    static int playbackCallback(const void* input, void* output,
        unsigned long frameCount,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData);

    // Внутренние методы
    void encodeLoop();
    void decodeLoop();
    void jitterBufferLoop();
    bool initializePortAudio();
    bool initializeOpus();
    void cleanupPortAudio();
    void cleanupOpus();
    void sendAudioPacket(const std::vector<uint8_t>& encodedData);
    void processJitterBuffer();
    void triggerEvent(AudioEventType type, const std::string& message, int data = 0);

public:
    AudioStreamer(websocket::stream<beast::tcp_stream>& websocket);
    ~AudioStreamer();

    // Инициализация и управление
    bool initialize(const AudioConfig& cfg = AudioConfig());
    void shutdown();

    // Управление стримингом
    bool startCapture();
    bool startPlayback();
    void stopCapture();
    void stopPlayback();
    void stopAll();

    // Получение данных от сервера
    void processIncomingAudio(const nlohmann::json& message);

    // Управление конфигурацией
    void setConfig(const AudioConfig& cfg);
    AudioConfig getConfig() const;

    // Управление громкостью
    void setVolume(float vol);
    float getVolume() const;

    // Callback установка
    void setEventCallback(std::function<void(const AudioEvent&)> callback);
    void setAudioProcessCallback(std::function<void(const std::vector<float>&)> callback);

    // Статус и статистика
    StreamerState getState() const;
    std::string getStateString() const;
    void getStats(uint32_t& sent, uint32_t& received, uint32_t& lost,
        uint64_t& bytesSent, uint64_t& bytesReceived, float& jitter) const;

    // Список устройств
    static std::vector<std::pair<std::string, std::string>> getInputDevices();
    static std::vector<std::pair<std::string, std::string>> getOutputDevices();
};