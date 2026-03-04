#pragma once

#include "contracts/IMessage.h"
#include "EDS_server/App/wsFormat.h"
#include <utility>

namespace core::adapters {

class LegacyWsMessageAdapter final : public core::contracts::IMessage {
public:
    explicit LegacyWsMessageAdapter(StandardWsMessage message)
        : message_(std::move(message)) {
    }

    const StandardWsMessage& message() const {
        return message_;
    }

private:
    StandardWsMessage message_;
};

} // namespace core::adapters
