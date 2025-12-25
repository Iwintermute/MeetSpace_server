#pragma once
#include <functional>
#include <memory>
#include <string>

#include "cNetIoContext.h"

namespace Sys {
    namespace Network {

        class cNetHttpServer {
        public:
            using tHealthFn = std::function<std::string()>;
            using tMetricsFn = std::function<std::string()>;

            cNetHttpServer(Sys::Network::cNetIoContext::sIoStub& ioCtx, unsigned short port);
            ~cNetHttpServer();

            void fnSetHealthFn(tHealthFn fn) { m_fnHealth = std::move(fn); }
            void fnSetMetricsFn(tMetricsFn fn) { m_fnMetrics = std::move(fn); }

            bool fnStart();
            void fnStop();

        private:
            void fnDoAccept();

        private:
            Sys::Network::cNetIoContext::sIoStub& m_rIoCtx;
            unsigned short m_uPort;
            bool m_bRunning;

            tHealthFn m_fnHealth;
            tMetricsFn m_fnMetrics;
        };

    } // namespace Network
} // namespace Sys
