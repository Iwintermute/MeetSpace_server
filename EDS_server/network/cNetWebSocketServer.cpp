#include "cNetWebSocketServer.h"
#include <iostream>

using namespace Sys::Network;

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

// ---------------- session ----------------

cNetWebSocketServer::sWsSession::sWsSession(tcp::socket&& sock, cNetWebSocketServer* owner)
    : m_ws(std::move(sock))
    , m_owner(owner)
{
    // можно настроить max message size и т.п.
    // m_ws.read_message_max(16 * 1024 * 1024);
}

void cNetWebSocketServer::sWsSession::fnStart()
{
    auto self = shared_from_this();

    // Важно: accept должен выполняться в io_context потоке.
    m_ws.async_accept([self](beast::error_code ec)
        {
            if (ec) {
                std::cerr << "[WS] accept failed: " << ec.message() << "\n";
                return;
            }

            self->m_open = true;

            if (self->m_owner) self->m_owner->fnRegisterSession(self.get(), self);
            if (self->m_owner && self->m_owner->m_fnOnConnected)
                self->m_owner->m_fnOnConnected(self.get());

            self->fnDoRead();
        });
}

void cNetWebSocketServer::sWsSession::fnDoRead()
{
    auto self = shared_from_this();

    m_ws.async_read(m_buffer, [self](beast::error_code ec, std::size_t bytes)
        {
            if (ec) {
                self->fnClose();
                return;
            }

            std::string msg = beast::buffers_to_string(self->m_buffer.data());
            self->m_buffer.consume(bytes);

            if (self->m_owner && self->m_owner->m_fnOnMessage)
                self->m_owner->m_fnOnMessage(msg, self.get());

            self->fnDoRead();
        });
}

void cNetWebSocketServer::sWsSession::fnSendTextQueued(std::string txt)
{
    if (!m_open) return;

    auto self = shared_from_this();

    // post в executor websocket stream, чтобы и очередь и writes жили в одном потоке.
    boost::asio::post(m_ws.get_executor(), [self, txt = std::move(txt)]() mutable
        {
            if (!self->m_open) return;

            self->m_outQ.push_back(std::move(txt));
            if (!self->m_writing) {
                self->m_writing = true;
                self->fnDoWrite();
            }
        });
}

void cNetWebSocketServer::sWsSession::fnDoWrite()
{
    auto self = shared_from_this();

    if (!m_open || m_outQ.empty()) {
        m_writing = false;
        return;
    }

    const std::string& front = m_outQ.front();
    m_ws.text(true);

    m_ws.async_write(boost::asio::buffer(front),
        [self](beast::error_code ec, std::size_t)
        {
            if (ec) {
                self->fnClose();
                return;
            }

            self->m_outQ.pop_front();
            self->fnDoWrite();
        });
}

void cNetWebSocketServer::sWsSession::fnClose()
{
    if (!m_open) return;
    m_open = false;

    if (m_owner) m_owner->fnUnregisterSession(this);
    if (m_owner && m_owner->m_fnOnDisconnected)
        m_owner->m_fnOnDisconnected(this);

    beast::error_code ec;
    // нормальное закрытие
    m_ws.close(websocket::close_code::normal, ec);
}

// ---------------- server ----------------

cNetWebSocketServer::cNetWebSocketServer(boost::asio::io_context& ctx, unsigned short port)
    : m_ctx(ctx)
    , m_port(port)
{
}

cNetWebSocketServer::~cNetWebSocketServer()
{
    fnStop();
}

bool cNetWebSocketServer::fnStart()
{
    if (m_running) return true;

    beast::error_code ec;
    tcp::endpoint ep(tcp::v4(), m_port);

    m_acceptor = std::make_unique<tcp::acceptor>(m_ctx);

    m_acceptor->open(ep.protocol(), ec);
    if (ec) { std::cerr << "[WS] open: " << ec.message() << "\n"; return false; }

    m_acceptor->set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) { std::cerr << "[WS] reuse_address: " << ec.message() << "\n"; return false; }

    m_acceptor->bind(ep, ec);
    if (ec) { std::cerr << "[WS] bind: " << ec.message() << "\n"; return false; }

    m_acceptor->listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) { std::cerr << "[WS] listen: " << ec.message() << "\n"; return false; }

    m_running = true;
    fnDoAccept();
    return true;
}

void cNetWebSocketServer::fnStop()
{
    if (!m_running) return;
    m_running = false;

    if (m_acceptor) {
        beast::error_code ec;
        m_acceptor->close(ec);
        m_acceptor.reset();
    }

    std::lock_guard<std::mutex> lg(m_sessionsMtx);
    m_sessions.clear();
}

void cNetWebSocketServer::fnDoAccept()
{
    if (!m_running || !m_acceptor) return;

    m_acceptor->async_accept([this](beast::error_code ec, tcp::socket sock)
        {
            if (!ec) {
                auto session = std::make_shared<sWsSession>(std::move(sock), this);
                session->fnStart();
            }
            else {
                std::cerr << "[WS] accept error: " << ec.message() << "\n";
            }

            // продолжаем принимать
            fnDoAccept();
        });
}

void cNetWebSocketServer::fnRegisterSession(void* key, std::shared_ptr<sWsSession> s)
{
    std::lock_guard<std::mutex> lg(m_sessionsMtx);
    m_sessions[key] = s;
}

void cNetWebSocketServer::fnUnregisterSession(void* key)
{
    std::lock_guard<std::mutex> lg(m_sessionsMtx);
    m_sessions.erase(key);
}

bool cNetWebSocketServer::fnSendText(void* pSession, const std::string& txt)
{
    std::shared_ptr<sWsSession> s;
    {
        std::lock_guard<std::mutex> lg(m_sessionsMtx);
        auto it = m_sessions.find(pSession);
        if (it == m_sessions.end()) return false;
        s = it->second.lock();
    }
    if (!s) return false;

    s->fnSendTextQueued(txt);
    return true;
}
