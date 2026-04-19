#include "features/direct_chat/runtime/DirectChatFeatureModule.h"

#include "Auth/runtime/AuthServices.h"
#include "contracts/TypedMessage.h"
#include "features/direct_chat/runtime/DirectChatFeatureManager.h"
#include "features/direct_chat/runtime/DirectChatStateStore.h"
#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"
#include <cstdint>
#include <string>

#include <utility>

namespace eds::server_new::features::direct_chat {
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

    DirectChatFeatureModule::DirectChatFeatureModule()
        : BaseModule("DirectChatFeatureModule", static_cast<core::contracts::ModuleId>(-1)) {
    }

    std::string_view DirectChatFeatureModule::objectType() const {
        return kDirectChatRouteObject;
    }

    std::string_view DirectChatFeatureModule::defaultAgent() const {
        return kDirectChatMessagingAgent;
    }

    core::contracts::OperationStatus DirectChatFeatureModule::ensureRegistered(core::runtime::MessageDispatcher& dispatcher) {
        if (registered_) {
            return core::contracts::OperationStatus::success();
        }

        auto sessionStore = eds::server_new::auth::AuthServices::sessionStore();
        auto repository = eds::server_new::control_plane::ControlPlaneServices::repository();
        if (!sessionStore || !repository || !repository->isReady()) {
            return core::contracts::OperationStatus::failure("Direct chat control-plane is not configured.");
        }

        if (!stateStore_) {
            stateStore_ = std::make_shared<DirectChatStateStore>(sessionStore, repository);
        }
        if (!manager_) {
            manager_ = std::make_shared<DirectChatFeatureManager>(stateStore_);
        }

        auto status = dispatcher.registerManager(std::string(kDirectChatRouteObject), manager_);
        if (!status.ok) {
            return status;
        }
        registered_ = true;
        return core::contracts::OperationStatus::success();
    }

    eds::server_new::features::runtime::FeatureDispatchResult DirectChatFeatureModule::dispatch(
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
            result.status = core::contracts::OperationStatus::failure("Unauthorized direct chat request.");
            return result;
        }

        const auto peerId = request.context.value("peerId", request.context.value("peer", request.peerId));
        if (peerId != request.peerId) {
            result.status = core::contracts::OperationStatus::failure("peer impersonation detected.");
            return result;
        }

        if (request.actionType != kActionSendDirectMessage
            && request.actionType != kActionSyncDirectMessages
            && request.actionType != kActionListDirectThreads
            && request.actionType != kActionAckDirectMessages
            && request.actionType != kActionSearchUsers) {
            result.status = core::contracts::OperationStatus::failure("Unsupported direct chat action.");
            return result;
        }
        DirectChatCommand command;
        command.sessionHandle = request.sessionHandle;
        command.sessionId = std::to_string(static_cast<unsigned long long>(request.sessionHandle));
        command.peerId = request.peerId;
        command.userId = authSession->userId;
        command.targetUserId = request.context.value("targetUserId", request.context.value("userIdTo", std::string{}));
        command.targetPeerId = request.context.value("targetPeerId", request.context.value("peerIdTo", std::string{}));
        command.clientRequestId = request.context.value(
            "clientRequestId",
            request.context.value("messageId", std::string{}));
        command.text = request.context.value("text", request.context.value("message", std::string{}));
        command.query = request.context.value(
            "query",
            request.context.value(
                "emailQuery",
                request.context.value("email", std::string{})));
        command.threadId = request.context.value("threadId", std::string{});
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
            ? std::string(kDirectChatMessagingAgent)
            : request.agentType;

        core::contracts::MessageRoute route{
            std::string(kDirectChatRouteObject),
            result.effectiveAgent,
            request.actionType
        };
        core::contracts::TypedMessage<DirectChatCommand> payload(std::move(command));
        result.status = dispatcher.dispatch(route, payload);

        appendOutboundEventsFromStatus(result.status, result);
        return result;
    }

    bool DirectChatFeatureModule::onInitialize() {
        return true;
    }

    void DirectChatFeatureModule::onShutdown() {
    }

} // namespace eds::server_new::features::direct_chat