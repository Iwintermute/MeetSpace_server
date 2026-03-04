#pragma once

#include "contracts/IAction.h"
#include "contracts/IMessage.h"
#include "contracts/IModule.h"
#include "contracts/Primitives.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace core::contracts {

class IAgent : public IModule {
public:
    using ActionFactory = std::function<std::unique_ptr<IAction>()>;

    virtual ~IAgent() = default;

    virtual OperationStatus registerAction(std::string actionType, ActionFactory factory) = 0;
    virtual OperationStatus unregisterAction(std::string_view actionType) = 0;
    virtual OperationStatus handle(const IMessage& message) = 0;
};

} // namespace core::contracts
