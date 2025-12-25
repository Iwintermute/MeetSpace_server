#include "cNetHttpServer.h"
#include <boost/beast/http.hpp>
#include <iostream>

namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;

namespace Sys {
    namespace Network {

        cNetHttpServer::cNetHttpServer(boost::asio::io_context& ioCtx, unsigned short port)
            : m_rIoCtx(ioCtx), m_uPort(port), m_bRunning(false)
        {
        }

        cNetHttpServer::~cNetHttpServer() { fnStop(); }

        bool cNetHttpServer::fnStart()
        {
            if (m_bRunning) return true;

            try {
                tcp::endpoint ep(tcp::v4(), m_uPort);
                m_pAcceptor = std::make_unique<tcp::acceptor>(m_rIoCtx);
                boost::system::error_code ec;

                m_pAcceptor->open(ep.protocol(), ec);
                m_pAcceptor->set_option(boost::asio::socket_base::reuse_address(true), ec);
                m_pAcceptor->bind(ep, ec);
                m_pAcceptor->listen(boost::asio::socket_base::max_listen_connections, ec);

                m_bRunning = true;
                fnDoAccept();
            }
            catch (const std::exception& e) {
                std::cerr << "[HTTP] start error: " << e.what() << std::endl;
                return false;
            }

            return true;
        }

        void cNetHttpServer::fnStop()
        {
            if (!m_bRunning) return;
            m_bRunning = false;

            if (m_pAcceptor) {
                boost::system::error_code ec;
                m_pAcceptor->close(ec);
                m_pAcceptor.reset();
            }
        }

        void cNetHttpServer::fnDoAccept()
        {
            if (!m_bRunning || !m_pAcceptor) return;

            m_pAcceptor->async_accept([this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    auto pStream = std::make_shared<boost::beast::tcp_stream>(std::move(socket));
                    auto pBuffer = std::make_shared<boost::beast::flat_buffer>();

                    http::request<http::string_body> req;
                    http::async_read(*pStream, *pBuffer, req,
                        [this, pStream, pBuffer, req = std::move(req)](boost::system::error_code ec, std::size_t) mutable {
                            http::response<http::string_body> res{ http::status::ok, 11 };
                            res.set(http::field::server, "cNetHttpServer");
                            res.set(http::field::content_type, "application/json");
                            res.keep_alive(false);

                            std::string target = std::string(req.target());
                            if (target == "/health")
                                res.body() = m_fnHealth ? m_fnHealth() : R"({"status":"ok"})";
                            else if (target == "/metrics")
                                res.body() = m_fnMetrics ? m_fnMetrics() : R"({"metrics":{}})";
                            else {
                                res.result(http::status::not_found);
                                res.body() = R"({"error":"not_found"})";
                            }
                            res.prepare_payload();

                            http::async_write(*pStream, res,
                                [pStream](boost::system::error_code ec2, std::size_t) {
                                    boost::system::error_code ec3;
                                    try { pStream->socket().shutdown(tcp::socket::shutdown_send, ec3); }
                                    catch (...) {}
                                });
                        });
                }

                boost::asio::post(m_rIoCtx, [this]() { fnDoAccept(); });
                });
        }

    } // namespace Network
} // namespace Sys
