#pragma once

#include <libpq-fe.h>

#include <nlohmann/json.hpp>
#include <atomic>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace eds::server_new::infrastructure::db {

    class PostgresClient final {
    public:
        using json = nlohmann::json;

        PostgresClient();
        ~PostgresClient();

        PostgresClient(const PostgresClient&) = delete;
        PostgresClient& operator=(const PostgresClient&) = delete;
        bool connect(const std::string& conninfo, std::string& error, std::size_t poolSize);
        bool connect(const std::string& conninfo, std::string& error);
        bool isConnected() const noexcept;
        std::size_t poolSize() const noexcept;

        std::optional<json> queryScalarJson(
            const std::string& sql,
            const std::vector<std::string>& params,
            std::string& error) const;

        std::vector<json> queryJsonRows(
            const std::string& sql,
            const std::vector<std::string>& params,
            std::string& error) const;

        std::vector<std::string> queryStringRows(
            const std::string& sql,
            const std::vector<std::string>& params,
            std::string& error) const;

        bool execute(
            const std::string& sql,
            const std::vector<std::string>& params,
            std::string& error) const;

    private:
        struct ConnectionSlot {
            mutable std::mutex mutex;
            PGconn* connection = nullptr;
        };
        PGresult* execParamsLocked(
            PGconn* connection,
            const std::string& sql,
            const std::vector<std::string>& params) const;
        PGresult* execParamsResilient(
            const std::string& sql,
            const std::vector<std::string>& params,
            bool allowCommandStatus,
            std::string& error) const;
        std::shared_ptr<ConnectionSlot> acquireSlot(std::string& error) const;
        std::size_t slotCountSnapshot() const noexcept;
        std::string conninfoSnapshot() const;
        static void closeSlots(std::vector<std::shared_ptr<ConnectionSlot>>& slots) noexcept;

    private:
        mutable std::mutex stateMutex_;
        std::vector<std::shared_ptr<ConnectionSlot>> slots_;
        std::string conninfo_;
        mutable std::atomic<std::size_t> nextSlotIndex_{ 0 };
    };

} // namespace eds::server_new::infrastructure::db
