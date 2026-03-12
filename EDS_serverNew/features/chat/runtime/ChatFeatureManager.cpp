#include "features/chat/runtime/ChatFeatureManager.h"

#include "features/chat/runtime/ChatCommand.h"
#include "features/chat/runtime/ChatMessagingAgent.h"

#include <stdexcept>
#include <utility>

namespace eds::server_new::features::chat {

ChatFeatureManager::ChatFeatureManager(std::shared_ptr<ChatStateStore> stateStore)
    : BaseFeatureManager("ChatFeatureManager", static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
    if (!stateStore_) {
        throw std::invalid_argument("ChatFeatureManager requires a state store.");
    }

    auto messagingStatus = registerAgent(std::string(kChatMessagingAgent), [stateStore = stateStore_]() {
        return std::make_unique<ChatMessagingAgent>(stateStore);
    });
    if (!messagingStatus.ok) {
        throw std::runtime_error(messagingStatus.message);
    }
}

} // namespace eds::server_new::features::chat
