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
            if (!m_rtcMgr) return;

            auto encoded = m_encoder.fnEncode(frame); // vector<uint8_t>
            if (encoded.empty()) return;

            for (auto& [peerId, decoder] : m_peerDecoders) {
                (void)decoder;

                auto peer = m_rtcMgr->fnGetPeer(peerId);   // <-- НЕ создаём новый peer
                if (peer) peer->fnSendBinary(encoded);     // <-- бинарь, не string
            }
        }



    } // namespace Media
} // namespace Sys
