#include "networkEDS.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// A minimal interactive client that wires NetworkEDS into a command loop.
// It demonstrates connecting to the demo websocket server (ws://localhost:9000),
// joining/creating conferences, sending chat messages, and toggling audio capture
// and playback.

static void PrintHelp()
{
    std::cout << "Available commands:\n"
        << "  /connect <host> <port>   -- connect to websocket server (default 127.0.0.1 9000)\n"
        << "  /create <title>          -- create a conference with the given title\n"
        << "  /join <id>               -- join a conference by id\n"
        << "  /leave                   -- leave the active conference\n"
        << "  /chat <text>             -- send a chat message to the active conference\n"
        << "  /mic <on|off>            -- enable/disable microphone capture\n"
        << "  /speaker <on|off>        -- enable/disable speaker playback\n"
        << "  /start_audio             -- manually start audio capture & playback threads\n"
        << "  /stop_audio              -- stop audio capture & playback threads\n"
        << "  /status                  -- print current connection/status string\n"
        << "  /help                    -- show this help\n"
        << "  /quit                    -- exit the demo\n";
}

static std::vector<std::string> Tokenize(const std::string& line)
{
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

int main()
{
    auto manager = NetworkEDS::CreateNetworkManager();
    if (!manager->Initialize())
    {
        std::cerr << "Failed to initialize NetworkEDS" << std::endl;
        return 1;
    }

    // Basic logging callbacks for demo visibility.
    manager->SetConnectedCallback([]()
        {
            std::cout << "[demo] Connected to server" << std::endl;
        });

    manager->SetDisconnectedCallback([]()
        {
            std::cout << "[demo] Disconnected from server" << std::endl;
        });

    manager->SetMessageCallback([](const std::string& message)
        {
            std::cout << "[server] " << message << std::endl;
        });

    manager->SetConferenceCreatedCallback([](int id)
        {
            std::cout << "[demo] Conference created with id " << id << std::endl;
        });

    manager->SetConferenceJoinedCallback([](int id, const std::string& self)
        {
            std::cout << "[demo] Joined conference " << id << " as " << self << std::endl;
        });

    manager->SetPeerJoinedCallback([](const std::string& peer)
        {
            std::cout << "[demo] Peer joined: " << peer << std::endl;
        });

    manager->SetPeerLeftCallback([](const std::string& peer)
        {
            std::cout << "[demo] Peer left: " << peer << std::endl;
        });

    std::cout << "NetworkEDS demo client. Type /help for commands.\n";

    // Attempt default connection upfront.
    manager->ConnectToServer("127.0.0.1", 9000);

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.empty()) continue;
        auto tokens = Tokenize(line);
        if (tokens.empty()) continue;

        const auto& cmd = tokens[0];
        if (cmd == "/quit")
        {
            break;
        }
        else if (cmd == "/help")
        {
            PrintHelp();
        }
        else if (cmd == "/status")
        {
            std::cout << manager->GetStatus() << std::endl;
        }
        else if (cmd == "/connect")
        {
            std::string host = tokens.size() > 1 ? tokens[1] : "127.0.0.1";
            uint16_t port = tokens.size() > 2 ? static_cast<uint16_t>(std::stoi(tokens[2])) : 9000;
            manager->ConnectToServer(host, port);
        }
        else if (cmd == "/create" && tokens.size() >= 2)
        {
            std::string title = line.substr(line.find(tokens[1]));
            int id = manager->CreateConference(title);
            std::cout << "[demo] Requested conference creation (temp id " << id << ")" << std::endl;
        }
        else if (cmd == "/join" && tokens.size() >= 2)
        {
            int confId = std::stoi(tokens[1]);
            if (!manager->JoinConference(confId))
            {
                std::cout << "[demo] Join failed" << std::endl;
            }
        }
        else if (cmd == "/leave")
        {
            manager->LeaveConference();
        }
        else if (cmd == "/chat" && tokens.size() >= 2)
        {
            std::string message = line.substr(line.find(tokens[1]));
            manager->SendChatMessage(message);
        }
        else if (cmd == "/mic" && tokens.size() >= 2)
        {
            manager->ToggleMicrophone(tokens[1] == "on");
        }
        else if (cmd == "/speaker" && tokens.size() >= 2)
        {
            manager->ToggleSpeaker(tokens[1] == "on");
        }
        else if (cmd == "/start_audio")
        {
            manager->StartAudioCapture();
            manager->StartAudioPlayback();
        }
        else if (cmd == "/stop_audio")
        {
            manager->StopAudioCapture();
            manager->StopAudioPlayback();
        }
        else
        {
            std::cout << "Unknown command. Type /help for usage." << std::endl;
        }
    }

    manager->Shutdown();
    return 0;
}