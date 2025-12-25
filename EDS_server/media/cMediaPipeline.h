#pragma once
#include "../rtc/cRtcManager.h"
#include "cMediaAudioDevice.h"
#include "cMediaOpusEncDec.h"
#include <unordered_map>
#include <memory>

namespace Sys {
    namespace Media {

        class cMediaPipeline {
        public:
            cMediaPipeline(std::shared_ptr<Rtc::cRtcManager> rtcMgr);
            ~cMediaPipeline();

            void fnAddPeer(const std::string& peerId);
            void fnRemovePeer(const std::string& peerId);

            void fnBroadcastAudioFrame(const std::vector<int16_t>& frame);

        private:
            std::shared_ptr<Rtc::cRtcManager> m_rtcMgr;
            std::unordered_map<std::string, std::shared_ptr<cMediaOpusDecoder>> m_peerDecoders;
            cMediaOpusEncoder m_encoder;
        };

    } // namespace Media
} // namespace Sys
