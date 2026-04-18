#pragma once

#include <libpq-fe.h>

#include <nlohmann/json.hpp>

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

        bool connect(const std::string& conninfo, std::string& error);
        bool isConnected() const noexcept;

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
        PGresult* execParamsLocked(
            const std::string& sql,
            const std::vector<std::string>& params) const;

    private:
        mutable std::mutex mutex_;
        PGconn* connection_ = nullptr;
    };

} // namespace eds::server_new::infrastructure::db
