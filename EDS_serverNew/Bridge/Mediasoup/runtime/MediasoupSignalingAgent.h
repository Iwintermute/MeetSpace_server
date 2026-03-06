#pragma once
#include "Bridge/Mediasoup/runtime/MediasoupRtcBridge.h"

#include "Bridge/Mediasoup/runtime/MediasoupStateStore.h"
#include "modules/BaseAgent.h"

#include <memory>

namespace eds::server_new::mediasoup {

class MediasoupSignalingAgent final : public BaseAgent {
public:
    MediasoupSignalingAgent(
        std::shared_ptr<MediasoupStateStore> stateStore,
        std::shared_ptr<MediasoupRtcBridge> rtcBridge);

private:
    std::shared_ptr<MediasoupStateStore> stateStore_;
    std::shared_ptr<MediasoupRtcBridge> rtcBridge_;
};

} // namespace eds::server_new::mediasoup
