#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <enet/enet.h>
#include "shared/network/Packet.h"
#include "shared/ecs/World.h"
#include "shared/TileMap.h"
#include "client/InputHandler.h"

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// PredictionRecord — one buffered unacked input + resulting local state
// ─────────────────────────────────────────────────────────────────────────────
struct PredictionRecord {
    uint32_t   seqNum = 0;
    InputState input;
    float      x = 0.0f, y = 0.0f;  ///< local position AFTER applying this input
    float      dt = 0.0f;            ///< frame dt used when applying this input
};

// ─────────────────────────────────────────────────────────────────────────────
// RemoteEntityState — interpolation buffer for remote entities
// ─────────────────────────────────────────────────────────────────────────────
struct RemoteSnapshot {
    float    x, y;
    uint8_t  statusFlags;
    uint16_t tick;
    float    hp = 100.0f;   ///< 좀비만 사용 (seqAck 재활용)
};

struct RemoteEntityState {
    uint16_t       entityID  = 0;
    uint8_t        recType   = 0;
    RemoteSnapshot snap[2]   = {};  ///< [0]=older  [1]=newer
    float          interpT   = 0.0f;
    float          snapDt    = WORLD_TICK_DT;
    float          maxHp     = 100.0f;
};

// S2C_DeathEvent 기반 드랍 이벤트 (신뢰 채널, 정확히 1회 수신)
struct ZombieDeathInfo {
    uint16_t entityID = 0;
    float    x = 0.0f, y = 0.0f;
};

// 클라이언트 측 포탑 레이저 빔 이펙트 (렌더링 후 타이머 소멸)
struct TurretBeam {
    float fromX, fromY;
    float toX,   toY;
    uint8_t ownerTeam;
    float   ttl = 0.15f;  ///< 지속 시간 (초) - 빠르게 사라짐
};

// ─────────────────────────────────────────────────────────────────────────────
// NetworkClient — client-side networking, prediction, and reconciliation
//
// Client-Side Prediction (CSP) flow:
//   1. InputHandler samples input → poll()
//   2. applyPrediction(): apply locally to m_localX/Y, push to m_predBuf
//   3. sendInput(): wrap in InputPacket, send CHAN_UNRELIABLE
//   4. receiveSnapshot(): iterate EntityStateRecord
//        → For our own entity: reconcile() against seqAck
//        → For others: push to interpolation buffer
//   5. interpolateRemotes(dt): lerp remote entities toward newest snapshot
// ─────────────────────────────────────────────────────────────────────────────
class NetworkClient {
public:
    bool connect(const std::string& host, uint16_t port);
    void disconnect();
    void update(float dt);

    void setIsRegister(bool val) { m_isRegister = val; }
    void setAuthData(const std::string& user, const std::string& pass) {
        m_authUsername = user;
        m_authPassword = pass;
        m_authError.clear();
        m_redirectPort = 0;
    }
    const std::string& getAuthError() const { return m_authError; }
    uint16_t getRedirectPort() const { return m_redirectPort; }
    void clearRedirectPort() { m_redirectPort = 0; }
    void clearAuthError() { m_authError.clear(); }

    /// 비블로킹 연결 시작 — 매 프레임 pollConnect() 호출로 진행
    bool startConnect(const std::string& host, uint16_t port);
    /// 비블로킹 연결 폴링 — ConnectAck 수신 시 true 반환
    bool pollConnect();

    // ── Client-side prediction ─────────────────────────────────────────────────
    /// Apply input locally (no wait for server) and push to prediction buffer.
    void applyPrediction(const InputState& input, float dt);

    /// Send buffered input to server.
    void sendInput(const InputState& input);

    // ── Remote entity interpolation ───────────────────────────────────────────
    void updateRemoteInterp(float dt);

    // ── State ─────────────────────────────────────────────────────────────────
    /// 충돌용 TileMap 연결 (Game::run() 초기화 후 호출)
    void setMap(const TileMap* map) noexcept { m_map = map; }

    // ── 새 send 메서드 ──────────────────────────────────────────────────────────
    void sendUseItem(const char* key);
    void sendAlliancePropose(uint8_t toTeam);
    void sendBuildPlace(int16_t tileX, int16_t tileY, uint8_t buildingType);
    void sendCraftRequest(uint8_t recipeID);
    void sendLootPickup(uint32_t lootNetID);

    void sendJoinMatch();
    void sendStashTransfer(uint8_t srcType, uint8_t srcIdx, uint8_t dstType, uint8_t dstIdx);

    bool     isConnected()   const noexcept { return m_peer != nullptr; }
    bool     isAuthenticated() const noexcept { return m_isAuthenticated; }
    void     clearAuthenticated() noexcept { m_isAuthenticated = false; }
    void     clearLocalNetID() { 
        m_localNetID = 0; 
        m_localHp = 100.0f; 
        m_localMaxHp = 100.0f; 
        m_localFlags = 0; 
        m_localDead = false;
        m_predCount = 0;
        m_predHead = 0;
    }
    float    localX()        const noexcept { return m_localX; }
    float       localY()        const noexcept { return m_localY; }
    uint32_t    localNetID()    const noexcept { return m_localNetID; }
    uint8_t     localTeam()     const noexcept { return m_localTeam; }
    float       localHp()       const noexcept { return m_localHp; }
    float       localMaxHp()    const noexcept { return m_localMaxHp; }
    float       localStamina()  const noexcept { return m_localStamina; }
    float       localMaxStamina() const noexcept { return m_localMaxStamina; }
    bool        localBleeding() const noexcept { return (m_localFlags & STATUS_BLEEDING) != 0; }
    bool        localOnFire()   const noexcept { return (m_localFlags & STATUS_ON_FIRE)  != 0; }
    bool        localReloading()const noexcept { return (m_localFlags & STATUS_RELOADING)!= 0; }
    bool        isDead()          const noexcept { return m_localDead; }
    void        clearDead()       noexcept       { m_localDead = false; }
    bool        justSpawned()     const noexcept { return m_justSpawned; }
    void        clearJustSpawned()noexcept       { m_justSpawned = false; }
    
    std::string m_deathCause;
    const std::string& getDeathCause() const { return m_deathCause; }
    bool        consumeSirenEvent() noexcept {
        bool res = m_hasSirenEvent;
        m_hasSirenEvent = false;
        return res;
    }
    float    extractionProgress() const noexcept { return m_extractProg; }

    int      teamAlive(int team) const noexcept {
        return (team >= 1 && team <= 4) ? m_teamAlive[team-1] : 0;
    }
    uint8_t  allianceBits() const noexcept { return m_allianceBits; }
    uint16_t gameTime() const { return m_gameTimeSec; }

    const std::array<RemoteEntityState, 512>& remotes() const { return m_remotes; }
    int  remoteCount() const noexcept { return m_remoteCount; }

    // 좀비 사망 이벤트 큐 (S2C_DeathEvent 기반, UDP 순서 무관)
    const std::vector<ZombieDeathInfo>& zombieDeaths() const noexcept { return m_zombieDeaths; }
    void clearZombieDeaths() noexcept { m_zombieDeaths.clear(); }

    const std::vector<DamageEventPacket>& damageEvents() const noexcept { return m_damageEvents; }
    void clearDamageEvents() noexcept { m_damageEvents.clear(); }

    bool hasInventorySync() const { return m_hasInvSync; }
    const InventorySyncPacket& getInventorySync() const { return m_invSyncPkt; }
    void clearInventorySync() { m_hasInvSync = false; }

    bool hasStashSync() const { return m_hasStashSync; }
    const StashSyncPacket& getStashSync() const { return m_stashSyncPkt; }
    void clearStashSync() { m_hasStashSync = false; }

    bool hasRecentHit() const { return m_recentHit; }
    void clearRecentHit() { m_recentHit = false; }

    // 포탑 레이저 빔 이펙트 접근
    const std::vector<TurretBeam>& turretBeams() const noexcept { return m_turretBeams; }
    void tickTurretBeams(float dt) noexcept {
        for (auto it = m_turretBeams.begin(); it != m_turretBeams.end(); ) {
            it->ttl -= dt;
            if (it->ttl <= 0.0f) it = m_turretBeams.erase(it);
            else ++it;
        }
    }

private:
    // ── ENet ──────────────────────────────────────────────────────────────────
    ENetHost*   m_host = nullptr;
    ENetPeer*   m_peer = nullptr;

    // ── Our identity ──────────────────────────────────────────────────────────
    uint32_t    m_localNetID   = 0;
    uint8_t     m_localTeam    = 0;
    float       m_localX       = 0.0f;
    float       m_localY       = 0.0f;
    float       m_localHp      = 100.0f;
    float       m_localMaxHp   = 100.0f;
    float       m_localStamina = 100.0f;
    float       m_localMaxStamina = 100.0f;
    uint8_t     m_localFlags   = 0;        ///< STATUS_* bitmask
    bool        m_localDead    = false;
    bool        m_justSpawned  = false;   ///< ConnectAck 수신 직후 1프레임 true
    uint32_t    m_seqCounter   = 0;
    float       m_extractProg  = 0.0f;
    bool        m_isAuthenticated = false;

    int         m_teamAlive[4] = {0, 0, 0, 0};
    uint8_t     m_allianceBits = 0;
    bool        m_hasSirenEvent = false;
    uint16_t    m_gameTimeSec  = 0;

    int      m_tickCount    = 0;

    // ── Prediction buffer ─────────────────────────────────────────────────────
    std::array<PredictionRecord, PREDICTION_BUFFER> m_predBuf{};

    bool m_isRegister = false;
    std::string m_authUsername;
    std::string m_authPassword;
    std::string m_authError;
    uint16_t m_redirectPort = 0;

    bool m_hasInvSync = false;
    InventorySyncPacket m_invSyncPkt{};
    bool m_hasStashSync = false;
    StashSyncPacket m_stashSyncPkt{};

    std::vector<DamageEventPacket> m_damageEvents;
    
    // 게임 상태 (승패 등)
    bool m_recentHit = false;

    int      m_predHead = 0;
    int      m_predCount= 0;

    // ── Remote entities ───────────────────────────────────────────────────────
    std::array<RemoteEntityState, 512> m_remotes{};
    int m_remoteCount = 0;

    // ── 좀비 사망 이벤트 큐 ───────────────────────────────────────────────────
    std::vector<ZombieDeathInfo> m_zombieDeaths;

    // ── TileMap (충돌용) ──────────────────────────────────────────────────────
    const TileMap* m_map = nullptr;

    // ── 포탑 레이저 빔 이펙트 ──────────────────────────────────────────────────
    std::vector<TurretBeam> m_turretBeams;


    // ── Helpers ───────────────────────────────────────────────────────────────
    void processSnapshot(const uint8_t* data, size_t len);
    void reconcile(uint16_t ackedSeq, float serverX, float serverY);
    void replayInputsFrom(uint32_t fromSeq);
    RemoteEntityState* findRemote(uint16_t id);
    void applyInputToPos(const InputState& input, float& x, float& y, float dt);
};

} // namespace dz
