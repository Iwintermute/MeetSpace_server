#pragma once

#include <rtc/rtc.hpp>
#include <memory>
#include <string>
#include <functional>
#include "../../networkEDS/include/nlohmann/json.hpp"

namespace Sys {
    namespace Rtc {

        class cRtcPeer : public std::enable_shared_from_this<cRtcPeer>
        {
        public:
            using tOnLocalDescription = std::function<void(const std::string&)>;
            using tOnLocalCandidate = std::function<void(const std::string&, const std::string&)>;
            using tOnMessage = std::function<void(const std::string&)>;
            using tOnStateChanged = std::function<void(const rtc::PeerConnection::State)>;

            cRtcPeer();
            ~cRtcPeer();

            void fnSetOnLocalDescription(tOnLocalDescription fn) { m_fnOnLocalDescription = std::move(fn); }
            void fnSetOnLocalCandidate(tOnLocalCandidate fn) { m_fnOnLocalCandidate = std::move(fn); }
            void fnSetOnMessage(tOnMessage fn) { m_fnOnMessage = std::move(fn); }
            void fnSetOnStateChanged(tOnStateChanged fn) { m_fnOnStateChanged = std::move(fn); }

            void fnCreateOffer();
            void fnSetRemoteDescription(const std::string& sSdp, const std::string& sType);
            void fnAddRemoteCandidate(const std::string& sMid, const std::string& sCandidate);
            void fnSend(const std::string& sMsg);

            // For cRtcManager compatibility
            bool fnInit();
            bool fnHandleOffer(const std::string& sOffer, std::string& sAnswer);
            void fnAddRemoteIce(const std::string& sMid, const std::string& sCandidate);
            void fnClose();

        private:
            std::shared_ptr<rtc::PeerConnection> m_pPc;
            std::shared_ptr<rtc::DataChannel>    m_pDc;

            tOnLocalDescription m_fnOnLocalDescription;
            tOnLocalCandidate   m_fnOnLocalCandidate;
            tOnMessage          m_fnOnMessage;
            tOnStateChanged     m_fnOnStateChanged;
        };

    } // namespace Rtc
} // namespace Sys
