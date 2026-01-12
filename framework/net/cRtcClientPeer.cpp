#include "cRtcClientPeer.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool cRtcClientPeer::init(const std::string& peerKey, tSendSignal sendSignal)
{
    m_peerKey = peerKey;
    m_sendSignal = std::move(sendSignal);

    rtc::InitLogger(rtc::LogLevel::Info);

    rtc::Configuration cfg;
    cfg.iceServers.emplace_back("stun:stun.l.google.com:19302");

    m_pc = std::make_shared<rtc::PeerConnection>(cfg);

    m_pc->onLocalDescription([this](rtc::Description const& desc)
        {
            json j{
                {"type","webrtc_offer"},
                {"peer", m_peerKey},
                {"sdp", std::string(desc)}
            };
            if (m_sendSignal) m_sendSignal(j.dump());
        });

    m_pc->onLocalCandidate([this](rtc::Candidate const& cand)
        {
            json j{
                {"type","webrtc_ice"},
                {"peer", m_peerKey},
                {"sdpMid", cand.mid()},
                {"candidate", cand.candidate()}
            };
            if (m_sendSignal) m_sendSignal(j.dump());
        });

    m_dc = m_pc->createDataChannel("audio");
    m_dc->onOpen([] { /* ok */ });

    m_dc->onMessage([this](rtc::message_variant msg)
        {
            if (!m_onBinary) return;

            if (std::holds_alternative<rtc::binary>(msg)) {
                const auto& b = std::get<rtc::binary>(msg);
                m_onBinary(std::vector<uint8_t>(b.begin(), b.end()));
            }
        });

    // trigger offer
    m_pc->setLocalDescription();
    return true;
}

void cRtcClientPeer::onAnswer(const std::string& sdp)
{
    if (!m_pc) return;
    rtc::Description desc(sdp, "answer");
    m_pc->setRemoteDescription(desc);
}

void cRtcClientPeer::onIce(const std::string& mid, const std::string& cand)
{
    if (!m_pc) return;
    m_pc->addRemoteCandidate(rtc::Candidate(cand, mid));
}

void cRtcClientPeer::sendBinary(const std::vector<uint8_t>& data)
{
    if (!m_dc || !m_dc->isOpen()) return;
    rtc::binary b(data.begin(), data.end());
    m_dc->send(b);
}

void cRtcClientPeer::close()
{
    if (m_dc) m_dc->close();
    if (m_pc) m_pc->close();
}
