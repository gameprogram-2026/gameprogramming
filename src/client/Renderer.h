#pragma once
#include "shared/ecs/World.h"
#include "shared/TileMap.h"
#include "client/TextureCache.h"
#include "client/NetworkClient.h"
#include "client/FontManager.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <string>
#include <cstring>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// Camera
// ─────────────────────────────────────────────────────────────────────────────
struct Camera {
    float x = 0.0f, y = 0.0f;
    float zoom = 1.0f;
    int   screenW = 1280, screenH = 720;

    void worldToScreen(float wx, float wy, int& sx, int& sy) const noexcept {
        sx = static_cast<int>((wx - x) * zoom + screenW * 0.5f);
        sy = static_cast<int>((wy - y) * zoom + screenH * 0.5f);
    }
    SDL_Rect worldRect(float wx, float wy, float w, float h) const noexcept {
        int sx, sy;
        worldToScreen(wx - w * 0.5f, wy - h * 0.5f, sx, sy);
        return {sx, sy, static_cast<int>(w * zoom), static_cast<int>(h * zoom)};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// LobbyButton
// ─────────────────────────────────────────────────────────────────────────────
struct LobbyButton {
    SDL_Rect rect{};
    bool hit(int mx, int my) const noexcept {
        return mx >= rect.x && mx < rect.x + rect.w &&
               my >= rect.y && my < rect.y + rect.h;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 클라이언트 인벤토리 (UI 표시용)
// ─────────────────────────────────────────────────────────────────────────────
struct InventoryItem {
    std::string name;
    std::string grade;   // "normal", "enhanced", "rare", "unique"
    int         qty    = 0;
    float       weight = 0.0f;
    bool        isValid() const { return qty > 0; }
};

struct ClientInventory {
    InventoryItem gridSlots[20];       // 최대 20 슬롯
    InventoryItem stashSlots[40];      // 40 스태시 슬롯
    InventoryItem primaryWeapon;
    InventoryItem secondaryWeapon;
    int           money        = 0;
    float         totalWeight  = 0.0f;
    float         maxWeight    = 30.0f;
    int           usedSlots    = 0;

    // 이름 기반 무기 판별 (grade 기반 금지 — 구급상자 등 오장착 방지)
    static bool isWeaponItem(const std::string& name) {
        static const char* WEAPON_KEYS[] = {
            "pipe", "bat", "axe", "pistol", "rifle", "shotgun", "flamethrower", "knife"
        };
        for (auto* k : WEAPON_KEYS) {
            if (name.find(k) != std::string::npos) return true;
        }
        return false;
    }

    bool addItem(const InventoryItem& item) {
        // 무기류는 장비 슬롯으로
        if (isWeaponItem(item.name)) {
            if (!primaryWeapon.isValid()) {
                primaryWeapon = item;
                totalWeight  += item.weight * item.qty;
                return true;
            }
            if (!secondaryWeapon.isValid()) {
                secondaryWeapon = item;
                totalWeight    += item.weight * item.qty;
                return true;
            }
        }
        // 일반 아이템은 그리드 슬롯
        if (usedSlots >= 20) return false;
        for (auto& s : gridSlots) {
            if (!s.isValid()) {
                s = item;
                ++usedSlots;
                totalWeight += item.weight * item.qty;
                return true;
            }
        }
        return false;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// LootBox (렌더러에 전달되는 뷰 데이터)
// ─────────────────────────────────────────────────────────────────────────────
struct LootBoxView {
    float wx, wy;
    bool  looted     = false;
    bool  nearPlayer = false; // F키 힌트 표시 여부
    bool  isBuilding = false;
    uint8_t buildingType = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Particle System
// ─────────────────────────────────────────────────────────────────────────────
struct Particle {
    float x, y;
    float vx, vy;
    float life, maxLife;
    SDL_Color color;
    float size;
    int type = 0;
};

struct BloodStain {
    float x, y;
    float size;
};

// ─────────────────────────────────────────────────────────────────────────────
// Renderer
// ─────────────────────────────────────────────────────────────────────────────
class Renderer {
public:
    bool init(SDL_Window* window, int w, int h);
    void shutdown();
    void beginFrame();
    void endFrame();

    SDL_Renderer* raw() const { return m_renderer; }
    void getScreenSize(int& w, int& h) const { w = m_screenW; h = m_screenH; }

    void updateParticles(float dt);
    void drawParticles(const Camera& cam);
    void spawnBlood(float x, float y);
    void spawnMuzzleFlash(float x, float y, float angle);
    void spawnCasing(float x, float y, float aimAngle);
    void spawnMeleeArc(float x, float y, float angle);
    void spawnBloodStain(float x, float y);
    void spawnHealEffect(float x, float y);

    // ── 인게임 ────────────────────────────────────────────────────────────────
    void drawTileMap(const TileMap& map, const Camera& cam, float localX, float localY);
    void drawFire(const std::vector<std::pair<int,int>>& fires, const Camera& cam);
    void drawLootBoxes(const std::vector<LootBoxView>& boxes, const Camera& cam);
    /// 로컬 플레이어 + 원격 엔티티 + 건물 루프/벽 + 상자를 y-sort 후 한 번에 그림 (입체감 핵심)
    void drawWorldEntities(const NetworkClient& net, const Camera& cam,
                           const class TileMap* map,
                           const std::vector<LootBoxView>& lootBoxes,
                           float localX, float localY, float aimAngle,
                           float hpPct, bool bleeding, int teamID,
                           const std::string& wName, const std::string& wGrade,
                           float attackTimer, float attackAngle);

    // 하위 호환 — 내부에서 drawWorldEntities 호출
    void drawLocalPlayer(float wx, float wy, float angle, float hpPct,
                         bool bleeding, int teamID, const Camera& cam,
                         const std::string& weaponName  = "",
                         const std::string& weaponGrade = "normal",
                         float attackTimer = 0.0f,
                         float attackAngle = 0.0f);
    void drawRemotes(const NetworkClient& net, const Camera& cam);
    void drawHUD(float hp, float maxHp, float stamina, float maxStamina, bool bleeding,
                 float extractProgress, int teamID, float extractCountdown, 
                 const std::string& teamName,
                 const std::string& weaponName  = "",
                 const std::string& weaponGrade = "normal",
                 const int teamAlive[4] = nullptr,   ///< 팀1~4 생존 인원 (nullptr=미표시)
                 uint8_t allianceBits  = 0,          ///< 연합 비트맵
                 float gameTime = 0.0f);             ///< 서버 진행 시간 (초)

    void drawBuildModeOverlay(bool active, int buildType, int mouseX, int mouseY,
                              const Camera& cam);
    void drawExtractionZones(const Camera& cam,
                             const std::vector<std::pair<float,float>>& zones,
                             bool open);
    void drawMinimap(const NetworkClient& net, float lx, float ly, int teamID);
    void drawFullMap(const class TileMap& map, const NetworkClient& net, float lx, float ly, int teamID);
    void drawDisconnectPopup(int mouseX, int mouseY, bool& outClickedOK);

    /// dragItem: 드래그 중인 아이템 (nullptr이면 드래그 없음)
    void drawInventory(const ClientInventory& inv, int mouseX, int mouseY,
                       const InventoryItem* dragItem = nullptr, bool showStash = false);

    /// 제작대 UI
    void drawCraftingUI(const ClientInventory& inv, int mouseX, int mouseY, int& outClickedRecipe);

    /// 화면 하단 핫바 (무기 슬롯 1-2, 소모품 슬롯 3-5)
    /// consumableIdx[3]: 그리드 슬롯 인덱스 (-1이면 빈 슬롯)
    void drawHotbar(const ClientInventory& inv, int selectedSlot,
                    const int consumableIdx[3], int mouseX, int mouseY);

    /// 120도 시야각 안개-of-war.  모든 월드 요소 렌더링 후, HUD 전에 호출.
    void drawFOV(float wx, float wy, float aimAngleDeg, const Camera& cam, float gameTime);

    void drawNoiseDebug(float wx, float wy, float radius, const Camera& cam);
    void drawDeathScreen(float countdown, const std::string& cause);
    /// 화면 중앙 상단에 단기 알림 메시지 (무게 초과 등)
    void drawNotification(const std::string& msg, float alpha);

    // ── 로비 ──────────────────────────────────────────────────────────────────
    void drawLobby(int mouseX, int mouseY,
                   const std::string& username, const std::string& password,
                   int focusIdx,
                   const std::string& status,
                   bool isRegisterTab,
                   LobbyButton& outLoginTab, LobbyButton& outRegisterTab,
                   LobbyButton& outActionBtn, LobbyButton& outQuit,
                   LobbyButton& outUserBox, LobbyButton& outPassBox);

    void drawMatchmaking(float elapsedTime);
    void drawMapLoading(float progress);

    SDL_Renderer* sdlRenderer() noexcept { return m_renderer; }
    TextureCache& textures()    noexcept { return m_texCache;  }
    TTF_Font*     debugFont()   noexcept { return m_fonts.mono(12); }

    /// 건물 외벽 타일을 TileMap에 SOLID로 마킹 (문 위치는 통로로 개방)
    void setupCollisionMap(TileMap& map);

private:
    SDL_Renderer* m_renderer  = nullptr;
    TextureCache  m_texCache;
    FontManager   m_fonts;
    int           m_screenW   = 0;
    int           m_screenH   = 0;
    SDL_Texture*  m_fowTexture = nullptr;  ///< 시야각 안개-of-war 렌더 타깃

    std::vector<Particle> m_particles;
    std::vector<BloodStain> m_bloodStains;



    // ── 건물 정의 (이제 TileMap에서 받아옴) ────────────────────────────────────────────────
    void drawBuildings(const TileMap& map, const Camera& cam, float localX, float localY);
    void drawLootBox(const LootBoxView& box, const Camera& cam);
    void drawBuildingOverlay(const TileMap::BuildingDef& b, const Camera& cam);

public:
    // ── 공통 헬퍼 ─────────────────────────────────────────────────────────────
    void drawText(const std::string& txt, int x, int y,
                  SDL_Color col, TTF_Font* f, bool centered = false);
    void drawTextShadow(const std::string& txt, int x, int y,
                        SDL_Color col, SDL_Color shadow,
                        TTF_Font* f, bool centered = false);
    void drawPanel(int x, int y, int w, int h,
                   SDL_Color bg, SDL_Color border, int bw = 1);
    void drawButton(int x, int y, int w, int h,
                    const std::string& label, bool hovered,
                    SDL_Color base, SDL_Color hover, SDL_Color border,
                    TTF_Font* f);
    void drawHpBar(int sx, int sy, float pct, int w, int h = 5);
    void drawFilledCircle(int cx, int cy, int r, SDL_Color c);
    void drawScanlines(int alpha = 18);
    SDL_Color gradeColor(const std::string& grade) const noexcept;
};

} // namespace dz
