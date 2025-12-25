#include "cRtcPeer.h"
#include <iostream>

using namespace Sys::Rtc;

cRtcPeer::cRtcPeer() {
    rtc::InitLogger(rtc::LogLevel::Info);

    rtc::Configuration cfg;
    cfg.iceServers.emplace_back("stun:stun.l.google.com:19302");

    m_pPc = std::make_shared<rtc::PeerConnection>(cfg);

    // peer callbacks
    m_pPc->onStateChange([this](rtc::PeerConnection::State eState) {
        if (m_fnOnStateChanged) m_fnOnStateChanged(eState);
        });

    m_pPc->onLocalDescription([this](rtc::Description const& desc) {
        if (!m_fnOnLocalDescription) return;
        nlohmann::json j;
        j["type"] = desc.typeString();
        j["sdp"] = std::string(desc);
        m_fnOnLocalDescription(j.dump());
        });

    m_pPc->onLocalCandidate([this](rtc::Candidate const& cand) {
        if (!m_fnOnLocalCandidate) return;
        m_fnOnLocalCandidate(cand.mid(), cand.candidate());
        });

    // data channel
    m_pPc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
        m_pDc = dc;
        dc->onOpen([this]() { std::cout << "[RTC] DataChannel open\n"; });
        dc->onClosed([this]() { std::cout << "[RTC] DataChannel closed\n"; });
        dc->onMessage([this](rtc::message_variant msg) {
            if (!m_fnOnMessage) return;
            if (std::holds_alternative<std::string>(msg))
                m_fnOnMessage(std::get<std::string>(msg));
            });
        });

    m_pDc = m_pPc->createDataChannel("data");
    m_pDc->onOpen([] { std::cout << "[RTC] local datachannel open\n"; });
    m_pDc->onMessage([this](auto msg) {
        if (!m_fnOnMessage) return;
        if (std::holds_alternative<std::string>(msg))
            m_fnOnMessage(std::get<std::string>(msg));
        });
}

cRtcPeer::~cRtcPeer() { fnClose(); }

void cRtcPeer::fnCreateOffer() { m_pPc->setLocalDescription(); }
void cRtcPeer::fnSetRemoteDescription(const std::string& sSdp, const std::string& sType) {
    rtc::Description desc(sSdp, sType);
    m_pPc->setRemoteDescription(desc);
}
void cRtcPeer::fnAddRemoteCandidate(const std::string& sMid, const std::string& sCandidate) {
    rtc::Candidate cand(sCandidate, sMid);
    m_pPc->addRemoteCandidate(cand);
}
void cRtcPeer::fnSend(const std::string& sMsg) {
    if (m_pDc && m_pDc->isOpen()) m_pDc->send(sMsg);
}

bool cRtcPeer::fnInit() { return m_pPc != nullptr; }

bool cRtcPeer::fnHandleOffer(const std::string& sOffer, std::string& sAnswer) {
    fnSetRemoteDescription(sOffer, "offer");
    m_pPc->setLocalDescription();
    if (m_pPc->localDescription()) {
        sAnswer = std::string(*m_pPc->localDescription());
        return true;
    }
    return false;
}

void cRtcPeer::fnAddRemoteIce(const std::string& sMid, const std::string& sCandidate) {
    fnAddRemoteCandidate(sMid, sCandidate);
}

void cRtcPeer::fnClose() {
    if (m_pDc) m_pDc->close();
    if (m_pPc) m_pPc->close();
}
