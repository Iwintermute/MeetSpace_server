#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <sstream>

#include<windows.h>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

std::atomic<bool> g_running{ true };

void fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

void do_read(websocket::stream<beast::tcp_stream>& ws)
{
    beast::flat_buffer buffer;

    while (g_running)
    {
        try
        {
            ws.read(buffer);
            auto msg = beast::buffers_to_string(buffer.data());
            std::cout << "\n< " << msg << std::endl;
            buffer.consume(buffer.size());
        }
        catch (beast::system_error const& se)
        {
            if (se.code() == websocket::error::closed ||
                se.code() == net::error::eof ||
                se.code() == net::error::operation_aborted)
            {
                std::cout << "[ws] connection closed\n";
                break;
            }
            fail(se.code(), "read");
            break;
        }
        catch (std::exception const& e)
        {
            std::cerr << "read exception: " << e.what() << "\n";
            break;
        }
    }
}

int main(int argc, char** argv)
{

#ifdef _WIN32
    SetConsoleOutputCP(1251);
#endif
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <host> <port>\n";
        std::cerr << "Example:\n";
        std::cerr << "  " << argv[0] << " 127.0.0.1 8080\n";
        std::cerr << "  " << argv[0] << " localhost 4433\n";
        return 1;
    }

    std::string host = argv[1];
    auto const  port = argv[2];

    try
    {
        net::io_context ioc{ 1 };

        tcp::resolver resolver{ ioc };
        auto const results = resolver.resolve(host, port);

        beast::tcp_stream stream{ ioc };

        // Устанавливаем таймаут на операцию connect
        stream.expires_after(std::chrono::seconds(30));

        // Подключаемся
        stream.connect(results);

        // Теперь создаём websocket поверх tcp_stream
        websocket::stream<beast::tcp_stream> ws{ std::move(stream) };

        // Устанавливаем decorator (опционально)
        ws.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req)
            {
                req.set(beast::http::field::user_agent, "TestCli/1.0");
            }));

        // Отключаем таймауты на чтение/запись (для websocket)
        ws.next_layer().expires_never();

        // Выполняем websocket handshake
        ws.handshake(host, "/ws");   // ← укажи правильный путь, если он у тебя не /ws

        std::cout << "[connected] " << host << ":" << port << "\n";
        std::cout << "Commands:\n";
        std::cout << "  send <json>     - отправить сообщение\n";
        std::cout << "  quit / exit     - выйти\n";
        std::cout << "  help            - эта справка\n\n";

        // Запускаем поток чтения
        std::thread reader([&ws] { do_read(ws); });

        std::string line;
        while (g_running && std::getline(std::cin, line))
        {
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;

            if (cmd == "quit" || cmd == "exit" || cmd == "q")
            {
                g_running = false;
                break;
            }
            else if (cmd == "help" || cmd == "?")
            {
                std::cout << "send <message>     - отправить json или текст\n";
                std::cout << "quit / exit / q    - завершить\n";
                continue;
            }
            else if (cmd == "send")
            {
                std::string rest;
                std::getline(iss >> std::ws, rest);
                if (rest.empty())
                {
                    std::cout << "Usage: send {\"type\":\"hello\"}\n";
                    continue;
                }

                try
                {
                    ws.write(net::buffer(rest));
                    std::cout << "> " << rest << "\n";
                }
                catch (std::exception const& e)
                {
                    std::cerr << "write error: " << e.what() << "\n";
                    g_running = false;
                    break;
                }
            }
            else
            {
                std::cout << "Unknown command. Type 'help' for commands.\n";
            }
        }

        g_running = false;
        ws.close(websocket::close_code::normal);
        reader.join();
    }
    catch (beast::system_error const& se)
    {
        fail(se.code(), "main");
    }
    catch (std::exception const& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }

    std::cout << "[disconnected]\n";
    return 0;
}