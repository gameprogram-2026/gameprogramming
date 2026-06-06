#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <SDL2/SDL.h>
#include "client/Game.h"
#include "shared/util/Logger.h"
#include "shared/network/Protocol.h"

namespace {

bool isLocalHost(const char* host) {
    return std::strcmp(host, "127.0.0.1") == 0 ||
           std::strcmp(host, "localhost") == 0 ||
           std::strcmp(host, "::1") == 0;
}

bool hasServerEnv() {
    return std::getenv("DEADZONE_DB_HOST") ||
           std::getenv("DEADZONE_DB_USER") ||
           std::getenv("DEADZONE_DB_PASS") ||
           std::getenv("DEADZONE_DB_NAME");
}

bool isUdpPortInUse(uint16_t port) {
    char command[128];
    std::snprintf(command, sizeof(command), "lsof -iUDP:%u -nP -t 2>/dev/null", port);

    FILE* pipe = popen(command, "r");
    if (!pipe) return false;

    char buffer[32]{};
    const bool found = std::fgets(buffer, sizeof(buffer), pipe) != nullptr;
    pclose(pipe);
    return found;
}

void autoStartLocalServer(const char* host, uint16_t port) {
    if (!isLocalHost(host)) return;
    if (isUdpPortInUse(port)) {
        DZ_LOG_INFO("Local server already running on UDP %u", port);
        return;
    }
    if (access("./DeadZoneServer", X_OK) != 0) {
        DZ_LOG_WARN("DeadZoneServer not found next to client; auto-start skipped");
        return;
    }

    if (!hasServerEnv()) {
        DZ_LOG_WARN("DB environment is not set; local server auth will be disabled. Run run_server.sh or scripts/setup_database.sh first.");
    }

    char command[512];
    std::snprintf(command, sizeof(command),
                  "nohup ./DeadZoneServer %u > server.log 2>&1 &",
                  port);

    DZ_LOG_INFO("Auto-starting local server on UDP %u", port);
    if (std::system(command) != 0) {
        DZ_LOG_WARN("Failed to auto-start local server");
        return;
    }

    usleep(700 * 1000);
}

}

int main(int argc, char* argv[]) {
    if (char* basePath = SDL_GetBasePath()) {
        if (chdir(basePath) != 0) {
            DZ_LOG_WARN("Failed to change working directory to %s", basePath);
        }
        SDL_free(basePath);
    }

    DZ_LOG_INFO("=== Dead Zone: Ashes — Client ===");

    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    uint16_t    port = (argc > 2) ? static_cast<uint16_t>(std::atoi(argv[2]))
                                   : dz::DEFAULT_SERVER_PORT;

    DZ_LOG_INFO("Connecting to %s:%u ...", host, port);
    autoStartLocalServer(host, port);

    dz::Game game;
    return game.run(host, port);
}
