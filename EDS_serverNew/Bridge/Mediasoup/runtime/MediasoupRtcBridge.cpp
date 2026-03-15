#include "Bridge/Mediasoup/runtime/MediasoupRtcBridge.h"

#include "EDS_server/rtc/cRtcManager.h"
#include "EDS_server/rtc/cRtcPeer.h"

#include <nlohmann/json.hpp>

namespace eds::server_new::mediasoup {

namespace {
using json = nlohmann::json;
}

MediasoupRtcBridge::MediasoupRtcBridge()
    : rtcManager_(std::make_shared<Sys::Rtc::cRtcManager>(
          [this](void* session, const std::string& message) {
              try {
                  const auto payload = json::parse(message);
                  MediasoupSignalingEvent event;
                  event.type = payload.value("type", std::string{});
                  event.peerId = payload.value("peer", std::string{});
                  event.sdp = payload.value("sdp", std::string{});
                  event.sdpMid = payload.value("sdpMid", std::string{});
                  event.candidate = payload.value("candidate", std::string{});

                  enqueueEvent(reinterpret_cast<std::uintptr_t>(session), std::move(event));
              } catch (...) {
              }
          })) {
    if (rtcManager_) {
        rtcManager_->fnInit();
        rtcManager_->fnSetOnPeerBinary([this](const std::string& peerId, const std::vector<uint8_t>& data) {
            tOnPeerBinary callback;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                callback = onPeerBinary_;
            }
            if (callback) {
                callback(peerId, data);
            }
        });
    }
}

MediasoupRtcBridge::~MediasoupRtcBridge() {
    if (rtcManager_) {
        rtcManager_->fnShutdown();
    }
}

core::contracts::OperationStatus MediasoupRtcBridge::handleOffer(const MediasoupCommand& command) {
    const auto sessionStatus = validateSessionBoundCommand(command);
    if (!sessionStatus.ok) {
        return sessionStatus;
    }
    if (command.sdp.empty()) {
        return core::contracts::OperationStatus::failure("sdp must not be empty for webrtc_offer.");
    }

    rememberSessionMapping(command.peerId, command.sessionHandle);

    json message{
        { "type", "webrtc_offer" },
        { "peer", command.peerId },
        { "sdp", command.sdp }
    };
    rtcManager_->fnOnSignalingMessage(reinterpret_cast<void*>(command.sessionHandle), message);
    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus MediasoupRtcBridge::handleIce(const MediasoupCommand& command) {
    const auto sessionStatus = validateSessionBoundCommand(command);
    if (!sessionStatus.ok) {
        return sessionStatus;
    }
    if (command.candidate.empty()) {
        return core::contracts::OperationStatus::failure("candidate must not be empty for webrtc_ice.");
    }

    rememberSessionMapping(command.peerId, command.sessionHandle);

    json message{
        { "type", "webrtc_ice" },
        { "peer", command.peerId },
        { "sdpMid", command.sdpMid },
        { "candidate", command.candidate }
    };
    rtcManager_->fnOnSignalingMessage(reinterpret_cast<void*>(command.sessionHandle), message);
    return core::contracts::OperationStatus::success();
}

core::contracts::OperationStatus MediasoupRtcBridge::handleClose(const MediasoupCommand& command) {
    const auto sessionStatus = validateSessionBoundCommand(command);
    if (!sessionStatus.ok) {
        return sessionStatus;
    }

    json message{
        { "type", "webrtc_close" },
        { "peer", command.peerId }
    };
    rtcManager_->fnOnSignalingMessage(reinterpret_cast<void*>(command.sessionHandle), message);
    cleanupSessionMapping(command.peerId, command.sessionHandle);
    return core::contracts::OperationStatus::success();
}

void MediasoupRtcBridge::onSessionDisconnected(std::string_view peerId, std::uintptr_t sessionHandle) {
    if (!rtcManager_ || sessionHandle == 0) {
        return;
    }

    rtcManager_->fnOnWsDisconnected(reinterpret_cast<void*>(sessionHandle));
    cleanupSessionMapping(peerId, sessionHandle);
}

void MediasoupRtcBridge::setOnPeerBinary(tOnPeerBinary callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    onPeerBinary_ = std::move(callback);
}

core::contracts::OperationStatus MediasoupRtcBridge::sendBinaryToPeer(
    std::string_view targetPeerId,
    const std::vector<uint8_t>& data) {
    if (!rtcManager_) {
        return core::contracts::OperationStatus::failure("RTC manager is not initialized.");
    }
    if (targetPeerId.empty()) {
        return core::contracts::OperationStatus::failure("targetPeerId must not be empty.");
    }
    if (data.empty()) {
        return core::contracts::OperationStatus::failure("binary payload must not be empty.");
    }

    const auto targetPeer = rtcManager_->fnGetPeer(std::string(targetPeerId));
    if (!targetPeer || !targetPeer->fnIsReady()) {
        return core::contracts::OperationStatus::failure("target peer RTC channel is not ready.");
    }

    targetPeer->fnSendBinary(data);
    return core::contracts::OperationStatus::success();
}

std::vector<MediasoupSignalingEvent> MediasoupRtcBridge::consumeEventsForPeer(std::string_view peerId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto iterator = pendingEventsByPeer_.find(std::string(peerId));
    if (iterator == pendingEventsByPeer_.end()) {
        return {};
    }

    auto events = std::move(iterator->second);
    pendingEventsByPeer_.erase(iterator);
    return events;
}

core::contracts::OperationStatus MediasoupRtcBridge::validateSessionBoundCommand(const MediasoupCommand& command) const {
    if (!rtcManager_) {
        return core::contracts::OperationStatus::failure("RTC manager is not initialized.");
    }
    if (command.sessionHandle == 0) {
        return core::contracts::OperationStatus::failure("sessionHandle must not be empty.");
    }
    if (command.peerId.empty()) {
        return core::contracts::OperationStatus::failure("peerId must not be empty.");
    }
    return core::contracts::OperationStatus::success();
}

void MediasoupRtcBridge::rememberSessionMapping(std::string_view peerId, std::uintptr_t sessionHandle) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto peerKey = std::string(peerId);
    sessionByPeer_[peerKey] = sessionHandle;
    peerBySession_[sessionHandle] = peerKey;
}

void MediasoupRtcBridge::cleanupSessionMapping(std::string_view peerId, std::uintptr_t sessionHandle) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!peerId.empty()) {
        sessionByPeer_.erase(std::string(peerId));
        pendingEventsByPeer_.erase(std::string(peerId));
    }
    if (sessionHandle != 0) {
        auto peerIterator = peerBySession_.find(sessionHandle);
        if (peerIterator != peerBySession_.end()) {
            sessionByPeer_.erase(peerIterator->second);
            pendingEventsByPeer_.erase(peerIterator->second);
            peerBySession_.erase(peerIterator);
        }
    }
}

void MediasoupRtcBridge::enqueueEvent(std::uintptr_t sessionHandle, MediasoupSignalingEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string peerKey;

    auto peerIterator = peerBySession_.find(sessionHandle);
    if (peerIterator != peerBySession_.end()) {
        peerKey = peerIterator->second;
    } else if (!event.peerId.empty()) {
        peerKey = event.peerId;
    }

    if (peerKey.empty()) {
        return;
    }

    if (event.peerId.empty()) {
        event.peerId = peerKey;
    }
    pendingEventsByPeer_[peerKey].push_back(std::move(event));
}

} // namespace eds::server_new::mediasoup
