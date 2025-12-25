#include "cNetWebSocketServer.h"
#include <iostream>

using namespace Sys::Network;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

/////////////////////////////////////////////////////////////////
// sWsSession
/////////////////////////////////////////////////////////////////

cNetWebSocketServer::sWsSession::sWsSession(tcp::socket&& sock, cNetWebSocketServer* owner)
    : m_ws(std::move(sock))
    , m_owner(owner)
    , m_bOpen(false)
{
}

cNetWebSocketServer::sWsSession::~sWsSession()
{
    // no close() here — beast handles cleanup
}

void cNetWebSocketServer::sWsSession::fnStart()
{
    auto self = shared_from_this();
    m_ws.async_accept([self](boost::system::error_code ec)
        {
            if (ec) {
                std::cerr << "[WS] accept failed: " << ec.message() << "\n";
                return;
            }

            self->m_bOpen = true;

            if (self->m_owner && self->m_owner->m_fnOnConnected)
                self->m_owner->m_fnOnConnected(self.get());

            self->fnDoRead();
        });
}

void cNetWebSocketServer::sWsSession::fnDoRead()
{
    auto self = shared_from_this();
    m_ws.async_read(m_buffer, [self](boost::system::error_code ec, std::size_t bytes)
        {
            if (ec) {
                if (self->m_bOpen) {
                    self->m_bOpen = false;
                    if (self->m_owner && self->m_owner->m_fnOnDisconnected)
                        self->m_owner->m_fnOnDisconnected(self.get());
                }
                return;
            }

            std::string msg = beast::buffers_to_string(self->m_buffer.data());
            self->m_buffer.consume(bytes);

            if (self->m_owner && self->m_owner->m_fnOnMessage)
                self->m_owner->m_fnOnMessage(msg, self.get());

            self->fnDoRead();
        });
}

void cNetWebSocketServer::sWsSession::fnSendText(const std::string& txt)
{
    if (!m_bOpen) return;

    auto self = shared_from_this();
    boost::asio::post(m_ws.get_executor(), [self, txt]()
        {
            if (!self->m_bOpen) return;

            self->m_ws.text(true);
            self->m_ws.async_write(boost::asio::buffer(txt),
                [self](boost::system::error_code ec, std::size_t)
                {
                    if (ec && self->m_bOpen) {
                        self->m_bOpen = false;
                        if (self->m_owner && self->m_owner->m_fnOnDisconnected)
                            self->m_owner->m_fnOnDisconnected(self.get());
                    }
                });
        });
}

/////////////////////////////////////////////////////////////////
// cNetWebSocketServer
/////////////////////////////////////////////////////////////////

cNetWebSocketServer::cNetWebSocketServer(boost::asio::io_context& ctx, unsigned short port)
    : m_ctx(ctx)
    , m_port(port)
    , m_running(false)
{
}

cNetWebSocketServer::~cNetWebSocketServer()
{
    fnStop();
}

bool cNetWebSocketServer::fnStart()
{
    if (m_running) return true;

    boost::system::error_code ec;
    tcp::endpoint ep(tcp::v4(), m_port);

    m_acceptor = std::make_unique<tcp::acceptor>(m_ctx);
    m_acceptor->open(ep.protocol(), ec);
    if (ec) { std::cerr << "[WS] open: " << ec.message() << "\n"; return false; }

    m_acceptor->set_option(boost::asio::socket_base::reuse_address(true));
    m_acceptor->bind(ep, ec);
    if (ec) { std::cerr << "[WS] bind: " << ec.message() << "\n"; return false; }

    m_acceptor->listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) { std::cerr << "[WS] listen: " << ec.message() << "\n"; return false; }

    m_running = true;
    fnDoAccept();
    return true;
}

void cNetWebSocketServer::fnDoAccept()
{
    if (!m_running) return;

    m_acceptor->async_accept([this](boost::system::error_code ec, tcp::socket sock)
        {
            if (!ec) {
                auto session = std::make_shared<sWsSession>(std::move(sock), this);
                session->fnStart();
            }
            else {
                std::cerr << "[WS] accept error: " << ec.message() << "\n";
            }

            fnDoAccept();
        });
}

void cNetWebSocketServer::fnStop()
{
    if (!m_running) return;
    m_running = false;

    if (m_acceptor) {
        boost::system::error_code ec;
        m_acceptor->close(ec);
        m_acceptor.reset();
    }
}
