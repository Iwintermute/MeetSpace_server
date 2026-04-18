#include "features/chat/runtime/ChatFeatureModule.h"

#include "Auth/runtime/AuthServices.h"
#include "contracts/TypedMessage.h"
#include "features/chat/runtime/ChatFeatureManager.h"
#include "features/chat/runtime/ChatStateStore.h"
#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"
#include <cstdint>
#include <string>

#include <utility>

namespace eds::server_new::features::chat {
    namespace {

        void appendOutboundEventsFromStatus(
            const core::contracts::OperationStatus& status,
            eds::server_new::features::runtime::FeatureDispatchResult& result) {
            const auto it = status.data.find("outboundEvents");
            if (it == status.data.end() || !it->is_array()) {
                return;
            }

            for (const auto& item : *it) {
                if (item.is_object()) {
                    result.outboundEvents.push_back(item);
                }
            }
        }

    } // namespace

    ChatFeatureModule::ChatFeatureModule()
        : BaseModule("ChatFeatureModule", static_cast<core::contracts::ModuleId>(-1)) {
    }

    std::string_view ChatFeatureModule::objectType() const {
        return kChatRouteObject;
    }

    std::string_view ChatFeatureModule::defaultAgent() const {
        return kChatMessagingAgent;
    }

    core::contracts::OperationStatus ChatFeatureModule::ensureRegistered(core::runtime::MessageDispatcher& dispatcher) {
        if (registered_) {
            return core::contracts::OperationStatus::success();
        }

        auto sessionStore = eds::server_new::auth::AuthServices::sessionStore();
        auto repository = eds::server_new::control_plane::ControlPlaneServices::repository();
        if (!sessionStore || !repository || !repository->isReady()) {
            return core::contracts::OperationStatus::failure("Conference chat control-plane is not configured.");
        }

        if (!stateStore_) {
            stateStore_ = std::make_shared<ChatStateStore>(sessionStore, repository);
        }
        if (!manager_) {
            manager_ = std::make_shared<ChatFeatureManager>(stateStore_);
        }

        auto status = dispatcher.registerManager(std::string(kChatRouteObject), manager_);
        if (!status.ok) {
            return status;
        }
        registered_ = true;
        return core::contracts::OperationStatus::success();
    }

    eds::server_new::features::runtime::FeatureDispatchResult ChatFeatureModule::dispatch(
        const eds::server_new::features::runtime::FeatureDispatchRequest& request,
        core::runtime::MessageDispatcher& dispatcher) {
        eds::server_new::features::runtime::FeatureDispatchResult result;
        result.status = ensureRegistered(dispatcher);
        if (!result.status.ok) {
            return result;
        }

        auto sessionStore = eds::server_new::auth::AuthServices::sessionStore();
        if (!sessionStore) {
            result.status = core::contracts::OperationStatus::failure("Auth session store is not configured.");
            return result;
        }

        const auto authSession = sessionStore->get(request.sessionHandle);
        if (!authSession.has_value() || !authSession->authenticated) {
            result.status = core::contracts::OperationStatus::failure("Unauthorized conference chat request.");
            return result;
        }

        const auto peerId = request.context.value("peerId", request.context.value("peer", request.peerId));
        if (peerId != request.peerId) {
            result.status = core::contracts::OperationStatus::failure("peer impersonation detected.");
            return result;
        }

        if (request.actionType != kActionSendMessage
            && request.actionType != kActionSyncMessages
            && request.actionType != kActionAckMessages) {
            result.status = core::contracts::OperationStatus::failure("Unsupported conference chat action.");
            return result;
        }
        ChatCommand command;
        command.sessionHandle = request.sessionHandle;
        command.sessionId = std::to_string(static_cast<unsigned long long>(request.sessionHandle));
        command.peerId = request.peerId;
        command.userId = authSession->userId;
        command.conferenceId = request.context.value("conferenceId", request.context.value("roomId", std::string{}));
        command.targetUserId = request.context.value("targetUserId", request.context.value("userIdTo", std::string{}));
        command.targetPeerId = request.context.value("targetPeerId", request.context.value("peerIdTo", std::string{}));
        command.clientRequestId = request.context.value(
            "clientRequestId",
            request.context.value("messageId", std::string{}));
        command.text = request.context.value("text", request.context.value("message", std::string{}));
        command.limit = request.context.value("limit", static_cast<std::size_t>(50));
        command.beforeCreatedAt = request.context.value("beforeCreatedAt", request.context.value("before", std::string{}));
        command.afterCreatedAt = request.context.value("afterCreatedAt", request.context.value("after", std::string{}));
        if (request.context.contains("messageIds") && request.context["messageIds"].is_array()) {
            for (const auto& item : request.context["messageIds"]) {
                if (item.is_string()) {
                    command.messageIds.push_back(item.get<std::string>());
                }
            }
        }
        if (command.messageIds.empty()) {
            const auto singleMessageId = request.context.value("messageId", std::string{});
            if (!singleMessageId.empty()) {
                command.messageIds.push_back(singleMessageId);
            }
        }
        command.markRead = request.context.value("markRead", request.context.value("read", false));

        result.effectiveAgent = request.agentType.empty()
            ? std::string(kChatMessagingAgent)
            : request.agentType;

        core::contracts::MessageRoute route{
            std::string(kChatRouteObject),
            result.effectiveAgent,
            request.actionType
        };
        core::contracts::TypedMessage<ChatCommand> payload(std::move(command));
        result.status = dispatcher.dispatch(route, payload);

        appendOutboundEventsFromStatus(result.status, result);
        return result;
    }

    bool ChatFeatureModule::onInitialize() {
        return true;
    }

    void ChatFeatureModule::onShutdown() {
    }

} // namespace eds::server_new::features::chat