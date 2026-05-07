#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"
#include <chrono>
#include <cstdlib>
#include <iostream>

#include <mutex>
#include <thread>

namespace eds::server_new::control_plane {
    namespace {

        std::mutex gMutex;
        std::shared_ptr<eds::server_new::infrastructure::db::PostgresClient> gPostgres;
        std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> gRepository;
        constexpr int kDefaultConnectAttempts = 4;
        constexpr int kMaxConnectAttempts = 12;
        constexpr int kDefaultRetryDelayMs = 900;
        constexpr int kMaxRetryDelayMs = 8000;

        int clampInt(int value, int minValue, int maxValue) {
            if (value < minValue) {
                return minValue;
            }
            if (value > maxValue) {
                return maxValue;
            }
            return value;
        }

        int readPositiveEnvInt(const char* envName) {
            if (envName == nullptr || envName[0] == '\0') {
                return 0;
            }
            const char* raw = std::getenv(envName);
            if (raw == nullptr || raw[0] == '\0') {
                return 0;
            }
            try {
                return std::stoi(raw);
            }
            catch (...) {
                return 0;
            }
        }

        int readEnvIntWithCompat(
            const char* primaryName,
            const char* compatName,
            int fallbackValue,
            int minValue,
            int maxValue) {
            auto value = readPositiveEnvInt(primaryName);
            if (value <= 0) {
                value = readPositiveEnvInt(compatName);
            }
            if (value <= 0) {
                value = fallbackValue;
            }
            return clampInt(value, minValue, maxValue);
        }

    } // namespace

    bool ControlPlaneServices::configure(const std::string& conninfo, std::string& error) {
        error.clear();
        const auto connectAttempts = readEnvIntWithCompat(
            "MEETSPACE_CONTROL_PLANE_CONNECT_ATTEMPTS",
            "EDUSPACE_CONTROL_PLANE_CONNECT_ATTEMPTS",
            kDefaultConnectAttempts,
            1,
            kMaxConnectAttempts);
        const auto retryDelayMs = readEnvIntWithCompat(
            "MEETSPACE_CONTROL_PLANE_CONNECT_RETRY_MS",
            "EDUSPACE_CONTROL_PLANE_CONNECT_RETRY_MS",
            kDefaultRetryDelayMs,
            100,
            kMaxRetryDelayMs);

        std::string lastError = "unknown bootstrap error";
        for (int attempt = 1; attempt <= connectAttempts; ++attempt) {
            auto postgres = std::make_shared<eds::server_new::infrastructure::db::PostgresClient>();
            std::string connectError;
            if (!postgres->connect(conninfo, connectError)) {
                lastError = connectError.empty() ? std::string("Postgres connection failed.") : connectError;
                if (attempt < connectAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
                }
                continue;
            }

            auto repository =
                std::make_shared<eds::server_new::infrastructure::db::MessengerRepository>(postgres);
            const auto cleanupStatus = repository->markServerNodeSessionsDisconnected();
            if (!cleanupStatus.ok) {
                std::cerr << "[control-plane] stale session cleanup warning: "
                    << cleanupStatus.message
                    << '\n';
            }

            std::lock_guard<std::mutex> lock(gMutex);
            gPostgres = std::move(postgres);
            gRepository = std::move(repository);
            error.clear();
            return true;
        }

        error = "Control-plane Postgres bootstrap failed after " +
            std::to_string(connectAttempts) +
            " attempt(s): " +
            lastError;
        return false;
    }

    std::shared_ptr<eds::server_new::infrastructure::db::PostgresClient>
        ControlPlaneServices::postgres() {
        std::lock_guard<std::mutex> lock(gMutex);
        return gPostgres;
    }

    std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository>
        ControlPlaneServices::repository() {
        std::lock_guard<std::mutex> lock(gMutex);
        return gRepository;
    }

} // namespace eds::server_new::control_plane