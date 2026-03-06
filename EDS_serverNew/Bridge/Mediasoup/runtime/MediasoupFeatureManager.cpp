#include "Bridge/Mediasoup/runtime/MediasoupFeatureManager.h"

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "Bridge/Mediasoup/runtime/MediasoupSignalingAgent.h"

#include <stdexcept>
#include <utility>

namespace eds::server_new::mediasoup {

MediasoupFeatureManager::MediasoupFeatureManager(std::shared_ptr<MediasoupStateStore> stateStore)
    : BaseFeatureManager("MediasoupFeatureManager", static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
    if (!stateStore_) {
        throw std::invalid_argument("MediasoupFeatureManager requires a state store.");
    }

    auto status = registerAgent(std::string(kDefaultAgent), [stateStore = stateStore_]() {
        return std::make_unique<MediasoupSignalingAgent>(stateStore);
    });

    if (!status.ok) {
        throw std::runtime_error(status.message);
    }
}

} // namespace eds::server_new::mediasoup
