#pragma once

#include "features/conference/runtime/ConferenceStateStore.h"
#include "modules/BaseFeatureManager.h"

#include <memory>

namespace eds::server_new::features::conference {

class ConferenceFeatureManager final : public BaseFeatureManager {
public:
    explicit ConferenceFeatureManager(std::shared_ptr<ConferenceStateStore> stateStore);

private:
    std::shared_ptr<ConferenceStateStore> stateStore_;
};

} // namespace eds::server_new::features::conference
