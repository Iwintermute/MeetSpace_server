#include "NetworkManager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>

namespace NetworkEDS {

    NetworkManagerImpl::NetworkManagerImpl()
        : m_wsStream(nullptr) {

        // ��������� ���������� peer ID
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        std::stringstream ss;
        for (int i = 0; i < 8; i++) {
            ss << std::hex << dis(gen);
        }
        m_peerId = ss.str();
    }

    NetworkManagerImpl::~NetworkManagerImpl() {
        Shutdown();
    }

    bool NetworkManagerImpl::Initialize() {
        try {
            // ������������� PortAudio
            PaError err = Pa_Initialize();
            if (err != paNoError) {
                std::cerr << "[NetworkEDS] PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
                return false;
            }

            // ������������� Opus ������
            int opusErr = 0;
            m_opusEncoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &opusErr);
            if (opusErr != OPUS_OK) {
                std::cerr << "[NetworkEDS] Opus encoder creation failed: " << opusErr << std::endl;
                return false;
            }

            m_opusDecoder = opus_decoder_create(48000, 1, &opusErr);
            if (opusErr != OPUS_OK) {
                std::cerr << "[NetworkEDS] Opus decoder creation failed: " << opusErr << std::endl;
                return false;
            }

            // ��������� ���������� ������
            opus_encoder_ctl(m_opusEncoder, OPUS_SET_BITRATE(64000));
            opus_encoder_ctl(m_opusEncoder, OPUS_SET_COMPLEXITY(8));

            m_running = true;
            std::cout << "[NetworkEDS] Initialized successfully, Peer ID: " << m_peerId << std::endl;
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[NetworkEDS] Initialization failed: " << e.what() << std::endl;
            return false;
        }
    }

    void NetworkManagerImpl::Shutdown() {
        m_running = false;

        // ���������� �� �������
        Disconnect();

        // ��������� �����
        StopAudioCapture();
        StopAudioPlayback();

        // ��������� IO ���������
        if (m_ioThread.joinable()) {
            m_ioContext.stop();
            m_ioThread.join();
        }

        // ������� WebSocket
        if (m_wsStream) {
            m_wsStream.reset();
        }

        // ������� ����� ��������
        CleanupAudio();

        // ������� Opus
        if (m_opusEncoder) {
            opus_encoder_destroy(m_opusEncoder);
            m_opusEncoder = nullptr;
        }

        if (m_opusDecoder) {
            opus_decoder_destroy(m_opusDecoder);
            m_opusDecoder = nullptr;
        }

        // ���������� PortAudio
        Pa_Terminate();

        std::cout << "[NetworkEDS] Shutdown completed" << std::endl;
    }

    bool NetworkManagerImpl::ConnectToServer(const std::string& serverIp, uint16_t port) {
        if (m_wsConnected) {
            std::cerr << "[NetworkEDS] Already connected to server" << std::endl;
            return false;
        }

        try {
            m_serverHost = serverIp;
            m_serverPort = port;

            // �������� WebSocket ����������
            m_wsStream = std::make_unique<websocket::stream<tcp::socket>>(m_ioContext);

            // ������ IO ��������� � ��������� ������
            m_ioThread = std::thread([this]() {
                RunIOContext();
                });

            // ���������� ����� �����
            m_resolver = std::make_unique<tcp::resolver>(m_ioContext);

            std::cout << "[NetworkEDS] Resolving " << m_serverHost << ":" << m_serverPort << std::endl;

            m_resolver->async_resolve(m_serverHost, std::to_string(m_serverPort),
                [this](beast::error_code ec, tcp::resolver::results_type results) {
                    OnResolve(ec, results);
                });

            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[NetworkEDS] ConnectToServer failed: " << e.what() << std::endl;
            return false;
        }
    }

    void NetworkManagerImpl::RunIOContext() {
        try {
            m_ioContext.run();
        }
        catch (const std::exception& e) {
            std::cerr << "[NetworkEDS] IO context error: " << e.what() << std::endl;
        }
    }

    void NetworkManagerImpl::OnResolve(beast::error_code ec, tcp::resolver::results_type results) {
        if (ec) {
            std::cerr << "[NetworkEDS] Resolve failed: " << ec.message() << std::endl;
            return;
        }

        std::cout << "[NetworkEDS] Connecting to server..." << std::endl;

        // ��������� TCP ����������
        asio::async_connect(m_wsStream->next_layer(), results,
            [this](beast::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
                OnConnect(ec, ep);
            });
    }

    void NetworkManagerImpl::OnConnect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
        if (ec) {
            std::cerr << "[NetworkEDS] Connect failed: " << ec.message() << std::endl;
            return;
        }

        std::cout << "[NetworkEDS] TCP connected to " << ep.address().to_string() << std::endl;

        // ��������� WebSocket ����������
        m_wsStream->async_handshake(m_serverHost + ":" + std::to_string(m_serverPort), "/",
            [this](beast::error_code ec) {
                OnWebSocketConnect(ec);
            });
    }

    void NetworkManagerImpl::OnWebSocketConnect(beast::error_code ec) {
        if (ec) {
            std::cerr << "[NetworkEDS] WebSocket handshake failed: " << ec.message() << std::endl;
            return;
        }

        std::cout << "[NetworkEDS] WebSocket connected successfully" << std::endl;

        m_wsConnected = true;
        OnServerConnected();

        if (!m_authToken.empty()) {
            AuthenticateWithToken(m_authToken);
        }
        else {
            nlohmann::json registerMsg;
            registerMsg["type"] = "register";
            registerMsg["peer_id"] = m_peerId;
            SendWebSocketMessage(registerMsg.dump());
        }

        // ������ ������ ���������
        StartWebSocketRead();
    }

    void NetworkManagerImpl::StartWebSocketRead() {
        if (!m_wsConnected || !m_wsStream) {
            return;
        }

        m_wsStream->async_read(m_readBuffer,
            [this](beast::error_code ec, std::size_t bytesRead) {
                OnWebSocketRead(ec, bytesRead);
            });
    }

    void NetworkManagerImpl::OnWebSocketRead(beast::error_code ec, std::size_t bytesRead) {
        if (ec == websocket::error::closed) {
            std::cout << "[NetworkEDS] WebSocket connection closed" << std::endl;
            OnServerDisconnected();
            return;
        }

        if (ec) {
            std::cerr << "[NetworkEDS] WebSocket read error: " << ec.message() << std::endl;
            OnServerDisconnected();
            return;
        }

        // ��������� ����������� ���������
        std::string message = beast::buffers_to_string(m_readBuffer.data());
        m_readBuffer.consume(bytesRead);

        HandleServerMessage(message);

        // ����������� ������
        StartWebSocketRead();
    }

    void NetworkManagerImpl::SendWebSocketMessage(const std::string& message) {
        if (!m_wsConnected || !m_wsStream) {
            std::cerr << "[NetworkEDS] Not connected, cannot send message" << std::endl;
            return;
        }

        try {
            m_wsStream->write(asio::buffer(message));
        }
        catch (const std::exception& e) {
            std::cerr << "[NetworkEDS] Send message failed: " << e.what() << std::endl;
        }
    }

    void NetworkManagerImpl::Disconnect() {
        if (m_wsConnected && m_wsStream) {
            try {
                // �������� ��������� � ������
                if (m_currentConferenceId != -1) {
                    nlohmann::json leaveMsg;
                    leaveMsg["type"] = "leave_conference";
                    leaveMsg["conference_id"] = m_currentConferenceId;
                    leaveMsg["peer_id"] = m_peerId;
                    SendWebSocketMessage(leaveMsg.dump());
                }

                // �������� WebSocket
                beast::error_code ec;
                m_wsStream->close(websocket::close_code::normal, ec);

                if (ec) {
                    std::cerr << "[NetworkEDS] WebSocket close error: " << ec.message() << std::endl;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "[NetworkEDS] Disconnect error: " << e.what() << std::endl;
            }
        }

        m_wsConnected = false;
        m_currentConferenceId = -1;

        OnServerDisconnected();
    }

    void NetworkManagerImpl::OnServerConnected() {
        std::cout << "[NetworkEDS] Server connected" << std::endl;

        if (m_connectedCallback) {
            m_connectedCallback();
        }
    }

    void NetworkManagerImpl::OnServerDisconnected() {
        std::cout << "[NetworkEDS] Server disconnected" << std::endl;

        // Reset connection state so status reporting remains accurate and
        // dependent services (audio capture/playback) stop cleanly when the
        // server drops unexpectedly.
        m_wsConnected = false;
        m_authenticated = false;
        m_currentConferenceId = -1;

        StopAudioCapture();
        StopAudioPlayback();

        if (m_authStateChangedCallback) {
            m_authStateChangedCallback(false, "Disconnected from server");
        }

        if (m_disconnectedCallback) {
            m_disconnectedCallback();
        }
    }

    void NetworkManagerImpl::HandleServerMessage(const std::string& message) {
        try {
            std::cout << "[NetworkEDS] Received message: " << message << std::endl;

            if (m_messageCallback) {
                m_messageCallback(message);
            }

            auto jsonMsg = nlohmann::json::parse(message);
            HandleSignalingMessage(jsonMsg);
        }
        catch (const std::exception& e) {
            std::cerr << "[NetworkEDS] Error parsing message: " << e.what() << std::endl;
        }
    }

    void NetworkManagerImpl::HandleSignalingMessage(const nlohmann::json& jsonMsg) {
        std::string type = jsonMsg.value("type", "");

        if (type == "welcome") {
            std::cout << "[NetworkEDS] Server welcome: " << jsonMsg.dump() << std::endl;
        }
        else if (type == "conference_created") {
            int conferenceId = jsonMsg.value("conference_id", -1);
            m_currentConferenceId = conferenceId;

            std::cout << "[NetworkEDS] Conference created: " << conferenceId << std::endl;

            if (m_conferenceCreatedCallback) {
                m_conferenceCreatedCallback(conferenceId);
            }
        }
        else if (type == "conference_joined") {
            int conferenceId = jsonMsg.value("conference_id", -1);
            m_currentConferenceId = conferenceId;

            std::cout << "[NetworkEDS] Joined conference: " << conferenceId << std::endl;

            // ��������� ����� ��� �������� �������������
            StartAudioCapture();
            StartAudioPlayback();

            if (m_conferenceJoinedCallback) {
                m_conferenceJoinedCallback(conferenceId, m_peerId);
            }
        }
        else if (type == "peer_joined") {
            std::string peerId = jsonMsg.value("peer_id", "");

            std::cout << "[NetworkEDS] Peer joined: " << peerId << std::endl;

            if (m_peerJoinedCallback) {
                m_peerJoinedCallback(peerId);
            }
        }
        else if (type == "peer_left") {
            std::string peerId = jsonMsg.value("peer_id", "");

            std::cout << "[NetworkEDS] Peer left: " << peerId << std::endl;

            if (m_peerLeftCallback) {
                m_peerLeftCallback(peerId);
            }
        }
        else if (type == "auth_ok") {
            m_authenticated = true;

            std::cout << "[NetworkEDS] Authentication succeeded" << std::endl;

            if (m_authStateChangedCallback) {
                m_authStateChangedCallback(true, jsonMsg.value("message", ""));
            }

            nlohmann::json registerMsg;
            registerMsg["type"] = "register";
            registerMsg["peer_id"] = m_peerId;
            SendWebSocketMessage(registerMsg.dump());
        }
        else if (type == "auth_error") {
            m_authenticated = false;

            std::string errorMsg = jsonMsg.value("message", "Authentication failed");
            std::cerr << "[NetworkEDS] Authentication failed: " << errorMsg << std::endl;

            if (m_authStateChangedCallback) {
                m_authStateChangedCallback(false, errorMsg);
            }
        }
        else if (type == "audio_frame") {
            // ��������� ����� ������
            if (jsonMsg.contains("data") && m_speakerEnabled) {
                try {
                    std::vector<uint8_t> encodedData = jsonMsg["data"];
                    std::string fromPeer = jsonMsg.value("peer_id", "");

                    // ������������� Opus
                    std::vector<int16_t> decoded(960); // 20ms ��� 48kHz
                    int decodedSamples = opus_decode(m_opusDecoder,
                        encodedData.data(),
                        static_cast<opus_int32>(encodedData.size()),
                        decoded.data(),
                        static_cast<int>(decoded.size()),
                        0);

                    if (decodedSamples > 0) {
                        decoded.resize(decodedSamples);

                        // ���������� � ������� ���������������
                        std::lock_guard<std::mutex> lock(m_audioMutex);
                        m_audioPlaybackQueue.push(std::move(decoded));
                    }
                }
                catch (...) {
                    std::cerr << "[NetworkEDS] Error processing audio frame" << std::endl;
                }
            }
        }
        else if (type == "chat_message") {
            std::string sender = jsonMsg.value("peer_id", "");
            std::string message = jsonMsg.value("message", "");

            std::cout << "[NetworkEDS] Chat from " << sender << ": " << message << std::endl;
        }
        else if (type == "error") {
            std::string errorMsg = jsonMsg.value("message", "Unknown error");
            std::cerr << "[NetworkEDS] Server error: " << errorMsg << std::endl;
        }
    }

    int NetworkManagerImpl::CreateConference(const std::string& title, const std::string& password) {
        if (!m_wsConnected) {
            std::cerr << "[NetworkEDS] Not connected to server" << std::endl;
            return -1;
        }

        nlohmann::json createMsg;
        createMsg["type"] = "create_conference";
        createMsg["title"] = title;
        createMsg["peer_id"] = m_peerId;

        if (!password.empty()) {
            createMsg["password"] = password;
        }
        if (!m_authToken.empty()) {
            createMsg["auth_token"] = m_authToken;
        }

        SendWebSocketMessage(createMsg.dump());

        // ���������� ��������� ID, �������� ������ � ������ �������
        static int tempId = 1000;
        return tempId++;
    }

    bool NetworkManagerImpl::JoinConference(int conferenceId, const std::string& password) {
        if (!m_wsConnected) {
            std::cerr << "[NetworkEDS] Not connected to server" << std::endl;
            return false;
        }

        nlohmann::json joinMsg;
        joinMsg["type"] = "join_conference";
        joinMsg["conference_id"] = conferenceId;
        joinMsg["peer_id"] = m_peerId;

        if (!password.empty()) {
            joinMsg["password"] = password;
        }
        if (!m_authToken.empty()) {
            joinMsg["auth_token"] = m_authToken;
        }

        SendWebSocketMessage(joinMsg.dump());
        return true;
    }

    void NetworkManagerImpl::LeaveConference() {
        if (m_currentConferenceId != -1 && m_wsConnected) {
            nlohmann::json leaveMsg;
            leaveMsg["type"] = "leave_conference";
            leaveMsg["conference_id"] = m_currentConferenceId;
            leaveMsg["peer_id"] = m_peerId;
            if (!m_authToken.empty()) {
                leaveMsg["auth_token"] = m_authToken;
            }

            SendWebSocketMessage(leaveMsg.dump());

            m_currentConferenceId = -1;
            StopAudioCapture();
            StopAudioPlayback();
        }
    }

    bool NetworkManagerImpl::InviteToConference(int conferenceId, const std::string& peerId) {
        if (!m_wsConnected) {
            return false;
        }

        nlohmann::json inviteMsg;
        inviteMsg["type"] = "invite_to_conference";
        inviteMsg["conference_id"] = conferenceId;
        inviteMsg["peer_id"] = peerId;
        if (!m_authToken.empty()) {
            inviteMsg["auth_token"] = m_authToken;
        }

        SendWebSocketMessage(inviteMsg.dump());
        return true;
    }

    void NetworkManagerImpl::StartAudioCapture() {
        if (m_capturingAudio) {
            return;
        }

        PaError err = Pa_OpenDefaultStream(&m_captureStream,
            1, // input channels
            0, // output channels
            paInt16,
            48000,
            480, // frames per buffer (10ms at 48kHz)
            AudioCaptureCallback,
            this);

        if (err == paNoError) {
            err = Pa_StartStream(m_captureStream);
            if (err == paNoError) {
                m_capturingAudio = true;

                // ������ ������ ��������� �����
                m_audioThread = std::thread([this]() {
                    ProcessAudioCapture();
                    });

                std::cout << "[NetworkEDS] Audio capture started" << std::endl;
            }
        }

        if (err != paNoError) {
            std::cerr << "[NetworkEDS] Audio capture start failed: " << Pa_GetErrorText(err) << std::endl;
        }
    }

    void NetworkManagerImpl::StopAudioCapture() {
        if (m_capturingAudio && m_captureStream) {
            m_capturingAudio = false;

            // ���������� ����� ���������
            m_audioCV.notify_all();

            Pa_StopStream(m_captureStream);
            Pa_CloseStream(m_captureStream);
            m_captureStream = nullptr;

            // �������� ���������� ������ ���������
            if (m_audioThread.joinable()) {
                m_audioThread.join();
            }

            std::cout << "[NetworkEDS] Audio capture stopped" << std::endl;
        }
    }

    void NetworkManagerImpl::StartAudioPlayback() {
        if (m_playingAudio) {
            return;
        }

        PaError err = Pa_OpenDefaultStream(&m_playbackStream,
            0, // input channels
            1, // output channels
            paInt16,
            48000,
            480, // frames per buffer
            AudioPlaybackCallback,
            this);

        if (err == paNoError) {
            err = Pa_StartStream(m_playbackStream);
            if (err == paNoError) {
                m_playingAudio = true;
                std::cout << "[NetworkEDS] Audio playback started" << std::endl;
            }
        }

        if (err != paNoError) {
            std::cerr << "[NetworkEDS] Audio playback start failed: " << Pa_GetErrorText(err) << std::endl;
        }
    }

    void NetworkManagerImpl::StopAudioPlayback() {
        if (m_playingAudio && m_playbackStream) {
            m_playingAudio = false;

            Pa_StopStream(m_playbackStream);
            Pa_CloseStream(m_playbackStream);
            m_playbackStream = nullptr;

            // ������� ������� ���������������
            std::lock_guard<std::mutex> lock(m_audioMutex);
            while (!m_audioPlaybackQueue.empty()) {
                m_audioPlaybackQueue.pop();
            }

            std::cout << "[NetworkEDS] Audio playback stopped" << std::endl;
        }
    }

    int NetworkManagerImpl::AudioCaptureCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {
        NetworkManagerImpl* manager = static_cast<NetworkManagerImpl*>(userData);

        if (!manager->m_microphoneEnabled) {
            return paContinue;
        }

        const int16_t* samples = static_cast<const int16_t*>(inputBuffer);
        std::vector<int16_t> audioFrame(samples, samples + framesPerBuffer);

        // ���������� � ������� ���������
        std::lock_guard<std::mutex> lock(manager->m_audioMutex);
        manager->m_audioCaptureQueue.push(std::move(audioFrame));
        manager->m_audioCV.notify_one();

        return paContinue;
    }

    int NetworkManagerImpl::AudioPlaybackCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData) {
        NetworkManagerImpl* manager = static_cast<NetworkManagerImpl*>(userData);
        int16_t* output = static_cast<int16_t*>(outputBuffer);

        std::lock_guard<std::mutex> lock(manager->m_audioMutex);

        if (!manager->m_audioPlaybackQueue.empty()) {
            auto& audioFrame = manager->m_audioPlaybackQueue.front();
            size_t samplesToCopy = std::min(framesPerBuffer, audioFrame.size());

            std::copy(audioFrame.begin(), audioFrame.begin() + samplesToCopy, output);

            if (audioFrame.size() > framesPerBuffer) {
                // ������� �������������� ������
                audioFrame.erase(audioFrame.begin(), audioFrame.begin() + framesPerBuffer);
            }
            else {
                manager->m_audioPlaybackQueue.pop();
            }

            // ��������� ������� ������
            if (samplesToCopy < framesPerBuffer) {
                std::fill(output + samplesToCopy, output + framesPerBuffer, 0);
            }
        }
        else {
            // ���� ��� ������, ��������� ������
            std::fill(output, output + framesPerBuffer, 0);
        }

        return paContinue;
    }

    void NetworkManagerImpl::ProcessAudioCapture() {
        while (m_capturingAudio && m_running) {
            std::vector<int16_t> audioFrame;

            {
                std::unique_lock<std::mutex> lock(m_audioMutex);
                m_audioCV.wait(lock, [this]() {
                    return !m_audioCaptureQueue.empty() || !m_capturingAudio || !m_running;
                    });

                if (!m_capturingAudio || !m_running) {
                    break;
                }

                if (!m_audioCaptureQueue.empty()) {
                    audioFrame = std::move(m_audioCaptureQueue.front());
                    m_audioCaptureQueue.pop();
                }
            }

            if (!audioFrame.empty() && m_microphoneEnabled) {
                // ����������� � Opus
                std::vector<uint8_t> encoded(4000);
                int encodedSize = opus_encode(m_opusEncoder,
                    audioFrame.data(),
                    static_cast<int>(audioFrame.size()),
                    encoded.data(),
                    static_cast<opus_int32>(encoded.size()));

                if (encodedSize > 0) {
                    encoded.resize(encodedSize);

                    // �������� �� ������
                    if (m_currentConferenceId != -1 && m_wsConnected) {
                        nlohmann::json audioMsg;
                        audioMsg["type"] = "audio_frame";
                        audioMsg["conference_id"] = m_currentConferenceId;
                        audioMsg["peer_id"] = m_peerId;
                        audioMsg["data"] = encoded;
                        audioMsg["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();

                        SendWebSocketMessage(audioMsg.dump());
                    }
                }

                // ������ ��� ��������� ���������
                if (m_audioFrameCallback) {
                    m_audioFrameCallback(audioFrame.data(), static_cast<int>(audioFrame.size()));
                }
            }
        }
    }

    void NetworkManagerImpl::InitializeAudio() {
        // ��� ����������� � Initialize()
    }

    void NetworkManagerImpl::CleanupAudio() {
        // ������� ��������
        {
            std::lock_guard<std::mutex> lock(m_audioMutex);
            while (!m_audioCaptureQueue.empty()) {
                m_audioCaptureQueue.pop();
            }
            while (!m_audioPlaybackQueue.empty()) {
                m_audioPlaybackQueue.pop();
            }
        }
    }

    void NetworkManagerImpl::ToggleMicrophone(bool enabled) {
        m_microphoneEnabled = enabled;
        if (!enabled) {
            // ���� �������� ��������, ������� ������� �������
            std::lock_guard<std::mutex> lock(m_audioMutex);
            while (!m_audioCaptureQueue.empty()) {
                m_audioCaptureQueue.pop();
            }
        }
    }

    void NetworkManagerImpl::ToggleSpeaker(bool enabled) {
        m_speakerEnabled = enabled;
        if (!enabled) {
            // ���� ������� ��������, ������� ������� ���������������
            std::lock_guard<std::mutex> lock(m_audioMutex);
            while (!m_audioPlaybackQueue.empty()) {
                m_audioPlaybackQueue.pop();
            }
        }
    }

    void NetworkManagerImpl::SendChatMessage(const std::string& message) {
        if (m_currentConferenceId == -1 || !m_wsConnected) {
            std::cerr << "[NetworkEDS] Not in conference or not connected" << std::endl;
            return;
        }

        nlohmann::json chatMsg;
        chatMsg["type"] = "chat_message";
        chatMsg["conference_id"] = m_currentConferenceId;
        chatMsg["peer_id"] = m_peerId;
        chatMsg["message"] = message;
        chatMsg["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        SendWebSocketMessage(chatMsg.dump());
    }

    void NetworkManagerImpl::SendReaction(const std::string& emoji) {
        if (m_currentConferenceId == -1 || !m_wsConnected) {
            return;
        }

        nlohmann::json reactionMsg;
        reactionMsg["type"] = "reaction";
        reactionMsg["conference_id"] = m_currentConferenceId;
        reactionMsg["peer_id"] = m_peerId;
        reactionMsg["emoji"] = emoji;

        SendWebSocketMessage(reactionMsg.dump());
    }

    void NetworkManagerImpl::SetMessageCallback(OnMessageCallback callback) {
        m_messageCallback = callback;
    }

    void NetworkManagerImpl::SetConnectedCallback(OnConnectedCallback callback) {
        m_connectedCallback = callback;
    }

    void NetworkManagerImpl::SetDisconnectedCallback(OnDisconnectedCallback callback) {
        m_disconnectedCallback = callback;
    }

    void NetworkManagerImpl::SetAudioFrameCallback(OnAudioFrameCallback callback) {
        m_audioFrameCallback = callback;
    }

    void NetworkManagerImpl::SetConferenceCreatedCallback(OnConferenceCreatedCallback callback) {
        m_conferenceCreatedCallback = callback;
    }

    void NetworkManagerImpl::SetConferenceJoinedCallback(OnConferenceJoinedCallback callback) {
        m_conferenceJoinedCallback = callback;
    }

    void NetworkManagerImpl::SetPeerJoinedCallback(OnPeerJoinedCallback callback) {
        m_peerJoinedCallback = callback;
    }

    void NetworkManagerImpl::SetPeerLeftCallback(OnPeerLeftCallback callback) {
        m_peerLeftCallback = callback;
    }

    void NetworkManagerImpl::SetAuthStateChangedCallback(OnAuthStateChangedCallback callback) {
        m_authStateChangedCallback = callback;
    }

    bool NetworkManagerImpl::IsConnected() const {
        return m_wsConnected;
    }

    int NetworkManagerImpl::GetCurrentConference() const {
        return m_currentConferenceId;
    }

    std::string NetworkManagerImpl::GetPeerId() const {
        return m_peerId;
    }

    std::string NetworkManagerImpl::GetStatus() const {
        std::string status;

        if (m_wsConnected) {
            status += "Connected to server";
            if (m_authenticated) {
                status += " (authenticated)";
            }
            if (m_currentConferenceId != -1) {
                status += ", in conference #" + std::to_string(m_currentConferenceId);
            }
        }
        else {
            status += "Disconnected";
        }

        if (m_capturingAudio) {
            status += ", capturing audio";
        }

        if (m_playingAudio) {
            status += ", playing audio";
        }

        return status;
    }

    bool NetworkManagerImpl::IsAuthenticated() const {
        return m_authenticated;
    }

    void NetworkManagerImpl::SetAuthToken(const std::string& token) {
        m_authToken = token;
    }

    void NetworkManagerImpl::AuthenticateWithToken(const std::string& token) {
        m_authToken = token;

        if (!m_wsConnected) {
            std::cerr << "[NetworkEDS] Cannot authenticate without WebSocket connection" << std::endl;
            return;
        }

        nlohmann::json authMsg;
        authMsg["type"] = "authenticate";
        authMsg["auth_token"] = token;
        authMsg["peer_id"] = m_peerId;

        SendWebSocketMessage(authMsg.dump());
    }

    bool NetworkManagerImpl::JoinConferenceByToken(const std::string& conferenceToken) {
        if (!m_wsConnected) {
            std::cerr << "[NetworkEDS] Not connected to server" << std::endl;
            return false;
        }

        if (conferenceToken.empty()) {
            std::cerr << "[NetworkEDS] Conference token is empty" << std::endl;
            return false;
        }

        nlohmann::json joinMsg;
        joinMsg["type"] = "join_conference_by_token";
        joinMsg["conference_token"] = conferenceToken;
        joinMsg["peer_id"] = m_peerId;
        if (!m_authToken.empty()) {
            joinMsg["auth_token"] = m_authToken;
        }

        SendWebSocketMessage(joinMsg.dump());
        return true;
    }

    // �������
    std::unique_ptr<INetworkManager> CreateNetworkManager() {
        return std::make_unique<NetworkManagerImpl>();
    }

    // C-����������� �������
    extern "C" {

        NETWORKEDS_API void* CreateNetworkManagerInstance() {
            try {
                auto manager = new NetworkManagerImpl();
                return manager;
            }
            catch (...) {
                return nullptr;
            }
        }

        NETWORKEDS_API void DestroyNetworkManagerInstance(void* manager) {
            if (manager) {
                delete static_cast<NetworkManagerImpl*>(manager);
            }
        }

        NETWORKEDS_API bool NetworkManager_Initialize(void* manager) {
            if (manager) {
                return static_cast<NetworkManagerImpl*>(manager)->Initialize();
            }
            return false;
        }

        NETWORKEDS_API void NetworkManager_Shutdown(void* manager) {
            if (manager) {
                static_cast<NetworkManagerImpl*>(manager)->Shutdown();
            }
        }

        NETWORKEDS_API bool NetworkManager_ConnectToServer(void* manager, const char* serverIp, uint16_t port) {
            if (manager && serverIp) {
                return static_cast<NetworkManagerImpl*>(manager)->ConnectToServer(serverIp, port);
            }
            return false;
        }

        NETWORKEDS_API void NetworkManager_Disconnect(void* manager) {
            if (manager) {
                static_cast<NetworkManagerImpl*>(manager)->Disconnect();
            }
        }

        NETWORKEDS_API int NetworkManager_CreateConference(void* manager, const char* title, const char* password) {
            if (manager && title) {
                std::string pass = password ? password : "";
                return static_cast<NetworkManagerImpl*>(manager)->CreateConference(title, pass);
            }
            return -1;
        }

        NETWORKEDS_API bool NetworkManager_JoinConference(void* manager, int conferenceId, const char* password) {
            if (manager) {
                std::string pass = password ? password : "";
                return static_cast<NetworkManagerImpl*>(manager)->JoinConference(conferenceId, pass);
            }
            return false;
        }

        NETWORKEDS_API void NetworkManager_LeaveConference(void* manager) {
            if (manager) {
                static_cast<NetworkManagerImpl*>(manager)->LeaveConference();
            }
        }

        NETWORKEDS_API void NetworkManager_StartAudioCapture(void* manager) {
            if (manager) {
                static_cast<NetworkManagerImpl*>(manager)->StartAudioCapture();
            }
        }

        NETWORKEDS_API void NetworkManager_StopAudioCapture(void* manager) {
            if (manager) {
                static_cast<NetworkManagerImpl*>(manager)->StopAudioCapture();
            }
        }

        NETWORKEDS_API void NetworkManager_ToggleMicrophone(void* manager, bool enabled) {
            if (manager) {
                static_cast<NetworkManagerImpl*>(manager)->ToggleMicrophone(enabled);
            }
        }

        NETWORKEDS_API void NetworkManager_ToggleSpeaker(void* manager, bool enabled) {
            if (manager) {
                static_cast<NetworkManagerImpl*>(manager)->ToggleSpeaker(enabled);
            }
        }

        NETWORKEDS_API void NetworkManager_SendChatMessage(void* manager, const char* message) {
            if (manager && message) {
                static_cast<NetworkManagerImpl*>(manager)->SendChatMessage(message);
            }
        }

        NETWORKEDS_API void NetworkManager_SetAuthToken(void* manager, const char* token) {
            if (manager && token) {
                static_cast<NetworkManagerImpl*>(manager)->SetAuthToken(token);
            }
        }

        NETWORKEDS_API void NetworkManager_AuthenticateWithToken(void* manager, const char* token) {
            if (manager && token) {
                static_cast<NetworkManagerImpl*>(manager)->AuthenticateWithToken(token);
            }
        }

        NETWORKEDS_API bool NetworkManager_JoinConferenceByToken(void* manager, const char* conferenceToken) {
            if (manager && conferenceToken) {
                return static_cast<NetworkManagerImpl*>(manager)->JoinConferenceByToken(conferenceToken);
            }
            return false;
        }

        NETWORKEDS_API bool NetworkManager_IsConnected(void* manager) {
            if (manager) {
                return static_cast<NetworkManagerImpl*>(manager)->IsConnected();
            }
            return false;
        }

        NETWORKEDS_API bool NetworkManager_IsAuthenticated(void* manager) {
            if (manager) {
                return static_cast<NetworkManagerImpl*>(manager)->IsAuthenticated();
            }
            return false;
        }

        NETWORKEDS_API int NetworkManager_GetCurrentConference(void* manager) {
            if (manager) {
                return static_cast<NetworkManagerImpl*>(manager)->GetCurrentConference();
            }
            return -1;
        }

        NETWORKEDS_API const char* NetworkManager_GetPeerId(void* manager) {
            if (manager) {
                static std::string peerId;
                peerId = static_cast<NetworkManagerImpl*>(manager)->GetPeerId();
                return peerId.c_str();
            }
            return nullptr;
        }

    } // extern "C"

} // namespace NetworkEDS
