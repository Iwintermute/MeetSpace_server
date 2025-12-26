#pragma once
#include <string>
#include <functional>
#include <cstdint>
#include <memory>
#include "../EDS_server/includes.h"
#ifdef NETWORKEDS_EXPORTS
#define NETWORKEDS_API __declspec(dllexport)
#else
// Build as part of the same executable/static library: avoid dllimport
#define NETWORKEDS_API
#endif

namespace NetworkEDS {

    // ���� ��������
    using OnMessageCallback = std::function<void(const std::string& message)>;
    using OnConnectedCallback = std::function<void()>;
    using OnDisconnectedCallback = std::function<void()>;
    using OnAudioFrameCallback = std::function<void(const int16_t* samples, int sampleCount)>;
    using OnConferenceCreatedCallback = std::function<void(int conferenceId)>;
    using OnConferenceJoinedCallback = std::function<void(int conferenceId, const std::string& peerId)>;
    using OnPeerJoinedCallback = std::function<void(const std::string& peerId)>;
    using OnPeerLeftCallback = std::function<void(const std::string& peerId)>;
    using OnAuthStateChangedCallback = std::function<void(bool authenticated, const std::string& message)>;

    // ��������� ��� ���������� ������� �����������
    class INetworkManager {
    public:
        virtual ~INetworkManager() = default;

        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;

        virtual bool ConnectToServer(const std::string& serverIp, uint16_t port) = 0;
        virtual void Disconnect() = 0;

        virtual int CreateConference(const std::string& title, const std::string& password = "") = 0;
        virtual bool JoinConference(int conferenceId, const std::string& password = "") = 0;
        virtual void LeaveConference() = 0;
        virtual bool InviteToConference(int conferenceId, const std::string& peerId) = 0;

        virtual void StartAudioCapture() = 0;
        virtual void StopAudioCapture() = 0;
        virtual void StartAudioPlayback() = 0;
        virtual void StopAudioPlayback() = 0;

        virtual void ToggleMicrophone(bool enabled) = 0;
        virtual void ToggleSpeaker(bool enabled) = 0;

        virtual void SendChatMessage(const std::string& message) = 0;
        virtual void SendReaction(const std::string& emoji) = 0;

        // �������
        virtual void SetMessageCallback(OnMessageCallback callback) = 0;
        virtual void SetConnectedCallback(OnConnectedCallback callback) = 0;
        virtual void SetDisconnectedCallback(OnDisconnectedCallback callback) = 0;
        virtual void SetAudioFrameCallback(OnAudioFrameCallback callback) = 0;
        virtual void SetConferenceCreatedCallback(OnConferenceCreatedCallback callback) = 0;
        virtual void SetConferenceJoinedCallback(OnConferenceJoinedCallback callback) = 0;
        virtual void SetPeerJoinedCallback(OnPeerJoinedCallback callback) = 0;
        virtual void SetPeerLeftCallback(OnPeerLeftCallback callback) = 0;
        virtual void SetAuthStateChangedCallback(OnAuthStateChangedCallback callback) = 0;

        // ��������� ���������
        virtual bool IsConnected() const = 0;
        virtual int GetCurrentConference() const = 0;
        virtual std::string GetPeerId() const = 0;
        virtual std::string GetStatus() const = 0;
        virtual bool IsAuthenticated() const = 0;

        // Authentication
        virtual void SetAuthToken(const std::string& token) = 0;
        virtual void AuthenticateWithToken(const std::string& token) = 0;
        virtual bool JoinConferenceByToken(const std::string& conferenceToken) = 0;
    };

    // ������� ��� �������� ����������
    NETWORKEDS_API std::unique_ptr<INetworkManager> CreateNetworkManager();

    // C-����������� ���������
    extern "C" {
        NETWORKEDS_API void* CreateNetworkManagerInstance();
        NETWORKEDS_API void DestroyNetworkManagerInstance(void* manager);

        NETWORKEDS_API bool NetworkManager_Initialize(void* manager);
        NETWORKEDS_API void NetworkManager_Shutdown(void* manager);

        NETWORKEDS_API bool NetworkManager_ConnectToServer(void* manager, const char* serverIp, uint16_t port);
        NETWORKEDS_API void NetworkManager_Disconnect(void* manager);

        NETWORKEDS_API int NetworkManager_CreateConference(void* manager, const char* title, const char* password);
        NETWORKEDS_API bool NetworkManager_JoinConference(void* manager, int conferenceId, const char* password);
        NETWORKEDS_API void NetworkManager_LeaveConference(void* manager);

        NETWORKEDS_API void NetworkManager_StartAudioCapture(void* manager);
        NETWORKEDS_API void NetworkManager_StopAudioCapture(void* manager);

        NETWORKEDS_API void NetworkManager_ToggleMicrophone(void* manager, bool enabled);
        NETWORKEDS_API void NetworkManager_ToggleSpeaker(void* manager, bool enabled);

        NETWORKEDS_API void NetworkManager_SendChatMessage(void* manager, const char* message);

        NETWORKEDS_API void NetworkManager_SetAuthToken(void* manager, const char* token);
        NETWORKEDS_API void NetworkManager_AuthenticateWithToken(void* manager, const char* token);
        NETWORKEDS_API bool NetworkManager_JoinConferenceByToken(void* manager, const char* conferenceToken);

        NETWORKEDS_API bool NetworkManager_IsConnected(void* manager);
        NETWORKEDS_API bool NetworkManager_IsAuthenticated(void* manager);
        NETWORKEDS_API int NetworkManager_GetCurrentConference(void* manager);
        NETWORKEDS_API const char* NetworkManager_GetPeerId(void* manager);
    }

} // namespace NetworkEDS

// If we're building/using this code in the same binary (not as DLL), provide inline wrappers
#ifndef NETWORKEDS_EXPORTS
#include "NetworkManager.h"

namespace NetworkEDS {
    inline std::unique_ptr<INetworkManager> CreateNetworkManager() {
        return std::make_unique<NetworkManagerImpl>();
    }
}

extern "C" {
    inline void* CreateNetworkManagerInstance() {
        return static_cast<void*>(new NetworkEDS::NetworkManagerImpl());
    }
    inline void DestroyNetworkManagerInstance(void* manager) {
        delete static_cast<NetworkEDS::NetworkManagerImpl*>(manager);
    }

    inline bool NetworkManager_Initialize(void* manager) {
        if (!manager) return false;
        return static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->Initialize();
    }
    inline void NetworkManager_Shutdown(void* manager) {
        if (!manager) return;
        static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->Shutdown();
    }

    inline bool NetworkManager_ConnectToServer(void* manager, const char* serverIp, uint16_t port) {
        if (!manager || !serverIp) return false;
        return static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->ConnectToServer(serverIp, port);
    }
    inline void NetworkManager_Disconnect(void* manager) {
        if (!manager) return;
        static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->Disconnect();
    }

    inline int NetworkManager_CreateConference(void* manager, const char* title, const char* password) {
        if (!manager || !title) return -1;
        return static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->CreateConference(title, password ? password : "");
    }
    inline bool NetworkManager_JoinConference(void* manager, int conferenceId, const char* password) {
        if (!manager) return false;
        return static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->JoinConference(conferenceId, password ? password : "");
    }
    inline void NetworkManager_LeaveConference(void* manager) {
        if (!manager) return;
        static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->LeaveConference();
    }

    inline void NetworkManager_StartAudioCapture(void* manager) {
        if (!manager) return;
        static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->StartAudioCapture();
    }
    inline void NetworkManager_StopAudioCapture(void* manager) {
        if (!manager) return;
        static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->StopAudioCapture();
    }

    inline void NetworkManager_ToggleMicrophone(void* manager, bool enabled) {
        if (!manager) return;
        static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->ToggleMicrophone(enabled);
    }
    inline void NetworkManager_ToggleSpeaker(void* manager, bool enabled) {
        if (!manager) return;
        static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->ToggleSpeaker(enabled);
    }

    inline void NetworkManager_SendChatMessage(void* manager, const char* message) {
        if (!manager || !message) return;
        static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->SendChatMessage(message);
    }

    inline void NetworkManager_SetAuthToken(void* manager, const char* token) {
        if (!manager || !token) return;
        static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->SetAuthToken(token);
    }
    inline void NetworkManager_AuthenticateWithToken(void* manager, const char* token) {
        if (!manager || !token) return;
        static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->AuthenticateWithToken(token);
    }
    inline bool NetworkManager_JoinConferenceByToken(void* manager, const char* conferenceToken) {
        if (!manager || !conferenceToken) return false;
        return static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->JoinConferenceByToken(conferenceToken);
    }

    inline bool NetworkManager_IsConnected(void* manager) {
        if (!manager) return false;
        return static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->IsConnected();
    }
    inline bool NetworkManager_IsAuthenticated(void* manager) {
        if (!manager) return false;
        return static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->IsAuthenticated();
    }
    inline int NetworkManager_GetCurrentConference(void* manager) {
        if (!manager) return -1;
        return static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->GetCurrentConference();
    }
    inline const char* NetworkManager_GetPeerId(void* manager) {
        if (!manager) return nullptr;
        static std::string peerId;
        peerId = static_cast<NetworkEDS::NetworkManagerImpl*>(manager)->GetPeerId();
        return peerId.c_str();
    }
}
#endif