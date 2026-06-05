#include "Game.h"
#include "shared/util/Logger.h"
#include "shared/network/Packet.h"
#include "shared/MapSetup.h"
#include "shared/ItemData.h"
#include "shared/ecs/components/BuildingComponent.h"
#include <SDL2/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>

namespace dz {

namespace {
struct DismantlePreviewResult {
    const char* key;
    int qty;
};

struct DismantlePreviewRecipe {
    const char* source;
    DismantlePreviewResult results[3];
    int resultCount;
};

const DismantlePreviewRecipe* findDismantlePreview(const std::string& key) {
    static const DismantlePreviewRecipe recipes[] = {
        {"scrap_pipe",   {{"scrap_metal", 1}, {"", 0}, {"", 0}}, 1},
        {"nail_bat",     {{"plank", 1}, {"scrap_metal", 1}, {"", 0}}, 2},
        {"fire_axe",     {{"scrap_metal", 2}, {"", 0}, {"", 0}}, 1},
        {"pistol_9mm",   {{"scrap_metal", 2}, {"electronic_part", 1}, {"", 0}}, 2},
        {"smg_9mm",      {{"scrap_metal", 3}, {"electronic_part", 1}, {"", 0}}, 2},
        {"flamethrower", {{"scrap_metal", 3}, {"oil", 2}, {"electronic_part", 1}}, 3},
        {"molotov",      {{"oil", 1}, {"", 0}, {"", 0}}, 1},
    };
    for (const auto& recipe : recipes) {
        if (key == recipe.source) return &recipe;
    }
    return nullptr;
}

std::string formatDismantlePreview(const DismantlePreviewRecipe& recipe) {
    std::string out = "분해 결과: ";
    for (int i = 0; i < recipe.resultCount; ++i) {
        if (i > 0) out += ", ";
        out += getDisplayName(recipe.results[i].key);
        out += " x";
        out += std::to_string(recipe.results[i].qty);
    }
    out += " / X 다시 누르면 분해";
    return out;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Game constructor
// ─────────────────────────────────────────────────────────────────────────────
Game::Game() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    m_camera.zoom    = 1.5f;
    m_camera.screenW = 1280;
    m_camera.screenH = 720;

    // 탈출존은 맵 로드 후 loadExtractionZones()에서 동적 설정
}

Game::~Game() { shutdown(); }

// ─────────────────────────────────────────────────────────────────────────────
// tryInteract — send pickup or open workbench
// ─────────────────────────────────────────────────────────────────────────────
void Game::tryInteract() {
    int doorID = m_map.findNearestDoor(m_net.localX(), m_net.localY(), TILE_SIZE * 2.0f);
    if (doorID >= 0) {
        const auto& door = m_map.getDoors()[doorID];
        if (door.broken) return;
        m_net.sendDoorToggle(static_cast<uint16_t>(doorID));
        m_notifyMsg = door.open ? "문을 닫는 중..." : "문을 여는 중...";
        m_notifyTimer = 1.0f;
        return;
    }

    if (m_nearestInteractNetID < 0) return;
    
    if (m_nearestInteractType == REC_LOOT) {
        const bool inventoryFull = m_inventory.usedSlots >= 20 ||
                                   m_inventory.totalWeight >= m_inventory.maxWeight;
        if (inventoryFull) {
            m_notifyMsg = "가방이 가득 찼습니다.";
            m_notifyTimer = 1.2f;
            return;
        }

        const int pickedNetID = m_nearestInteractNetID;
        auto it = std::find_if(m_clientHiddenNetIDs.begin(), m_clientHiddenNetIDs.end(),
                               [&](const PendingHiddenLoot& h) { return h.netID == pickedNetID; });
        const uint32_t hideUntil = SDL_GetTicks() + 1250;
        if (it == m_clientHiddenNetIDs.end()) {
            m_clientHiddenNetIDs.push_back({pickedNetID, hideUntil});
        } else {
            it->expiresAtMs = hideUntil;
        }
        m_net.sendLootPickup(static_cast<uint32_t>(pickedNetID));
        m_audio.playSound("loot");
        m_notifyMsg = "파밍 중...";
        m_notifyTimer = 0.6f;
        m_nearestInteractNetID = -1; 
    } else if (m_nearestInteractType == REC_BUILDING) {
        bool isWorkbench = false;
        for (int i = 0; i < m_net.remoteCount(); ++i) {
            const auto& rem = m_net.remotes()[i];
            if (rem.entityID == m_nearestInteractNetID) {
                isWorkbench = rem.snap[1].statusFlags == static_cast<uint8_t>(BuildingType::Workbench);
                break;
            }
        }
        if (!isWorkbench) return;
        m_showCrafting = !m_showCrafting;
        if (m_showCrafting) {
            m_notifyMsg = "제작대를 열었습니다.";
            m_notifyTimer = 2.0f;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// run — 최상위 진입점
// ─────────────────────────────────────────────────────────────────────────────
int Game::run(const std::string& serverHost, uint16_t port) {
    m_serverHost = serverHost;
    m_serverPort = port;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        DZ_LOG_FATAL("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    m_window = SDL_CreateWindow("Dead Zone: Ashes — Team Extraction",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 m_camera.screenW, m_camera.screenH,
                                 SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        DZ_LOG_FATAL("SDL_CreateWindow: %s", SDL_GetError());
        return 1;
    }

    if (!m_renderer.init(m_window, m_camera.screenW, m_camera.screenH)) return 1;
    if (!m_audio.init()) {
        DZ_LOG_WARN("Audio init 실패 — 무음으로 실행");
    } else {
        m_audio.loadSound("loot", "assets/sounds/loot.wav");
        m_audio.loadSound("footstep", "assets/sounds/footstep.wav");
        m_audio.loadSound("shoot", "assets/sounds/shoot.wav");
        m_audio.loadSound("swing", "assets/sounds/swing.wav");
        m_audio.loadSound("dry_fire", "assets/sounds/dry_fire.wav");
        m_audio.loadSound("hit", "assets/sounds/hit.wav");
        m_audio.loadSound("siren", "assets/sounds/siren.wav");
    }

    if (!m_map.loadFromJSON("data/map.json"))
        m_map = TileMap(80, 80);

    // 탈출존을 map.json에서 파싱된 값으로 동기화 (서버와 좌표 일치)
    m_extractionZones.clear();
    for (const auto& ez : m_map.getExtractionZones()) {
        float cx = (ez.tileX + ez.w * 0.5f) * TILE_SIZE;
        float cy = (ez.tileY + ez.h * 0.5f) * TILE_SIZE;
        m_extractionZones.push_back({cx, cy});
    }
    if (m_extractionZones.empty()) {
        // fallback: 맵 중앙 2곳
        m_extractionZones.push_back({m_map.width() * TILE_SIZE * 0.5f,
                                     m_map.height() * TILE_SIZE * 0.5f});
    }

    // 건물 외벽을 TileMap SOLID 타일로 마킹 (서버와 동일한 공유 함수 사용)
    applyBuildingCollisions(m_map);

    // 클라이언트 예측에도 동일 TileMap 적용
    m_net.setMap(&m_map);

    m_running = true;
    m_state   = GameState::Login;

    while (m_running) {
        switch (m_state) {
        case GameState::Login:
            runLogin();
            break;
        case GameState::Lobby:
            runLobby();
            break;
        case GameState::Matchmaking:
            runMatchmaking();
            break;
        case GameState::Connecting:
            runConnecting();
            break;
        case GameState::MapLoading:
            runMapLoading();
            break;
        case GameState::InGame:
            runIngame();
            break;
        case GameState::Dead:
            runDead();
            break;
        }
    }

    shutdown();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// runLogin
// ─────────────────────────────────────────────────────────────────────────────
void Game::runLogin() {
    using Clock = std::chrono::steady_clock;
    auto lastTime = Clock::now();

    while (m_running && m_state == GameState::Login) {
        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - lastTime).count();
        lastTime  = now;
        if (dt > 0.05f) dt = 0.05f;

        if (!m_input.pollUI()) {
            m_running = false;
            break;
        }

        int mx, my;
        m_input.mousePos(mx, my);

        m_renderer.beginFrame();
        LobbyButton loginTabBtn, regTabBtn, actionBtn, quitBtn, uBox, pBox;
        m_renderer.drawLobby(mx, my, m_username, m_password, m_focusIdx, m_statusMsg,
                             m_isRegisterTab,
                             loginTabBtn, regTabBtn, actionBtn, quitBtn, uBox, pBox);
        m_renderer.endFrame();

        int cx, cy;
        if (m_input.consumeClick(cx, cy)) {
            if (loginTabBtn.hit(cx, cy)) {
                m_isRegisterTab = false;
                m_statusMsg.clear();
            } else if (regTabBtn.hit(cx, cy)) {
                m_isRegisterTab = true;
                m_statusMsg.clear();
            } else if (uBox.hit(cx, cy)) {
                m_focusIdx = 1;
                m_input.setFocusedTextInput(&m_username);
            } else if (pBox.hit(cx, cy)) {
                m_focusIdx = 2;
                m_input.setFocusedTextInput(&m_password);
            } else {
                m_focusIdx = 0;
                m_input.setFocusedTextInput(nullptr);
            }

            if (actionBtn.hit(cx, cy) && !m_username.empty() && !m_password.empty()) {
                m_state = GameState::Connecting;
                m_net.setIsRegister(m_isRegisterTab);
                break;
            }
            if (quitBtn.hit(cx, cy)) { m_running = false; break; }
        }

        // macOS App Nap/Occlusion 시 VSync 블로킹이 풀려 뻗는 현상 방지
        SDL_Delay(1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// runMatchmaking
// ─────────────────────────────────────────────────────────────────────────────
void Game::runLobby() {
    using Clock = std::chrono::steady_clock;
    auto lastTime = Clock::now();

    while (m_running && m_state == GameState::Lobby) {
        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - lastTime).count();
        lastTime  = now;
        if (dt > 0.05f) dt = 0.05f;

        if (!m_input.pollUI()) {
            m_running = false;
            break;
        }

        m_net.update(dt);
        processInventorySync();
        if (!m_net.isAuthenticated()) {
            m_state = GameState::Login;
            m_statusMsg = "서버와 연결이 끊어졌습니다.";
            break;
        }

        processInventoryMouse(); // Lobby stash/inventory owns clicks before GAME START.

        int mx, my;
        m_input.mousePos(mx, my);

        m_renderer.beginFrame();
        // 렌더링 로비 배경
        SDL_SetRenderDrawColor(m_renderer.raw(), 10, 10, 15, 255);
        SDL_RenderClear(m_renderer.raw());
        
        const InventoryItem* dragPtr = m_drag.active ? &m_drag.item : nullptr;
        m_renderer.drawInventory(m_inventory, mx, my, dragPtr, true); // This includes stash

        // Game Start Button
        int sw, sh;
        m_renderer.getScreenSize(sw, sh);
        SDL_Rect startBtn = { sw / 2 - 100, sh - 80, 200, 50 };
        bool hov = (mx >= startBtn.x && mx <= startBtn.x + startBtn.w &&
                    my >= startBtn.y && my <= startBtn.y + startBtn.h);
        
        SDL_Color bg = hov ? SDL_Color{80, 120, 80, 255} : SDL_Color{50, 80, 50, 255};
        m_renderer.drawPanel(startBtn.x, startBtn.y, startBtn.w, startBtn.h, bg, {200, 255, 200, 255}, 2);
        m_renderer.drawText("GAME START", startBtn.x + 100, startBtn.y + 25, {255,255,255,255}, nullptr, true);

        m_renderer.endFrame();

        int cx, cy;
        if (hov && m_input.peekClick(cx, cy)) {
            m_input.consumeClick(cx, cy);
            // Join Match
            m_net.sendJoinMatch();
            m_state = GameState::Matchmaking;
            m_matchmakingTimer = 0.0f;
            break;
        }

        // macOS App Nap/Occlusion 시 VSync 블로킹이 풀려 뻗는 현상 방지
        SDL_Delay(1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// runMatchmaking
// ─────────────────────────────────────────────────────────────────────────────
void Game::runMatchmaking() {
    using Clock = std::chrono::steady_clock;
    auto lastTime = Clock::now();

    while (m_running && m_state == GameState::Matchmaking) {
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        m_matchmakingTimer += dt;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { m_running = false; return; }
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                m_state = GameState::Lobby;
                m_statusMsg = "매치메이킹 취소";
                return;
            }
        }

        m_net.update(dt);
        if (!m_net.isAuthenticated()) {
            m_state = GameState::Login;
            m_statusMsg = "서버와 연결이 끊어졌습니다.";
            break;
        }

        if (m_net.justSpawned()) {
            m_state = GameState::MapLoading;
            m_mapLoadingTimer = 0.0f;
            return;
        }

        m_renderer.beginFrame();
        m_renderer.drawMatchmaking(m_matchmakingTimer);
        m_renderer.endFrame();

        // macOS App Nap/Occlusion 시 VSync 블로킹이 풀려 뻗는 현상 방지
        SDL_Delay(1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// runConnecting — 비블로킹 연결 루프: SDL 이벤트를 계속 처리해 포커스 유지
// ─────────────────────────────────────────────────────────────────────────────
void Game::runConnecting() {
    m_statusMsg = "서버에 연결 중...";
    m_net.clearAuthError();
    m_net.setAuthData(m_username, m_password);

    if (!m_net.startConnect(m_serverHost, m_serverPort)) {
        m_statusMsg = "접속 실패 — 서버가 실행 중인지 확인하세요";
        m_state = GameState::Login;
        return;
    }

    const auto deadline = SDL_GetTicks() + 8000; // 8초 타임아웃

    while (m_running && SDL_GetTicks() < deadline) {
        // SDL 이벤트 처리 — 이 루프가 돌아야 macOS가 포커스를 유지
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { m_running = false; return; }
            if (e.type == SDL_KEYDOWN &&
                e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                m_net.disconnect();
                m_state = GameState::Login;
                m_statusMsg = "접속 취소";
                return;
            }
        }

        if (!m_net.getAuthError().empty()) {
            if (m_net.getRedirectPort() > 0) {
                m_serverPort = m_net.getRedirectPort();
                m_statusMsg = "새로운 서버로 리다이렉트 중...";
                m_net.clearRedirectPort();
                m_net.clearAuthError();
                m_net.disconnect();
                if (!m_net.startConnect(m_serverHost, m_serverPort)) {
                    m_statusMsg = "새로운 서버 접속 실패";
                    m_state = GameState::Login;
                    return;
                }
            } else {
                m_statusMsg = m_net.getAuthError();
                m_state = GameState::Login;
                return;
            }
        }

        // ENet 비블로킹 폴링 — AuthAck 수신 시 true
        if (m_net.pollConnect()) {
            // 연결 완료 -> 로비(Stash) 상태로 전환
            m_statusMsg.clear();
            m_state = GameState::Lobby;
            return;
        }

        // "연결 중" 화면 렌더링 (보통 아주 짧게 지나감)
        m_renderer.beginFrame();
        m_renderer.drawMatchmaking(m_matchmakingTimer + 3.0f); // 매칭 화면 유지
        m_renderer.endFrame();

        // macOS App Nap/Occlusion 시 VSync 블로킹이 풀려 뻗는 현상 방지
        SDL_Delay(1);
    }

    // 타임아웃
    m_net.disconnect();
    m_state     = GameState::Lobby;
    m_statusMsg = "서버 응답 없음 — 다시 시도하세요";
}

// ─────────────────────────────────────────────────────────────────────────────
// runMapLoading
// ─────────────────────────────────────────────────────────────────────────────
void Game::runMapLoading() {
    using Clock = std::chrono::steady_clock;
    auto lastTime = Clock::now();

    while (m_running && m_state == GameState::MapLoading) {
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        m_mapLoadingTimer += dt;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { m_running = false; return; }
        }

        m_net.update(dt); // 로딩 중에도 서버로부터 ConnectAck(새 위치, HP) 수신 및 처리

        // 맵 로딩 진행도 (가짜 로딩, 약 2초)
        float progress = m_mapLoadingTimer / 2.0f;
        if (progress >= 1.0f) {
            m_extractCountdown = 300.0f;
            m_zonesOpen        = false;

            m_camera.x = m_net.localX();
            m_camera.y = m_net.localY();
            m_net.clearJustSpawned();

            m_state = GameState::InGame;
            return;
        }

        m_renderer.beginFrame();
        m_renderer.drawMapLoading(progress);
        m_renderer.endFrame();

        // macOS App Nap/Occlusion 시 VSync 블로킹이 풀려 뻗는 현상 방지
        SDL_Delay(1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// runIngame
// ─────────────────────────────────────────────────────────────────────────────
void Game::runIngame() {
    using Clock = std::chrono::steady_clock;

    auto lastTime = Clock::now();

    while (m_running && m_state == GameState::InGame) {
        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - lastTime).count();
        lastTime  = now;
        if (dt > 0.05f) dt = 0.05f;

        processEvents();

        if (!m_net.isAuthenticated()) {
            m_statusMsg = "서버와 연결이 끊겼습니다.";
            m_state = GameState::Login;
            return; // 즉시 로컬 게임 진행 중지 및 로비 복귀
        }

        update(dt);

        if (m_net.consumeExtractionEvent()) {
            m_notifyMsg.clear();
            m_notifyTimer = 0.0f;
            m_showInventory = false;
            m_showCrafting = false;
            m_buildMode = false;
            m_drag = DragState{};
            m_clientHiddenNetIDs.clear();
            m_net.clearLocalNetID();
            m_state = GameState::Lobby;
            return;
        }

        renderIngame();

        // Disconnect popup overrides inputs but shouldn't block network events processing
        if (m_showDisconnectPopup) {
            // Wait for OK click in renderIngame
        } else {
            // 사망 감지 → Dead 상태 전환
            if (m_net.isDead()) {
                m_net.clearDead();
                m_deathTimer  = 5.0f;
                m_diedInGame  = true;
                m_state       = GameState::Dead;
                return;
            }
        }

        // macOS App Nap/Occlusion 시 VSync 블로킹이 풀려 뻗는 현상 방지
        SDL_Delay(1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// runDead — 사망 화면 (5초 카운트다운 → 로비 복귀)
// ─────────────────────────────────────────────────────────────────────────────
void Game::runDead() {
    using Clock = std::chrono::steady_clock;
    auto lastTime = Clock::now();

    while (m_running && m_state == GameState::Dead) {
        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - lastTime).count();
        lastTime  = now;
        if (dt > 0.05f) dt = 0.05f;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { m_running = false; return; }
            if (ev.type == SDL_KEYDOWN &&
                (ev.key.keysym.scancode == SDL_SCANCODE_RETURN ||
                 ev.key.keysym.scancode == SDL_SCANCODE_SPACE)) {
                m_deathTimer = 0.0f; // Enter/Space 로 즉시 건너뛰기
            }
        }

        m_deathTimer -= dt;

        if (m_deathTimer <= 0.0f) {
            m_net.clearLocalNetID();
            for (auto& slot : m_inventory.gridSlots) slot = {};
            m_inventory.primaryWeapon = {};
            m_inventory.usedSlots = 0;
            m_buildMode     = false;
            m_showInventory = false;
            m_showCrafting  = false;
            m_drag          = DragState{};
            m_state      = GameState::Lobby;
            m_statusMsg  = "사망 — 모든 아이템을 잃고 로비로 돌아왔습니다.";
            return;
        }

        // 슬로우 모션 (15% 속도)
        float slowDt = dt * 0.15f;
        m_net.update(slowDt);
        m_renderer.updateParticles(slowDt);

        // 마지막 게임 화면 위에 사망 오버레이 렌더링
        m_renderer.beginFrame();
        m_renderer.drawTileMap(m_map, m_camera, m_net.localX(), m_net.localY());
        
        std::vector<LootBoxView> emptyViews;
        m_renderer.drawWorldEntities(m_net, m_camera, &m_map, emptyViews,
                                     m_net.localX(), m_net.localY(),
                                     0.0f, 0.0f, true,
                                     m_net.localTeam(), "", "normal", 0.0f, 0.0f);
                                     
        m_renderer.drawParticles(m_camera);
        m_renderer.drawDeathScreen(m_deathTimer, m_net.getDeathCause());
        m_renderer.endFrame();

        // macOS App Nap/Occlusion 시 VSync 블로킹이 풀려 뻗는 현상 방지
        SDL_Delay(1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 인벤토리 슬롯 히트 테스트
// ─────────────────────────────────────────────────────────────────────────────
bool Game::hitTestInventorySlot(int mx, int my,
                                 DragState::Src& src, int& gridIdx) const {
    const int panW = 760, panH = 500;
    int panX = m_camera.screenW/2 - panW/2;
    
    if (m_state == GameState::Lobby) {
        int totalW = 760 + 20 + 280;
        panX = m_camera.screenW/2 - totalW/2;
    }

    int panY = m_camera.screenH/2 - panH/2;

    // 그리드 슬롯
    const int gridX = panX + 250, gridY = panY + 84;
    const int CELL_W = 96, CELL_H = 76, CELL_GAP = 6;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 5; ++col) {
            int cx = gridX + col*(CELL_W+CELL_GAP);
            int cy = gridY + row*(CELL_H+CELL_GAP);
            if (mx>=cx && mx<cx+CELL_W && my>=cy && my<cy+CELL_H) {
                src     = DragState::Src::Grid;
                gridIdx = row*5 + col;
                return true;
            }
        }
    }

    // 장비 슬롯
    const int eqX = panX + 16, eqY = panY + 84;
    const int slotW = 210, slotH = 64, slotGap = 8;
    if (mx>=eqX && mx<eqX+slotW && my>=eqY && my<eqY+slotH) {
        src = DragState::Src::Primary; gridIdx = -1; return true;
    }
    if (mx>=eqX && mx<eqX+slotW && my>=eqY+slotH+slotGap && my<eqY+slotH*2+slotGap) {
        src = DragState::Src::Secondary; gridIdx = -1; return true;
    }
    
    // 스태시 슬롯 (Lobby 전용)
    if (m_state == GameState::Lobby) {
        int stX = panX + 760 + 20;
        int stY = panY;
        
        const int S_COLS=5, S_ROWS=8;
        const int S_CELL_W=40, S_CELL_H=40, S_CELL_GAP=6;
        int gridTotalW = S_COLS * S_CELL_W + (S_COLS - 1) * S_CELL_GAP;
        int gridOffX = stX + (280 - gridTotalW) / 2;
        int contentY = stY + 56 + 28;

        for (int row=0; row<S_ROWS; ++row) {
            for (int col=0; col<S_COLS; ++col) {
                int cx = gridOffX + col*(S_CELL_W+S_CELL_GAP);
                int cy = contentY + row*(S_CELL_H+S_CELL_GAP);
                if (mx>=cx && mx<cx+S_CELL_W && my>=cy && my<cy+S_CELL_H) {
                    src = DragState::Src::Stash;
                    gridIdx = row*S_COLS + col;
                    return true;
                }
            }
        }
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// 핫바 소모품 슬롯 → 그리드 인덱스 매핑
// ─────────────────────────────────────────────────────────────────────────────
void Game::getHotbarConsumables(int outIdx[4]) const {
    int found = 0;
    for (int i = 0; i < 20 && found < 4; ++i) {
        const InventoryItem& s = m_inventory.gridSlots[i];
        if (s.isValid() && !ClientInventory::isWeaponItem(s.name))
            outIdx[found++] = i;
    }
    while (found < 4) outIdx[found++] = -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// 소모품 사용 — qty 1 감소, 0이면 슬롯 제거
// ─────────────────────────────────────────────────────────────────────────────
void Game::useConsumable(int gridIdx) {
    if (gridIdx < 0 || gridIdx >= 20) return;
    InventoryItem& item = m_inventory.gridSlots[gridIdx];
    if (!item.isValid()) return;

    // 서버에 소모품 사용 요청 전송 (서버가 HP를 갱신하고 HpSync로 응답)
    // 현재 item.name은 서버에서 전달된 원본 key("medkit", "bandage" 등)를 가지고 있습니다.
    const char* serverKey = nullptr;
    if (item.name == "medkit" || item.name == "bandage" || item.name == "food_can") {
        serverKey = item.name.c_str();
    }

    if (!serverKey) {
        m_notifyMsg   = "이 아이템은 사용할 수 없습니다.";
        m_notifyTimer = 2.0f;
        return;
    }

    m_net.sendUseItem(serverKey);   // 서버가 HP 갱신 → S2C_HpSync로 응답
    
    // 치유 효과 발생
    m_renderer.spawnHealEffect(m_net.localX(), m_net.localY());

    // 클라이언트 인벤토리에서 즉시 제거 (낙관적 업데이트)
    m_inventory.totalWeight -= item.weight;
    --item.qty;
    if (item.qty <= 0) {
        item = InventoryItem{};
        --m_inventory.usedSlots;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 인벤토리 마우스 — 드래그 앤 드롭 처리
// ─────────────────────────────────────────────────────────────────────────────
void Game::processInventoryMouse() {
    bool curX = m_input.isKeyDown(SDL_SCANCODE_X);
    if (curX && !m_prevX && !m_dropDialog.active && !m_drag.active) {
        const uint32_t nowMs = SDL_GetTicks();
        if (m_dismantleConfirm.active && nowMs >= m_dismantleConfirm.expiresAtMs) {
            m_dismantleConfirm = {};
        }

        int mx, my;
        m_input.mousePos(mx, my);
        DragState::Src src;
        int idx = -1;
        if (hitTestInventorySlot(mx, my, src, idx) && src != DragState::Src::Stash) {
            InventoryItem item;
            if (src == DragState::Src::Grid && idx >= 0 && idx < 20)
                item = m_inventory.gridSlots[idx];
            else if (src == DragState::Src::Primary)
                item = m_inventory.primaryWeapon;
            if (item.isValid()) {
                const DismantlePreviewRecipe* recipe = findDismantlePreview(item.name);
                if (!recipe) {
                    m_dismantleConfirm = {};
                    m_notifyMsg = "분해할 수 없는 아이템입니다.";
                    m_notifyTimer = 1.5f;
                    m_prevX = curX;
                    return;
                }

                uint8_t srcType = 0;
                uint8_t srcIdx = 0;
                if (src == DragState::Src::Grid) {
                    srcType = 0;
                    srcIdx = static_cast<uint8_t>(std::max(0, idx));
                } else if (src == DragState::Src::Primary) {
                    srcType = 1;
                } else if (src == DragState::Src::Secondary) {
                    srcType = 2;
                }

                const bool confirmed = m_dismantleConfirm.active &&
                                       m_dismantleConfirm.src == src &&
                                       m_dismantleConfirm.gridIdx == idx &&
                                       m_dismantleConfirm.itemName == item.name &&
                                       nowMs < m_dismantleConfirm.expiresAtMs;
                if (confirmed) {
                    m_net.sendDismantleItem(srcType, srcIdx);
                    m_notifyMsg = "아이템 분해 요청";
                    m_notifyTimer = 1.5f;
                    m_dismantleConfirm = {};
                } else {
                    m_dismantleConfirm.active = true;
                    m_dismantleConfirm.src = src;
                    m_dismantleConfirm.gridIdx = idx;
                    m_dismantleConfirm.itemName = item.name;
                    m_dismantleConfirm.expiresAtMs = nowMs + 4000;
                    m_notifyMsg = formatDismantlePreview(*recipe);
                    m_notifyTimer = 4.0f;
                }
            }
        }
    }
    m_prevX = curX;

    if (m_dropDialog.active) {
        int mx, my;
        if (!m_input.consumeClick(mx, my)) return;

        const int W = 360;
        const int H = 210;
        const int X = m_camera.screenW / 2 - W / 2;
        const int Y = m_camera.screenH / 2 - H / 2;
        auto hit = [&](int x, int y, int w, int h) {
            return mx >= x && mx < x + w && my >= y && my < y + h;
        };

        const int stepY = Y + 100;
        if (hit(X + 86, stepY, 44, 44)) {
            m_dropDialog.quantity = std::max(1, m_dropDialog.quantity - 1);
            return;
        }
        if (hit(X + 244, stepY, 44, 44)) {
            m_dropDialog.quantity = std::min(m_dropDialog.item.qty, m_dropDialog.quantity + 1);
            return;
        }
        if (hit(X + 64, Y + 158, 120, 38)) {
            uint8_t srcType = 0;
            uint8_t srcIdx = 0;
            if (m_dropDialog.src == DragState::Src::Grid) {
                srcType = 0;
                srcIdx = static_cast<uint8_t>(std::max(0, m_dropDialog.gridIdx));
            } else if (m_dropDialog.src == DragState::Src::Primary) {
                srcType = 1;
            } else if (m_dropDialog.src == DragState::Src::Secondary) {
                srcType = 2;
            } else {
                m_dropDialog = {};
                return;
            }

            m_net.sendItemDrop(srcType, srcIdx,
                               static_cast<uint16_t>(std::max(1, m_dropDialog.quantity)));
            m_notifyMsg = "아이템을 버렸습니다.";
            m_notifyTimer = 1.5f;
            m_dropDialog = {};
            return;
        }
        if (hit(X + 196, Y + 158, 100, 38) ||
            mx < X || mx >= X + W || my < Y || my >= Y + H) {
            m_dropDialog = {};
            return;
        }
        return;
    }

    // ── 우클릭: 건축 재료 → 건설 모드 진입 (인게임 전용) ──────────────────────
    {
        int rx, ry;
        if (m_state == GameState::InGame && m_input.consumeRightClick(rx, ry)) {
            DragState::Src src; int gidx;
            if (hitTestInventorySlot(rx, ry, src, gidx) && src == DragState::Src::Grid
                && gidx >= 0 && gidx < 20) {
                const InventoryItem& it = m_inventory.gridSlots[gidx];
                int btype = -1;
                if (it.isValid()) {
                    if (it.name == "scrap_metal") btype = 0; // 바리케이드
                    else if (it.name == "electronic_part") btype = 1; // 포탑
                    else if (it.name == "plank") btype = 2; // 제작대
                }
                if (btype >= 0) {
                    static const char* names[] = {"바리케이드","포탑","제작대"};
                    m_buildMode = true;
                    m_buildType = btype;
                    m_showInventory = false;
                    m_drag = DragState{};
                    m_notifyMsg = std::string(names[btype]) + " — 클릭으로 설치";
                    m_notifyTimer = 2.0f;
                    return;
                }
            }
        }
    }

    // ── 드래그 시작 (마우스 다운) ─────────────────────────────────────────────
    if (!m_drag.active) {
        int cx, cy;
        DragState::Src src; int gidx;
        if (!m_input.peekClick(cx, cy) || !hitTestInventorySlot(cx, cy, src, gidx)) {
            return;
        }
        m_input.consumeClick(cx, cy);

        InventoryItem srcItem;
        if (src == DragState::Src::Grid && gidx >= 0 && gidx < 20)
            srcItem = m_inventory.gridSlots[gidx];
        else if (src == DragState::Src::Primary)
            srcItem = m_inventory.primaryWeapon;
        else if (src == DragState::Src::Stash && gidx >= 0 && gidx < 40)
            srcItem = m_inventory.stashSlots[gidx];

        if (srcItem.isValid()) {
            m_drag.active  = true;
            m_drag.src     = src;
            m_drag.gridIdx = gidx;
            m_drag.item    = srcItem;
        }
    }

    // ── 드래그 완료 (마우스 업) ───────────────────────────────────────────────
    if (m_drag.active) {
        int ux, uy;
        bool released = m_input.consumeMouseUp(ux, uy);
        if (!released) return;

        DragState::Src src; int gidx;
        DragState::Src dstSrc; int dstIdx;
        bool validDrop = hitTestInventorySlot(ux, uy, dstSrc, dstIdx);

        if (validDrop) {
            // 같은 슬롯 → 취소
            bool same = (dstSrc == m_drag.src && dstIdx == m_drag.gridIdx);
            if (!same) {
                // 목적지 아이템 가져오기
                InventoryItem dstItem;
                if (dstSrc == DragState::Src::Grid && dstIdx >= 0 && dstIdx < 20)
                    dstItem = m_inventory.gridSlots[dstIdx];
                else if (dstSrc == DragState::Src::Primary)
                    dstItem = m_inventory.primaryWeapon;
                else if (dstSrc == DragState::Src::Stash && dstIdx >= 0 && dstIdx < 40)
                    dstItem = m_inventory.stashSlots[dstIdx];

                // 무기 슬롯에는 무기만 허용
                bool srcIsWeapon = ClientInventory::isWeaponItem(m_drag.item.name);
                bool dstIsWeapon = dstItem.isValid() && ClientInventory::isWeaponItem(dstItem.name);
                bool srcIsEquip  = (m_drag.src == DragState::Src::Primary);
                bool dstIsEquip  = (dstSrc == DragState::Src::Primary);
                bool canDrop = (!dstIsEquip || srcIsWeapon) &&
                               (!srcIsEquip || !dstItem.isValid() || dstIsWeapon);

                if (canDrop) {
                    // 소스에서 아이템 제거
                    if (m_drag.src == DragState::Src::Grid)
                        m_inventory.gridSlots[m_drag.gridIdx] = dstItem;
                    else if (m_drag.src == DragState::Src::Primary)
                        m_inventory.primaryWeapon = dstItem;
                    else if (m_drag.src == DragState::Src::Stash)
                        m_inventory.stashSlots[m_drag.gridIdx] = dstItem;

                    // 목적지에 아이템 배치
                    if (dstSrc == DragState::Src::Grid)
                        m_inventory.gridSlots[dstIdx] = m_drag.item;
                    else if (dstSrc == DragState::Src::Primary)
                        m_inventory.primaryWeapon = m_drag.item;
                    else if (dstSrc == DragState::Src::Stash)
                        m_inventory.stashSlots[dstIdx] = m_drag.item;
                    m_inventory.usedSlots = 0;
                    for (auto& s : m_inventory.gridSlots)
                        if (s.isValid()) ++m_inventory.usedSlots;

                    uint8_t st = static_cast<uint8_t>(m_drag.src) - 1;
                    uint8_t dt = static_cast<uint8_t>(dstSrc) - 1;
                    auto netSlotIndex = [](DragState::Src src, int idx) -> uint8_t {
                        return (src == DragState::Src::Primary || src == DragState::Src::Secondary)
                                 ? 0
                                 : static_cast<uint8_t>(std::max(0, idx));
                    };
                    m_net.sendStashTransfer(st, netSlotIndex(m_drag.src, m_drag.gridIdx),
                                            dt, netSlotIndex(dstSrc, dstIdx));
                }
            }
        } else if (m_state == GameState::InGame &&
                   m_drag.src != DragState::Src::Stash &&
                   m_drag.item.isValid()) {
            m_dropDialog.active = true;
            m_dropDialog.src = m_drag.src;
            m_dropDialog.gridIdx = m_drag.gridIdx;
            m_dropDialog.item = m_drag.item;
            m_dropDialog.quantity = 1;
        }

        m_drag.active  = false;
        m_drag.src     = DragState::Src::None;
        m_drag.gridIdx = -1;
        m_drag.item    = {};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// processCraftingMouse
// ─────────────────────────────────────────────────────────────────────────────
void Game::processCraftingMouse() {
    int mouseX, mouseY;
    if (!m_input.consumeClick(mouseX, mouseY)) return;

    const int CW = 540, CH = 560;
    const int boxX = m_camera.screenW / 2 - CW / 2;
    const int boxY = m_camera.screenH / 2 - CH / 2;
    const int ROW_H = 76;
    const int PAD = 12;
    const int BTN_W = 100, BTN_H = 46;

    // 클리핑 영역 밖 클릭 무시
    const int contentTop    = boxY + 66;
    const int contentBottom = boxY + CH - 32;
    if (mouseY < contentTop || mouseY >= contentBottom) return;

    // 스크롤 오프셋 반영하여 실제 레시피 좌표 계산
    const int adjustedY = mouseY + m_craftScroll;

    auto countItem = [&](const char* key) {
        int cnt = 0;
        for (const auto& slot : m_inventory.gridSlots) {
            if (slot.isValid() && slot.name == key) cnt += slot.qty;
        }
        return cnt;
    };

    int recipeY = boxY + 66 - m_craftScroll; // 스크롤 오프셋 반영
    for (int ri = 0; ri < CRAFT_RECIPE_COUNT; ++ri) {
        const CraftingRecipe& rec = CRAFT_RECIPES[ri];
        bool canCraft = true;
        for (int ii = 0; ii < rec.ingredientCount; ++ii) {
            if (countItem(rec.ingredients[ii].key) < rec.ingredients[ii].qty) {
                canCraft = false;
                break;
            }
        }

        const int btnX = boxX + CW - PAD - BTN_W - 8;
        const int btnY = recipeY + ROW_H / 2 - BTN_H / 2 - 3;
        if (canCraft && mouseX >= btnX && mouseX <= btnX + BTN_W &&
            mouseY >= btnY && mouseY <= btnY + BTN_H) {
            m_net.sendCraftRequest(rec.id);
            m_notifyMsg = "조합 요청 전송 중...";
            m_notifyTimer = 1.5f;
            return;
        }
        recipeY += ROW_H;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// processEvents
// ─────────────────────────────────────────────────────────────────────────────
void Game::processEvents() {
    int psx, psy;
    m_camera.worldToScreen(m_net.localX(), m_net.localY(), psx, psy);

    if (!m_input.poll(m_curInput, static_cast<float>(psx), static_cast<float>(psy))) {
        m_running = false;
        return;
    }
    if (m_input.wantsQuit()) {
        m_running = false;
        return;
    }

    // ESC
    if (m_input.isKeyDown(SDL_SCANCODE_ESCAPE)) {
        if (m_showInventory) {
            m_showInventory = false;
            m_drag = DragState{};
        } else if (m_showCrafting) {
            m_showCrafting = false;
            m_craftScroll  = 0;
        } else if (m_buildMode) {
            m_buildMode = false;
            m_notifyMsg   = "건설 취소";
            m_notifyTimer = 1.0f;
        } else {
            m_notifyMsg = "탈출존에서 F키를 눌러 탈출하세요.";
            m_notifyTimer = 3.0f;
        }
        return;
    }

    if (m_showInventory) {
        processInventoryMouse();
    } else if (m_showCrafting) {
        // 마우스 휠 스크롤
        int wheel = m_input.consumeWheel();
        if (wheel != 0) {
            constexpr int ROW_H = 76;
            constexpr int MAX_SCROLL = (CRAFT_RECIPE_COUNT - 4) * ROW_H;
            m_craftScroll -= wheel * ROW_H;
            if (m_craftScroll < 0) m_craftScroll = 0;
            if (m_craftScroll > MAX_SCROLL) m_craftScroll = MAX_SCROLL;
        }
        processCraftingMouse();
    } else {
        // 인벤토리/제작UI 닫혀있을 때: 마우스 휠로 카메라 줌 조정
        int zoomWheel = m_input.consumeWheel();
        if (zoomWheel != 0) {
            m_camera.zoom *= std::pow(1.15f, static_cast<float>(zoomWheel));
            m_camera.zoom = std::max(1.0f, std::min(3.0f, m_camera.zoom));
        }
    }

    bool curI = m_input.isKeyDown(SDL_SCANCODE_I);
    if (curI && !m_prevI) {
        if (m_showInventory) {
            m_showInventory = false;
            m_drag = DragState{};
        } else {
            m_showInventory = true;
            m_showCrafting = false;
        }
    }
    m_prevI = curI;

    if (m_showInventory || m_showCrafting) {
        m_curInput.actions &= ~(ACT_SHOOT | ACT_MELEE);
        return;
    }

    // Z/X/C 키로 건설 유형 직접 선택 — 같은 키 다시 누르면 취소
    static bool prevZ = false, prevX = false, prevC = false;
    bool curZ = m_input.isKeyDown(SDL_SCANCODE_Z);
    bool curX = m_input.isKeyDown(SDL_SCANCODE_X);
    bool curC = m_input.isKeyDown(SDL_SCANCODE_C);

    auto enterBuild = [&](int type) {
        static const char* names[] = {"바리케이드", "포탑", "제작대", "문"};
        static const char* dirs[]  = {"북", "동", "남", "서"};
        if (m_buildMode && m_buildType == type) {
            m_buildMode = false;
            m_notifyMsg = "건설 취소";
        } else {
            m_buildMode = true;
            m_buildType = type;
            if (type == 1) // 포탑
                m_notifyMsg = std::string("포탑 ") + dirs[m_turretDir] + "향 — R: 회전 / 클릭: 설치";
            else if (type == static_cast<int>(BuildingType::Door))
                m_notifyMsg = "문 — 부서진 문 위치에서 클릭으로 설치";
            else
                m_notifyMsg = std::string(names[type]) + " — 클릭으로 설치 / 다시 누르면 취소";
        }
        m_notifyTimer = 2.0f;
    };

    static bool prevV = false;
    bool curV = m_input.isKeyDown(SDL_SCANCODE_V);
    if (curV && !prevV && m_buildMode) {
        enterBuild((m_buildType + 1) % 4);
    }
    prevV = curV;

    // R키 — 포탑 방향 회전 (포탑 모드일 때만)
    static bool prevR = false;
    bool curR = m_input.isKeyDown(SDL_SCANCODE_R);
    if (curR && !prevR && m_buildMode && m_buildType == 1) {
        static const char* dirs[] = {"북", "동", "남", "서"};
        m_turretDir = (m_turretDir + 1) % 4;
        m_notifyMsg   = std::string("포탑 방향: ") + dirs[m_turretDir];
        m_notifyTimer = 1.5f;
    }
    prevR = curR;

    if (curZ && !prevZ) enterBuild(0);
    if (curX && !prevX) enterBuild(1);
    if (curC && !prevC) enterBuild(2);
    prevZ = curZ; prevX = curX; prevC = curC;

    if (m_buildMode) {
        m_curInput.actions &= ~(ACT_SHOOT | ACT_MELEE | ACT_RELOAD);
        int cx, cy;
        if (m_input.consumeClick(cx, cy)) {
            float wx = (cx - m_camera.screenW * 0.5f) / m_camera.zoom + m_camera.x;
            float wy = (cy - m_camera.screenH * 0.5f) / m_camera.zoom + m_camera.y;
            int tileX = static_cast<int>(wx / 32.0f);
            int tileY = static_cast<int>(wy / 32.0f);
            if (m_buildType == static_cast<int>(BuildingType::Door)) {
                int doorID = m_map.findNearestDoor(wx, wy, TILE_SIZE * 1.5f);
                if (doorID < 0 ||
                    static_cast<size_t>(doorID) >= m_map.getDoors().size() ||
                    !m_map.getDoors()[doorID].broken) {
                    m_notifyMsg = "부서진 문 위치에서만 문을 설치할 수 있습니다.";
                    m_notifyTimer = 1.5f;
                    return;
                }
                tileX = m_map.getDoors()[doorID].tx;
                tileY = m_map.getDoors()[doorID].ty;
            }
            m_net.sendBuildPlace(static_cast<int16_t>(tileX),
                                 static_cast<int16_t>(tileY),
                                 static_cast<uint8_t>(m_buildType),
                                 static_cast<uint8_t>(m_buildType == 1 ? m_turretDir : 0));
        }
        return;
    }

    static const SDL_Scancode NUM_SCANCODES[5] = {
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5
    };
    int hotbarConsIdx[4];
    getHotbarConsumables(hotbarConsIdx);
    for (int i = 0; i < 5; ++i) {
        bool cur = m_input.isKeyDown(NUM_SCANCODES[i]);
        if (cur && !m_prevNum[i]) {
            if (i == 0) {
                // 키 1: 주무기 선택
                if (m_inventory.primaryWeapon.isValid() &&
                    ClientInventory::isWeaponItem(m_inventory.primaryWeapon.name)) {
                    m_hotbarSelected = 0;
                    m_net.sendSelectWeapon(0);
                    m_curInput.actions &= ~(ACT_SHOOT | ACT_MELEE);
                }
            } else {
                // 키 2-5: 소모품 사용
                m_hotbarSelected = i;
                useConsumable(hotbarConsIdx[i-1]);
            }
        }
        m_prevNum[i] = cur;
    }

    const auto& activeWeapon = m_inventory.primaryWeapon;
    const bool hasWeapon = activeWeapon.isValid() && ClientInventory::isWeaponItem(activeWeapon.name);
    const bool isPistol = hasWeapon && activeWeapon.name == "pistol_9mm";
    const bool isSMG = hasWeapon && activeWeapon.name == "smg_9mm";
    const bool isFlamethrower = hasWeapon && activeWeapon.name == "flamethrower";
    const bool isMolotov = hasWeapon && activeWeapon.name == "molotov";
    const bool isRanged = isPistol || isSMG || isFlamethrower;
    if (!isPistol && !isSMG) {
        m_curInput.actions &= ~ACT_RELOAD;
    }

    // 화염병 투척 처리 (서버에 C2S_FireThrow 전송)
    if (isMolotov && (m_curInput.actions & (ACT_SHOOT | ACT_MELEE)) && m_attackTimer <= 0.0f) {
        m_attackTimer = 1.2f;
        constexpr float THROW_DIST = 180.0f;
        float rad = m_curInput.aimAngle * (3.14159265f / 180.0f);
        float tx  = m_net.localX() + std::sin(rad) * THROW_DIST;
        float ty  = m_net.localY() - std::cos(rad) * THROW_DIST;
        m_net.sendFireThrow(tx, ty);
        m_renderer.spawnSoundRing(tx, ty, 400.0f, {255, 80, 0, 200});
        m_cameraShakeTimer     = 0.12f;
        m_cameraShakeIntensity = 4.0f;
        m_curInput.actions &= ~(ACT_SHOOT | ACT_MELEE);
    }

    if (m_curInput.actions & (ACT_SHOOT | ACT_MELEE)) {
        const auto& wpn = activeWeapon;
        if (!hasWeapon || isMolotov) {
            m_curInput.actions &= ~(ACT_SHOOT | ACT_MELEE);
        } else {
            if (isRanged) {
                m_curInput.actions &= ~ACT_MELEE;
            } else {
                m_curInput.actions &= ~ACT_SHOOT;
            }
            if ((isPistol || isSMG) && wpn.qty <= 0) {
                m_curInput.actions &= ~ACT_SHOOT;
                int reserve = 0;
                for (const auto& slot : m_inventory.gridSlots) {
                    if (slot.isValid() && slot.name == "ammo_9mm") reserve += slot.qty;
                }
                if (reserve > 0) {
                    m_curInput.actions |= ACT_RELOAD;
                } else if (m_attackTimer <= 0.0f) {
                    m_attackTimer = 0.35f;
                    m_audio.playSound("dry_fire", 0.6f);
                }
            } else if (m_attackTimer <= 0.0f) { 
                m_attackTimer = isSMG ? 0.09f : 0.35f;
                m_attackAngle = m_curInput.aimAngle; 
                
                if (isPistol || isSMG) {
                    m_audio.playSound("shoot", 0.7f);
                    m_cameraShakeTimer = isSMG ? 0.06f : 0.15f;
                    m_cameraShakeIntensity = isSMG ? 3.0f : 6.0f;
                    m_renderer.spawnMuzzleFlash(m_net.localX(), m_net.localY(), m_attackAngle);
                    m_renderer.spawnCasing(m_net.localX(), m_net.localY(), m_attackAngle);
                    float ringR = isSMG ? 560.0f : 400.0f;
                    m_renderer.spawnSoundRing(m_net.localX(), m_net.localY(), ringR, {255, 160, 40, 180});
                } else if (isFlamethrower) {
                    m_cameraShakeTimer = 0.08f;
                    m_cameraShakeIntensity = 2.0f;
                    m_renderer.spawnFlameEffect(m_net.localX(), m_net.localY(), m_attackAngle);
                } else {
                    m_audio.playSound("swing", 0.8f);
                    m_cameraShakeTimer = 0.1f;
                    m_cameraShakeIntensity = 3.0f;
                    m_renderer.spawnMeleeArc(m_net.localX(), m_net.localY(), m_attackAngle);
                }
            }
        }
    }

    bool curF = m_input.isKeyDown(SDL_SCANCODE_F);
    if (curF && !m_prevF) tryInteract();
    m_prevF = curF;

    static bool prevT = false;
    bool curT = m_input.isKeyDown(SDL_SCANCODE_T);
    if (curT && !prevT) {
        int target = (m_net.localTeam() % 4) + 1;
        m_net.sendAlliancePropose(static_cast<uint8_t>(target));
        char buf[64]; std::snprintf(buf, sizeof(buf), "팀 %d 에게 연합 제안!", target);
        m_notifyMsg = buf; m_notifyTimer = 2.5f;
    }
    prevT = curT;

}

// ─────────────────────────────────────────────────────────────────────────────
// processInventorySync
// ─────────────────────────────────────────────────────────────────────────────
void Game::processInventorySync() {
    if (m_net.hasInventorySync()) {
        const auto& sync = m_net.getInventorySync();
        // 보존할 것: 스태시 (서버에서만 보내줌)
        ClientInventory newInv{};
        for (int i=0; i<40; ++i) newInv.stashSlots[i] = m_inventory.stashSlots[i];
        
        newInv.money = sync.money;
        newInv.usedSlots = sync.usedSlots;
        float totalW = 0.0f;
        for (int i=0; i<20; ++i) {
            if (sync.gridSlots[i].itemID != 0) {
                newInv.gridSlots[i].name = sync.gridSlots[i].key;
                newInv.gridSlots[i].qty = sync.gridSlots[i].quantity;
                newInv.gridSlots[i].weight = sync.gridSlots[i].weight;
                newInv.gridSlots[i].grade = "normal";
                totalW += sync.gridSlots[i].weight * sync.gridSlots[i].quantity;
            }
        }
        // 주무기 슬롯(equipped[0])만 사용
        if (sync.equipped[0].itemID != 0) {
            InventoryItem eq;
            eq.name = sync.equipped[0].key;
            eq.qty = sync.equipped[0].quantity;
            eq.weight = sync.equipped[0].weight;
            eq.grade = "normal";
            totalW += sync.equipped[0].weight * sync.equipped[0].quantity;
            newInv.primaryWeapon = eq;
        }
        newInv.totalWeight = totalW;
        m_inventory = newInv;
        m_net.clearInventorySync();
    }

    if (m_net.hasStashSync()) {
        const auto& stashPkt = m_net.getStashSync();
        for (int i=0; i<40; ++i) {
            if (stashPkt.stashSlots[i].itemID != 0) {
                m_inventory.stashSlots[i].name = stashPkt.stashSlots[i].key;
                m_inventory.stashSlots[i].qty = stashPkt.stashSlots[i].quantity;
                m_inventory.stashSlots[i].weight = stashPkt.stashSlots[i].weight;
                m_inventory.stashSlots[i].grade = "normal";
            } else {
                m_inventory.stashSlots[i] = InventoryItem{};
            }
        }
        m_net.clearStashSync();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// update
// ─────────────────────────────────────────────────────────────────────────────
void Game::update(float dt) {
    m_curInput.dt = dt;
    m_net.applyPrediction(m_curInput, dt);
    m_net.sendInput(m_curInput);
    m_net.update(dt);

    const uint32_t nowMs = SDL_GetTicks();
    m_clientHiddenNetIDs.erase(
        std::remove_if(m_clientHiddenNetIDs.begin(), m_clientHiddenNetIDs.end(),
                       [&](const PendingHiddenLoot& h) { return nowMs >= h.expiresAtMs; }),
        m_clientHiddenNetIDs.end());

    bool currM = m_input.isKeyDown(SDL_SCANCODE_M);
    if (currM && !m_prevM) {
        m_showFullMap = !m_showFullMap;
    }
    m_prevM = currM;

    // 캐릭터 이동 방향 추적 (WASD 기준)
    if (m_curInput.moveX > 0.1f)       { m_charDir = 2; m_charMoving = true; }
    else if (m_curInput.moveX < -0.1f) { m_charDir = 3; m_charMoving = true; }
    else if (m_curInput.moveY < -0.1f) { m_charDir = 1; m_charMoving = true; }
    else if (m_curInput.moveY > 0.1f)  { m_charDir = 0; m_charMoving = true; }
    else                               { m_charMoving = false; }

    // 발자국 소리 재생
    if (m_curInput.moveX != 0 || m_curInput.moveY != 0) {
        float stepInterval = (m_curInput.actions & ACT_SPRINT) ? 0.25f : 0.4f;
        m_footstepTimer += dt;
        if (m_footstepTimer >= stepInterval) {
            m_audio.playSound("footstep", 0.6f);
            m_footstepTimer -= stepInterval;
            bool sprinting = (m_curInput.actions & ACT_SPRINT) != 0;
            float fRadius = sprinting ? 160.0f : 48.0f;
            SDL_Color fColor = sprinting ? SDL_Color{180, 210, 255, 100} : SDL_Color{160, 190, 255, 60};
            m_renderer.spawnSoundRing(m_net.localX(), m_net.localY(), fRadius, fColor);
        }
    } else {
        m_footstepTimer = 0.0f;
    }

    if (m_net.hasRecentHit()) {
        m_audio.playSound("hit", 0.9f);
        m_cameraShakeTimer = 0.2f;
        m_cameraShakeIntensity = 10.0f;
        m_hitFlashTimer = 0.1f;
        m_renderer.spawnBlood(m_net.localX(), m_net.localY());
        m_net.clearRecentHit();
    }
    
    // 타격 이벤트 처리 (원격 엔티티 피격 시)
    // attackerID == 0 = 환경 데미지(태양/불) → 소리 무시 (20Hz 반복 방지)
    for (const auto& ev : m_net.damageEvents()) {
        if (ev.victimID != m_net.localNetID() && ev.attackerID > 0) {
            m_audio.playSound("hit", 0.7f);
            for (int i = 0; i < m_net.remoteCount(); ++i) {
                const auto& rem = m_net.remotes()[i];
                if (rem.entityID == ev.victimID) {
                    m_renderer.spawnBlood(rem.snap[1].x, rem.snap[1].y);
                    break;
                }
            }
        }
    }
    m_net.clearDamageEvents();

    // 핏자국(Blood trail) 생성 (출혈 시)
    if (m_net.localBleeding() && (rand() % 100 < 10)) { // 10% per frame
        m_renderer.spawnBloodStain(m_net.localX(), m_net.localY());
    }
    for (int i = 0; i < m_net.remoteCount(); ++i) {
        const auto& rem = m_net.remotes()[i];
        if ((rem.snap[1].statusFlags & STATUS_BLEEDING) != 0 && (rand() % 100 < 10)) {
            m_renderer.spawnBloodStain(rem.snap[1].x, rem.snap[1].y);
        }
    }

    processInventorySync();

    // 카메라 스무스 팔로우
    float camSpeed = 8.0f;
    float targetX = m_net.localX();
    float targetY = m_net.localY();
    
    if (m_cameraShakeTimer > 0.0f) {
        m_cameraShakeTimer -= dt;
        targetX += (static_cast<float>(rand() % 100) / 100.0f - 0.5f) * m_cameraShakeIntensity;
        targetY += (static_cast<float>(rand() % 100) / 100.0f - 0.5f) * m_cameraShakeIntensity;
    }
    
    if (m_hitFlashTimer > 0.0f) m_hitFlashTimer -= dt;

    m_camera.x += (targetX - m_camera.x) * camSpeed * dt;
    m_camera.y += (targetY - m_camera.y) * camSpeed * dt;
    
    m_renderer.updateParticles(dt);
    m_renderer.updateSoundRings(dt);

    // 공격 모션 타이머 감소
    if (m_attackTimer  > 0.0f) m_attackTimer  -= dt;
    // 알림 메시지 타이머 감소
    if (m_notifyTimer  > 0.0f) { m_notifyTimer -= dt; if (m_notifyTimer <= 0.0f) m_notifyMsg.clear(); }

    // 탈출존 카운트다운
    if (!m_zonesOpen) {
        m_extractCountdown -= dt;
        if (m_extractCountdown <= 0.0f) {
            m_extractCountdown = 0.0f;
            m_zonesOpen        = true;
            m_audio.playSound("siren", 1.0f);
            m_notifyMsg = "경고: 탈출 지점이 활성화되었습니다. 엄청난 소음이 발생합니다!";
            m_notifyTimer = 5.0f;
        }
    }

    // 가장 가까운 파밍/상호작용 박스 탐색 (F키 힌트용)
    const float INTERACT_RANGE = 96.0f;  // 서버 파밍 허용 거리와 동일
    float lx = m_net.localX();
    float ly = m_net.localY();
    float bestLootDist = INTERACT_RANGE * INTERACT_RANGE;
    float bestBuildingDist = INTERACT_RANGE * INTERACT_RANGE;
    int bestBuildingNetID = -1;
    uint8_t bestBuildingType = static_cast<uint8_t>(BuildingType::Barricade);
    m_nearestInteractNetID = -1;
    m_nearestInteractType = 0;
    m_interactPrompt.clear();
    m_interactPromptBlocked = false;
    const bool inventoryFull = m_inventory.usedSlots >= 20 ||
                               m_inventory.totalWeight >= m_inventory.maxWeight;

    constexpr float EXTRACTION_PROMPT_RADIUS = 72.0f;
    const float extractionPromptR2 = EXTRACTION_PROMPT_RADIUS * EXTRACTION_PROMPT_RADIUS;
    for (const auto& zone : m_extractionZones) {
        float dx = zone.first - lx;
        float dy = zone.second - ly;
        if (dx * dx + dy * dy <= extractionPromptR2) {
            if (m_zonesOpen) {
                m_interactPrompt = (m_net.extractionProgress() > 0.0f)
                                     ? "[F] 탈출 진행 중 - 움직이지 마세요"
                                     : "[F] 탈출 시작";
            } else {
                m_interactPrompt = "탈출구 잠김";
            }
            break;
        }
    }

    int doorID = m_map.findNearestDoor(lx, ly, TILE_SIZE * 2.0f);
    if (doorID >= 0) {
        const auto& door = m_map.getDoors()[doorID];
        m_interactPrompt = door.broken ? "부서진 문" : (door.open ? "[F] 문 닫기" : "[F] 문 열기");
    }

    for (int i = 0; i < m_net.remoteCount(); ++i) {
        const auto& rem = m_net.remotes()[i];
        if (std::any_of(m_clientHiddenNetIDs.begin(), m_clientHiddenNetIDs.end(),
                        [&](const PendingHiddenLoot& h) { return h.netID == rem.entityID; })) {
            continue;
        }
        float dx = rem.snap[1].x - lx;
        float dy = rem.snap[1].y - ly;
        float d2 = dx*dx + dy*dy;
        if (rem.recType == REC_LOOT) {
            if (d2 < bestLootDist) {
                bestLootDist = d2;
                m_nearestInteractNetID = rem.entityID;
                m_nearestInteractType  = rem.recType;
            }
        } else if (rem.recType == REC_BUILDING) {
            const uint8_t buildingType = rem.snap[1].statusFlags;
            if (buildingType == static_cast<uint8_t>(BuildingType::Workbench) &&
                d2 < bestBuildingDist) {
                bestBuildingDist = d2;
                bestBuildingNetID = rem.entityID;
                bestBuildingType = buildingType;
            }
        }
    }

    if (m_nearestInteractNetID < 0 && bestBuildingNetID >= 0) {
        m_nearestInteractNetID = bestBuildingNetID;
        m_nearestInteractType = REC_BUILDING;
    }

    if (m_interactPrompt.empty() && m_nearestInteractNetID >= 0) {
        if (m_nearestInteractType == REC_BUILDING &&
            bestBuildingType == static_cast<uint8_t>(BuildingType::Workbench)) {
            m_interactPrompt = "[F] 제작대 사용";
        } else if (inventoryFull) {
            m_interactPrompt = "[F] 파밍 불가";
            m_interactPromptBlocked = true;
        } else {
            m_interactPrompt = "[F] 파밍";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// renderIngame
// ─────────────────────────────────────────────────────────────────────────────
void Game::renderIngame() {
    m_renderer.beginFrame();

    // 1. 타일맵 + 건물
    m_renderer.drawTileMap(m_map, m_camera, m_net.localX(), m_net.localY());

    // 2. 탈출존
    m_renderer.drawExtractionZones(m_camera, m_extractionZones, m_zonesOpen);

    std::vector<LootBoxView> views;
    for (int i = 0; i < m_net.remoteCount(); ++i) {
        const auto& rem = m_net.remotes()[i];
        if (rem.recType == REC_LOOT || rem.recType == REC_BUILDING) {
            if (std::any_of(m_clientHiddenNetIDs.begin(), m_clientHiddenNetIDs.end(),
                            [&](const PendingHiddenLoot& h) { return h.netID == rem.entityID; })) {
                continue; // 클라이언트가 이미 주운 아이템
            }
            LootBoxView v;
            v.wx = rem.snap[1].x;
            v.wy = rem.snap[1].y;
            v.looted = false; 
            v.nearPlayer = (rem.entityID == m_nearestInteractNetID);
            v.blocked = (rem.recType == REC_LOOT && v.nearPlayer &&
                         (m_inventory.usedSlots >= 20 ||
                          m_inventory.totalWeight >= m_inventory.maxWeight));
            v.isBuilding   = (rem.recType == REC_BUILDING);
            v.buildingType = rem.snap[1].statusFlags & 0x0F;        // bits0-3: 건물 유형
            v.turretDir    = (rem.snap[1].statusFlags >> 4) & 0x03; // bits4-5: 포탑 방향
            views.push_back(v);
        }
    }

    m_renderer.drawParticles(m_camera);
    m_renderer.drawSoundRings(m_camera);

    // 4+5. 원격 + 로컬 엔티티 + 건물 + 파밍 상자를 y-sort 후 통합 그리기 (입체감)
    float hpPct    = m_net.localHp() / std::max(1.0f, m_net.localMaxHp());
    bool  bleeding = m_net.localBleeding();
    bool  onFire   = m_net.localOnFire();
    int   teamID   = m_net.localTeam();
    const InventoryItem& hudWeapon = m_inventory.primaryWeapon;
    std::string wName;
    if (hudWeapon.isValid()) {
        wName = hudWeapon.name;
        if (hudWeapon.name == "pistol_9mm" || hudWeapon.name == "smg_9mm") {
            int reserve = 0;
            for (const auto& slot : m_inventory.gridSlots) {
                if (slot.isValid() && slot.name == "ammo_9mm") reserve += slot.qty;
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), " (%d / %d)", hudWeapon.qty, reserve);
            wName += buf;
        }
    } else {
        wName = "";
    }
    const std::string& wGrade = hudWeapon.isValid() ? hudWeapon.grade : "normal";
    m_renderer.drawWorldEntities(m_net, m_camera, &m_map, views,
                                  m_net.localX(), m_net.localY(),
                                  m_curInput.aimAngle, hpPct, bleeding || onFire,
                                  teamID, wName, wGrade,
                                  m_attackTimer, m_attackAngle,
                                  m_charDir, m_charMoving);

    // 5-b. 시야각 안개 (120도, 마우스 방향)
    m_renderer.drawFOV(m_net.localX(), m_net.localY(),
                       m_curInput.aimAngle, m_camera, static_cast<float>(m_net.gameTime()));
    {
        float timeOfDay = std::fmod(static_cast<float>(m_net.gameTime()), 180.0f);
        if (timeOfDay > 120.0f) {
            m_renderer.drawBuildingEntitiesOverlay(views, m_camera);
        }
    }

    if (m_hitFlashTimer > 0.0f) {
        SDL_SetRenderDrawBlendMode(m_renderer.sdlRenderer(), SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer.sdlRenderer(), 255, 20, 20, static_cast<uint8_t>((m_hitFlashTimer / 0.1f) * 100));
        SDL_Rect full = {0, 0, m_camera.screenW, m_camera.screenH};
        SDL_RenderFillRect(m_renderer.sdlRenderer(), &full);
    }

    // 9. Siren Sound Check
    if (m_net.consumeSirenEvent()) {
        m_zonesOpen = true;
        m_extractCountdown = 0.0f;
        m_audio.playSound("siren", 0.8f, false);
    }

    // 건설 결과 알림
    if (m_net.hasBuildMsg()) {
        m_notifyMsg   = m_net.consumeBuildMsg();
        m_notifyTimer = 2.5f;
    }
    // 6. HUD
    static const char* teamNames[] = {"NEUTRAL","ALPHA","BRAVO","CHARLIE","DELTA"};
    int tidx = std::max(0, std::min(4, teamID));
    float extractProg = m_net.extractionProgress();
    // 팀 생존 정보 수집
    int teamAlive[4] = {
        m_net.teamAlive(1), m_net.teamAlive(2),
        m_net.teamAlive(3), m_net.teamAlive(4)
    };
    m_renderer.drawHUD(m_net.localHp(), m_net.localMaxHp(), m_net.localStamina(), m_net.localMaxStamina(), bleeding,
                       extractProg, teamID,
                       m_extractCountdown, teamNames[tidx],
                       wName, wGrade,
                       teamAlive, m_net.allianceBits(), m_net.gameTime(), m_net.localReloading());

    // ── HUD 및 미니맵 ────────────────────────────────────────────────────────
    m_renderer.drawMinimap(m_map, m_net, m_net.localX(), m_net.localY(), teamID, m_extractionZones);

    // 7-b. 건설 모드 오버레이
    {
        int mx, my; m_input.mousePos(mx, my);
        m_renderer.drawBuildModeOverlay(m_buildMode, m_buildType, mx, my, m_camera, &m_map, m_turretDir);
        if (m_buildMode) {
            m_renderer.drawBuildRecipePanel(m_inventory, m_buildType);
        }
    }

    // 7-c. 인게임 알림 메시지 (무게 초과 등)
    if (m_notifyTimer > 0.0f)
        m_renderer.drawNotification(m_notifyMsg, m_notifyTimer);

    if (!m_interactPrompt.empty()) {
        TTF_Font* f = m_renderer.debugFont();
        int y = m_camera.screenH - 148;
        SDL_Color promptColor = m_interactPromptBlocked
                              ? SDL_Color{255, 80, 80, 255}
                              : SDL_Color{255, 240, 160, 255};
        m_renderer.drawText(m_interactPrompt, m_camera.screenW / 2 + 2, y + 2,
                            {0, 0, 0, 180}, f, true);
        m_renderer.drawText(m_interactPrompt, m_camera.screenW / 2, y,
                            promptColor, f, true);
    }

    // 8. 핫바 (항상 표시)
    {
        int hotbarConsIdx[4];
        getHotbarConsumables(hotbarConsIdx);
        int mx, my;
        m_input.mousePos(mx, my);
        m_renderer.drawHotbar(m_inventory, m_hotbarSelected, hotbarConsIdx, mx, my);
    }

    // 9. 인벤토리 오버레이 (I키 토글)
    if (m_showInventory) {
        int mx, my;
        m_input.mousePos(mx, my);
        const InventoryItem* dragPtr = m_drag.active ? &m_drag.item : nullptr;
        m_renderer.drawInventory(m_inventory, mx, my, dragPtr);
        if (m_dropDialog.active) {
            m_renderer.drawDropQuantityDialog(m_dropDialog.item,
                                              m_dropDialog.quantity,
                                              mx, my);
        }
    } else if (m_showCrafting) {
        int mx, my;
        m_input.mousePos(mx, my);
        int clickedRecipe = -1;
        m_renderer.drawCraftingUI(m_inventory, mx, my, clickedRecipe, m_craftScroll);
    } else if (m_showFullMap) {
        m_renderer.drawFullMap(m_map, m_net, m_net.localX(), m_net.localY(), teamID, m_extractionZones);
    }

    if (m_showDisconnectPopup) {
        int mx, my; m_input.mousePos(mx, my);
        int cx, cy;
        bool hasClick = m_input.consumeClick(cx, cy);
        
        bool clickedOK = false;
        m_renderer.drawDisconnectPopup(mx, my, clickedOK);
        
        if (hasClick && clickedOK) {
            m_state = GameState::Login;
            m_showDisconnectPopup = false;
            m_statusMsg = "서버 연결 끊김! 다시 로그인하세요.";
        }
    }

    m_renderer.endFrame();
}

// ─────────────────────────────────────────────────────────────────────────────
// shutdown
// ─────────────────────────────────────────────────────────────────────────────
void Game::shutdown() {
    if (!m_window) return;
    m_net.disconnect();
    m_audio.shutdown();
    m_renderer.shutdown();
    if (m_window) { SDL_DestroyWindow(m_window); m_window = nullptr; }
    SDL_Quit();
    DZ_LOG_INFO("클라이언트 종료.");
}

} // namespace dz
