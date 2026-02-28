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
#include <iomanip>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif
#include <nlohmann/json.hpp>

#include "AudioStreamer.h"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

std::atomic<bool> g_running{ true };
std::unique_ptr<AudioStreamer> g_audioStreamer;

void printColored(const std::string& text, int color) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
    std::cout << text;
    SetConsoleTextAttribute(hConsole, 7); // Возвращаем белый
#else
    std::cout << text;
#endif
}

void printTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_s(&tm, &time);

    std::cout << "[";
    std::cout << std::put_time(&tm, "%H:%M:%S");
    std::cout << "." << std::setfill('0') << std::setw(3) << ms.count();
    std::cout << "] ";
}

void fail(beast::error_code ec, char const* what) {
    printTimestamp();
    std::cerr << "Error: " << what << ": " << ec.message() << "\n";
}

void audioEventHandler(const AudioEvent& event) {
    printTimestamp();

    switch (event.type) {
    case AudioEventType::STREAM_STARTED:
        printColored("[AUDIO] ", 10); // Зеленый
        std::cout << event.message << "\n";
        break;

    case AudioEventType::STREAM_STOPPED:
        printColored("[AUDIO] ", 14); // Желтый
        std::cout << event.message << "\n";
        break;

    case AudioEventType::BUFFER_OVERFLOW:
    case AudioEventType::BUFFER_UNDERRUN:
        printColored("[AUDIO] ", 12); // Красный
        std::cout << event.message;
        if (event.data > 0) {
            std::cout << " (" << event.data << " packets)";
        }
        std::cout << "\n";
        break;

    case AudioEventType::ENCODING_ERROR:
    case AudioEventType::DECODING_ERROR:
    case AudioEventType::DEVICE_ERROR:
        printColored("[AUDIO ERROR] ", 12); // Красный
        std::cout << event.message << "\n";
        break;

    case AudioEventType::VOLUME_CHANGED:
        printColored("[AUDIO] ", 10); // Зеленый
        std::cout << "Volume: " << event.data << "%\n";
        break;

    default:
        printColored("[AUDIO] ", 13); // Пурпурный
        std::cout << event.message << "\n";
        break;
    }
}

void do_read(websocket::stream<beast::tcp_stream>& ws) {
    beast::flat_buffer buffer;

    while (g_running) {
        try {
            ws.read(buffer);
            auto msg = beast::buffers_to_string(buffer.data());

            // Парсим JSON сообщение
            try {
                auto json = nlohmann::json::parse(msg);

                // Обработка аудио данных от сервера
                if (json.contains("type")) {
                    if (json["type"] == "audio_data" && g_audioStreamer) {
                        g_audioStreamer->processIncomingAudio(json);
                    }
                    else {
                        printTimestamp();
                        std::cout << "< " << msg << std::endl;
                    }
                }
                else {
                    printTimestamp();
                    std::cout << "< " << msg << std::endl;
                }
            }
            catch (...) {
                // Если не JSON, выводим как обычное сообщение
                printTimestamp();
                std::cout << "< " << msg << std::endl;
            }

            buffer.consume(buffer.size());
        }
        catch (beast::system_error const& se) {
            if (se.code() == websocket::error::closed ||
                se.code() == net::error::eof ||
                se.code() == net::error::operation_aborted) {
                printTimestamp();
                std::cout << "[ws] connection closed\n";
                break;
            }
            fail(se.code(), "read");
            break;
        }
        catch (std::exception const& e) {
            printTimestamp();
            std::cerr << "read exception: " << e.what() << "\n";
            break;
        }
    }
}

void printStats() {
    if (g_audioStreamer) {
        uint32_t sent, received, lost;
        uint64_t bytesSent, bytesReceived;
        float jitter;

        g_audioStreamer->getStats(sent, received, lost, bytesSent, bytesReceived, jitter);

        printTimestamp();
        printColored("[STATS] ", 11); // Голубой
        std::cout << "State: " << g_audioStreamer->getStateString();
        std::cout << " | Sent: " << sent << " pkts (" << bytesSent / 1024 << " KB)";
        std::cout << " | Recv: " << received << " pkts (" << bytesReceived / 1024 << " KB)";
        std::cout << " | Lost: " << lost << " pkts";
        std::cout << " | Jitter: " << std::fixed << std::setprecision(2) << jitter << " ms\n";
    }
}

void showAudioDevices() {
    std::cout << "\n=== Input Devices ===\n";
    auto inputs = AudioStreamer::getInputDevices();
    for (const auto& dev : inputs) {
        std::cout << "  " << dev.first << ": " << dev.second << "\n";
    }

    std::cout << "\n=== Output Devices ===\n";
    auto outputs = AudioStreamer::getOutputDevices();
    for (const auto& dev : outputs) {
        std::cout << "  " << dev.first << ": " << dev.second << "\n";
    }
    std::cout << "\n";
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(1251);
    SetConsoleCP(1251);
#endif

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <host> <port> [options]\n";
        std::cerr << "Options:\n";
        std::cerr << "  --audio           Enable audio streaming\n";
        std::cerr << "  --both            Enable both capture and playback\n";
        std::cerr << "  --capture-only    Enable only audio capture\n";
        std::cerr << "  --playback-only   Enable only audio playback\n";
        std::cerr << "  --devices         Show available audio devices\n";
        std::cerr << "  --rate <hz>       Set sample rate (default: 48000)\n";
        std::cerr << "  --bitrate <kbps>  Set Opus bitrate (default: 64)\n";
        std::cerr << "Example:\n";
        std::cerr << "  " << argv[0] << " 127.0.0.1 8080 --audio\n";
        std::cerr << "  " << argv[0] << " localhost 4433 --both --rate 44100\n";
        return 1;
    }

    std::string host = argv[1];
    auto const  port = argv[2];

    bool enableAudio = false;
    bool enableCapture = false;
    bool enablePlayback = false;
    bool showDevices = false;
    int sampleRate = 48000;
    int bitrate = 64000;

    // Парсим аргументы
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--audio") {
            enableAudio = true;
            enableCapture = true;
        }
        else if (arg == "--both") {
            enableAudio = true;
            enableCapture = true;
            enablePlayback = true;
        }
        else if (arg == "--capture-only") {
            enableAudio = true;
            enableCapture = true;
            enablePlayback = false;
        }
        else if (arg == "--playback-only") {
            enableAudio = true;
            enableCapture = false;
            enablePlayback = true;
        }
        else if (arg == "--devices") {
            showDevices = true;
        }
        else if (arg == "--rate" && i + 1 < argc) {
            sampleRate = std::stoi(argv[++i]);
        }
        else if (arg == "--bitrate" && i + 1 < argc) {
            bitrate = std::stoi(argv[++i]) * 1000;
        }
    }

    if (showDevices) {
        showAudioDevices();
        return 0;
    }

    try {
        net::io_context ioc{ 1 };
        tcp::resolver resolver{ ioc };
        auto const results = resolver.resolve(host, port);

        beast::tcp_stream stream{ ioc };
        stream.expires_after(std::chrono::seconds(30));
        stream.connect(results);

        websocket::stream<beast::tcp_stream> ws{ std::move(stream) };

        ws.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(beast::http::field::user_agent, "AudioStreamer/1.0");
            }));

        ws.next_layer().expires_never();
        ws.handshake(host, "/ws");

        printTimestamp();
        std::cout << "[connected] " << host << ":" << port << "\n";

        // Инициализация аудио если требуется
        if (enableAudio) {
            printTimestamp();
            std::cout << "[audio] Initializing audio streamer...\n";

            g_audioStreamer = std::make_unique<AudioStreamer>(ws);

            // Настройка конфигурации
            AudioConfig config;
            config.sampleRate = sampleRate;
            config.bitrate = bitrate;
            config.frameSize = sampleRate / 50; // 20ms frames

            g_audioStreamer->setEventCallback(audioEventHandler);

            if (g_audioStreamer->initialize(config)) {
                printTimestamp();
                std::cout << "[audio] Initialized successfully\n";

                // Автоматический запуск в зависимости от режима
                if (enableCapture) {
                    g_audioStreamer->startCapture();
                }
                if (enablePlayback) {
                    g_audioStreamer->startPlayback();
                }
            }
            else {
                printTimestamp();
                std::cerr << "[audio] Failed to initialize\n";
                g_audioStreamer.reset();
            }
        }

        std::cout << "\nCommands:\n";
        std::cout << "  send <json>          - отправить сообщение\n";
        if (enableAudio) {
            std::cout << "\nAudio Commands:\n";
            std::cout << "  audio start          - начать захват аудио\n";
            std::cout << "  audio stop           - остановить захват аудио\n";
            std::cout << "  audio playback start - начать воспроизведение\n";
            std::cout << "  audio playback stop  - остановить воспроизведение\n";
            std::cout << "  audio volume <0-200> - установить громкость (%)\n";
            std::cout << "  audio stats          - показать статистику\n";
            std::cout << "  audio config         - показать конфигурацию\n";
            std::cout << "  audio devices        - показать доступные устройства\n";
        }
        std::cout << "\nTest Commands:\n";
        std::cout << "  testCreate           - создать конференцию\n";
        std::cout << "  testJoin <code>      - присоединиться к конференции\n";
        std::cout << "  testSend             - отправить тестовое сообщение\n";
        std::cout << "\nOther:\n";
        std::cout << "  quit / exit / q      - завершить\n";
        std::cout << "  help                  - эта справка\n\n";

        // Запускаем поток чтения
        std::thread reader([&ws] { do_read(ws); });

        std::string line;
        bool statsTimerRunning = false;

        while (g_running && std::getline(std::cin, line)) {
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;

            if (cmd == "quit" || cmd == "exit" || cmd == "q") {
                g_running = false;
                break;
            }
            else if (cmd == "help" || cmd == "?") {
                std::cout << "Type 'help' for commands...\n";
                continue;
            }
            else if (cmd == "send") {
                std::string rest;
                std::getline(iss >> std::ws, rest);
                if (rest.empty()) {
                    std::cout << "Usage: send {\"type\":\"hello\"}\n";
                    continue;
                }

                try {
                    ws.write(net::buffer(rest));
                    printTimestamp();
                    std::cout << "> " << rest << "\n";
                }
                catch (std::exception const& e) {
                    std::cerr << "write error: " << e.what() << "\n";
                    g_running = false;
                    break;
                }
            }
            else if (enableAudio && cmd == "audio") {
                std::string subcmd;
                iss >> subcmd;

                if (!g_audioStreamer) {
                    std::cout << "Audio not initialized\n";
                    continue;
                }

                if (subcmd == "start") {
                    g_audioStreamer->startCapture();
                }
                else if (subcmd == "stop") {
                    g_audioStreamer->stopCapture();
                }
                else if (subcmd == "playback") {
                    std::string playcmd;
                    iss >> playcmd;
                    if (playcmd == "start") {
                        g_audioStreamer->startPlayback();
                    }
                    else if (playcmd == "stop") {
                        g_audioStreamer->stopPlayback();
                    }
                }
                else if (subcmd == "volume") {
                    int vol;
                    if (iss >> vol) {
                        g_audioStreamer->setVolume(vol / 100.0f);
                    }
                }
                else if (subcmd == "stats") {
                    printStats();
                }
                else if (subcmd == "config") {
                    auto cfg = g_audioStreamer->getConfig();
                    std::cout << "Sample Rate: " << cfg.sampleRate << " Hz\n";
                    std::cout << "Channels: " << cfg.channels << "\n";
                    std::cout << "Frame Size: " << cfg.frameSize << " samples\n";
                    std::cout << "Bitrate: " << cfg.bitrate / 1000 << " kbps\n";
                    std::cout << "Volume: " << static_cast<int>(cfg.volume * 100) << "%\n";
                }
                else if (subcmd == "devices") {
                    showAudioDevices();
                }
            }
            else if (cmd == "testCreate") {
                std::string rest = "{\"type\":\"conf_create\"}";
                try {
                    ws.write(net::buffer(rest));
                    printTimestamp();
                    std::cout << "> " << rest << "\n";
                }
                catch (std::exception const& e) {
                    std::cerr << "write error: " << e.what() << "\n";
                    g_running = false;
                    break;
                }
            }
            else if (cmd == "testJoin") {
                std::string rest;
                std::getline(iss >> std::ws, rest);
                if (rest.empty()) {
                    std::cout << "Usage: testJoin 65GE46D\n";
                    continue;
                }
                nlohmann::json j = {
                    {"type", "conf_join"},
                    {"invite", rest}
                };
                try {
                    ws.write(net::buffer(j.dump()));
                    printTimestamp();
                    std::cout << "> " << j.dump() << "\n";
                }
                catch (std::exception const& e) {
                    std::cerr << "write error: " << e.what() << "\n";
                    g_running = false;
                    break;
                }
            }
            else if (cmd == "testSend") {
                std::string rest = "{\"type\":\"chat_message\",\"text\":\"Hello World!\"}";
                try {
                    ws.write(net::buffer(rest));
                    printTimestamp();
                    std::cout << "> " << rest << "\n";
                }
                catch (std::exception const& e) {
                    std::cerr << "write error: " << e.what() << "\n";
                    g_running = false;
                    break;
                }
            }
            else {
                std::cout << "Unknown command. Type 'help' for commands.\n";
            }
        }

        g_running = false;

        // Останавливаем аудио перед закрытием
        if (g_audioStreamer) {
            g_audioStreamer->shutdown();
        }

        ws.close(websocket::close_code::normal);
        reader.join();
    }
    catch (beast::system_error const& se) {
        fail(se.code(), "main");
    }
    catch (std::exception const& e) {
        printTimestamp();
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }

    printTimestamp();
    std::cout << "[disconnected]\n";
    return 0;
}