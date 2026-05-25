#include <cstdlib>
#include "client/Game.h"
#include "shared/util/Logger.h"
#include "shared/network/Protocol.h"

int main(int argc, char* argv[]) {
    DZ_LOG_INFO("=== Dead Zone: Ashes — Client ===");

    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    uint16_t    port = (argc > 2) ? static_cast<uint16_t>(std::atoi(argv[2]))
                                   : dz::DEFAULT_SERVER_PORT;

    DZ_LOG_INFO("Connecting to %s:%u ...", host, port);

    dz::Game game;
    return game.run(host, port);
}
