#include "features/runtime/FeatureModuleRegistration.h"

#include "features/conference/runtime/ConferenceFeatureModule.h"
#include "features/mediasoup/runtime/MediasoupFeatureModule.h"
#include "features/runtime/FeatureRegistry.h"

#include <memory>

namespace eds::server_new::features::runtime {

void registerBuiltInFeatureModules(FeatureRegistry& registry) {
    registry.addFactory([]() {
        return std::make_unique<eds::server_new::features::conference::ConferenceFeatureModule>();
    });
    registry.addFactory([]() {
        return std::make_unique<eds::server_new::features::mediasoup::MediasoupFeatureModule>();
    });
}

} // namespace eds::server_new::features::runtime
