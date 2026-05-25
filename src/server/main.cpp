#include <cstdlib>
#include "shared/util/Logger.h"
#include "shared/network/Protocol.h"
#include "server/GameServer.h"

int main(int argc, char* argv[]) {
    uint16_t port = dz::DEFAULT_SERVER_PORT;
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));

    DZ_LOG_INFO("=== Dead Zone: Ashes — Dedicated Server ===");
    DZ_LOG_INFO("Port: %u | Max clients: %d | Tick rate: %.0f Hz",
                port, dz::MAX_CLIENTS, dz::WORLD_TICK_RATE);

    dz::GameServer server(port);
    return server.run();
}
