#pragma once
#include <SDL2/SDL.h>
#include "client/Renderer.h"
#include "client/InputHandler.h"
#include "client/AudioManager.h"
#include "client/NetworkClient.h"
#include "shared/TileMap.h"
#include <string>
#include <vector>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// GameState
// ─────────────────────────────────────────────────────────────────────────────
enum class GameState { Login, Lobby, Matchmaking, Connecting, MapLoading, InGame, Dead };



// ─────────────────────────────────────────────────────────────────────────────
// DragState — 인벤토리 드래그 앤 드롭 상태
// ─────────────────────────────────────────────────────────────────────────────
struct DragState {
    bool active = false;
    enum class Src : uint8_t { None, Grid, Primary, Secondary, Stash } src = Src::None;
    int  gridIdx = -1;        ///< Src::Grid일 때 그리드 슬롯 인덱스
    InventoryItem item;       ///< 드래그 중인 아이템 복사본
};

// ─────────────────────────────────────────────────────────────────────────────
// Game
// ─────────────────────────────────────────────────────────────────────────────
class Game {
public:
    Game();
    ~Game();

    int run(const std::string& serverHost = "127.0.0.1",
            uint16_t port = DEFAULT_SERVER_PORT);

private:
    void runLogin();
    void runLobby();
    void runMatchmaking();
    void runConnecting();   ///< 비블로킹 연결 대기 루프 (SDL 이벤트 유지)
    void runMapLoading();
    void runIngame();
    void runDead();
    void processInventorySync();

    void processEvents();
    void processInventoryMouse();   ///< 인벤토리 열린 동안 마우스 드래그 처리
    void processCraftingMouse();
    void update(float dt);
    void renderIngame();
    void shutdown();

    // 상호작용
    void tryInteract();

    // 인벤토리 헬퍼
    /// 화면 좌표 → 인벤토리 슬롯 판별. src에 위치 반환, gridIdx에 -1이면 장비 슬롯
    bool hitTestInventorySlot(int mx, int my,
                              DragState::Src& src, int& gridIdx) const;
    /// 핫바 소모품 3칸의 gridSlots 인덱스 반환 (-1 = 빈 칸)
    void getHotbarConsumables(int outIdx[3]) const;
    /// 소모품 한 번 사용 (qty 감소, 0이면 슬롯 제거)
    void useConsumable(int gridIdx);

    // SDL
    SDL_Window*  m_window  = nullptr;
    bool         m_running = false;

    // 서브시스템
    Renderer       m_renderer;
    InputHandler   m_input;
    AudioManager   m_audio;
    NetworkClient  m_net;
    TileMap        m_map;

    // 게임 상태
    GameState   m_state      = GameState::Login;
    std::string m_serverHost = "127.0.0.1";
    uint16_t    m_serverPort = 7777;
    std::string m_statusMsg;

    // 인게임 프레임 데이터
    InputState  m_curInput{};
    Camera      m_camera;

    // 탈출존
    std::vector<std::pair<float,float>> m_extractionZones;
    bool  m_zonesOpen        = false;
    float m_extractCountdown = 300.0f;

    // 파밍 및 상호작용
    int     m_nearestInteractNetID = -1;
    uint8_t m_nearestInteractType  = 0; // 0=None, 1=Loot, 2=Building
    std::vector<int> m_clientHiddenNetIDs; // 줍기 성공 후 즉시 숨길 아이템들

    // 인벤토리 & UI 상태
    ClientInventory m_inventory;
    bool            m_showInventory = false;
    bool            m_showCrafting  = false;

    // 드래그 앤 드롭
    DragState m_drag;

    // 핫바
    int  m_hotbarSelected = 0;    ///< 0-4 (슬롯 1-5)
    bool m_prevQ          = false;

    // 이전 F/I/숫자키 상태 (엣지 트리거용)
    bool  m_prevF  = false;
    bool  m_prevI  = false;
    bool  m_prevNum[5] = {};  ///< 1-5 키 이전 프레임 상태

    // 공격 모션 타이머 (클라이언트 사이드 애니메이션용)
    float m_attackTimer    = 0.0f;
    float m_attackAngle    = 0.0f;
    float m_footstepTimer  = 0.0f;

    // 게임 연출 (Juice)
    float m_cameraShakeTimer = 0.0f;
    float m_cameraShakeIntensity = 0.0f;
    float m_hitFlashTimer = 0.0f;

    // 사망 / 로비 복귀
    float m_deathTimer     = 5.0f;
    bool  m_diedInGame     = false;

    // 인게임 알림 메시지 (무게 초과 등 단기 표시)
    std::string     m_notifyMsg;
    float           m_notifyTimer = 0.0f;

    // Login State
    std::string     m_username;
    std::string     m_password;
    int             m_focusIdx = 0; // 0=None, 1=User, 2=Pass
    bool            m_isRegisterTab = false;
    
    // 매칭 & 로딩 상태 타이머
    float           m_matchmakingTimer = 0.0f;
    float           m_mapLoadingTimer  = 0.0f;
    
    // 건설 모드
    bool        m_buildMode    = false;
    int         m_buildType    = 0;   // 0=바리케이드, 1=포탑, 2=제작대

    // UI Toggles
    bool        m_showFullMap  = false;
    bool        m_prevM        = false;
    bool        m_showDisconnectPopup = false;
};

} // namespace dz
