#pragma once
#include <rtc/rtc.hpp>
#include <functional>
#include <string>
#include <vector>

class cRtcClientPeer {
public:
    using tSendSignal = std::function<void(const std::string& jsonText)>;
    using tOnBinary = std::function<void(const std::vector<uint8_t>&)>;

    bool init(const std::string& peerKey, tSendSignal sendSignal);
    void close();

    // signaling from server
    void onAnswer(const std::string& sdp);
    void onIce(const std::string& mid, const std::string& cand);

    // send audio packet
    void sendBinary(const std::vector<uint8_t>& data);

    void setOnBinary(tOnBinary cb) { m_onBinary = std::move(cb); }

private:
    std::string m_peerKey;
    tSendSignal m_sendSignal;
    tOnBinary m_onBinary;

    std::shared_ptr<rtc::PeerConnection> m_pc;
    std::shared_ptr<rtc::DataChannel> m_dc;
};
