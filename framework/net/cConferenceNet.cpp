#include "cConferenceNet.h"
#include <nlohmann/json.hpp>
#include <random>

using json = nlohmann::json;

std::string cConferenceNet::makePeerKeySuffix()
{
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<uint32_t> d;

    uint32_t r = d(rng);
    auto now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    return std::to_string(now) + "_" + std::to_string(r);
}

bool cConferenceNet::ensurePeerKey(const std::string& peerKeyBase)
{
    if (peerKeyBase.empty()) return false;

    m_peerKey = peerKeyBase;
    if (m_peerKey.find('_') == std::string::npos)
        m_peerKey += "_" + makePeerKeySuffix();

    return true;
}

bool cConferenceNet::start(const std::string& host, int port, const std::string& path)
{
    m_ws.setOnMessage([this](const std::string& s) { onWsMessage(s); });
    if (!m_ws.connect(host, port, path))
        return false;

    if (!m_audio.init(48000, 1, 480)) {
        m_ws.close();
        return false;
    }
    if (!m_audio.startPlayback()) {
        m_audio.shutdown();
        m_ws.close();
        return false;
    }

    if (!m_opus.init(48000, 1)) {
        m_audio.shutdown();
        m_ws.close();
        return false;
    }

    return true;
}

void cConferenceNet::stop()
{
    leave();
    m_ws.close();
    m_opus.shutdown();
    m_audio.shutdown();
}

std::string cConferenceNet::lastInvite() const
{
    std::lock_guard<std::mutex> lg(m_mtx);
    return m_lastInvite;
}

std::string cConferenceNet::createConference(const std::string& title, const std::string& peerKeyBase)
{
    if (!m_ws.isConnected()) return "";
    if (!ensurePeerKey(peerKeyBase)) return "";

    {
        std::lock_guard<std::mutex> lg(m_mtx);
        m_lastError.clear();
        m_gotCreate = false;
        m_lastInvite.clear();
        m_confId = -1;
    }

    json j{
        {"type","conf_create"},
        {"title", title.empty() ? "Conference" : title},
        {"peer", m_peerKey}
    };
    m_ws.sendText(j.dump());

    {
        std::unique_lock<std::mutex> ul(m_mtx);
        m_cv.wait_for(ul, m_waitTimeout, [&] { return m_gotCreate; });
    }

    std::string invite;
    {
        std::lock_guard<std::mutex> lg(m_mtx);
        invite = m_lastInvite;
    }

    if (invite.empty()) {
        std::lock_guard<std::mutex> lg(m_mtx);
        if (!m_ws.isConnected()) setErrorLocked("WS disconnected while waiting conf_created");
        else setErrorLocked("Timeout / no conf_created from server");
        return "";
    }

    m_joined = true;

    if (!startRtc()) {
        m_joined = false;
        std::lock_guard<std::mutex> lg(m_mtx);
        setErrorLocked("RTC init failed");
        return "";
    }

    if (!startAudio()) {
        stopRtc();
        m_joined = false;
        std::lock_guard<std::mutex> lg(m_mtx);
        setErrorLocked("Audio capture start failed");
        return "";
    }

    return invite;
}

bool cConferenceNet::joinByInvite(const std::string& invite, const std::string& peerKeyBase)
{
    if (!m_ws.isConnected()) return false;
    if (invite.empty()) return false;
    if (!ensurePeerKey(peerKeyBase)) return false;

    {
        std::lock_guard<std::mutex> lg(m_mtx);
        m_lastError.clear();
        m_gotJoin = false;
        m_joinOk = false;
        m_confId = -1;
    }

    json j{
        {"type","conf_join"},
        {"invite", invite},
        {"peer", m_peerKey}
    };
    m_ws.sendText(j.dump());

    {
        std::unique_lock<std::mutex> ul(m_mtx);
        m_cv.wait_for(ul, m_waitTimeout, [&] { return m_gotJoin; });
    }

    bool ok = false;
    {
        std::lock_guard<std::mutex> lg(m_mtx);
        ok = m_joinOk;
        if (!ok) {
            if (!m_ws.isConnected()) setErrorLocked("WS disconnected while waiting conf_joined");
            else setErrorLocked("Join rejected or timeout");
        }
    }
    if (!ok) return false;

    m_joined = true;

    if (!startRtc()) {
        m_joined = false;
        std::lock_guard<std::mutex> lg(m_mtx);
        setErrorLocked("RTC init failed");
        return false;
    }
    if (!startAudio()) {
        stopRtc();
        m_joined = false;
        std::lock_guard<std::mutex> lg(m_mtx);
        setErrorLocked("Audio capture start failed");
        return false;
    }

    return true;
}

void cConferenceNet::leave()
{
    if (!m_joined.exchange(false)) {
        stopAudio();
        stopRtc();
        m_confId = -1;
        return;
    }

    if (!m_peerKey.empty() && m_ws.isConnected()) {
        json j{
            {"type","conf_leave"},
            {"peer", m_peerKey}
        };
        m_ws.sendText(j.dump());
    }

    stopAudio();
    stopRtc();
    m_confId = -1;
}

void cConferenceNet::setMicEnabled(bool en)
{
    m_micEnabled.store(en);

    if (!m_peerKey.empty() && m_ws.isConnected()) {
        json j{
            {"type","conf_mic"},
            {"peer", m_peerKey},
            {"enabled", en}
        };
        m_ws.sendText(j.dump());
    }
}

bool cConferenceNet::startRtc()
{
    bool ok = m_rtc.init(m_peerKey, [this](const std::string& sig) {
        m_ws.sendText(sig);
        });
    if (!ok) return false;

    m_rtc.setOnBinary([this](const std::vector<uint8_t>& data) {
        auto pcm = m_opus.decode(data.data(), (int)data.size(), 960);
        if (!pcm.empty())
            m_audio.pushToPlayback(pcm.data(), (int)pcm.size());
        });

    return true;
}

void cConferenceNet::stopRtc()
{
    m_rtc.close();
}

bool cConferenceNet::startAudio()
{
    return m_audio.startCapture([this](const int16_t* pcm, int frames) {
        if (!m_joined.load()) return;
        if (!m_micEnabled.load()) return;
        if (!m_rtc.isDcOpen()) return;

        auto pkt = m_opus.encode(pcm, frames);
        if (!pkt.empty()) m_rtc.sendBinary(pkt);
        });
}

void cConferenceNet::stopAudio()
{
    m_audio.stopCapture();
}

void cConferenceNet::onWsMessage(const std::string& msg)
{
    try {
        json j = json::parse(msg);
        const std::string type = j.value("type", "");

        if (type == "conf_created") {
            {
                std::lock_guard<std::mutex> lg(m_mtx);
                m_lastInvite = j.value("invite", "");
                m_confId = j.value("confId", -1);
                m_gotCreate = true;
            }
            m_cv.notify_all();
            return;
        }

        if (type == "conf_joined") {
            {
                std::lock_guard<std::mutex> lg(m_mtx);
                m_joinOk = j.value("ok", false);
                m_confId = j.value("confId", -1);
                m_gotJoin = true;
            }
            m_cv.notify_all();
            return;
        }

        if (type == "webrtc_answer") {
            if (j.value("peer", "") == m_peerKey)
                m_rtc.onAnswer(j.value("sdp", ""));
            return;
        }

        if (type == "webrtc_ice") {
            if (j.value("peer", "") == m_peerKey)
                m_rtc.onIce(j.value("sdpMid", ""), j.value("candidate", ""));
            return;
        }
    }
    catch (...) {}
}

// ---------------- ASYNC ----------------

void cConferenceNet::createConferenceAsync(const std::string& title, const std::string& peerKeyBase)
{
    if (m_busy.exchange(true)) return;

    {
        std::lock_guard<std::mutex> lg(m_mtx);
        m_lastError.clear();
    }

    m_createFuture = std::async(std::launch::async, [this, title, peerKeyBase]() -> std::string {
        auto invite = this->createConference(title, peerKeyBase);
        return invite; // lastError уже выставляется внутри createConference
        });
}

void cConferenceNet::joinByInviteAsync(const std::string& invite, const std::string& peerKeyBase)
{
    if (m_busy.exchange(true)) return;

    {
        std::lock_guard<std::mutex> lg(m_mtx);
        m_lastError.clear();
    }

    m_joinFuture = std::async(std::launch::async, [this, invite, peerKeyBase]() -> bool {
        auto ok = this->joinByInvite(invite, peerKeyBase);
        return ok; // lastError выставляется внутри joinByInvite
        });
}

bool cConferenceNet::pollCreateResult(std::string& outInvite)
{
    if (!m_busy.load()) return false;
    if (!m_createFuture.valid()) { m_busy = false; return true; }

    auto st = m_createFuture.wait_for(std::chrono::milliseconds(0));
    if (st != std::future_status::ready) return false;

    outInvite = m_createFuture.get();
    m_busy = false;
    return true;
}

bool cConferenceNet::pollJoinResult(bool& outOk)
{
    if (!m_busy.load()) return false;
    if (!m_joinFuture.valid()) { m_busy = false; outOk = false; return true; }

    auto st = m_joinFuture.wait_for(std::chrono::milliseconds(0));
    if (st != std::future_status::ready) return false;

    outOk = m_joinFuture.get();
    m_busy = false;
    return true;
}
