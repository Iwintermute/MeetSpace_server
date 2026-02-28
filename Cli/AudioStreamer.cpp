#include "AudioStreamer.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// Конструктор
AudioStreamer::AudioStreamer(websocket::stream<beast::tcp_stream>& websocket)
    : ws(websocket)
    , captureStream(nullptr)
    , playbackStream(nullptr)
    , encoder(nullptr)
    , decoder(nullptr)
    , state(StreamerState::IDLE)
    , threadsRunning(false)
    , packetsSent(0)
    , packetsReceived(0)
    , packetsLost(0)
    , bytesSent(0)
    , bytesReceived(0)
    , currentJitter(0.0f)
    , sequenceNumber(0)
    , lastPlayedSequence(0)
{
    // Инициализация буферов
    captureBuffer.resize(config.frameSize * config.channels);
    playbackBuffer.resize(config.frameSize * config.channels, 0.0f);
    encodeBuffer.resize(1275); // Максимальный размер пакета Opus
    resampleBuffer.resize(config.frameSize * config.channels * 2);
}

// Деструктор
AudioStreamer::~AudioStreamer() {
    shutdown();
}

// Инициализация
bool AudioStreamer::initialize(const AudioConfig& cfg) {
    state = StreamerState::INITIALIZING;
    config = cfg;

    // Изменяем размеры буферов под новую конфигурацию
    captureBuffer.resize(config.frameSize * config.channels);
    playbackBuffer.resize(config.frameSize * config.channels, 0.0f);

    if (!initializePortAudio()) {
        state = StreamerState::ERR;
        triggerEvent(AudioEventType::DEVICE_ERROR, "Failed to initialize PortAudio");
        return false;
    }

    if (!initializeOpus()) {
        state = StreamerState::ERR;
        triggerEvent(AudioEventType::DEVICE_ERROR, "Failed to initialize Opus");
        return false;
    }

    state = StreamerState::IDLE;
    triggerEvent(AudioEventType::DEVICE_LIST_CHANGED, "Audio initialized successfully");
    return true;
}

// Инициализация PortAudio
bool AudioStreamer::initializePortAudio() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    return true;
}

// Инициализация Opus
bool AudioStreamer::initializeOpus() {
    int opusError;

    // Создание энкодера
    encoder = opus_encoder_create(config.sampleRate, config.channels,
        config.application, &opusError);
    if (opusError != OPUS_OK || !encoder) {
        std::cerr << "Opus encoder creation failed: " << opus_strerror(opusError) << std::endl;
        return false;
    }

    // Настройка энкодера
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(config.bitrate));
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(config.complexity));
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(15));

    // Создание декодера
    decoder = opus_decoder_create(config.sampleRate, config.channels, &opusError);
    if (opusError != OPUS_OK || !decoder) {
        std::cerr << "Opus decoder creation failed: " << opus_strerror(opusError) << std::endl;
        opus_encoder_destroy(encoder);
        encoder = nullptr;
        return false;
    }

    return true;
}

// Запуск захвата аудио
bool AudioStreamer::startCapture() {
    if (state != StreamerState::IDLE && state != StreamerState::STOPPED) {
        triggerEvent(AudioEventType::DEVICE_ERROR, "Invalid state for capture start");
        return false;
    }

    PaError err = Pa_OpenDefaultStream(&captureStream,
        config.channels,           // input channels
        0,                         // output channels
        paFloat32,                 // sample format
        config.sampleRate,
        config.frameSize,          // frames per buffer
        captureCallback,
        this);

    if (err != paNoError) {
        triggerEvent(AudioEventType::DEVICE_ERROR,
            "Failed to open capture stream: " + std::string(Pa_GetErrorText(err)));
        return false;
    }

    err = Pa_StartStream(captureStream);
    if (err != paNoError) {
        triggerEvent(AudioEventType::DEVICE_ERROR,
            "Failed to start capture stream: " + std::string(Pa_GetErrorText(err)));
        return false;
    }

    // Запуск потоков обработки
    threadsRunning = true;
    encodeThread = std::make_unique<std::thread>(&AudioStreamer::encodeLoop, this);

    state = StreamerState::CAPTURING;
    triggerEvent(AudioEventType::STREAM_STARTED, "Capture started");

    return true;
}

// Запуск воспроизведения
bool AudioStreamer::startPlayback() {
    if (state != StreamerState::IDLE && state != StreamerState::STOPPED) {
        triggerEvent(AudioEventType::DEVICE_ERROR, "Invalid state for playback start");
        return false;
    }

    PaError err = Pa_OpenDefaultStream(&playbackStream,
        0,                         // input channels
        config.channels,           // output channels
        paFloat32,                 // sample format
        config.sampleRate,
        config.frameSize,          // frames per buffer
        playbackCallback,
        this);

    if (err != paNoError) {
        triggerEvent(AudioEventType::DEVICE_ERROR,
            "Failed to open playback stream: " + std::string(Pa_GetErrorText(err)));
        return false;
    }

    err = Pa_StartStream(playbackStream);
    if (err != paNoError) {
        triggerEvent(AudioEventType::DEVICE_ERROR,
            "Failed to start playback stream: " + std::string(Pa_GetErrorText(err)));
        return false;
    }

    // Запуск потоков обработки
    threadsRunning = true;
    decodeThread = std::make_unique<std::thread>(&AudioStreamer::decodeLoop, this);
    jitterBufferThread = std::make_unique<std::thread>(&AudioStreamer::jitterBufferLoop, this);

    state = StreamerState::PLAYING;
    triggerEvent(AudioEventType::STREAM_STARTED, "Playback started");

    return true;
}

// Callback для захвата аудио
int AudioStreamer::captureCallback(const void* input, void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData) {
    auto* self = static_cast<AudioStreamer*>(userData);

    if (!input || !self) return paContinue;

    const float* in = static_cast<const float*>(input);

    // Копируем данные в буфер
    std::lock_guard<std::mutex> lock(self->captureMutex);
    std::copy(in, in + frameCount, self->captureBuffer.begin());

    // Применяем усиление если нужно
    if (self->config.volume != 1.0f) {
        for (size_t i = 0; i < frameCount; ++i) {
            self->captureBuffer[i] *= self->config.volume;
        }
    }

    // Помещаем в очередь для обработки
    if (self->captureQueue.size() < 10) { // Предотвращаем переполнение
        self->captureQueue.push(self->captureBuffer);
    }
    else {
        self->triggerEvent(AudioEventType::BUFFER_OVERFLOW, "Capture buffer overflow");
    }

    return paContinue;
}

// Callback для воспроизведения аудио
int AudioStreamer::playbackCallback(const void* input, void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData) {
    auto* self = static_cast<AudioStreamer*>(userData);

    if (!output || !self) return paContinue;

    float* out = static_cast<float*>(output);

    std::lock_guard<std::mutex> lock(self->receiveMutex);

    if (!self->playbackBuffer.empty()) {
        std::copy(self->playbackBuffer.begin(),
            self->playbackBuffer.begin() + frameCount,
            out);
    }
    else {
        // Тишина если нет данных
        std::fill(out, out + frameCount, 0.0f);
        self->triggerEvent(AudioEventType::BUFFER_UNDERRUN, "Playback buffer underrun");
    }

    return paContinue;
}

// Цикл кодирования
void AudioStreamer::encodeLoop() {
    while (threadsRunning) {
        std::vector<float> audioData;

        {
            std::lock_guard<std::mutex> lock(captureMutex);
            if (!captureQueue.empty()) {
                audioData = captureQueue.front();
                captureQueue.pop();
            }
        }

        if (!audioData.empty() && encoder) {
            int encodedSize = opus_encode_float(encoder,
                audioData.data(), config.frameSize,
                encodeBuffer.data(), static_cast<opus_int32>(encodeBuffer.size()));

            if (encodedSize > 0) {
                std::vector<uint8_t> packetData(encodeBuffer.begin(),
                    encodeBuffer.begin() + encodedSize);
                sendAudioPacket(packetData);

                if (audioProcessCallback) {
                    audioProcessCallback(audioData);
                }
            }
            else if (encodedSize < 0) {
                triggerEvent(AudioEventType::ENCODING_ERROR,
                    "Encoding error: " + std::string(opus_strerror(encodedSize)));
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

// Цикл декодирования
void AudioStreamer::decodeLoop() {
    std::vector<float> decodedAudio(config.frameSize * config.channels);

    while (threadsRunning) {
        AudioPacket packet;
        bool hasPacket = false;

        {
            std::lock_guard<std::mutex> lock(receiveMutex);
            if (!receiveQueue.empty()) {
                packet = receiveQueue.front();
                receiveQueue.pop();
                hasPacket = true;
            }
        }

        if (hasPacket && decoder) {
            int decodedSamples = opus_decode_float(decoder,
                packet.data.data(), static_cast<opus_int32>(packet.data.size()),
                decodedAudio.data(), config.frameSize, 0);

            if (decodedSamples > 0) {
                std::lock_guard<std::mutex> lock(receiveMutex);
                playbackBuffer = decodedAudio;

                // Проверка на потерю пакетов
                uint32_t expectedSeq = lastPlayedSequence + 1;
                if (packet.sequence > expectedSeq) {
                    uint32_t lost = packet.sequence - expectedSeq;
                    packetsLost += lost;
                    currentJitter = static_cast<float>(lost);
                    triggerEvent(AudioEventType::BUFFER_UNDERRUN,
                        "Packet loss detected", lost);
                }

                lastPlayedSequence = packet.sequence;
                packetsReceived++;
            }
            else if (decodedSamples < 0) {
                triggerEvent(AudioEventType::DECODING_ERROR,
                    "Decoding error: " + std::string(opus_strerror(decodedSamples)));
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

// Jitter buffer loop
void AudioStreamer::jitterBufferLoop() {
    while (threadsRunning) {
        processJitterBuffer();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// Обработка jitter buffer
void AudioStreamer::processJitterBuffer() {
    std::lock_guard<std::mutex> lock(jitterMutex);

    if (jitterBuffer.empty()) return;

    auto it = jitterBuffer.begin();

    // Если буфер слишком маленький, ждем накопления
    if (jitterBuffer.size() < JITTER_TARGET &&
        it->second.sequence > lastPlayedSequence + 1) {
        return;
    }

    // Ищем следующий пакет для воспроизведения
    uint32_t nextSequence = lastPlayedSequence + 1;
    it = jitterBuffer.find(nextSequence);

    if (it != jitterBuffer.end()) {
        // Добавляем пакет в очередь воспроизведения
        {
            std::lock_guard<std::mutex> lock(receiveMutex);
            receiveQueue.push(it->second);
        }

        jitterBuffer.erase(it);
        currentJitter = static_cast<float>(jitterBuffer.size());
    }
    else {
        // Пропущенный пакет - PLC (Packet Loss Concealment)
        packetsLost++;
        lastPlayedSequence++;
    }
}

// Отправка аудио пакета
void AudioStreamer::sendAudioPacket(const std::vector<uint8_t>& encodedData) {
    try {
        nlohmann::json msg;
        msg["type"] = "audio_data";
        msg["sequence"] = sequenceNumber++;
        msg["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        msg["sample_rate"] = config.sampleRate;
        msg["channels"] = config.channels;
        msg["frame_size"] = config.frameSize;
        msg["data"] = nlohmann::json::binary_t(encodedData);

        ws.write(net::buffer(msg.dump()));

        packetsSent++;
        bytesSent += encodedData.size();

    }
    catch (const std::exception& e) {
        triggerEvent(AudioEventType::ENCODING_ERROR,
            "WebSocket write error: " + std::string(e.what()));
    }
}

// Обработка входящего аудио
void AudioStreamer::processIncomingAudio(const nlohmann::json& message) {
    try {
        AudioPacket packet;
        packet.sequence = message["sequence"];
        packet.timestamp = message["timestamp"];

        if (message.contains("data") && message["data"].is_binary()) {
            auto binary = message["data"].get_binary();
            packet.data = binary;
        }

        {
            std::lock_guard<std::mutex> lock(jitterMutex);

            // Проверяем не слишком ли большой jitter buffer
            if (jitterBuffer.size() < MAX_JITTER_SIZE) {
                jitterBuffer[packet.sequence] = std::move(packet);
            }
            else {
                triggerEvent(AudioEventType::BUFFER_OVERFLOW, "Jitter buffer overflow");
            }
        }

        bytesReceived += packet.data.size();

    }
    catch (const std::exception& e) {
        triggerEvent(AudioEventType::DECODING_ERROR,
            "Failed to parse incoming audio: " + std::string(e.what()));
    }
}

// Остановка захвата
void AudioStreamer::stopCapture() {
    if (captureStream) {
        Pa_StopStream(captureStream);
        Pa_CloseStream(captureStream);
        captureStream = nullptr;
    }

    if (state == StreamerState::CAPTURING || state == StreamerState::STREAMING) {
        state = StreamerState::IDLE;
    }

    triggerEvent(AudioEventType::STREAM_STOPPED, "Capture stopped");
}

// Остановка воспроизведения
void AudioStreamer::stopPlayback() {
    if (playbackStream) {
        Pa_StopStream(playbackStream);
        Pa_CloseStream(playbackStream);
        playbackStream = nullptr;
    }

    if (state == StreamerState::PLAYING || state == StreamerState::STREAMING) {
        state = StreamerState::IDLE;
    }

    triggerEvent(AudioEventType::STREAM_STOPPED, "Playback stopped");
}

// Остановка всего
void AudioStreamer::stopAll() {
    threadsRunning = false;

    if (encodeThread && encodeThread->joinable()) {
        encodeThread->join();
    }

    if (decodeThread && decodeThread->joinable()) {
        decodeThread->join();
    }

    if (jitterBufferThread && jitterBufferThread->joinable()) {
        jitterBufferThread->join();
    }

    stopCapture();
    stopPlayback();

    // Очистка очередей
    {
        std::lock_guard<std::mutex> lock(captureMutex);
        while (!captureQueue.empty()) captureQueue.pop();
    }

    {
        std::lock_guard<std::mutex> lock(receiveMutex);
        while (!receiveQueue.empty()) receiveQueue.pop();
    }

    {
        std::lock_guard<std::mutex> lock(jitterMutex);
        jitterBuffer.clear();
    }

    state = StreamerState::STOPPED;
}

// Завершение работы
void AudioStreamer::shutdown() {
    stopAll();
    cleanupPortAudio();
    cleanupOpus();
}

// Очистка PortAudio
void AudioStreamer::cleanupPortAudio() {
    Pa_Terminate();
}

// Очистка Opus
void AudioStreamer::cleanupOpus() {
    if (encoder) {
        opus_encoder_destroy(encoder);
        encoder = nullptr;
    }

    if (decoder) {
        opus_decoder_destroy(decoder);
        decoder = nullptr;
    }
}

// Установка конфигурации
void AudioStreamer::setConfig(const AudioConfig& cfg) {
    config = cfg;

    // Обновляем параметры энкодера если он существует
    if (encoder) {
        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(config.bitrate));
        opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(config.complexity));
    }
}

// Получение конфигурации
AudioConfig AudioStreamer::getConfig() const {
    return config;
}

// Установка громкости
void AudioStreamer::setVolume(float vol) {
    config.volume = std::max(0.0f, std::min(2.0f, vol));
    triggerEvent(AudioEventType::VOLUME_CHANGED, "Volume changed",
        static_cast<int>(config.volume * 100));
}

// Получение громкости
float AudioStreamer::getVolume() const {
    return config.volume;
}

// Установка callback для событий
void AudioStreamer::setEventCallback(std::function<void(const AudioEvent&)> callback) {
    eventCallback = callback;
}

// Установка callback для обработки аудио
void AudioStreamer::setAudioProcessCallback(std::function<void(const std::vector<float>&)> callback) {
    audioProcessCallback = callback;
}

// Получение состояния
StreamerState AudioStreamer::getState() const {
    return state;
}

// Получение строкового представления состояния
std::string AudioStreamer::getStateString() const {
    switch (state) {
    case StreamerState::IDLE: return "IDLE";
    case StreamerState::INITIALIZING: return "INITIALIZING";
    case StreamerState::CAPTURING: return "CAPTURING";
    case StreamerState::PLAYING: return "PLAYING";
    case StreamerState::STREAMING: return "STREAMING";
    case StreamerState::ERR: return "ERROR";
    case StreamerState::STOPPED: return "STOPPED";
    default: return "UNKNOWN";
    }
}

// Получение статистики
void AudioStreamer::getStats(uint32_t& sent, uint32_t& received, uint32_t& lost,
    uint64_t& bytesSent, uint64_t& bytesReceived, float& jitter) const {
    sent = packetsSent;
    received = packetsReceived;
    lost = packetsLost;
    bytesSent = this->bytesSent;
    bytesReceived = this->bytesReceived;
    jitter = currentJitter;
}

// Триггер события
void AudioStreamer::triggerEvent(AudioEventType type, const std::string& message, int data) {
    AudioEvent event;
    event.type = type;
    event.message = message;
    event.timestamp = std::chrono::system_clock::now();
    event.data = data;

    if (eventCallback) {
        eventCallback(event);
    }
}

// Получение списка входных устройств
std::vector<std::pair<std::string, std::string>> AudioStreamer::getInputDevices() {
    std::vector<std::pair<std::string, std::string>> devices;

    Pa_Initialize();

    int numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) return devices;

    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo && deviceInfo->maxInputChannels > 0) {
            devices.emplace_back(std::to_string(i), deviceInfo->name);
        }
    }

    Pa_Terminate();
    return devices;
}

// Получение списка выходных устройств
std::vector<std::pair<std::string, std::string>> AudioStreamer::getOutputDevices() {
    std::vector<std::pair<std::string, std::string>> devices;

    Pa_Initialize();

    int numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) return devices;

    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo && deviceInfo->maxOutputChannels > 0) {
            devices.emplace_back(std::to_string(i), deviceInfo->name);
        }
    }

    Pa_Terminate();
    return devices;
}