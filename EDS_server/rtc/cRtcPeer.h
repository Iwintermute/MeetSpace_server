#pragma once
#include <rtc/rtc.hpp>
#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <mutex>

namespace Sys::Rtc {

    class cRtcPeer : public std::enable_shared_from_this<cRtcPeer> {
    public:
        using tOnLocalDescription = std::function<void(const rtc::Description&)>;
        using tOnLocalCandidate = std::function<void(const rtc::Candidate&)>;
        using tOnBinary = std::function<void(const std::vector<uint8_t>&)>;
        using tOnState = std::function<void(rtc::PeerConnection::State)>;

        cRtcPeer();
        ~cRtcPeer();

        void fnSetOnLocalDescription(tOnLocalDescription cb) { m_onLocalDesc = std::move(cb); }
        void fnSetOnLocalCandidate(tOnLocalCandidate cb) { m_onLocalCand = std::move(cb); }
        void fnSetOnBinary(tOnBinary cb) { m_onBinary = std::move(cb); }
        void fnSetOnState(tOnState cb) { m_onState = std::move(cb); }

        // signaling
        void fnApplyRemoteOffer(const std::string& sdpOffer);
        void fnApplyRemoteIce(const std::string& mid, const std::string& cand);

        // data
        void fnSendBinary(const std::vector<uint8_t>& data);

        // lifecycle
        void fnClose();
        bool fnIsReady() const;

    private:
        void fnBindDataChannel(std::shared_ptr<rtc::DataChannel> dc);
        void fnEnsureDataChannel(); // если клиент не создаст DC — создадим сами

    private:
        std::shared_ptr<rtc::PeerConnection> m_pc;
        std::shared_ptr<rtc::DataChannel>    m_dc;

        tOnLocalDescription m_onLocalDesc;
        tOnLocalCandidate   m_onLocalCand;
        tOnBinary           m_onBinary;
        tOnState            m_onState;

        mutable std::mutex m_mtx;
        bool m_closed{ false };
        bool m_dcEnsured{ false };
    };

} // namespace Sys::Rtc