#pragma once

#include "Auth/SessionAuthStore.h"
#include "contracts/Primitives.h"
#include "features/direct_call/runtime/DirectCallCommand.h"
#include "infrastructure/db/MessengerRepository.h"

#include <memory>

namespace eds::server_new::features::direct_call {

    class DirectCallStateStore final {
    public:
        DirectCallStateStore(
            std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore,
            std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository);

        core::contracts::OperationStatus createCall(const DirectCallCommand& command);
        core::contracts::OperationStatus acceptCall(const DirectCallCommand& command);
        core::contracts::OperationStatus declineCall(const DirectCallCommand& command);
        core::contracts::OperationStatus hangupCall(const DirectCallCommand& command);

    private:
        std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore_;
        std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository_;
    };

} // namespace eds::server_new::features::direct_call