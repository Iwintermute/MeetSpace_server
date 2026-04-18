#include "Bridge/Mediasoup/service/SharedMediaTransportService.h"
#include "Bridge/Mediasoup/service/MediasoupTransportService.h"

#include <mutex>

namespace eds::server_new::mediasoup::service {
    namespace {
        std::mutex gMutex;
        std::weak_ptr<IMediaTransportService> gShared;
    }

    std::shared_ptr<IMediaTransportService> sharedMediaTransportService(bool debugMode) {
        std::lock_guard<std::mutex> lock(gMutex);

        auto existing = gShared.lock();
        if (existing) {
            return existing;
        }

        auto created = std::make_shared<MediasoupTransportService>(nullptr, debugMode);
        gShared = created;
        return created;
    }

} // namespace eds::server_new::mediasoup::service