#include "Game.h"
#include "shared/util/Logger.h"
#include "shared/network/Packet.h"
#include "shared/MapSetup.h"
#include <SDL2/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// 맵 상수
// ─────────────────────────────────────────────────────────────────────────────
static const float TILE_SZ = 32.0f;
static const std::vector<std::pair<float,float>> EXTRACTION_ZONE_POS = {
    { 5*TILE_SZ,  5*TILE_SZ},
    {74*TILE_SZ,  5*TILE_SZ},
    { 5*TILE_SZ, 74*TILE_SZ},
    {74*TILE_SZ, 74*TILE_SZ},
};

// ─────────────────────────────────────────────────────────────────────────────
// Game constructor
// ─────────────────────────────────────────────────────────────────────────────
Game::Game() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    m_camera.zoom    = 1.5f;
    m_camera.screenW = 1280;
    m_camera.screenH = 720;

    m_extractionZones = EXTRACTION_ZONE_POS;
}

Game::~Game() { shutdown(); }

// ─────────────────────────────────────────────────────────────────────────────
// tryInteract — send pickup or open workbench
// ─────────────────────────────────────────────────────────────────────────────
void Game::tryInteract() {
    if (m_nearestInteractNetID < 0) return;
    
    if (m_nearestInteractType == 3) { // REC_LOOT equivalent is 3
        m_net.sendLootPickup(static_cast<uint32_t>(m_nearestInteractNetID));
        m_audio.playSound("loot");
        m_nearestInteractNetID = -1; 
    } else if (m_nearestInteractType == 2) { // REC_BUILDING equivalent is 2
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
        m_audio.loadSound("hit", "assets/sounds/hit.wav");
        m_audio.loadSound("siren", "assets/sounds/siren.wav");
    }

    if (!m_map.loadFromJSON("data/map.json"))
        m_map = TileMap(80, 80);

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

        // SDL_Delay(16); removed to rely on VSync
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

        processInventoryMouse(); // Handles drag and drop in stash/inventory

        // SDL_Delay(16); removed to rely on VSync
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

        // SDL_Delay(16); removed to rely on VSync
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

        // SDL_Delay(16); removed to rely on VSync
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

        // 맵 로딩 진행도 (가짜 로딩, 약 2초)
        float progress = m_mapLoadingTimer / 2.0f;
        if (progress >= 1.0f) {
            m_extractCountdown = 300.0f;
            m_zonesOpen        = false;
            m_inventory        = ClientInventory{};

            m_camera.x = m_net.localX();
            m_camera.y = m_net.localY();
            m_net.clearJustSpawned();

            m_state = GameState::InGame;
            return;
        }

        m_renderer.beginFrame();
        m_renderer.drawMapLoading(progress);
        m_renderer.endFrame();

        // SDL_Delay(16); removed to rely on VSync
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
        update(dt);

        if (!m_net.isAuthenticated()) {
            m_showDisconnectPopup = true;
            // 계속 루프를 돌면서 팝업만 표시 (입력/업데이트 중단)
            m_curInput.moveX = 0; m_curInput.moveY = 0;
            m_curInput.actions = 0;
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
            // 인벤토리 초기화 (아이템 손실) 및 로컬 상태 정리
            m_net.clearLocalNetID();
            m_inventory  = ClientInventory{};
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

        // SDL_Delay(16); removed to rely on VSync
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
void Game::getHotbarConsumables(int outIdx[3]) const {
    int found = 0;
    for (int i = 0; i < 20 && found < 3; ++i) {
        const InventoryItem& s = m_inventory.gridSlots[i];
        if (s.isValid() && !ClientInventory::isWeaponItem(s.name))
            outIdx[found++] = i;
    }
    while (found < 3) outIdx[found++] = -1;
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
    int cx, cy, ux, uy;
    bool clicked  = m_input.consumeClick(cx, cy);
    bool released = m_input.consumeMouseUp(ux, uy);

    // ── 드래그 시작 (마우스 다운) ─────────────────────────────────────────────
    if (clicked && !m_drag.active) {
        DragState::Src src; int gidx;
        if (hitTestInventorySlot(cx, cy, src, gidx)) {
            InventoryItem srcItem;
            if (src == DragState::Src::Grid && gidx >= 0 && gidx < 20)
                srcItem = m_inventory.gridSlots[gidx];
            else if (src == DragState::Src::Primary)
                srcItem = m_inventory.primaryWeapon;
            else if (src == DragState::Src::Secondary)
                srcItem = m_inventory.secondaryWeapon;
            else if (src == DragState::Src::Stash && gidx >= 0 && gidx < 40)
                srcItem = m_inventory.stashSlots[gidx];

            if (srcItem.isValid()) {
                m_drag.active  = true;
                m_drag.src     = src;
                m_drag.gridIdx = gidx;
                m_drag.item    = srcItem;
            }
        }
    }

    // ── 드래그 완료 (마우스 업) ───────────────────────────────────────────────
    if (released && m_drag.active) {
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
                else if (dstSrc == DragState::Src::Secondary)
                    dstItem = m_inventory.secondaryWeapon;
                else if (dstSrc == DragState::Src::Stash && dstIdx >= 0 && dstIdx < 40)
                    dstItem = m_inventory.stashSlots[dstIdx];

                // 무기 슬롯에는 무기만 허용
                bool srcIsWeapon = ClientInventory::isWeaponItem(m_drag.item.name);
                bool dstIsEquip  = (dstSrc == DragState::Src::Primary ||
                                    dstSrc == DragState::Src::Secondary);
                bool canDrop = !dstIsEquip || srcIsWeapon;

                if (canDrop) {
                    // 소스에서 아이템 제거
                    if (m_drag.src == DragState::Src::Grid)
                        m_inventory.gridSlots[m_drag.gridIdx] = dstItem;
                    else if (m_drag.src == DragState::Src::Primary)
                        m_inventory.primaryWeapon = dstItem;
                    else if (m_drag.src == DragState::Src::Secondary)
                        m_inventory.secondaryWeapon = dstItem;
                    else if (m_drag.src == DragState::Src::Stash)
                        m_inventory.stashSlots[m_drag.gridIdx] = dstItem;

                    // 목적지에 아이템 배치
                    if (dstSrc == DragState::Src::Grid)
                        m_inventory.gridSlots[dstIdx] = m_drag.item;
                    else if (dstSrc == DragState::Src::Primary)
                        m_inventory.primaryWeapon = m_drag.item;
                    else if (dstSrc == DragState::Src::Secondary)
                        m_inventory.secondaryWeapon = m_drag.item;
                    else if (dstSrc == DragState::Src::Stash)
                        m_inventory.stashSlots[dstIdx] = m_drag.item;
                    m_inventory.usedSlots = 0;
                    for (auto& s : m_inventory.gridSlots)
                        if (s.isValid()) ++m_inventory.usedSlots;

                    uint8_t st = static_cast<uint8_t>(m_drag.src) - 1;
                    uint8_t dt = static_cast<uint8_t>(dstSrc) - 1;
                    m_net.sendStashTransfer(st, m_drag.gridIdx, dt, dstIdx);
                }
            }
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
    // 새 조합 UI는 renderIngame()에서 drawCraftingUI 호출 시 outClickedRecipe로 처리됨
    // 여기서는 ESC 외 특별한 처리 없음
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
        } else {
            m_notifyMsg = "생존 지역을 통해 탈출해야 로비로 돌아갈 수 있습니다.";
            m_notifyTimer = 3.0f;
        }
        return;
    }

    if (m_showInventory) {
        processInventoryMouse();
    } else if (m_showCrafting) {
        processCraftingMouse();
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

    if (m_curInput.actions & (ACT_SHOOT | ACT_MELEE)) {
        if (m_attackTimer <= 0.0f) { 
            m_attackTimer = 0.35f; 
            m_attackAngle = m_curInput.aimAngle; 
            
            // 무기 종류에 따른 사운드 분기
            const auto& wpn = m_inventory.primaryWeapon;
            bool isGun = (wpn.isValid() && (wpn.name.find("pistol") != std::string::npos || wpn.name.find("rifle") != std::string::npos || wpn.name.find("shotgun") != std::string::npos));
            if (isGun) {
                m_audio.playSound("shoot", 0.7f);
                m_cameraShakeTimer = 0.15f;
                m_cameraShakeIntensity = 6.0f;
                m_renderer.spawnMuzzleFlash(m_net.localX(), m_net.localY(), m_attackAngle);
                m_renderer.spawnCasing(m_net.localX(), m_net.localY(), m_attackAngle);
            } else {
                m_audio.playSound("swing", 0.8f);
                m_cameraShakeTimer = 0.1f;
                m_cameraShakeIntensity = 3.0f;
                m_renderer.spawnMeleeArc(m_net.localX(), m_net.localY(), m_attackAngle);
            }
        }
    }

    bool curF = m_input.isKeyDown(SDL_SCANCODE_F);
    if (curF && !m_prevF) tryInteract();
    m_prevF = curF;

    bool curQ = m_input.isKeyDown(SDL_SCANCODE_Q);
    if (curQ && !m_prevQ) std::swap(m_inventory.primaryWeapon, m_inventory.secondaryWeapon);
    m_prevQ = curQ;

    static bool prevT = false;
    bool curT = m_input.isKeyDown(SDL_SCANCODE_T);
    if (curT && !prevT) {
        int target = (m_net.localTeam() % 4) + 1;
        m_net.sendAlliancePropose(static_cast<uint8_t>(target));
        char buf[64]; std::snprintf(buf, sizeof(buf), "팀 %d 에게 연합 제안!", target);
        m_notifyMsg = buf; m_notifyTimer = 2.5f;
    }
    prevT = curT;

    static bool prevB = false;
    bool curB = m_input.isKeyDown(SDL_SCANCODE_B);
    if (curB && !prevB) {
        m_buildMode = !m_buildMode;
        m_notifyMsg   = m_buildMode ? "건설 모드 ON — 클릭으로 바리케이드 설치" : "건설 모드 OFF";
        m_notifyTimer = 2.0f;
    }
    prevB = curB;

    static bool prevV = false;
    bool curV = m_input.isKeyDown(SDL_SCANCODE_V);
    if (curV && !prevV && m_buildMode) {
        m_buildType = (m_buildType + 1) % 3;
        m_notifyMsg   = m_buildType == 0 ? "건설: 바리케이드" : m_buildType == 1 ? "건설: 포탑" : "건설: 제작대";
        m_notifyTimer = 1.5f;
    }
    prevV = curV;

    if (m_buildMode) {
        int cx, cy;
        if (m_input.consumeClick(cx, cy)) {
            float wx = (cx - m_camera.screenW * 0.5f) / m_camera.zoom + m_camera.x;
            float wy = (cy - m_camera.screenH * 0.5f) / m_camera.zoom + m_camera.y;
            m_net.sendBuildPlace(static_cast<int16_t>(wx / 32.0f),
                                 static_cast<int16_t>(wy / 32.0f),
                                 static_cast<uint8_t>(m_buildType));
        }
    }

    static const SDL_Scancode NUM_SCANCODES[5] = {
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5
    };
    int hotbarConsIdx[3];
    getHotbarConsumables(hotbarConsIdx);
    for (int i = 0; i < 5; ++i) {
        bool cur = m_input.isKeyDown(NUM_SCANCODES[i]);
        if (cur && !m_prevNum[i]) {
            m_hotbarSelected = i;
            if (i >= 2) useConsumable(hotbarConsIdx[i-2]);
        }
        m_prevNum[i] = cur;
    }
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
        for (int i=0; i<2; ++i) {
            if (sync.equipped[i].itemID != 0) {
                InventoryItem eq;
                eq.name = sync.equipped[i].key;
                eq.qty = sync.equipped[i].quantity;
                eq.weight = sync.equipped[i].weight;
                eq.grade = "normal";
                totalW += sync.equipped[i].weight * sync.equipped[i].quantity;
                if (i == 0) newInv.primaryWeapon = eq;
                if (i == 1) newInv.secondaryWeapon = eq;
            }
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

    bool currM = m_input.isKeyDown(SDL_SCANCODE_M);
    if (currM && !m_prevM) {
        m_showFullMap = !m_showFullMap;
    }
    m_prevM = currM;

    // 발자국 소리 재생
    if (m_curInput.moveX != 0 || m_curInput.moveY != 0) {
        float stepInterval = (m_curInput.actions & ACT_SPRINT) ? 0.25f : 0.4f;
        m_footstepTimer += dt;
        if (m_footstepTimer >= stepInterval) {
            m_audio.playSound("footstep", 0.6f);
            m_footstepTimer -= stepInterval;
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
    for (const auto& ev : m_net.damageEvents()) {
        if (ev.victimID != m_net.localNetID()) {
            m_audio.playSound("hit", 0.7f); // Play hit sound for other entities
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
    const float INTERACT_RANGE = 48.0f;  // 픽셀 단위 (world)
    float lx = m_net.localX();
    float ly = m_net.localY();
    float bestDist = INTERACT_RANGE * INTERACT_RANGE;
    m_nearestInteractNetID = -1;
    m_nearestInteractType = 0;

    for (int i = 0; i < m_net.remoteCount(); ++i) {
        const auto& rem = m_net.remotes()[i];
        if (rem.recType == 3 || rem.recType == 2) { // 3: REC_LOOT, 2: REC_BUILDING
            float dx = rem.snap[1].x - lx;
            float dy = rem.snap[1].y - ly;
            float d2 = dx*dx + dy*dy;
            if (d2 < bestDist) {
                bestDist = d2;
                m_nearestInteractNetID = rem.entityID;
                m_nearestInteractType  = rem.recType;
            }
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
        if (rem.recType == 3 || rem.recType == 2) { // 3: REC_LOOT, 2: REC_BUILDING
            LootBoxView v;
            v.wx = rem.snap[1].x;
            v.wy = rem.snap[1].y;
            v.looted = false; 
            v.nearPlayer = (rem.entityID == m_nearestInteractNetID);
            v.isBuilding = (rem.recType == 2); // Pass to Renderer
            v.buildingType = rem.snap[1].statusFlags; // Assuming we can use statusFlags for buildingType?
            views.push_back(v);
        }
    }

    m_renderer.drawParticles(m_camera);

    // 4+5. 원격 + 로컬 엔티티 + 건물 + 파밍 상자를 y-sort 후 통합 그리기 (입체감)
    float hpPct    = m_net.localHp() / std::max(1.0f, m_net.localMaxHp());
    bool  bleeding = m_net.localBleeding();
    bool  onFire   = m_net.localOnFire();
    int   teamID   = m_net.localTeam();
    const std::string& wName  = m_inventory.primaryWeapon.isValid()
                                  ? m_inventory.primaryWeapon.name  : "";
    const std::string& wGrade = m_inventory.primaryWeapon.isValid()
                                  ? m_inventory.primaryWeapon.grade : "normal";
    m_renderer.drawWorldEntities(m_net, m_camera, &m_map, views,
                                  m_net.localX(), m_net.localY(),
                                  m_curInput.aimAngle, hpPct, bleeding || onFire,
                                  teamID, wName, wGrade,
                                  m_attackTimer, m_attackAngle);

    // 5-b. 시야각 안개 (120도, 마우스 방향)
    m_renderer.drawFOV(m_net.localX(), m_net.localY(),
                       m_curInput.aimAngle, m_camera, static_cast<float>(m_net.gameTime()));

    if (m_hitFlashTimer > 0.0f) {
        SDL_SetRenderDrawBlendMode(m_renderer.sdlRenderer(), SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer.sdlRenderer(), 255, 20, 20, static_cast<uint8_t>((m_hitFlashTimer / 0.1f) * 100));
        SDL_Rect full = {0, 0, m_camera.screenW, m_camera.screenH};
        SDL_RenderFillRect(m_renderer.sdlRenderer(), &full);
    }

    // 9. Siren Sound Check
    if (m_net.consumeSirenEvent()) {
        m_audio.playSound("siren", 0.8f, false);
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
                       teamAlive, m_net.allianceBits(), m_net.gameTime());

    // 7. 미니맵
    m_renderer.drawMinimap(m_net, m_net.localX(), m_net.localY(), teamID);

    // 7-b. 건설 모드 오버레이
    {
        int mx, my; m_input.mousePos(mx, my);
        m_renderer.drawBuildModeOverlay(m_buildMode, m_buildType, mx, my, m_camera);
    }

    // 7-c. 인게임 알림 메시지 (무게 초과 등)
    if (m_notifyTimer > 0.0f)
        m_renderer.drawNotification(m_notifyMsg, m_notifyTimer);


    // 8. 핫바 (항상 표시)
    {
        int hotbarConsIdx[3];
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
    } else if (m_showCrafting) {
        int mx, my;
        m_input.mousePos(mx, my);
        int clickedRecipe = -1;
        m_renderer.drawCraftingUI(m_inventory, mx, my, clickedRecipe);
        // 클릭된 레시피 → 서버에 조합 요청
        if (clickedRecipe >= 0) {
            m_net.sendCraftRequest(static_cast<uint8_t>(clickedRecipe));
            m_notifyMsg = "조합 요청 전송 중...";
            m_notifyTimer = 1.5f;
        }
    }

    if (m_showFullMap) {
        m_renderer.drawFullMap(m_map, m_net, m_net.localX(), m_net.localY(), teamID);
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
