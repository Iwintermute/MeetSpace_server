#pragma once

#include "features/conference/runtime/ConferenceStateStore.h"
#include "modules/BaseAgent.h"

#include <memory>

namespace eds::server_new::features::conference {

class ConferenceLifecycleAgent final : public BaseAgent {
public:
    explicit ConferenceLifecycleAgent(std::shared_ptr<ConferenceStateStore> stateStore);

private:
    std::shared_ptr<ConferenceStateStore> stateStore_;
};

} // namespace eds::server_new::features::conference
