#include "cMediaPipeline.h"
#include <iostream>
#include "../rtc/cRtcPeer.h"
namespace Sys {
    namespace Media {

        cMediaPipeline::cMediaPipeline(std::shared_ptr<Rtc::cRtcManager> rtcMgr)
            : m_rtcMgr(rtcMgr) {
        }

        cMediaPipeline::~cMediaPipeline() {}

        void cMediaPipeline::fnAddPeer(const std::string& peerId) {
            m_peerDecoders[peerId] = std::make_shared<cMediaOpusDecoder>();
        }

        void cMediaPipeline::fnRemovePeer(const std::string& peerId) {
            m_peerDecoders.erase(peerId);
        }

        void cMediaPipeline::fnBroadcastAudioFrame(const std::vector<int16_t>& frame) {
            auto encoded = m_encoder.fnEncode(frame);
            for (auto& [peerId, decoder] : m_peerDecoders) {
                auto peer = m_rtcMgr->fnCreatePeer(peerId, nullptr);
                if (peer) peer->fnSend(std::string(encoded.begin(), encoded.end()));
            }
        }


    } // namespace Media
} // namespace Sys
