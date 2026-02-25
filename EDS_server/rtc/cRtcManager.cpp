#include "cRtcManager.h"
#include "cRtcPeer.h"
#include "utils/utils.h"

using namespace Sys::Rtc;

cRtcManager::cRtcManager(tSendToClient fnSend)
    : m_fnSendToClient(std::move(fnSend)) {
}

cRtcManager::~cRtcManager() { fnShutdown(); }

bool cRtcManager::fnInit() { m_inited = true; return true; }

void cRtcManager::fnShutdown()
{
    std::lock_guard<std::mutex> lg(m_mtx);
    for (auto& [k, e] : m_peers) {
        if (e.pPeer) e.pPeer->fnClose();
    }
    m_peers.clear();
    m_sess.clear();
    m_inited = false;
}

std::shared_ptr<cRtcPeer> cRtcManager::fnGetPeer(const std::string& peerKey)
{
    std::lock_guard<std::mutex> lg(m_mtx);
    auto it = m_peers.find(peerKey);
    if (it == m_peers.end()) return nullptr;
    return it->second.pPeer;
}

std::shared_ptr<cRtcPeer> cRtcManager::fnGetOrCreatePeer_Locked(const std::string& peerKey, void* pSession)
{
    auto it = m_peers.find(peerKey);
    if (it != m_peers.end()) {
        // обновляем session
        it->second.pSession = pSession;
        m_sess[pSession].insert(peerKey);
        return it->second.pPeer;
    }

    auto pPeer = std::make_shared<cRtcPeer>();

    // LocalDescription (answer) -> клиенту
    pPeer->fnSetOnLocalDescription([this, peerKey](const rtc::Description& desc) {
        // desc.typeString() будет "answer" после offer
        json j{
            {"type","webrtc_answer"},
            {"peer", peerKey},
            {"sdp", std::string(desc)}
        };

        void* sess = nullptr;
        {
            std::lock_guard<std::mutex> lg(m_mtx);
            auto it = m_peers.find(peerKey);
            if (it != m_peers.end()) sess = it->second.pSession;
        }
        if (sess) fnSendJsonToSession(sess, j);
        });

    // ICE -> клиенту
    pPeer->fnSetOnLocalCandidate([this, peerKey](const rtc::Candidate& cand) {
        json j{
            {"type","webrtc_ice"},
            {"peer", peerKey},
            {"sdpMid", cand.mid()},
            {"candidate", cand.candidate()}
        };

        void* sess = nullptr;
        {
            std::lock_guard<std::mutex> lg(m_mtx);
            auto it = m_peers.find(peerKey);
            if (it != m_peers.end()) sess = it->second.pSession;
        }
        if (sess) fnSendJsonToSession(sess, j);
        });

    // Binary -> наружу (релэй по конфе)
    pPeer->fnSetOnBinary([this, peerKey](const std::vector<uint8_t>& data) {
        if (m_onPeerBinary) m_onPeerBinary(peerKey, data);
        });

    m_peers.emplace(peerKey, sPeerEntry{ pPeer, pSession });
    m_sess[pSession].insert(peerKey);
    return pPeer;
}

void cRtcManager::fnDestroyPeer_Locked(const std::string& peerKey)
{
    auto it = m_peers.find(peerKey);
    if (it == m_peers.end()) return;

    void* sess = it->second.pSession;
    if (it->second.pPeer) it->second.pPeer->fnClose();
    m_peers.erase(it);

    if (sess) {
        auto sit = m_sess.find(sess);
        if (sit != m_sess.end()) {
            sit->second.erase(peerKey);
            if (sit->second.empty()) m_sess.erase(sit);
        }
    }
}

std::vector<std::string> cRtcManager::fnOnWsDisconnected(void* pSession)
{
    std::vector<std::string> removed;

    std::lock_guard<std::mutex> lg(m_mtx);
    auto sit = m_sess.find(pSession);
    if (sit == m_sess.end()) return removed;

    // копируем peerKeys
    for (const auto& peerKey : sit->second) removed.push_back(peerKey);

    // уничтожаем peer-ы
    for (const auto& peerKey : sit->second) {
        fnDestroyPeer_Locked(peerKey);
    }

    // fnDestroyPeer_Locked уже чистит m_sess, но на всякий случай:
    m_sess.erase(pSession);

    return removed;
}

void cRtcManager::fnOnSignalingMessage(void* pSession, const json& jMsg)
{
    try {
        const std::string type = jMsg.at("type").get<std::string>();
        const std::string peerKey = jMsg.value("peer", jMsg.value("id", ""));

        if (peerKey.empty()) return;

        if (type == "webrtc_offer") {
            std::shared_ptr<cRtcPeer> pPeer;
            {
                std::lock_guard<std::mutex> lg(m_mtx);
                pPeer = fnGetOrCreatePeer_Locked(peerKey, pSession);
            }

            const std::string sdp = jMsg.at("sdp").get<std::string>();
            // ВАЖНО: answer отправится async из onLocalDescription
            pPeer->fnApplyRemoteOffer(sdp);
        }
        else if (type == "webrtc_ice") {
            std::shared_ptr<cRtcPeer> pPeer;
            {
                std::lock_guard<std::mutex> lg(m_mtx);
                pPeer = fnGetOrCreatePeer_Locked(peerKey, pSession);
            }

            pPeer->fnApplyRemoteIce(jMsg.value("sdpMid", ""), jMsg.at("candidate").get<std::string>());
        }
        else if (type == "webrtc_close") {
            std::lock_guard<std::mutex> lg(m_mtx);
            fnDestroyPeer_Locked(peerKey);
        }
    }
    catch (const std::exception& e) {
        Sys::cLogger::fnLog(Sys::cLogger::Level::Error, std::string("[RtcManager] ") + e.what());
    }
}

void cRtcManager::fnSendJsonToSession(void* pSession, const json& j)
{
    try {
        if (m_fnSendToClient) m_fnSendToClient(pSession, j.dump());
    }
    catch (...) {}
}
