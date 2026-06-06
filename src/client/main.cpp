#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits.h>
#include <string>
#include <vector>
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

bool loadServerEnvFile() {
    const std::vector<std::string> candidates = {
        ".env.server",
        "../.env.server",
        "../../.env.server"
    };

    for (const auto& path : candidates) {
        std::ifstream file(path);
        if (!file.is_open()) continue;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos || eq == 0) continue;

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            if (!key.empty()) setenv(key.c_str(), value.c_str(), 0);
        }
        DZ_LOG_INFO("Loaded server DB environment from %s", path.c_str());
        return true;
    }

    return false;
}

std::string shellQuote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string appleScriptString(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    out += "\"";
    return out;
}

std::string absolutePath(const std::string& path) {
    char resolved[PATH_MAX]{};
    if (realpath(path.c_str(), resolved)) return resolved;
    return {};
}

std::string findSetupScript() {
    const std::vector<std::string> candidates = {
        "scripts/setup_database.sh",
        "../scripts/setup_database.sh",
        "../../scripts/setup_database.sh"
    };

    for (const auto& path : candidates) {
        if (access(path.c_str(), X_OK) == 0) return absolutePath(path);
    }
    return {};
}

std::string parentDir(const std::string& path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}

bool runDatabaseSetupInTerminal() {
    const std::string setupScript = findSetupScript();
    if (setupScript.empty()) {
        DZ_LOG_WARN("Database setup script not found");
        return false;
    }

    const std::string scriptsDir = parentDir(setupScript);
    const std::string rootDir = parentDir(scriptsDir);
    const std::string terminalCommand =
        "cd " + shellQuote(rootDir) +
        " && if bash ./scripts/setup_database.sh; then " +
        "echo ''; echo 'DeadZone DB setup finished. You can return to the game window.'; " +
        "else echo ''; echo 'DeadZone DB setup failed. Check the error above and rerun the client.'; fi";

    const std::string osascript =
        "osascript -e " + shellQuote(
            "tell application \"Terminal\" to do script " + appleScriptString(terminalCommand)
        );

    DZ_LOG_INFO("Opening Terminal for database setup: %s", setupScript.c_str());
    return std::system(osascript.c_str()) == 0;
}

bool ensureServerEnv() {
    if (hasServerEnv()) return true;
    if (loadServerEnvFile()) return true;

    if (!runDatabaseSetupInTerminal()) return false;

    constexpr int WAIT_SECONDS = 180;
    for (int i = 0; i < WAIT_SECONDS; ++i) {
        SDL_Delay(1000);
        if (loadServerEnvFile() || hasServerEnv()) return true;
    }

    DZ_LOG_WARN("Timed out waiting for .env.server after database setup");
    return false;
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

bool autoStartLocalServer(const char* host, uint16_t port) {
    if (!isLocalHost(host)) return true;
    if (isUdpPortInUse(port)) {
        DZ_LOG_INFO("Local server already running on UDP %u", port);
        return true;
    }
    if (access("./DeadZoneServer", X_OK) != 0) {
        DZ_LOG_WARN("DeadZoneServer not found next to client; auto-start skipped");
        return false;
    }

    if (!ensureServerEnv()) {
        DZ_LOG_WARN("DB environment is not set; auto-start skipped. Run start_deadzone.command once to create .env.server.");
        return false;
    }

    char command[512];
    std::snprintf(command, sizeof(command),
                  "nohup ./DeadZoneServer %u > server.log 2>&1 &",
                  port);

    DZ_LOG_INFO("Auto-starting local server on UDP %u", port);
    if (std::system(command) != 0) {
        DZ_LOG_WARN("Failed to auto-start local server");
        return false;
    }

    usleep(700 * 1000);
    return isUdpPortInUse(port);
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
    if (!autoStartLocalServer(host, port)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "DeadZone",
                                 "서버 자동 실행에 실패했습니다. 자동으로 열린 Terminal에서 DB 설정을 완료한 뒤 다시 실행하세요.",
                                 nullptr);
        return 1;
    }

    dz::Game game;
    return game.run(host, port);
}
