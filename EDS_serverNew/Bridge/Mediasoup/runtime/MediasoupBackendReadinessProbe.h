#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace eds::server_new::mediasoup::runtime {

struct MediasoupBackendProbeResult {
    bool ok = false;
    std::string message;
    std::string engine;
    std::string version;
};

class MediasoupBackendReadinessProbe {
public:
    static MediasoupBackendProbeResult probe(
        std::string_view backendUrl,
        std::chrono::milliseconds timeout);
};

} // namespace eds::server_new::mediasoup::runtime
