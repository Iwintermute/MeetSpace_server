#pragma once

#include "Bridge/Mediasoup/service/IMediaTransportService.h"

#include <memory>

namespace eds::server_new::mediasoup::service {

	std::shared_ptr<IMediaTransportService> sharedMediaTransportService(bool debugMode = false);

} // namespace eds::server_new::mediasoup::service