#pragma once

#include "contracts/IModule.h"
#include "contracts/Primitives.h"
#include <memory>
#include <vector>

namespace core::contracts {

class IModuleRegistry {
public:
    virtual ~IModuleRegistry() = default;

    virtual ModuleId registerModule(std::unique_ptr<IModule> module) = 0;
    virtual std::vector<ModuleId> registerModules(std::vector<std::unique_ptr<IModule>> modules) = 0;

    virtual IModule* getModule(ModuleId id) = 0;
    virtual const IModule* getModule(ModuleId id) const = 0;

    virtual bool initializeAll() = 0;
    virtual void shutdownAll() = 0;

    virtual std::vector<ModuleSnapshot> snapshots() const = 0;
};

} // namespace core::contracts
