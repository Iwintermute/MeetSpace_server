#pragma once

#include "Auth/SessionAuthStore.h"
#include "contracts/Primitives.h"
#include "features/direct_chat/runtime/DirectChatCommand.h"
#include "infrastructure/db/MessengerRepository.h"

#include <memory>

namespace eds::server_new::features::direct_chat {

    class DirectChatStateStore final {
    public:
        DirectChatStateStore(
            std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore,
            std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository);

        core::contracts::OperationStatus sendMessage(const DirectChatCommand& command);
        core::contracts::OperationStatus listThreads(const DirectChatCommand& command);
        core::contracts::OperationStatus syncMessages(const DirectChatCommand& command);
        core::contracts::OperationStatus ackMessages(const DirectChatCommand& command);

    private:
        std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore_;
        std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository_;
    };

} // namespace eds::server_new::features::direct_chat