#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include <thread>

namespace Sys {
    namespace Network {

        class cNetIoContext {
        public:
            cNetIoContext();
            ~cNetIoContext();

            // initialize internal structures; must be called before fnStart
            bool fnInit(unsigned int uThreadCount = 0);

            // start background threads running the io_context
            bool fnStart();

            // stop and join threads
            void fnStop();

            // accessor to underlying io_context
            boost::asio::io_context& fnIo() noexcept { return m_oIoContext; }

        private:
            // non-copyable
            cNetIoContext(const cNetIoContext&) = delete;
            cNetIoContext& operator=(const cNetIoContext&) = delete;

            boost::asio::io_context m_oIoContext;
            // work guard to keep io_context::run from exiting
            std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> m_pWorkGuard;

            // threads running the io_context
            std::vector<std::thread> m_vThreads;
            unsigned int m_uThreadCountHint; // 0 -> hardware_concurrency
            bool m_bRunning;
        };

    } // namespace Network
} // namespace Sys
