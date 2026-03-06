#pragma once

#include "Bridge/Mediasoup/runtime/MediasoupStateStore.h"
#include "modules/BaseAgent.h"

#include <memory>

namespace eds::server_new::mediasoup {

class MediasoupSignalingAgent final : public BaseAgent {
public:
    explicit MediasoupSignalingAgent(std::shared_ptr<MediasoupStateStore> stateStore);

private:
    std::shared_ptr<MediasoupStateStore> stateStore_;
};

} // namespace eds::server_new::mediasoup
