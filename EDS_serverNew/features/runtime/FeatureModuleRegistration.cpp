#include "features/runtime/FeatureModuleRegistration.h"
#include "Auth/runtime/AuthFeatureModule.h"
#include "Auth/runtime/AuthServices.h"
#include "features/chat/runtime/ChatFeatureModule.h"
#include "features/conference/runtime/ConferenceFeatureModule.h"
#include "features/direct_call/runtime/DirectCallFeatureModule.h"
#include "features/direct_chat/runtime/DirectChatFeatureModule.h"
#include "features/mediasoup/runtime/MediasoupFeatureModule.h"
#include "features/runtime/FeatureRegistry.h"

#include <memory>

namespace eds::server_new::features::runtime {

    void registerBuiltInFeatureModules(FeatureRegistry& registry) {
        registry.addFactory([]() {
            return std::make_unique<eds::server_new::features::auth::AuthFeatureModule>(
                eds::server_new::auth::AuthServices::sessionStore(),
                eds::server_new::auth::AuthServices::verifier());
            });
        registry.addFactory([]() {
            return std::make_unique<eds::server_new::features::conference::ConferenceFeatureModule>();
            });
        registry.addFactory([]() {
            return std::make_unique<eds::server_new::features::chat::ChatFeatureModule>();
            });
        registry.addFactory([]() {
            return std::make_unique<eds::server_new::features::direct_chat::DirectChatFeatureModule>();
            });
        registry.addFactory([]() {
            return std::make_unique<eds::server_new::features::direct_call::DirectCallFeatureModule>();
            });
        registry.addFactory([]() {
            return std::make_unique<eds::server_new::features::mediasoup::MediasoupFeatureModule>();
            });
    }

} // namespace eds::server_new::features::runtime