#pragma once

#include "contracts/Primitives.h"
#include <functional>
#include <memory>
#include <typeindex>
#include <utility>

namespace core::contracts {

class IEventBus {
public:
    using RawEventPtr = std::shared_ptr<const void>;
    using RawHandler = std::function<void(const RawEventPtr&)>;

    virtual ~IEventBus() = default;

    virtual SubscriptionId subscribe(std::type_index eventType, RawHandler handler) = 0;
    virtual void unsubscribe(SubscriptionId subscriptionId) = 0;
    virtual void publish(std::type_index eventType, RawEventPtr eventData) = 0;

    template<typename TEvent>
    SubscriptionId subscribe(std::function<void(const TEvent&)> handler) {
        return subscribe(
            std::type_index(typeid(TEvent)),
            [h = std::move(handler)](const RawEventPtr& payload) {
                h(*std::static_pointer_cast<const TEvent>(payload));
            }
        );
    }

    template<typename TEvent>
    void publish(TEvent event) {
        publish(
            std::type_index(typeid(TEvent)),
            std::make_shared<TEvent>(std::move(event))
        );
    }
};

} // namespace core::contracts
