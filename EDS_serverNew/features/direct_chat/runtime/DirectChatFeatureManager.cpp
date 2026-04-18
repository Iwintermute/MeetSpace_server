#include "features/direct_chat/runtime/DirectChatFeatureManager.h"

#include "features/direct_chat/runtime/DirectChatCommand.h"
#include "features/direct_chat/runtime/DirectChatMessagingAgent.h"

#include <stdexcept>
#include <utility>

namespace eds::server_new::features::direct_chat {

    DirectChatFeatureManager::DirectChatFeatureManager(std::shared_ptr<DirectChatStateStore> stateStore)
        : BaseFeatureManager("DirectChatFeatureManager", static_cast<ModuleId>(-1)),
        stateStore_(std::move(stateStore)) {
        if (!stateStore_) {
            throw std::invalid_argument("DirectChatFeatureManager requires a state store.");
        }

        auto messagingStatus = registerAgent(std::string(kDirectChatMessagingAgent), [stateStore = stateStore_]() {
            return std::make_unique<DirectChatMessagingAgent>(stateStore);
            });
        if (!messagingStatus.ok) {
            throw std::runtime_error(messagingStatus.message);
        }
    }

} // namespace eds::server_new::features::direct_chat