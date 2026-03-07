#include "features/runtime/FeatureRegistry.h"

namespace eds::server_new::features::runtime {

FeatureRegistry& FeatureRegistry::instance() {
    static FeatureRegistry registry;
    return registry;
}

void FeatureRegistry::addFactory(tFactory factory) {
    if (!factory) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    factories_.push_back(std::move(factory));
}

std::vector<std::unique_ptr<IFeatureModule>> FeatureRegistry::instantiateModules() const {
    std::vector<tFactory> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = factories_;
    }

    std::vector<std::unique_ptr<IFeatureModule>> modules;
    modules.reserve(snapshot.size());
    for (const auto& factory : snapshot) {
        if (!factory) {
            continue;
        }
        auto module = factory();
        if (module) {
            modules.push_back(std::move(module));
        }
    }
    return modules;
}

} // namespace eds::server_new::features::runtime
