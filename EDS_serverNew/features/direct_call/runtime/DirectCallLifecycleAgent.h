#pragma once

#include "features/direct_call/runtime/DirectCallStateStore.h"
#include "modules/BaseAgent.h"

#include <memory>

namespace eds::server_new::features::direct_call {

    class DirectCallLifecycleAgent final : public BaseAgent {
    public:
        explicit DirectCallLifecycleAgent(std::shared_ptr<DirectCallStateStore> stateStore);

    private:
        std::shared_ptr<DirectCallStateStore> stateStore_;
    };

} // namespace eds::server_new::features::direct_call