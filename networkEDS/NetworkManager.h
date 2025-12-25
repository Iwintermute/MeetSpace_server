#pragma once
#include "networkEDS.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include "../networkEDS/include/nlohmann/json.hpp"
#include <rtc/rtc.hpp>
#include <portaudio.h>
#include <opus.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <memory>
#include <unordered_map>
#include <condition_variable>

namespace NetworkEDS {

    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    namespace asio = boost::asio;
    using tcp = boost::asio::ip::tcp;

    class NetworkManagerImpl : public INetworkManager {
    private:
        // WebSocket
        asio::io_context m_ioContext;
        std::unique_ptr<tcp::resolver> m_resolver;
        std::unique_ptr<websocket::stream<tcp::socket>> m_wsStream;
        std::thread m_ioThread;
        std::atomic<bool> m_running{ false };
        std::atomic<bool> m_wsConnected{ false };

        // �����
        PaStream* m_captureStream{ nullptr };
        PaStream* m_playbackStream{ nullptr };
        std::atomic<bool> m_capturingAudio{ false };
        std::atomic<bool> m_playingAudio{ false };
        std::thread m_audioThread;

        // Opus �����
        OpusEncoder* m_opusEncoder{ nullptr };
        OpusDecoder* m_opusDecoder{ nullptr };

        // ������� � ������
        std::queue<std::vector<int16_t>> m_audioCaptureQueue;
        std::queue<std::vector<int16_t>> m_audioPlaybackQueue;
        std::mutex m_audioMutex;
        std::condition_variable m_audioCV;

        // �������
        OnMessageCallback m_messageCallback;
        OnConnectedCallback m_connectedCallback;
        OnDisconnectedCallback m_disconnectedCallback;
        OnAudioFrameCallback m_audioFrameCallback;
        OnConferenceCreatedCallback m_conferenceCreatedCallback;
        OnConferenceJoinedCallback m_conferenceJoinedCallback;
        OnPeerJoinedCallback m_peerJoinedCallback;
        OnPeerLeftCallback m_peerLeftCallback;

        // ���������
        std::string m_peerId;
        std::string m_serverHost;
        uint16_t m_serverPort{ 0 };
        int m_currentConferenceId{ -1 };
        std::atomic<bool> m_microphoneEnabled{ true };
        std::atomic<bool> m_speakerEnabled{ true };
        std::string m_authToken;

        // ����������� ������� ��� PortAudio
        static int AudioCaptureCallback(const void* inputBuffer, void* outputBuffer,
            unsigned long framesPerBuffer,
            const PaStreamCallbackTimeInfo* timeInfo,
            PaStreamCallbackFlags statusFlags,
            void* userData);

        static int AudioPlaybackCallback(const void* inputBuffer, void* outputBuffer,
            unsigned long framesPerBuffer,
            const PaStreamCallbackTimeInfo* timeInfo,
            PaStreamCallbackFlags statusFlags,
            void* userData);

        // ���������� ������
        void InitializeAudio();
        void CleanupAudio();

        void RunIOContext();
        void ConnectWebSocket();
        void OnWebSocketConnect(beast::error_code ec);
        void StartWebSocketRead();
        void OnWebSocketRead(beast::error_code ec, std::size_t bytesRead);
        void SendWebSocketMessage(const std::string& message);

        void HandleServerMessage(const std::string& message);
        void HandleSignalingMessage(const nlohmann::json& jsonMsg);

        void ProcessAudioCapture();
        void ProcessAudioPlayback();
        void SendAudioFrame(const std::vector<int16_t>& audioFrame);

        void OnServerConnected();
        void OnServerDisconnected();

    public:
        NetworkManagerImpl();
        virtual ~NetworkManagerImpl();

        // ���������� INetworkManager
        bool Initialize() override;
        void Shutdown() override;

        bool ConnectToServer(const std::string& serverIp, uint16_t port) override;
        void Disconnect() override;

        int CreateConference(const std::string& title, const std::string& password = "") override;
        bool JoinConference(int conferenceId, const std::string& password = "") override;
        void LeaveConference() override;
        bool InviteToConference(int conferenceId, const std::string& peerId) override;

        void StartAudioCapture() override;
        void StopAudioCapture() override;
        void StartAudioPlayback() override;
        void StopAudioPlayback() override;

        void ToggleMicrophone(bool enabled) override;
        void ToggleSpeaker(bool enabled) override;

        void SendChatMessage(const std::string& message) override;
        void SendReaction(const std::string& emoji) override;

        void SetMessageCallback(OnMessageCallback callback) override;
        void SetConnectedCallback(OnConnectedCallback callback) override;
        void SetDisconnectedCallback(OnDisconnectedCallback callback) override;
        void SetAudioFrameCallback(OnAudioFrameCallback callback) override;
        void SetConferenceCreatedCallback(OnConferenceCreatedCallback callback) override;
        void SetConferenceJoinedCallback(OnConferenceJoinedCallback callback) override;
        void SetPeerJoinedCallback(OnPeerJoinedCallback callback) override;
        void SetPeerLeftCallback(OnPeerLeftCallback callback) override;

        bool IsConnected() const override;
        int GetCurrentConference() const override;
        std::string GetPeerId() const override;
        std::string GetStatus() const override;
        void SetAuthToken(const std::string& token) override;

    private:
        // ����������� ��� Asio
        void OnResolve(beast::error_code ec, tcp::resolver::results_type results);
        void OnConnect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep);

        beast::flat_buffer m_readBuffer;
    };

} // namespace NetworkEDS