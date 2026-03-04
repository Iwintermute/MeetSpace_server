#pragma once

#include "contracts/IAgent.h"
#include "contracts/IMessage.h"
#include "contracts/IModule.h"
#include "contracts/Primitives.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace core::contracts {

class IFeatureManager : public IModule {
public:
    using AgentFactory = std::function<std::unique_ptr<IAgent>()>;

    virtual ~IFeatureManager() = default;

    virtual OperationStatus registerAgent(std::string agentType, AgentFactory factory) = 0;
    virtual OperationStatus unregisterAgent(std::string_view agentType) = 0;
    virtual OperationStatus handle(const IMessage& message) = 0;
};

} // namespace core::contracts
