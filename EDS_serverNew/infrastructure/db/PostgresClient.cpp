#include "infrastructure/db/PostgresClient.h"

namespace eds::server_new::infrastructure::db {

    PostgresClient::PostgresClient() = default;

    PostgresClient::~PostgresClient() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_ != nullptr) {
            PQfinish(connection_);
            connection_ = nullptr;
        }
    }

    bool PostgresClient::connect(const std::string& conninfo, std::string& error) {
        error.clear();

        std::lock_guard<std::mutex> lock(mutex_);

        if (connection_ != nullptr) {
            PQfinish(connection_);
            connection_ = nullptr;
        }

        connection_ = PQconnectdb(conninfo.c_str());
        if (connection_ == nullptr) {
            error = "PQconnectdb returned nullptr.";
            return false;
        }

        if (PQstatus(connection_) != CONNECTION_OK) {
            error = PQerrorMessage(connection_);
            PQfinish(connection_);
            connection_ = nullptr;
            return false;
        }

        return true;
    }

    bool PostgresClient::isConnected() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_ != nullptr && PQstatus(connection_) == CONNECTION_OK;
    }

    PGresult* PostgresClient::execParamsLocked(
        const std::string& sql,
        const std::vector<std::string>& params) const {
        std::vector<const char*> values;
        values.reserve(params.size());

        for (const auto& value : params) {
            values.push_back(value.c_str());
        }

        return PQexecParams(
            connection_,
            sql.c_str(),
            static_cast<int>(values.size()),
            nullptr,
            values.empty() ? nullptr : values.data(),
            nullptr,
            nullptr,
            0);
    }

    std::optional<PostgresClient::json> PostgresClient::queryScalarJson(
        const std::string& sql,
        const std::vector<std::string>& params,
        std::string& error) const {
        error.clear();

        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_ == nullptr) {
            error = "Postgres connection is not initialized.";
            return std::nullopt;
        }

        PGresult* result = execParamsLocked(sql, params);
        if (result == nullptr) {
            error = "PQexecParams returned nullptr.";
            return std::nullopt;
        }

        const auto status = PQresultStatus(result);
        if (status != PGRES_TUPLES_OK) {
            error = PQresultErrorMessage(result);
            PQclear(result);
            return std::nullopt;
        }

        if (PQntuples(result) == 0 || PQnfields(result) == 0) {
            PQclear(result);
            return std::nullopt;
        }

        const auto raw = PQgetvalue(result, 0, 0);
        json parsed = json::parse(raw != nullptr ? raw : "null", nullptr, false);
        PQclear(result);

        if (parsed.is_discarded()) {
            error = "Failed to parse JSON returned from database.";
            return std::nullopt;
        }

        return parsed;
    }

    std::vector<PostgresClient::json> PostgresClient::queryJsonRows(
        const std::string& sql,
        const std::vector<std::string>& params,
        std::string& error) const {
        error.clear();
        std::vector<json> rows;

        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_ == nullptr) {
            error = "Postgres connection is not initialized.";
            return rows;
        }

        PGresult* result = execParamsLocked(sql, params);
        if (result == nullptr) {
            error = "PQexecParams returned nullptr.";
            return rows;
        }

        const auto status = PQresultStatus(result);
        if (status != PGRES_TUPLES_OK) {
            error = PQresultErrorMessage(result);
            PQclear(result);
            return rows;
        }

        const auto tupleCount = PQntuples(result);
        rows.reserve(static_cast<size_t>(tupleCount));
        for (int i = 0; i < tupleCount; ++i) {
            const auto raw = PQgetvalue(result, i, 0);
            json parsed = json::parse(raw != nullptr ? raw : "null", nullptr, false);
            if (parsed.is_discarded()) {
                rows.clear();
                error = "Failed to parse JSON row returned from database.";
                PQclear(result);
                return rows;
            }
            rows.push_back(std::move(parsed));
        }

        PQclear(result);
        return rows;
    }

    std::vector<std::string> PostgresClient::queryStringRows(
        const std::string& sql,
        const std::vector<std::string>& params,
        std::string& error) const {
        error.clear();
        std::vector<std::string> rows;

        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_ == nullptr) {
            error = "Postgres connection is not initialized.";
            return rows;
        }

        PGresult* result = execParamsLocked(sql, params);
        if (result == nullptr) {
            error = "PQexecParams returned nullptr.";
            return rows;
        }

        const auto status = PQresultStatus(result);
        if (status != PGRES_TUPLES_OK) {
            error = PQresultErrorMessage(result);
            PQclear(result);
            return rows;
        }

        const auto tupleCount = PQntuples(result);
        rows.reserve(static_cast<size_t>(tupleCount));
        for (int i = 0; i < tupleCount; ++i) {
            const auto raw = PQgetvalue(result, i, 0);
            rows.emplace_back(raw != nullptr ? raw : "");
        }

        PQclear(result);
        return rows;
    }

    bool PostgresClient::execute(
        const std::string& sql,
        const std::vector<std::string>& params,
        std::string& error) const {
        error.clear();

        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_ == nullptr) {
            error = "Postgres connection is not initialized.";
            return false;
        }

        PGresult* result = execParamsLocked(sql, params);
        if (result == nullptr) {
            error = "PQexecParams returned nullptr.";
            return false;
        }

        const auto status = PQresultStatus(result);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            error = PQresultErrorMessage(result);
            PQclear(result);
            return false;
        }

        PQclear(result);
        return true;
    }

} // namespace eds::server_new::infrastructure::db