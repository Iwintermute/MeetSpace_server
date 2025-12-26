#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <functional>
#include <memory>
#include <string>

namespace Sys {
    namespace Network {

        class cNetHttpServer {
        public:
            using tHealthFn = std::function<std::string()>;
            using tMetricsFn = std::function<std::string()>;

            cNetHttpServer(boost::asio::io_context& ioCtx, unsigned short port);
            ~cNetHttpServer();

            void fnSetHealthFn(tHealthFn fn) { m_fnHealth = std::move(fn); }
            void fnSetMetricsFn(tMetricsFn fn) { m_fnMetrics = std::move(fn); }

            bool fnStart();
            void fnStop();

        private:
            void fnDoAccept();

        private:
            boost::asio::io_context& m_rIoCtx;
            unsigned short m_uPort;
            std::unique_ptr<boost::asio::ip::tcp::acceptor> m_pAcceptor;
            bool m_bRunning;

            tHealthFn m_fnHealth;
            tMetricsFn m_fnMetrics;
        };

    } // namespace Network
} // namespace Sys
