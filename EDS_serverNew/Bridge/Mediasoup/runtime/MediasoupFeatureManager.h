#pragma once

#include "Bridge/Mediasoup/runtime/MediasoupStateStore.h"
#include "modules/BaseFeatureManager.h"

#include <memory>

namespace eds::server_new::mediasoup {

class MediasoupFeatureManager final : public BaseFeatureManager {
public:
    explicit MediasoupFeatureManager(std::shared_ptr<MediasoupStateStore> stateStore);

private:
    std::shared_ptr<MediasoupStateStore> stateStore_;
};

} // namespace eds::server_new::mediasoup
