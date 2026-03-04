#pragma once

#include "contracts/IMessage.h"
#include "contracts/IModule.h"
#include "contracts/Primitives.h"
#include <string_view>

namespace core::contracts {

class IAction : public IModule {
public:
    virtual ~IAction() = default;
    virtual std::string_view actionKey() const = 0;
    virtual OperationStatus execute(const IMessage& message) = 0;
};

} // namespace core::contracts
