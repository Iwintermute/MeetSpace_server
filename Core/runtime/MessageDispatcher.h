#pragma once

#include "contracts/IMessage.h"
#include "contracts/Primitives.h"
#include "contracts/TypedMessage.h"
#include "interfaces/iAgentManager.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace core::runtime {

// Обёртка маршрутизации, передаваемая в feature manager.
class RoutedMessageEnvelope final : public core::contracts::IMessage {
public:
    RoutedMessageEnvelope(core::contracts::MessageRoute route, std::shared_ptr<const core::contracts::IMessage> payload)
        : route_(std::move(route)),
          payload_(std::move(payload)) {
    }

    const core::contracts::MessageRoute& route() const {
        return route_;
    }

    const core::contracts::IMessage& payload() const {
        return *payload_;
    }

private:
    core::contracts::MessageRoute route_;
    std::shared_ptr<const core::contracts::IMessage> payload_;
};

// Потокобезопасный диспетчер сообщений: object -> manager.
class MessageDispatcher {
public:
    core::contracts::OperationStatus registerManager(std::string objectType, std::shared_ptr<iAgentManager> manager) {
        if (objectType.empty()) {
            return core::contracts::OperationStatus::failure("Object type must not be empty.");
        }
        if (!manager) {
            return core::contracts::OperationStatus::failure("Manager must not be null.");
        }

        std::lock_guard<std::mutex> lock(mutex_);
        managers_[std::move(objectType)] = std::move(manager);
        return core::contracts::OperationStatus::success();
    }

    core::contracts::OperationStatus unregisterManager(std::string_view objectType) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto removed = managers_.erase(std::string(objectType));
        if (removed == 0) {
            return core::contracts::OperationStatus::failure("Manager not found for object: " + std::string(objectType));
        }
        return core::contracts::OperationStatus::success();
    }

    core::contracts::OperationStatus dispatch(const core::contracts::MessageRoute& route, const core::contracts::IMessage& message) {
        std::shared_ptr<iAgentManager> manager;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = managers_.find(route.object);
            if (it == managers_.end()) {
                return core::contracts::OperationStatus::failure("No manager for object: " + route.object);
            }

            manager = it->second.lock();
            if (!manager) {
                return core::contracts::OperationStatus::failure("Manager expired for object: " + route.object);
            }
        }

        auto payload = std::shared_ptr<const core::contracts::IMessage>(&message, [](const core::contracts::IMessage*) {});
        RoutedMessageEnvelope envelope(route, std::move(payload));
        return manager->handleMessage(envelope);
    }

private:
    std::unordered_map<std::string, std::weak_ptr<iAgentManager>> managers_;
    std::mutex mutex_;
};

} // namespace core::runtime
