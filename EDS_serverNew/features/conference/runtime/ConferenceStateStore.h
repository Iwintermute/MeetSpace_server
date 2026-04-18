#pragma once

#include "Auth/SessionAuthStore.h"
#include "contracts/Primitives.h"
#include "features/conference/runtime/ConferenceCommand.h"
#include "infrastructure/db/MessengerRepository.h"

#include <memory>

namespace eds::server_new::features::conference {

    class ConferenceStateStore final {
    public:
        ConferenceStateStore(
            std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore,
            std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository);

        core::contracts::OperationStatus createConference(const ConferenceCommand& command);
        core::contracts::OperationStatus getConference(const ConferenceCommand& command);
        core::contracts::OperationStatus closeConference(const ConferenceCommand& command);
        core::contracts::OperationStatus joinConference(const ConferenceCommand& command);
        core::contracts::OperationStatus leaveConference(const ConferenceCommand& command);
        core::contracts::OperationStatus listMembers(const ConferenceCommand& command);

    private:
        std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore_;
        std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository_;
    };

} // namespace eds::server_new::features::conference