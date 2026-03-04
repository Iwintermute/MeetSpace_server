#pragma once

#include "contracts/IMessage.h"
#include <string>
#include <utility>

namespace core::contracts {

struct MessageRoute {
    std::string object;
    std::string agent;
    std::string action;
};

template<typename TPayload>
class TypedMessage final : public IMessage {
public:
    explicit TypedMessage(TPayload payload)
        : payload_(std::move(payload)) {
    }

    const TPayload& payload() const {
        return payload_;
    }

private:
    TPayload payload_;
};

template<typename TPayload>
class RoutedMessage final : public IMessage {
public:
    RoutedMessage(MessageRoute route, TPayload payload)
        : route_(std::move(route)),
          payload_(std::move(payload)) {
    }

    const MessageRoute& route() const {
        return route_;
    }

    const TPayload& payload() const {
        return payload_;
    }

private:
    MessageRoute route_;
    TPayload payload_;
};

} // namespace core::contracts
