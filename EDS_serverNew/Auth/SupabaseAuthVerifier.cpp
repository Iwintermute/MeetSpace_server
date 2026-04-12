#include "Auth/SupabaseAuthVerifier.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include <nlohmann/json.hpp>
#include <openssl/ssl.h>

#include <cstring>
#include <optional>
#include <string>

namespace eds::server_new::auth {
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    namespace ssl = net::ssl;
    using tcp = net::ip::tcp;

    static bool parseHttpsUrl(
        const std::string& url,
        std::string& host,
        std::string& port,
        std::string& target) {
        constexpr const char* prefix = "https://";
        if (url.rfind(prefix, 0) != 0) {
            return false;
        }

        std::string rest = url.substr(std::strlen(prefix));
        auto slash = rest.find('/');
        std::string hostPort = slash == std::string::npos ? rest : rest.substr(0, slash);
        std::string basePath = slash == std::string::npos ? std::string() : rest.substr(slash);

        if (!basePath.empty() && basePath.back() == '/') {
            basePath.pop_back();
        }

        target = basePath + "/auth/v1/user";

        auto colon = hostPort.find(':');
        if (colon == std::string::npos) {
            host = hostPort;
            port = "443";
        }
        else {
            host = hostPort.substr(0, colon);
            port = hostPort.substr(colon + 1);
        }

        return !host.empty();
    }

    std::optional<VerifiedSupabaseUser> SupabaseAuthVerifier::verifyAccessToken(const std::string& accessToken) {
        if (accessToken.empty()) {
            return std::nullopt;
        }
        if (projectUrl_.empty() || publishableKey_.empty()) {
            return std::nullopt;
        }

        std::string host;
        std::string port;
        std::string target;
        if (!parseHttpsUrl(projectUrl_, host, port, target)) {
            return std::nullopt;
        }

        try {
            net::io_context io;
            ssl::context ctx(ssl::context::tls_client);
            ctx.set_default_verify_paths();

            beast::ssl_stream<beast::tcp_stream> stream(io, ctx);
            stream.set_verify_mode(ssl::verify_peer);

            if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                return std::nullopt;
            }

            auto const results = tcp::resolver(io).resolve(host, port);
            beast::get_lowest_layer(stream).connect(results);
            stream.handshake(ssl::stream_base::client);

            http::request<http::empty_body> req{ http::verb::get, target, 11 };
            req.set(http::field::host, host);
            req.set(http::field::user_agent, "MeetSpaceServer/1.0");
            req.set("apikey", publishableKey_);
            req.set(http::field::authorization, "Bearer " + accessToken);

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.shutdown(ec);

            if (res.result() != http::status::ok) {
                return std::nullopt;
            }

            auto json = nlohmann::json::parse(res.body(), nullptr, false);
            if (!json.is_object()) {
                return std::nullopt;
            }

            VerifiedSupabaseUser out;
            out.userId = json.value("id", "");
            out.email = json.value("email", "");

            if (out.userId.empty()) {
                return std::nullopt;
            }

            return out;
        }
        catch (...) {
            return std::nullopt;
        }
    }

} // namespace eds::server_new::auth