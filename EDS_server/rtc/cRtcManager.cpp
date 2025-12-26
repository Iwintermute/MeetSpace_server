#include "cRtcManager.h"
#include "cRtcPeer.h"
#include "../util/cLogger.h"
#include <iostream>

using namespace Sys::Rtc;

cRtcManager::cRtcManager(tSendToClient fnSend)
    : m_fnSendToClient(std::move(fnSend)), m_bInited(false) {
}

cRtcManager::~cRtcManager() { fnShutdown(); }

bool cRtcManager::fnInit() { return m_bInited = true; }

void cRtcManager::fnShutdown() {
    std::lock_guard<std::mutex> lg(m_mtx);
    for (auto& [id, entry] : m_mPeers) entry.pPeer->fnClose();
    m_mPeers.clear();
    m_bInited = false;
}

std::shared_ptr<cRtcPeer> cRtcManager::fnCreatePeer(const std::string& sPeerId, void* pSession) {
    std::lock_guard<std::mutex> lg(m_mtx);
    auto it = m_mPeers.find(sPeerId);
    if (it != m_mPeers.end()) return it->second.pPeer;

    auto pPeer = std::make_shared<cRtcPeer>();

    pPeer->fnSetOnLocalDescription([this, pSession, sPeerId](const std::string& sDescJson) {
        try { json j = json::parse(sDescJson); j["peer"] = sPeerId; fnSendJsonToSession(pSession, j); }
        catch (...) {}
        });

    pPeer->fnSetOnLocalCandidate([this, pSession, sPeerId](const std::string& sMid, const std::string& sCandidate) {
        json j{ {"type","ice"}, {"peer",sPeerId}, {"sdpMid",sMid}, {"candidate",sCandidate} };
        fnSendJsonToSession(pSession, j);
        });

    pPeer->fnSetOnMessage([this, sPeerId](const std::string& sMsg) {
        fnBroadcast(sPeerId, sMsg);
        });

    m_mPeers.emplace(sPeerId, sPeerEntry{ pPeer, pSession });
    return pPeer;
}

void cRtcManager::fnDestroyPeer(const std::string& sPeerId) {
    std::lock_guard<std::mutex> lg(m_mtx);
    auto it = m_mPeers.find(sPeerId);
    if (it != m_mPeers.end()) { it->second.pPeer->fnClose(); m_mPeers.erase(it); }
}

void cRtcManager::fnBroadcast(const std::string& sFromPeer, const std::string& sMsg) {
    std::lock_guard<std::mutex> lg(m_mtx);
    for (auto& [id, entry] : m_mPeers) {
        if (id != sFromPeer) entry.pPeer->fnSend(sMsg);
    }
}

void cRtcManager::fnOnSignalingMessage(void* pSession, const json& jMsg) {
    try {
        std::string type = jMsg.at("type").get<std::string>();
        std::string peerId = jMsg.value("id", jMsg.value("peer", ""));
        if (type == "join") { fnCreatePeer(peerId, pSession); }
        else if (type == "offer") {
            auto pPeer = fnCreatePeer(peerId, pSession);
            std::string sAnswer; if (!pPeer->fnHandleOffer(jMsg.at("sdp"), sAnswer)) return;
            json jAnswer{ {"type","answer"}, {"peer",peerId}, {"sdp",sAnswer} };
            fnSendJsonToSession(pSession, jAnswer);
        }
        else if (type == "answer") {
            auto itPeer = m_mPeers.find(peerId);
            if (itPeer != m_mPeers.end()) {
                itPeer->second.pPeer->fnSetRemoteDescription(jMsg.at("sdp"), "answer");
            }
        }
        else if (type == "ice") { fnCreatePeer(peerId, pSession)->fnAddRemoteIce(jMsg.value("sdpMid", ""), jMsg.at("candidate")); }
        else if (type == "leave") fnDestroyPeer(peerId);
    }
    catch (const std::exception& e) { cLogger::fnLog(cLogger::Level::Error, std::string("[RtcManager] ") + e.what()); }
}

void cRtcManager::fnSendJsonToSession(void* pSession, const json& j) {
    try { if (m_fnSendToClient) m_fnSendToClient(pSession, j.dump()); }
    catch (...) {}
}
