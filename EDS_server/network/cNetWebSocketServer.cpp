#include "cNetWebSocketServer.h"
#include <iostream>
#include <cstring>

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
    // no close() here  beast handles cleanup
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
                    if (self->m_owner)
                        self->m_owner->fnOnSessionClosed(self.get());
                }
                return;
            }

            bool isBinary = self->m_ws.got_binary();

            if (isBinary) {
                std::vector<uint8_t> data(bytes);
                auto buffers = self->m_buffer.data();
                auto it = buffers.begin();
                size_t copied = 0;
                while (it != buffers.end() && copied < bytes) {
                    auto b = *it++;
                    auto size = std::min<std::size_t>(bytes - copied, b.size());
                    std::memcpy(data.data() + copied, b.data(), size);
                    copied += size;
                }
                if (self->m_owner && self->m_owner->m_fnOnBinary)
                    self->m_owner->m_fnOnBinary(data, self.get());
            }
            else {
                std::string msg = beast::buffers_to_string(self->m_buffer.data());
                if (self->m_owner && self->m_owner->m_fnOnMessage)
                    self->m_owner->m_fnOnMessage(msg, self.get());
            }
            self->m_buffer.consume(bytes);

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
                        if (self->m_owner)
                            self->m_owner->fnOnSessionClosed(self.get());
                    }
                });
        });
}

void cNetWebSocketServer::sWsSession::fnSendBinary(const std::vector<uint8_t>& data)
{
    if (!m_bOpen) return;

    auto self = shared_from_this();
    boost::asio::post(m_ws.get_executor(), [self, data]()
        {
            if (!self->m_bOpen) return;

            self->m_ws.text(false);
            self->m_ws.binary(true);
            self->m_ws.async_write(boost::asio::buffer(data),
                [self](boost::system::error_code ec, std::size_t)
                {
                    if (ec && self->m_bOpen) {
                        self->m_bOpen = false;
                        if (self->m_owner && self->m_owner->m_fnOnDisconnected)
                            self->m_owner->m_fnOnDisconnected(self.get());
                        if (self->m_owner)
                            self->m_owner->fnOnSessionClosed(self.get());
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
    , m_fnOnBinary(nullptr)
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
                {
                    std::lock_guard<std::mutex> lg(m_mtxSessions);
                    m_sessions.emplace(session.get(), session);
                }
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

void cNetWebSocketServer::fnSendText(void* pSession, const std::string& txt)
{
    std::shared_ptr<sWsSession> session;
    {
        std::lock_guard<std::mutex> lg(m_mtxSessions);
        auto it = m_sessions.find(pSession);
        if (it != m_sessions.end()) session = it->second.lock();
    }
    if (session) session->fnSendText(txt);
}

void cNetWebSocketServer::fnSendBinary(void* pSession, const std::vector<uint8_t>& data)
{
    std::shared_ptr<sWsSession> session;
    {
        std::lock_guard<std::mutex> lg(m_mtxSessions);
        auto it = m_sessions.find(pSession);
        if (it != m_sessions.end()) session = it->second.lock();
    }
    if (session) session->fnSendBinary(data);
}

void cNetWebSocketServer::fnBroadcastText(const std::string& txt, void* pSkip)
{
    std::vector<std::shared_ptr<sWsSession>> sessions;
    {
        std::lock_guard<std::mutex> lg(m_mtxSessions);
        for (auto it = m_sessions.begin(); it != m_sessions.end(); ) {
            if (auto s = it->second.lock()) {
                if (it->first != pSkip) sessions.push_back(std::move(s));
                ++it;
            }
            else {
                it = m_sessions.erase(it);
            }
        }
    }
    for (auto& s : sessions) s->fnSendText(txt);
}

void cNetWebSocketServer::fnBroadcastBinary(const std::vector<uint8_t>& data, void* pSkip)
{
    std::vector<std::shared_ptr<sWsSession>> sessions;
    {
        std::lock_guard<std::mutex> lg(m_mtxSessions);
        for (auto it = m_sessions.begin(); it != m_sessions.end(); ) {
            if (auto s = it->second.lock()) {
                if (it->first != pSkip) sessions.push_back(std::move(s));
                ++it;
            }
            else {
                it = m_sessions.erase(it);
            }
        }
    }
    for (auto& s : sessions) s->fnSendBinary(data);
}

void cNetWebSocketServer::fnOnSessionClosed(void* ptr)
{
    std::lock_guard<std::mutex> lg(m_mtxSessions);
    m_sessions.erase(ptr);
}
