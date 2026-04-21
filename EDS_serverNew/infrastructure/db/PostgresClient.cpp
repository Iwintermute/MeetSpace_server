#include "infrastructure/db/PostgresClient.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <thread>

namespace eds::server_new::infrastructure::db {
    namespace {
        constexpr std::size_t kMinPoolSize = 1;
        constexpr std::size_t kMaxPoolSize = 64;
        constexpr std::size_t kFallbackPoolSize = 8;
        constexpr const char* kPoolSizeEnvName = "EDUSPACE_POSTGRES_POOL_SIZE";

        std::size_t clampPoolSize(std::size_t value) {
            if (value < kMinPoolSize) {
                return kMinPoolSize;
            }
            if (value > kMaxPoolSize) {
                return kMaxPoolSize;
            }
            return value;
        }

        std::size_t readPoolSizeFromEnv() {
            const char* raw = std::getenv(kPoolSizeEnvName);
            if (raw == nullptr || raw[0] == '\0') {
                return 0;
            }

            try {
                const auto parsed = std::stoll(raw);
                if (parsed <= 0) {
                    return 0;
                }
                return clampPoolSize(static_cast<std::size_t>(parsed));
            }
            catch (...) {
                return 0;
            }
        }

        std::size_t resolvePoolSize(std::size_t requestedPoolSize) {
            if (requestedPoolSize > 0) {
                return clampPoolSize(requestedPoolSize);
            }

            const auto envPoolSize = readPoolSizeFromEnv();
            if (envPoolSize > 0) {
                return envPoolSize;
            }

            const auto hw = static_cast<std::size_t>(std::thread::hardware_concurrency());
            if (hw == 0) {
                return kFallbackPoolSize;
            }

            return clampPoolSize(std::max<std::size_t>(4, hw));
        }
    } // namespace

    PostgresClient::PostgresClient() = default;

    PostgresClient::~PostgresClient() {
        std::vector<std::shared_ptr<ConnectionSlot>> staleSlots;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            staleSlots.swap(slots_);
        }
        closeSlots(staleSlots);
    }

    bool PostgresClient::connect(const std::string& conninfo, std::string& error) {
        return connect(conninfo, error, 0);
    }

    bool PostgresClient::connect(const std::string& conninfo, std::string& error, std::size_t poolSize) {
        error.clear();
        const auto effectivePoolSize = resolvePoolSize(poolSize);
        std::vector<std::shared_ptr<ConnectionSlot>> newSlots;
        newSlots.reserve(effectivePoolSize);

        for (std::size_t i = 0; i < effectivePoolSize; ++i) {
            auto slot = std::make_shared<ConnectionSlot>();
            slot->connection = PQconnectdb(conninfo.c_str());

            if (slot->connection == nullptr) {
                std::ostringstream stream;
                stream << "PQconnectdb returned nullptr for slot #" << i << ".";
                error = stream.str();
                closeSlots(newSlots);
                return false;
            }

            if (PQstatus(slot->connection) != CONNECTION_OK) {
                std::ostringstream stream;
                stream << "Postgres connection failed for slot #" << i << ": "
                    << PQerrorMessage(slot->connection);
                error = stream.str();
                PQfinish(slot->connection);
                slot->connection = nullptr;
                closeSlots(newSlots);
                return false;
            }

            newSlots.push_back(std::move(slot));
        }

        std::vector<std::shared_ptr<ConnectionSlot>> staleSlots;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            staleSlots.swap(slots_);
            slots_ = std::move(newSlots);
            nextSlotIndex_.store(0, std::memory_order_relaxed);
        }
        closeSlots(staleSlots);
        return true;
    }

    bool PostgresClient::isConnected() const noexcept {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (const auto& slot : slots_) {
            if (slot == nullptr || slot->connection == nullptr) {
                continue;
            }

            if (PQstatus(slot->connection) == CONNECTION_OK) {
                return true;
            }
        }
        return false;
    }

    std::size_t PostgresClient::poolSize() const noexcept {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return slots_.size();
    }

    std::shared_ptr<PostgresClient::ConnectionSlot> PostgresClient::acquireSlot(std::string& error) const {
        error.clear();

        std::lock_guard<std::mutex> lock(stateMutex_);
        if (slots_.empty()) {
            error = "Postgres connection pool is not initialized.";
            return nullptr;
        }

        const auto index = nextSlotIndex_.fetch_add(1, std::memory_order_relaxed) % slots_.size();
        auto slot = slots_[index];
        if (slot == nullptr) {
            error = "Postgres connection pool slot is null.";
            return nullptr;
        }

        return slot;
    }

    void PostgresClient::closeSlots(std::vector<std::shared_ptr<ConnectionSlot>>& slots) noexcept {
        for (auto& slot : slots) {
            if (!slot) {
                continue;
            }

            std::lock_guard<std::mutex> lock(slot->mutex);
            if (slot->connection != nullptr) {
                PQfinish(slot->connection);
                slot->connection = nullptr;
            }
        }
        slots.clear();
    }

    PGresult* PostgresClient::execParamsLocked(
        PGconn* connection,
        const std::string& sql,
        const std::vector<std::string>& params) const {
        if (connection == nullptr) {
            return nullptr;
        }

        std::vector<const char*> values;
        values.reserve(params.size());

        for (const auto& value : params) {
            values.push_back(value.c_str());
        }

        return PQexecParams(
            connection,
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

        auto slot = acquireSlot(error);
        if (!slot) {
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(slot->mutex);
        if (slot->connection == nullptr || PQstatus(slot->connection) != CONNECTION_OK) {
            error = "Postgres connection slot is not ready.";
            return std::nullopt;
        }

        PGresult* result = execParamsLocked(slot->connection, sql, params);
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

        auto slot = acquireSlot(error);
        if (!slot) {
            return rows;
        }

        std::lock_guard<std::mutex> lock(slot->mutex);
        if (slot->connection == nullptr || PQstatus(slot->connection) != CONNECTION_OK) {
            error = "Postgres connection slot is not ready.";
            return rows;
        }

        PGresult* result = execParamsLocked(slot->connection, sql, params);
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

        auto slot = acquireSlot(error);
        if (!slot) {
            return rows;
        }

        std::lock_guard<std::mutex> lock(slot->mutex);
        if (slot->connection == nullptr || PQstatus(slot->connection) != CONNECTION_OK) {
            error = "Postgres connection slot is not ready.";
            return rows;
        }

        PGresult* result = execParamsLocked(slot->connection, sql, params);
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

        auto slot = acquireSlot(error);
        if (!slot) {
            return false;
        }

        std::lock_guard<std::mutex> lock(slot->mutex);
        if (slot->connection == nullptr || PQstatus(slot->connection) != CONNECTION_OK) {
            error = "Postgres connection slot is not ready.";
            return false;
        }

        PGresult* result = execParamsLocked(slot->connection, sql, params);
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
