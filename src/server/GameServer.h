#pragma once
#include <cstdint>
#include <chrono>
#include <memory>
#include "shared/ecs/World.h"
#include "shared/TileMap.h"
#include "server/NetworkSystem.h"
#include "server/GameLogic.h"
#include "server/ZombieAI.h"
#include "server/FireSystem.h"
#include "server/systems/MovementSystem.h"
#include "server/systems/CombatSystem.h"
#include "server/systems/NoiseSystem.h"
#include "server/systems/BuildSystem.h"
#include "server/systems/AllianceSystem.h"
#include "server/systems/ExtractionSystem.h"
#include "server/Database.h"

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// GameServer — owns all server-side state and wires every system together
// ─────────────────────────────────────────────────────────────────────────────
class GameServer {
public:
    explicit GameServer(uint16_t port);
    ~GameServer();

    int run();

private:
    // ── Fixed-step tick ───────────────────────────────────────────────────────
    void tick(float dt);
    void sendSnapshots();
    void applyBufferedInputs(float dt);

    // ── Event handlers (called from system callbacks) ──────────────────────────
    void onClientConnect(uint32_t peerIdx);
    void onClientAuth(uint32_t peerIdx, const char* username, const char* password, bool isRegister);
    void onClientDisconnect(uint32_t peerIdx);
    void onInputReceived(uint32_t peerIdx, const InputPacket& pkt);
    void onDamage(const DamageResult& result);
    void onDeath(Entity victim, Entity killer);
    void onExtracted(Entity player, uint8_t zoneID);
    void onDeathLoot(Entity player);
    void onBuildingDestroyed(uint32_t buildingNetID, bool explosion);
    void onAllianceChanged(uint8_t teamA, uint8_t teamB, bool active);
    void onUseItem(uint32_t peerIdx, const char* key);
    void onAllianceProposeReq(uint32_t peerIdx, uint8_t toTeam);
    void onBuildPlace(uint32_t peerIdx, int16_t tileX, int16_t tileY, uint8_t buildingType);
    void onCraftRequest(uint32_t peerIdx, uint8_t recipeID);
    void onLootPickupReq(uint32_t peerIdx, uint32_t lootNetID);

    void sendHpSyncToPeer(uint32_t peerIdx);   ///< HP + 상태 플래그를 해당 피어에 전송
    void broadcastTeamStatus();                 ///< 매초 팀 상태 브로드캐스트
    
    void resetRound();                          ///< 라운드 초기화

    float m_teamStatusTimer = 0.0f;
    
    struct LobbyPlayer {
        std::string username;
        InventoryComponent inv;
    };
    std::unordered_map<uint32_t, LobbyPlayer> m_lobbyPlayers;
    
    void onStashTransferReq(uint32_t peerIdx, uint8_t srcType, uint8_t srcIdx, uint8_t dstType, uint8_t dstIdx);
    void onJoinMatch(uint32_t peerIdx);
    
    // 라운드 및 세션 관리
    bool     m_gameStarted = false;
    float    m_gameTimer = 0.0f;
    int      m_activePlayers = 0; // 생존(현재 접속 중) 플레이어 수
    uint16_t m_port = 0;
    bool     m_childServerLaunched = false;

    // ── Map setup ─────────────────────────────────────────────────────────────
    bool loadMap(const std::string& path);
    void spawnZombies();
    void spawnLootBoxes();

    // ── State ─────────────────────────────────────────────────────────────────
    World           m_world;
    TileMap         m_map;
    uint32_t        m_tick     = 0;
    bool            m_running  = false;
    uint32_t        m_nextPlayerNetID = 1;
    float           m_gameTime = 0.0f;  ///< Seconds since server start

    // ── Systems ───────────────────────────────────────────────────────────────
    NetworkSystem    m_net;
    Database         m_db;
    MovementSystem   m_movement;
    CombatSystem     m_combat;
    NoiseSystem      m_noise;
    ZombieAISystem   m_zombieAI;
    FireSystem       m_fire;
    BuildSystem      m_build;
    AllianceSystem   m_alliance;
    ExtractionSystem m_extraction;
    // Initialized last — holds refs to all other systems
    std::unique_ptr<GameLogic> m_logic;

    std::string m_peerUsernames[MAX_CLIENTS]; // To save on disconnect
};

} // namespace dz
