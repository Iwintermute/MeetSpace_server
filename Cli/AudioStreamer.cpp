#include "AudioStreamer.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace {
    std::string trimCopy(const std::string& value) {
        auto begin = value.begin();
        auto end = value.end();
        while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
            ++begin;
        }
        while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
            --end;
        }
        return std::string(begin, end);
    }

    std::string toLowerCopy(const std::string& value) {
        std::string lowered = value;
        std::transform(
            lowered.begin(),
            lowered.end(),
            lowered.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return lowered;
    }

    int tryParseDeviceIndex(const std::string& value) {
        if (value.empty()) {
            return -1;
        }
        char* end = nullptr;
        const auto parsed = std::strtol(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0') {
            return -1;
        }
        if (parsed < 0 || parsed > std::numeric_limits<int>::max()) {
            return -1;
        }
        return static_cast<int>(parsed);
    }
}

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
    , signalingTransportWarningShown(false)
    , lastPlayedSequence(0)
    , agcSmoothedGain(1.0f)
{
    // Инициализация буферов
    captureBuffer.resize(config.frameSize * config.channels);
    playbackBuffer.resize(config.frameSize * config.channels, 0.0f);
    encodeBuffer.resize(1275); // Максимальный размер пакета Opus
    resampleBuffer.resize(config.frameSize * config.channels * 2);
    highPassPrevInput.resize(config.channels, 0.0f);
    highPassPrevOutput.resize(config.channels, 0.0f);
    activeInputDeviceName = "default";
    activeOutputDeviceName = "default";
}

// Деструктор
AudioStreamer::~AudioStreamer() {
    shutdown();
}

// Инициализация
bool AudioStreamer::initialize(const AudioConfig& cfg) {
    state = StreamerState::INITIALIZING;
    config = cfg;
    if (config.channels <= 0) {
        config.channels = 1;
    }
    agcSmoothedGain = 1.0f;
    signalingTransportWarningShown.store(false);

    // Изменяем размеры буферов под новую конфигурацию
    captureBuffer.resize(config.frameSize * config.channels);
    playbackBuffer.resize(config.frameSize * config.channels, 0.0f);
    highPassPrevInput.assign(config.channels, 0.0f);
    highPassPrevOutput.assign(config.channels, 0.0f);

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
    opus_encoder_ctl(encoder, OPUS_SET_DTX(1)); // Включить DTX (Discontinuous Transmission)

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
    if (captureStream != nullptr) {
        return true;
    }

    const auto currentState = state.load();
    if (currentState == StreamerState::INITIALIZING || currentState == StreamerState::ERR) {
        triggerEvent(AudioEventType::DEVICE_ERROR, "Invalid state for capture start");
        return false;
    }

    std::string deviceName;
    std::string resolveError;
    const auto inputDeviceIndex = resolveInputDeviceIndex(deviceName, resolveError);
    if (inputDeviceIndex < 0) {
        triggerEvent(AudioEventType::DEVICE_ERROR, "Failed to resolve input device: " + resolveError);
        return false;
    }

    const auto* inputInfo = Pa_GetDeviceInfo(inputDeviceIndex);
    if (inputInfo == nullptr || inputInfo->maxInputChannels <= 0) {
        triggerEvent(AudioEventType::DEVICE_ERROR, "Selected input device has no capture channels.");
        return false;
    }

    PaStreamParameters inputParams{};
    inputParams.device = inputDeviceIndex;
    inputParams.channelCount = std::min(config.channels, inputInfo->maxInputChannels);
    if (inputParams.channelCount <= 0) {
        inputParams.channelCount = 1;
    }
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = inputInfo->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    config.channels = inputParams.channelCount;
    captureBuffer.assign(static_cast<std::size_t>(config.frameSize * config.channels), 0.0f);
    highPassPrevInput.assign(config.channels, 0.0f);
    highPassPrevOutput.assign(config.channels, 0.0f);
    agcSmoothedGain = 1.0f;
    activeInputDeviceName = deviceName;

    PaError err = Pa_OpenStream(
        &captureStream,
        &inputParams,
        nullptr,
        config.sampleRate,
        config.frameSize,
        paNoFlag,
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
    if (!encodeThread || !encodeThread->joinable()) {
        encodeThread = std::make_unique<std::thread>(&AudioStreamer::encodeLoop, this);
    }

    state = (playbackStream != nullptr) ? StreamerState::STREAMING : StreamerState::CAPTURING;
    triggerEvent(AudioEventType::STREAM_STARTED, "Capture started (input: " + activeInputDeviceName + ")");

    return true;
}

// Запуск воспроизведения
bool AudioStreamer::startPlayback() {
    if (playbackStream != nullptr) {
        return true;
    }

    const auto currentState = state.load();
    if (currentState == StreamerState::INITIALIZING || currentState == StreamerState::ERR) {
        triggerEvent(AudioEventType::DEVICE_ERROR, "Invalid state for playback start");
        return false;
    }

    std::string deviceName;
    std::string resolveError;
    const auto outputDeviceIndex = resolveOutputDeviceIndex(deviceName, resolveError);
    if (outputDeviceIndex < 0) {
        triggerEvent(AudioEventType::DEVICE_ERROR, "Failed to resolve output device: " + resolveError);
        return false;
    }

    const auto* outputInfo = Pa_GetDeviceInfo(outputDeviceIndex);
    if (outputInfo == nullptr || outputInfo->maxOutputChannels <= 0) {
        triggerEvent(AudioEventType::DEVICE_ERROR, "Selected output device has no playback channels.");
        return false;
    }

    PaStreamParameters outputParams{};
    outputParams.device = outputDeviceIndex;
    outputParams.channelCount = std::min(config.channels, outputInfo->maxOutputChannels);
    if (outputParams.channelCount <= 0) {
        outputParams.channelCount = 1;
    }
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = outputInfo->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;
    activeOutputDeviceName = deviceName;

    PaError err = Pa_OpenStream(
        &playbackStream,
        nullptr,
        &outputParams,
        config.sampleRate,
        config.frameSize,
        paNoFlag,
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
    if (!decodeThread || !decodeThread->joinable()) {
        decodeThread = std::make_unique<std::thread>(&AudioStreamer::decodeLoop, this);
    }
    if (!jitterBufferThread || !jitterBufferThread->joinable()) {
        jitterBufferThread = std::make_unique<std::thread>(&AudioStreamer::jitterBufferLoop, this);
    }

    state = (captureStream != nullptr) ? StreamerState::STREAMING : StreamerState::PLAYING;
    triggerEvent(AudioEventType::STREAM_STARTED, "Playback started (output: " + activeOutputDeviceName + ")");

    return true;
}

// Callback для захвата аудио
int AudioStreamer::captureCallback(const void* input, void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData) {
    auto* self = static_cast<AudioStreamer*>(userData);
    if (!input || !self) {
        return paContinue;
    }
    if (statusFlags & paInputOverflow) {
        self->triggerEvent(AudioEventType::BUFFER_OVERFLOW, "Capture input overflow");
    }

    const float* in = static_cast<const float*>(input);
    const auto channels = static_cast<std::size_t>(std::max(1, self->config.channels));
    const auto totalSamples = static_cast<std::size_t>(frameCount) * channels;
    if (totalSamples == 0) {
        return paContinue;
    }

    std::lock_guard<std::mutex> lock(self->captureMutex);
    if (self->captureBuffer.size() < totalSamples) {
        self->captureBuffer.resize(totalSamples, 0.0f);
    }
    std::copy(in, in + totalSamples, self->captureBuffer.begin());
    self->processCaptureBuffer(frameCount);

    float sumSquares = 0.0f;
    for (std::size_t i = 0; i < totalSamples; ++i) {
        sumSquares += self->captureBuffer[i] * self->captureBuffer[i];
    }
    const auto rms = std::sqrt(sumSquares / static_cast<float>(totalSamples));
    constexpr float kSilenceDropThreshold = 0.0012f;

    if (rms >= kSilenceDropThreshold || !self->config.noiseSuppression) {
        if (self->captureQueue.size() >= 10) {
            self->captureQueue.pop();
        }
        self->captureQueue.push(self->captureBuffer);
    }
    // else - тишина, ничего не добавляем в очередь

    return paContinue;
}

// Callback для воспроизведения аудио
int AudioStreamer::playbackCallback(const void* input, void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData) {
    auto* self = static_cast<AudioStreamer*>(userData);
    if (!output || !self) {
        return paContinue;
    }
    if (statusFlags & paOutputUnderflow) {
        self->triggerEvent(AudioEventType::BUFFER_UNDERRUN, "Playback output underflow");
    }

    float* out = static_cast<float*>(output);
    const auto channels = static_cast<std::size_t>(std::max(1, self->config.channels));
    const auto totalSamples = static_cast<std::size_t>(frameCount) * channels;

    std::lock_guard<std::mutex> lock(self->receiveMutex);

    if (!self->playbackBuffer.empty()) {
        const auto copyCount = std::min(totalSamples, self->playbackBuffer.size());
        std::copy(self->playbackBuffer.begin(),
            self->playbackBuffer.begin() + copyCount,
            out);
        if (copyCount < totalSamples) {
            std::fill(out + copyCount, out + totalSamples, 0.0f);
        }
    }
    else {
        // Тишина если нет данных
        std::fill(out, out + totalSamples, 0.0f);
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
                const auto channels = static_cast<std::size_t>(std::max(1, config.channels));
                const auto decodedSampleCount = static_cast<std::size_t>(decodedSamples) * channels;
                playbackBuffer.assign(decodedAudio.begin(), decodedAudio.begin() + std::min(decodedSampleCount, decodedAudio.size()));
                if (playbackBuffer.size() < decodedAudio.size()) {
                    playbackBuffer.resize(decodedAudio.size(), 0.0f);
                }

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

void AudioStreamer::processCaptureBuffer(unsigned long frameCount) {
    const auto channels = static_cast<std::size_t>(std::max(1, config.channels));
    const auto totalSamples = static_cast<std::size_t>(frameCount) * channels;
    if (totalSamples == 0 || captureBuffer.size() < totalSamples) {
        return;
    }
    if (highPassPrevInput.size() != channels) {
        highPassPrevInput.assign(channels, 0.0f);
    }
    if (highPassPrevOutput.size() != channels) {
        highPassPrevOutput.assign(channels, 0.0f);
    }

    const auto safeSampleRate = static_cast<float>(std::max(8000, config.sampleRate));
    const float dt = 1.0f / safeSampleRate;
    constexpr float kHighPassCutoffHz = 85.0f;
    const float rc = 1.0f / (2.0f * 3.14159265f * kHighPassCutoffHz);
    const float highPassAlpha = rc / (rc + dt);

    float sumSquares = 0.0f;
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const auto index = frame * channels + channel;
            auto sample = captureBuffer[index];

            if (config.echoCancellation) {
                const auto filtered = highPassAlpha * (highPassPrevOutput[channel] + sample - highPassPrevInput[channel]);
                highPassPrevInput[channel] = sample;
                highPassPrevOutput[channel] = filtered;
                sample = filtered;
            }

            captureBuffer[index] = sample;
            sumSquares += sample * sample;
        }
    }

    const auto rms = std::sqrt(sumSquares / static_cast<float>(totalSamples));

    if (config.noiseSuppression) {
        constexpr float kGateClose = 0.0040f;
        constexpr float kGateOpen = 0.0110f;
        for (std::size_t index = 0; index < totalSamples; ++index) {
            const auto sample = captureBuffer[index];
            const auto amplitude = std::abs(sample);
            if (amplitude <= kGateClose) {
                captureBuffer[index] = 0.0f;
            }
            else if (amplitude < kGateOpen) {
                const auto scale = (amplitude - kGateClose) / (kGateOpen - kGateClose);
                captureBuffer[index] = sample * scale;
            }
        }
    }

    float effectiveGain = 1.0f;
    if (config.autoGainControl) {
        constexpr float kTargetRms = 0.11f;
        constexpr float kMinGain = 0.65f;
        constexpr float kMaxGain = 2.20f;
        const auto safeRms = std::max(rms, 0.008f);
        const auto desiredGain = std::clamp(kTargetRms / safeRms, kMinGain, kMaxGain);
        const auto smoothing = desiredGain < agcSmoothedGain ? 0.30f : 0.08f;
        agcSmoothedGain += (desiredGain - agcSmoothedGain) * smoothing;
        effectiveGain = agcSmoothedGain;
    }
    else {
        agcSmoothedGain = 1.0f;
    }

    const auto volume = std::clamp(config.volume, 0.0f, 2.0f);
    const auto limiterScale = std::tanh(1.6f);
    for (std::size_t index = 0; index < totalSamples; ++index) {
        auto sample = captureBuffer[index] * effectiveGain * volume;
        sample = std::clamp(sample, -1.0f, 1.0f);
        sample = std::tanh(sample * 1.6f) / limiterScale;
        captureBuffer[index] = sample;
    }
}

int AudioStreamer::resolveDeviceIndex(
    const std::string& selector,
    bool isInput,
    std::string& deviceName,
    std::string& error) {
    error.clear();
    deviceName.clear();

    const auto normalizedSelector = trimCopy(selector);
    int deviceIndex = paNoDevice;

    if (normalizedSelector.empty() || toLowerCopy(normalizedSelector) == "default") {
        deviceIndex = isInput ? Pa_GetDefaultInputDevice() : Pa_GetDefaultOutputDevice();
    }
    else {
        const auto parsedIndex = tryParseDeviceIndex(normalizedSelector);
        if (parsedIndex >= 0) {
            deviceIndex = parsedIndex;
        }
        else {
            const auto needle = toLowerCopy(normalizedSelector);
            const auto deviceCount = Pa_GetDeviceCount();
            if (deviceCount < 0) {
                error = "PortAudio could not enumerate devices: " + std::string(Pa_GetErrorText(static_cast<PaError>(deviceCount)));
                return -1;
            }
            for (int index = 0; index < deviceCount; ++index) {
                const auto* info = Pa_GetDeviceInfo(index);
                if (info == nullptr) {
                    continue;
                }
                if (isInput && info->maxInputChannels <= 0) {
                    continue;
                }
                if (!isInput && info->maxOutputChannels <= 0) {
                    continue;
                }
                const auto candidateName = info->name ? std::string(info->name) : std::string{};
                if (toLowerCopy(candidateName).find(needle) != std::string::npos) {
                    deviceIndex = index;
                    break;
                }
            }
        }
    }

    if (deviceIndex == paNoDevice) {
        error = std::string(isInput ? "Default input device is unavailable." : "Default output device is unavailable.");
        return -1;
    }

    const auto deviceCount = Pa_GetDeviceCount();
    if (deviceCount < 0) {
        error = "PortAudio could not enumerate devices: " + std::string(Pa_GetErrorText(static_cast<PaError>(deviceCount)));
        return -1;
    }
    if (deviceIndex < 0 || deviceIndex >= deviceCount) {
        error = "Device index is out of range: " + std::to_string(deviceIndex);
        return -1;
    }

    const auto* info = Pa_GetDeviceInfo(deviceIndex);
    if (info == nullptr) {
        error = "PortAudio returned null device info for index " + std::to_string(deviceIndex);
        return -1;
    }
    if (isInput && info->maxInputChannels <= 0) {
        error = "Selected device has no input channels: " + std::to_string(deviceIndex);
        return -1;
    }
    if (!isInput && info->maxOutputChannels <= 0) {
        error = "Selected device has no output channels: " + std::to_string(deviceIndex);
        return -1;
    }

    deviceName = info->name ? std::string(info->name) : ("device-" + std::to_string(deviceIndex));
    return deviceIndex;
}

int AudioStreamer::resolveInputDeviceIndex(std::string& deviceName, std::string& error) const {
    return resolveDeviceIndex(config.inputDevice, true, deviceName, error);
}

int AudioStreamer::resolveOutputDeviceIndex(std::string& deviceName, std::string& error) const {
    return resolveDeviceIndex(config.outputDevice, false, deviceName, error);
}

// Отправка аудио пакета
void AudioStreamer::sendAudioPacket(const std::vector<uint8_t>& encodedData) {
    if (!config.allowLegacySignalingAudio) {
        const auto wasShown = signalingTransportWarningShown.exchange(true);
        if (!wasShown) {
            triggerEvent(
                AudioEventType::DEVICE_ERROR,
                "Legacy audio_data over signaling websocket is disabled. Use mediasoup WebRTC media transport.");
        }
        return;
    }
    try {
        const auto sequence = sequenceNumber++;
        const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        nlohmann::json payload{
            { "type", "audio_data" },
            { "sequence", sequence },
            { "timestamp", timestamp },
            { "sampleRate", config.sampleRate },
            { "channels", config.channels },
            { "frameSize", config.frameSize },
            { "data", encodedData }
        };

        ws.binary(false);
        const auto serialized = payload.dump();
        ws.write(net::buffer(serialized));

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
    if (!config.allowLegacySignalingAudio) {
        return;
    }
    try {
        AudioPacket packet;
        packet.sequence = message.value("sequence", 0u);
        packet.timestamp = message.value("timestamp", 0ull);

        if (message.contains("data")) {
            if (message["data"].is_binary()) {
                auto binary = message["data"].get_binary();
                packet.data = binary;
            }
            else if (message["data"].is_array()) {
                packet.data.reserve(message["data"].size());
                for (const auto& item : message["data"]) {
                    if (item.is_number_unsigned()) {
                        packet.data.push_back(static_cast<uint8_t>(item.get<uint32_t>()));
                    }
                    else if (item.is_number_integer()) {
                        packet.data.push_back(static_cast<uint8_t>(item.get<int32_t>()));
                    }
                }
            }
        }
        if (packet.data.empty()) {
            return;
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
    if (state == StreamerState::CAPTURING || state == StreamerState::STREAMING || state == StreamerState::PLAYING) {
        state = (playbackStream != nullptr) ? StreamerState::PLAYING : StreamerState::IDLE;
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
    if (state == StreamerState::PLAYING || state == StreamerState::STREAMING || state == StreamerState::CAPTURING) {
        state = (captureStream != nullptr) ? StreamerState::CAPTURING : StreamerState::IDLE;
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
    if (config.channels <= 0) {
        config.channels = 1;
    }
    captureBuffer.resize(static_cast<std::size_t>(config.frameSize * config.channels));
    playbackBuffer.resize(static_cast<std::size_t>(config.frameSize * config.channels), 0.0f);
    highPassPrevInput.assign(config.channels, 0.0f);
    highPassPrevOutput.assign(config.channels, 0.0f);
    agcSmoothedGain = 1.0f;

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

std::string AudioStreamer::getActiveInputDeviceName() const {
    return activeInputDeviceName;
}

std::string AudioStreamer::getActiveOutputDeviceName() const {
    return activeOutputDeviceName;
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