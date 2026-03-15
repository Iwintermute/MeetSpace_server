#include "Bridge/Mediasoup/runtime/MediasoupDebugConfig.h"

#include <atomic>

namespace eds::server_new::mediasoup::debug {
namespace {
std::atomic<bool> gServerDebugEnabled = false;
}

void setServerDebugEnabled(bool enabled) {
    gServerDebugEnabled.store(enabled, std::memory_order_release);
}

bool isServerDebugEnabled() noexcept {
    return gServerDebugEnabled.load(std::memory_order_acquire);
}

} // namespace eds::server_new::mediasoup::debug
