#pragma once
#include "cWsClient.h"
#include "cRtcClientPeer.h"
#include "cAudioIO.h"
#include "cOpusCodec.h"

#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <future>

class cConferenceNet {
public:
    cConferenceNet() = default;
    ~cConferenceNet() { stop(); }

    bool start(const std::string& host = "127.0.0.1", int port = 9000, const std::string& path = "/");
    void stop();

    // ---- SYNC (можно оставить для консольных утилит, но НЕ вызывать из UI потока) ----
    std::string createConference(const std::string& title, const std::string& peerKeyBase);
    bool joinByInvite(const std::string& invite, const std::string& peerKeyBase);

    void leave();

    void setMicEnabled(bool en);
    bool isMicEnabled() const { return m_micEnabled.load(); }

    bool isInConference() const { return m_joined.load(); }
    int  lastConfId() const { return m_confId.load(); }
    std::string lastInvite() const;

    bool isConnected() const { return m_ws.isConnected(); }

    // ---- ASYNC API (для ImGui) ----
    void createConferenceAsync(const std::string& title, const std::string& peerKeyBase);
    void joinByInviteAsync(const std::string& invite, const std::string& peerKeyBase);

    // poll: вернёт true когда операция завершена (успех или ошибка)
    bool pollCreateResult(std::string& outInvite);
    bool pollJoinResult(bool& outOk);

    bool isBusy() const { return m_busy.load(); }
    std::string lastError() const { std::lock_guard<std::mutex> lg(m_mtx); return m_lastError; }

    void setServerWaitTimeout(std::chrono::milliseconds t) { m_waitTimeout = t; }

    std::string peerKey() const { return m_peerKey; }

private:
    void onWsMessage(const std::string& msg);

    bool startRtc();
    void stopRtc();

    bool startAudio();
    void stopAudio();

    bool ensurePeerKey(const std::string& peerKeyBase);
    static std::string makePeerKeySuffix();

    void setErrorLocked(const std::string& e) { m_lastError = e; }

private:
    cWsClient       m_ws;
    cRtcClientPeer  m_rtc;
    cAudioIO        m_audio;
    cOpusCodec      m_opus;

    std::string m_peerKey;

    std::atomic<bool> m_joined{ false };
    std::atomic<bool> m_micEnabled{ true };
    std::atomic<int>  m_confId{ -1 };

    // ожидания create/join
    mutable std::mutex m_mtx;
    std::condition_variable m_cv;

    bool m_gotCreate{ false };
    bool m_gotJoin{ false };
    bool m_joinOk{ false };
    std::string m_lastInvite;
    std::string m_lastError;

    std::chrono::milliseconds m_waitTimeout{ 2000 };

    // async
    std::atomic<bool> m_busy{ false };
    std::future<std::string> m_createFuture;
    std::future<bool> m_joinFuture;
};
