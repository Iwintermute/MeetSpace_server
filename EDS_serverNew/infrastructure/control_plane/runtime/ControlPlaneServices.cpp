#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"

#include <mutex>

namespace eds::server_new::control_plane {
    namespace {

        std::mutex gMutex;
        std::shared_ptr<eds::server_new::infrastructure::db::PostgresClient> gPostgres;
        std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> gRepository;

    } // namespace

    bool ControlPlaneServices::configure(const std::string& conninfo, std::string& error) {
        error.clear();

        auto postgres = std::make_shared<eds::server_new::infrastructure::db::PostgresClient>();
        if (!postgres->connect(conninfo, error)) {
            return false;
        }

        auto repository =
            std::make_shared<eds::server_new::infrastructure::db::MessengerRepository>(postgres);

        std::lock_guard<std::mutex> lock(gMutex);
        gPostgres = std::move(postgres);
        gRepository = std::move(repository);
        return true;
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