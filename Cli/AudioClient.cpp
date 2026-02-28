#include "AudioClient.h"
#include <iostream>
#include <cstring>

bool AudioClient::init() {
    // Инициализация PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    
    // Создание Opus энкодера
    int opusError;
    encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, 
                                  OPUS_APPLICATION, &opusError);
    if (opusError != OPUS_OK || !encoder) {
        std::cerr << "Opus encoder creation failed: " << opus_strerror(opusError) << std::endl;
        return false;
    }
    
    // Создание Opus декодера - ИСПРАВЛЕНО: передаем правильные параметры
    decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &opusError);
    if (opusError != OPUS_OK || !decoder) {
        std::cerr << "Opus decoder creation failed: " << opus_strerror(opusError) << std::endl;
        opus_encoder_destroy(encoder);
        encoder = nullptr;
        return false;
    }
    
    // Настройка опциональных параметров энкодера
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(64000));  // 64 kbps
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(10));  // Максимальная сложность
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));  // Оптимизация для голоса
    
    return true;
}

void AudioClient::startCapture() {
    PaError err = Pa_OpenDefaultStream(&paStream,
        CHANNELS,          // input channels
        0,                 // output channels
        paFloat32,         // sample format
        SAMPLE_RATE,
        FRAME_SIZE,        // frames per buffer
        audioCallback,
        this);
    
    if (err != paNoError) {
        std::cerr << "Failed to open default stream: " << Pa_GetErrorText(err) << std::endl;
        return;
    }
    
    err = Pa_StartStream(paStream);
    if (err != paNoError) {
        std::cerr << "Failed to start stream: " << Pa_GetErrorText(err) << std::endl;
    }
}

int AudioClient::audioCallback(const void* input, void* output,
                              unsigned long frameCount,
                              const PaStreamCallbackTimeInfo* timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void* userData) {
    
    auto* self = static_cast<AudioClient*>(userData);
    
    if (input && self && self->encoder) {
        const float* in = static_cast<const float*>(input);
        
        // Копируем аудио данные в буфер
        std::copy(in, in + frameCount, self->audio_buffer.begin());
        
        // Encode and send
        int encodedSize = opus_encode_float(self->encoder,
            self->audio_buffer.data(), frameCount,
            self->encode_buffer.data(), static_cast<opus_int32>(self->encode_buffer.size()));
        
        if (encodedSize > 0) {
            // ИСПРАВЛЕНО: Создаем бинарный объект JSON правильно
            nlohmann::json msg;
            msg["type"] = "audio_data";
            
            // Создаем вектор байтов из закодированных данных
            std::vector<uint8_t> audioData(
                self->encode_buffer.begin(),
                self->encode_buffer.begin() + encodedSize
            );
            
            // Используем binary_t для бинарных данных
            msg["data"] = nlohmann::json::binary_t(audioData);
            
            try {
                self->ws.write(net::buffer(msg.dump()));
            } catch (const std::exception& e) {
                std::cerr << "WebSocket write error: " << e.what() << std::endl;
            }
        }
    }
    
    return paContinue;
}

void AudioClient::stop() {
    if (paStream) {
        Pa_StopStream(paStream);
        Pa_CloseStream(paStream);
        paStream = nullptr;
    }
    
    if (encoder) {
        opus_encoder_destroy(encoder);
        encoder = nullptr;
    }
    
    if (decoder) {
        opus_decoder_destroy(decoder);
        decoder = nullptr;
    }
    
    Pa_Terminate();
}