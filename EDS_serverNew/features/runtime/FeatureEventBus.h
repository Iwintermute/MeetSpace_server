#pragma once

#include <boost/signals2.hpp>

#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace eds::server_new::features::runtime {

class FeatureEventBus {
public:
    FeatureEventBus(const FeatureEventBus&) = delete;
    FeatureEventBus& operator=(const FeatureEventBus&) = delete;
    FeatureEventBus(FeatureEventBus&&) = delete;
    FeatureEventBus& operator=(FeatureEventBus&&) = delete;

    static std::shared_ptr<FeatureEventBus> instance() {
        static std::shared_ptr<FeatureEventBus> bus(new FeatureEventBus);
        return bus;
    }

    template<typename Event>
    boost::signals2::connection subscribe(typename boost::signals2::signal<void(const Event&)>::slot_type slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto signal = getSignal<Event>();
        return signal->connect(std::move(slot));
    }

    template<typename Event>
    void publish(const Event& event) {
        std::shared_ptr<boost::signals2::signal<void(const Event&)>> signalCopy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = signals_.find(typeid(Event));
            if (it != signals_.end()) {
                auto holder = std::dynamic_pointer_cast<SignalHolder<Event>>(it->second);
                if (holder) {
                    signalCopy = holder->signal;
                }
            }
        }

        if (signalCopy) {
            (*signalCopy)(event);
        }
    }

private:
    FeatureEventBus() = default;

    struct SignalHolderBase {
        virtual ~SignalHolderBase() = default;
    };

    template<typename Event>
    struct SignalHolder final : SignalHolderBase {
        std::shared_ptr<boost::signals2::signal<void(const Event&)>> signal =
            std::make_shared<boost::signals2::signal<void(const Event&)>>();
    };

    template<typename Event>
    std::shared_ptr<boost::signals2::signal<void(const Event&)>> getSignal() {
        auto it = signals_.find(typeid(Event));
        if (it != signals_.end()) {
            auto holder = std::dynamic_pointer_cast<SignalHolder<Event>>(it->second);
            if (holder) {
                return holder->signal;
            }
        }

        auto holder = std::make_shared<SignalHolder<Event>>();
        signals_[typeid(Event)] = holder;
        return holder->signal;
    }

private:
    std::unordered_map<std::type_index, std::shared_ptr<SignalHolderBase>> signals_;
    std::mutex mutex_;
};

} // namespace eds::server_new::features::runtime
