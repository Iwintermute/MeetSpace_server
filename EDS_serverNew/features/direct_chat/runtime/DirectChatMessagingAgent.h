#pragma once

#include "features/direct_chat/runtime/DirectChatStateStore.h"
#include "modules/BaseAgent.h"

#include <memory>

namespace eds::server_new::features::direct_chat {

    class DirectChatMessagingAgent final : public BaseAgent {
    public:
        explicit DirectChatMessagingAgent(std::shared_ptr<DirectChatStateStore> stateStore);

    private:
        std::shared_ptr<DirectChatStateStore> stateStore_;
    };

} // namespace eds::server_new::features::direct_chat