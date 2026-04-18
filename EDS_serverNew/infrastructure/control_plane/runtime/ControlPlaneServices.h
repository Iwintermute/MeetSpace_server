#pragma once

#include "infrastructure/db/MessengerRepository.h"
#include "infrastructure/db/PostgresClient.h"

#include <memory>
#include <string>

namespace eds::server_new::control_plane {

    class ControlPlaneServices final {
    public:
        static bool configure(const std::string& conninfo, std::string& error);

        static std::shared_ptr<eds::server_new::infrastructure::db::PostgresClient> postgres();
        static std::shared_ptr<eds::server_new::infrastructure::db::MessengerRepository> repository();

    private:
        ControlPlaneServices() = delete;
    };

} // namespace eds::server_new::control_plane