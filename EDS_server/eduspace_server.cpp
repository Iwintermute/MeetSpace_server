#include "core/cAppCore.h"
#include <iostream>

int main()
{
    Sys::cAppCore app;

    const unsigned short wsPort = 9000;
    const unsigned short httpPort = 8080;

    if (!app.fnInit(wsPort, httpPort)) {
        std::cerr << "Failed to init server\n";
        return 1;
    }

    std::cout << "Server running.\n";
    std::cout << "WS:   ws://127.0.0.1:" << wsPort << "\n";
    std::cout << "HTTP: http://127.0.0.1:" << httpPort << "/health\n";

    app.fnRun();
    return 0;
}
