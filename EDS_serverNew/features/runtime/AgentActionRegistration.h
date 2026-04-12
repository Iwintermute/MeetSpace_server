#pragma once

#include "interfaces/iAgent.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace eds::server_new::features::runtime {

inline void registerActionOrThrow(iAgent& agent, std::string actionKey, iAgent::tActionFactory factory) {
    auto status = agent.registerAction(std::move(actionKey), std::move(factory));
    if (!status.ok) {
        throw std::runtime_error(status.message);
    }
}

} // namespace eds::server_new::features::runtime
