#include "GameServer.h"
#include "shared/MapSetup.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/HealthComponent.h"
#include "shared/ecs/components/NetworkComponent.h"
#include "shared/ecs/components/CombatComponent.h"
#include "shared/ecs/components/InventoryComponent.h"
#include "shared/ecs/components/BuildingComponent.h"
#include "shared/util/Logger.h"
#include <thread>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <vector>

namespace dz {

namespace {

constexpr int DAY_ZOMBIE_TARGET = 60;
constexpr int NIGHT_ZOMBIE_TARGET = 85;
constexpr int MAX_ZOMBIE_SPAWN_BATCH = 18;
constexpr int NIGHT_WAVE_BASE = 8;
constexpr int NIGHT_WAVE_PER_PLAYER = 3;

uint32_t itemIDForKey(const char* key) {
    if (std::strcmp(key, "scrap_pipe") == 0)      return 1;
    if (std::strcmp(key, "nail_bat") == 0)        return 2;
    if (std::strcmp(key, "fire_axe") == 0)        return 3;
    if (std::strcmp(key, "pistol_9mm") == 0)      return 4;
    if (std::strcmp(key, "molotov") == 0)         return 5;
    if (std::strcmp(key, "flamethrower") == 0)    return 6;
    if (std::strcmp(key, "smg_9mm") == 0)         return 7;
    if (std::strcmp(key, "ammo_9mm") == 0)        return 10;
    if (std::strcmp(key, "scrap_metal") == 0)     return 20;
    if (std::strcmp(key, "plank") == 0)           return 21;
    if (std::strcmp(key, "electronic_part") == 0) return 22;
    if (std::strcmp(key, "oil") == 0)             return 23;
    if (std::strcmp(key, "wood") == 0)            return 24;
    if (std::strcmp(key, "medkit") == 0)          return 30;
    if (std::strcmp(key, "bandage") == 0)         return 31;
    if (std::strcmp(key, "food_can") == 0)        return 32;
    return 0;
}

int countLivingZombies(World& world) {
    int count = 0;
    for (EntityID id : world.alive()) {
        Entity e{id};
        auto* ai = world.tryGet<ZombieAIComponent>(e);
        if (!ai) continue;
        auto* hp = world.tryGet<HealthComponent>(e);
        if (hp && hp->isAlive) ++count;
    }
    return count;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Constructor — wire all system callbacks
// ─────────────────────────────────────────────────────────────────────────────
GameServer::GameServer(uint16_t port) : m_port(port) {
    // Network callbacks
    m_net.onConnect        ([this](uint32_t i)                       { onClientConnect(i); });
    m_net.onDisconnect     ([this](uint32_t i)                       { onClientDisconnect(i); });
    m_net.onInput          ([this](uint32_t i, const InputPacket& p) { onInputReceived(i, p); });
    m_net.onAuth           ([this](uint32_t i, const char* u, const char* p, bool r) { onClientAuth(i, u, p, r); });
    m_net.onJoinMatch      ([this](uint32_t i)                       { onJoinMatch(i); });
    m_net.onStashTransfer  ([this](uint32_t i, uint8_t st, uint8_t si, uint8_t dt, uint8_t di) { onStashTransferReq(i, st, si, dt, di); });
    m_net.onSelectWeapon   ([this](uint32_t i, uint8_t slot)         { onSelectWeaponReq(i, slot); });
    m_net.onDoorToggle     ([this](uint32_t i, uint16_t doorID)      { onDoorToggleReq(i, doorID); });
    m_net.onUseItem        ([this](uint32_t i, const char* k)        { onUseItem(i, k); });
    m_net.onAlliancePropose([this](uint32_t i, uint8_t toTeam)       { onAllianceProposeReq(i, toTeam); });
    m_net.onBuildPlace     ([this](uint32_t i, int16_t tx, int16_t ty, uint8_t bt, uint8_t dir){ onBuildPlace(i,tx,ty,bt,dir); });
    m_net.onCraft          ([this](uint32_t i, uint8_t recipeID)     { onCraftRequest(i, recipeID); });
    m_net.onLootPickup     ([this](uint32_t i, uint32_t nid)         { onLootPickupReq(i, nid); });
    m_net.onItemDrop       ([this](uint32_t i, uint8_t st, uint8_t si, uint16_t q) { onItemDropReq(i, st, si, q); });
    m_net.onDismantle      ([this](uint32_t i, uint8_t st, uint8_t si) { onDismantleReq(i, st, si); });

    // ── MySQL 연결 설정 ──────────────────────────────────────────────────────
    // 환경변수로 DB 비밀번호를 주입받는 것이 권장 방법.
    // 예: export DEADZONE_DB_PASS="mypassword"
    const char* dbHost = std::getenv("DEADZONE_DB_HOST");
    const char* dbUser = std::getenv("DEADZONE_DB_USER");
    const char* dbPass = std::getenv("DEADZONE_DB_PASS");
    const char* dbName = std::getenv("DEADZONE_DB_NAME");

    if (!m_db.init(
            dbHost ? dbHost : "127.0.0.1",
            dbUser ? dbUser : "root",
            dbPass ? dbPass : "",
            dbName ? dbName : "deadzone")) {
        DZ_LOG_ERROR("[Server] DB connection failed — set DEADZONE_DB_PASS env var. Running without DB.");
    }

    // Combat callbacks — 하나의 핸들러에서 HP 이벤트 + 배신 감지 모두 처리
    m_combat.onDamage([this](const DamageResult& r) {
        onDamage(r);  // HP 브로드캐스트
        if (r.betrayal) {
            uint8_t teamA = 0, teamB = 0;
            for (EntityID id : m_world.alive()) {
                Entity e{id};
                auto* net = m_world.tryGet<NetworkComponent>(e);
                auto* hp  = m_world.tryGet<HealthComponent>(e);
                if (!net || !hp) continue;
                if (net->netID == r.attackerID) teamA = static_cast<uint8_t>(hp->team);
                if (net->netID == r.victimID)   teamB = static_cast<uint8_t>(hp->team);
            }
            if (teamA && teamB) m_alliance.handleBetrayal(teamA, teamB);
        }
    });
    m_combat.onDeath  ([this](Entity v, Entity k, DamageType t) { onDeath(v, k, t); });

    // Extraction callbacks
    m_extraction.onExtracted([this](Entity p, uint8_t z)   { onExtracted(p, z); });
    m_extraction.onDeathLoot([this](Entity p)              { onDeathLoot(p); });

    // Fire destroys buildings
    m_fire.onDestroyBuilding([this](uint32_t id, bool expl){ onBuildingDestroyed(id, expl); });

    // Alliance broadcast
    m_alliance.onBroadcast([this](uint8_t a, uint8_t b, bool active){
        onAllianceChanged(a, b, active);
    });

    // 포탑 발사 → 클라이언트 레이저 빔 시각화용 브로드캐스트
    m_build.onTurretFire([this](uint16_t turretNetID,
                                float fromX, float fromY,
                                float toX,   float toY,
                                uint8_t ownerTeam) {
        TurretFirePacket pkt{};
        pkt.packetType  = static_cast<uint8_t>(PacketType::S2C_TurretFire);
        pkt.turretNetID = turretNetID;
        pkt.fromX       = fromX;
        pkt.fromY       = fromY;
        pkt.toX         = toX;
        pkt.toY         = toY;
        pkt.ownerTeam   = ownerTeam;
        m_net.broadcastReliable(&pkt, sizeof(pkt));
    });

    if (!m_net.init(port)) {
        DZ_LOG_FATAL("Failed to init network");
        return;
    }

    loadMap("data/map.json");
    m_zombieAI.setMap(&m_map);
    m_zombieAI.setCombatSystem(&m_combat);  // 좀비 공격 → CombatSystem → HP 이벤트
    spawnZombies();
    spawnLootBoxes();

    // GameLogic은 모든 시스템 초기화 후 마지막에 생성
    m_logic = std::make_unique<GameLogic>(
        m_world, m_map, m_build, m_alliance, m_combat, m_fire);

    m_logic->onRangedFire([this](uint16_t shooterID, float fromX, float fromY, float toX, float toY, uint8_t team) {
        TurretFirePacket pkt{};
        pkt.packetType  = static_cast<uint8_t>(PacketType::S2C_TurretFire);
        pkt.turretNetID = shooterID;
        pkt.fromX       = fromX;
        pkt.fromY       = fromY;
        pkt.toX         = toX;
        pkt.toY         = toY;
        pkt.ownerTeam   = team;
        m_net.broadcastReliable(&pkt, sizeof(pkt));
    });

    m_net.onFireThrow([this](uint32_t peerIdx, float x, float y) {
        if (!m_gameStarted) return;
        m_logic->handleFireThrow(peerIdx, x, y);
    });

    m_running = true;
}

GameServer::~GameServer() {
    m_net.shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// run — fixed-timestep main loop at WORLD_TICK_RATE (20 Hz)
// ─────────────────────────────────────────────────────────────────────────────
int GameServer::run() {
    using Clock = std::chrono::steady_clock;
    auto   lastTime    = Clock::now();
    float  accumulator = 0.0f;
    const float DT     = WORLD_TICK_DT;

    while (m_running) {
        auto  now     = Clock::now();
        float elapsed = std::chrono::duration<float>(now - lastTime).count();
        lastTime      = now;
        accumulator  += elapsed;

        // Process incoming packets at the top of each outer loop
        m_net.pollEvents();

        // Fixed-step ticks
        while (accumulator >= DT) {
            applyBufferedInputs(DT);
            tick(DT);
            sendSnapshots();
            ++m_tick;
            accumulator -= DT;
        }

        // Yield to prevent 100% CPU when idle
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// applyBufferedInputs — inject last known input for each connected player
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::applyBufferedInputs(float dt) {
    for (uint32_t pi = 0; pi < MAX_CLIENTS; ++pi) {
        if (!m_net.isConnected(pi)) continue;
        const auto& pinfo = m_net.peer(pi);
        // WORLD_TICK_DT 사용 — 서버 틱(20Hz)과 클라이언트 예측 속도를 맞춤
        m_movement.applyInput(m_world, m_map, pi, pinfo.lastInput, dt);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// tick — one authoritative server step
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::tick(float dt) {
    if (m_gameStarted) {
        m_gameTime += dt;
    }
    m_noise.update(m_world, dt);
    m_movement.update(m_world, m_map, dt);
    m_combat.update(m_world, dt);
    m_zombieAI.update(m_world, m_noise, dt, m_gameTime);
    m_fire.update(m_world, m_map, dt);
    m_build.updateTurrets(m_world, dt);
    m_extraction.update(m_world, dt, m_gameTime);

    // 좀비 리스폰 타이머 (10초마다 체크)
    if (m_gameStarted) {
        m_zombieSpawnTimer += dt;
        if (m_zombieSpawnTimer >= 10.0f) {
            m_zombieSpawnTimer = 0.0f;
            spawnZombies(); // 내부에서 maxZombies 체크 후 부족하면 스폰
        }
    }

    // 죽은 엔티티들의 시체 유지 시간(deathTimer) 처리
    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* hp = m_world.tryGet<HealthComponent>(e);
        if (hp && !hp->isAlive) {
            hp->deathTimer -= dt;
            if (hp->deathTimer <= 0.0f) {
                m_world.destroyEntity(e);
            }
        }
    }

    m_world.flushDestroyQueue();

    // 1초마다 팀 상태 브로드캐스트 (시간 포함)
    m_teamStatusTimer -= dt;
    if (m_teamStatusTimer <= 0.0f) {
        m_teamStatusTimer = 1.0f;
        broadcastTeamStatus();
    }

    // ── 낮/밤 웨이브 디펜스 ──────────────────────────────────────────────────────────
    // 1주기: 낮 2분(120초), 밤 1분(60초) = 180초
    float timeOfDay = std::fmod(m_gameTime, 180.0f);
    bool isNight = timeOfDay > 120.0f;

    if (m_gameStarted && isNight && !m_wasNight) {
        m_wasNight = true;
        DZ_LOG_INFO("[Server] Night has fallen! Spawning zombie wave...");
        spawnNightWave();
    } else if (m_gameStarted && !isNight && m_wasNight) {
        m_wasNight = false;
        DZ_LOG_INFO("[Server] Day breaks! Outdoor zombies will start melting.");
    }

    if (m_gameStarted && isNight) {
        updateZombieDoorAttacks(dt);
    }

    // 낮 시간(Daytime) 동안 야외에 있는 좀비에게 햇빛 데미지 지속 부여 (초당 20)
    if (m_gameStarted && !isNight) {
        for (EntityID id : m_world.alive()) {
            Entity e{id};
            if (m_world.tryGet<ZombieAIComponent>(e) && m_world.tryGet<HealthComponent>(e)->isAlive) {
                bool inBuilding = false;
                auto* xf = m_world.tryGet<TransformComponent>(e);
                if (xf) {
                    int tx = TileMap::worldToTile(xf->x);
                    int ty = TileMap::worldToTile(xf->y);
                    for (const auto& bd : m_map.getBuildings()) {
                        if (tx >= bd.x && tx < bd.x + bd.w && ty >= bd.y && ty < bd.y + bd.h) {
                            inBuilding = true;
                            break;
                        }
                    }
                }
                
                // 건물 내부 좀비는 살려둠
                if (!inBuilding) {
                    // CombatSystem을 통해 applyDamage → onDeath 콜백 정상 발동
                    m_combat.applyDamage(m_world, e, Entity{}, 20.0f * dt, DamageType::Fire, nullptr);
                }
            }
        }
    }

    // ── 라운드 타이머 ─────────────────────────────────────────────────────────
    if (m_gameStarted) {
        m_gameTimer += dt;
        
        // 마지막 사람이 나갔거나 모두 사망/탈출했다면 라운드 리셋 (게임 시작 후 5초 이후부터 검사)
        if (m_gameTimer > 5.0f && m_activePlayers <= 0) {
            DZ_LOG_INFO("[Server] All players left. Resetting round...");
            resetRound();
        }
    }

    // 탈출 프로그레스 주기적 동기화
    for (uint32_t pi = 0; pi < MAX_CLIENTS; ++pi) {
        if (!m_net.isConnected(pi)) continue;
        uint32_t netID = m_net.peer(pi).playerNetID;
        float prog = m_extraction.channelProgress(netID);
        // 상태 전송 (채널링 중이거나, 상태가 변했을 때)
        PktExtractionUpdate pkt{};
        pkt.header.type = PacketType::S2C_ExtractionUpdate;
        pkt.header.tick = m_tick;
        pkt.progress = prog;
        pkt.zoneID = m_extraction.channelZoneID(netID);
        m_net.sendUnreliable(pi, &pkt, sizeof(pkt));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// sendSnapshots
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::sendSnapshots() {
    m_net.broadcastSnapshot(m_world, static_cast<uint16_t>(m_tick));

    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (net && net->role == NetRole::LocallyOwned && net->ownerID < MAX_CLIENTS) {
            if (net->isDirty(DIRTY_INVENTORY)) {
                sendInventorySyncToPeer(net->ownerID);
            }
            if (net->isDirty(DIRTY_HEALTH)) {
                sendHpSyncToPeer(net->ownerID);
            }
            net->clearDirty();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Event handlers
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::onClientConnect(uint32_t peerIdx) {
    DZ_LOG_INFO("[Server] Peer %u connected, waiting for Auth...", peerIdx);
}

void GameServer::onClientAuth(uint32_t peerIdx, const char* username, const char* password, bool isRegister) {
    AuthAckPacket ack{};
    ack.packetType = static_cast<uint8_t>(PacketType::S2C_AuthAck);
    
    if (isRegister) {
        if (!m_db.isConnected() ||m_db.registerAccount(username, password)) {
            ack.success = 1;
            std::strncpy(ack.message, "Registration successful!", sizeof(ack.message));
            m_net.sendReliable(peerIdx, &ack, sizeof(ack));
        } else {
            ack.success = 0;
            std::strncpy(ack.message, "Username already exists.", sizeof(ack.message));
            m_net.sendReliable(peerIdx, &ack, sizeof(ack));
            return;
        }
    }
    
    if (m_gameStarted && m_gameTimer > 60.0f) {
        if (!m_childServerLaunched) {
            uint16_t nextPort = m_port + 1;
            char cmd[256];
            std::snprintf(cmd, sizeof(cmd), "nohup ./DeadZoneServer %u > /dev/null 2>&1 &", nextPort);
            std::system(cmd);
            m_childServerLaunched = true;
            DZ_LOG_INFO("[Server] Match full/started. Launched new server on port %u", nextPort);
        }
        
        ack.success = 0;
        ack.redirectPort = m_port + 1;
        std::strncpy(ack.message, "Match already started. Redirecting to new server...", sizeof(ack.message));
        m_net.sendReliable(peerIdx, &ack, sizeof(ack));
        return;
    }

    InventoryComponent loadedInv;
    if (m_db.isConnected()) {
        if (!m_db.loginAccount(username, password, loadedInv)) {
            ack.success = 0;
            std::strncpy(ack.message, "Login failed. Check credentials.", sizeof(ack.message));
            m_net.sendReliable(peerIdx, &ack, sizeof(ack));
            return;
        }
    } else {
        // DB 미연결 시 계정 검증 없이 허용 (테스트 / 오프라인 모드)
        DZ_LOG_WARN("[Server] DB offline — accepting '%s' without verification", username);
    }
    
    ack.success = 1;
    std::strncpy(ack.message, "Login successful!", sizeof(ack.message));
    m_net.sendReliable(peerIdx, &ack, sizeof(ack));

    m_peerUsernames[peerIdx] = username;
    
    m_lobbyPlayers[peerIdx] = { username, loadedInv };
    
    InventorySyncPacket syncPkt{};
    syncPkt.packetType = static_cast<uint8_t>(PacketType::S2C_InventorySync);
    syncPkt.money = loadedInv.money;
    syncPkt.usedSlots = static_cast<uint8_t>(loadedInv.usedSlots);
    for (int i=0; i<INVENTORY_GRID_SLOTS; ++i) {
        if (loadedInv.slots[i].isValid()) {
            syncPkt.gridSlots[i].itemID = loadedInv.slots[i].itemID;
            std::strncpy(syncPkt.gridSlots[i].key, loadedInv.slots[i].key.c_str(), 19);
            syncPkt.gridSlots[i].category = static_cast<uint8_t>(loadedInv.slots[i].category);
            syncPkt.gridSlots[i].quantity = loadedInv.slots[i].quantity;
            syncPkt.gridSlots[i].weight = loadedInv.slots[i].weight;
        }
    }
    for (int i=0; i<EQUIPMENT_SLOT_COUNT; ++i) {
        if (loadedInv.equipped[i].isValid()) {
            syncPkt.equipped[i].itemID = loadedInv.equipped[i].itemID;
            std::strncpy(syncPkt.equipped[i].key, loadedInv.equipped[i].key.c_str(), 19);
            syncPkt.equipped[i].category = static_cast<uint8_t>(loadedInv.equipped[i].category);
            syncPkt.equipped[i].quantity = loadedInv.equipped[i].quantity;
            syncPkt.equipped[i].weight = loadedInv.equipped[i].weight;
        }
    }
    m_net.sendReliable(peerIdx, &syncPkt, sizeof(syncPkt));

    StashSyncPacket stashPkt{};
    for (int i=0; i<40; ++i) {
        if (loadedInv.stash[i].isValid()) {
            stashPkt.stashSlots[i].itemID = loadedInv.stash[i].itemID;
            std::strncpy(stashPkt.stashSlots[i].key, loadedInv.stash[i].key.c_str(), 19);
            stashPkt.stashSlots[i].category = static_cast<uint8_t>(loadedInv.stash[i].category);
            stashPkt.stashSlots[i].quantity = loadedInv.stash[i].quantity;
            stashPkt.stashSlots[i].weight = loadedInv.stash[i].weight;
        }
    }
    m_net.sendReliable(peerIdx, &stashPkt, sizeof(stashPkt));

    DZ_LOG_INFO("[Server] Player %s authenticated in lobby (peer %u)", username, peerIdx);
}

void GameServer::onStashTransferReq(uint32_t peerIdx, uint8_t srcType, uint8_t srcIdx, uint8_t dstType, uint8_t dstIdx) {
    InventoryComponent* invPtr = nullptr;
    bool inMatch = false;

    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (net && net->role == NetRole::LocallyOwned && net->ownerID == peerIdx) {
            auto* hp = m_world.tryGet<HealthComponent>(e);
            if (!hp || !hp->isAlive) continue;
            invPtr = m_world.tryGet<InventoryComponent>(e);
            inMatch = invPtr != nullptr;
            break;
        }
    }

    if (!invPtr) {
        auto it = m_lobbyPlayers.find(peerIdx);
        if (it == m_lobbyPlayers.end()) return;
        invPtr = &it->second.inv;
    }

    auto& inv = *invPtr;

    auto getItemPtr = [&](uint8_t type, uint8_t idx) -> Item* {
        if (type == 0 && idx < INVENTORY_GRID_SLOTS) return &inv.slots[idx];
        if (type == 1 && idx == 0) return &inv.equipped[0];
        if (type == 2 && idx == 0) return &inv.equipped[1];
        if (!inMatch && type == 3 && idx < 40) return &inv.stash[idx];
        return nullptr;
    };

    Item* src = getItemPtr(srcType, srcIdx);
    Item* dst = getItemPtr(dstType, dstIdx);

    if (src && dst) {
        const bool srcIsEquip = (srcType == 1 || srcType == 2);
        const bool dstIsEquip = (dstType == 1 || dstType == 2);
        if (dstIsEquip && src->isValid() && src->category != ItemCategory::Weapon) return;
        if (srcIsEquip && dst->isValid() && dst->category != ItemCategory::Weapon) return;

        if (srcType == 0 && dstIsEquip && src->isValid() &&
            src->category == ItemCategory::Weapon && !dst->isValid()) {
            inv.equip(srcIdx, dstType == 1 ? EquipSlot::PrimaryWeapon : EquipSlot::SecondaryWeapon);
        } else {
            std::swap(*src, *dst);
            inv.recalculateGridStats();
        }

        if (inMatch) {
            sendInventorySyncToPeer(peerIdx);
            if (auto* net = [&]() -> NetworkComponent* {
                    for (EntityID id : m_world.alive()) {
                        Entity e{id};
                        auto* n = m_world.tryGet<NetworkComponent>(e);
                        if (n && n->role == NetRole::LocallyOwned && n->ownerID == peerIdx) return n;
                    }
                    return nullptr;
            }()) {
                net->markDirty(DIRTY_INVENTORY);
            }
        } else {
            inv.recalculateGridStats();
            if (!m_peerUsernames[peerIdx].empty()) {
                m_db.saveAccount(m_peerUsernames[peerIdx], inv);
            }
            sendInventorySyncToPeer(peerIdx);
            sendStashSyncToPeer(peerIdx);
        }
    }
}

void GameServer::onJoinMatch(uint32_t peerIdx) {
    if (m_lobbyPlayers.find(peerIdx) == m_lobbyPlayers.end()) return;

    for (EntityID id : m_world.alive()) {
        Entity existing{id};
        auto* net = m_world.tryGet<NetworkComponent>(existing);
        if (!net || net->role != NetRole::LocallyOwned || net->ownerID != peerIdx) continue;

        auto* hp = m_world.tryGet<HealthComponent>(existing);
        if (hp && hp->isAlive) return;
        m_world.destroyEntity(existing);
    }
    m_world.flushDestroyQueue();
    m_net.setPeerNetID(peerIdx, 0);
    
    auto& lobbyPlayer = m_lobbyPlayers[peerIdx];
    InventoryComponent invToSpawn = lobbyPlayer.inv;

    Entity e = m_world.createEntity();
    uint32_t netID = m_nextPlayerNetID++;

    int teamIdx = static_cast<int>(peerIdx / TEAM_SIZE); // 0-based
    if (teamIdx >= 4) teamIdx = 0;

    int baseTileX = 5;
    int baseTileY = 5;
    const auto& spawns = m_map.getPlayerSpawns();
    for (const auto& sp : spawns) {
        if (sp.team == static_cast<uint8_t>(teamIdx + 1)) {
            baseTileX = sp.x;
            baseTileY = sp.y;
            break;
        }
    }
    if (spawns.empty()) {
        const int maxTileX = m_map.width()  > 10 ? m_map.width()  - 6 : m_map.width()  - 1;
        const int maxTileY = m_map.height() > 10 ? m_map.height() - 6 : m_map.height() - 1;
        const int fallbackX[4] = {5, maxTileX, 5, maxTileX};
        const int fallbackY[4] = {5, 5, maxTileY, maxTileY};
        baseTileX = fallbackX[teamIdx];
        baseTileY = fallbackY[teamIdx];
    }
    const int jitterX = static_cast<int>(peerIdx % TEAM_SIZE) * 2;
    const int jitterY = static_cast<int>(peerIdx % TEAM_SIZE) * 2;
    float spawnX = TileMap::tileCentre(baseTileX + (teamIdx % 2 == 0 ? jitterX : -jitterX));
    float spawnY = TileMap::tileCentre(baseTileY + (teamIdx < 2 ? jitterY : -jitterY));

    auto& xf  = m_world.addComponent<TransformComponent>(e);
    xf.x      = spawnX;
    xf.y      = spawnY;

    auto& hp  = m_world.addComponent<HealthComponent>(e);
    hp.maxHp     = 100.0f;
    hp.currentHp = 100.0f;
    hp.team      = static_cast<Team>(teamIdx + 1); // Team enum 1-4
    hp.isAlive   = true;
    hp.isInvincible = true;
    hp.invincibleTimer = 3.0f;
    auto& cbt = m_world.addComponent<CombatComponent>(e);
    auto& inv = m_world.addComponent<InventoryComponent>(e);
    inv = invToSpawn;

    // Starter weapon and items if empty
    if (inv.usedSlots == 0 && !inv.equipped[0].isValid()) {
        Item startWeapon;
        startWeapon.itemID   = 4;
        startWeapon.key      = "pistol_9mm";
        startWeapon.category = ItemCategory::Weapon;
        startWeapon.quantity = PISTOL_MAG_CAPACITY;
        startWeapon.weight   = 1.5f;
        inv.addItem(startWeapon);
        
        Item medkit;
        medkit.itemID   = 30;
        medkit.key      = "medkit";
        medkit.category = ItemCategory::Consumable;
        medkit.quantity = 3;
        medkit.weight   = 1.0f;
        inv.addItem(medkit);

        Item bandage;
        bandage.itemID   = 31;
        bandage.key      = "bandage";
        bandage.category = ItemCategory::Consumable;
        bandage.quantity = 5;
        bandage.weight   = 0.2f;
        inv.addItem(bandage);
        
        // Auto-equip the pistol
        inv.equipped[0] = inv.slots[0];
        inv.removeItem(0);
    }

    auto& net = m_world.addComponent<NetworkComponent>(e);
    net.netID  = netID;
    net.ownerID= peerIdx;
    net.role   = NetRole::LocallyOwned;

    // Start with fully loaded pistol
    cbt.magCapacity = PISTOL_MAG_CAPACITY;
    cbt.ammoInMag = PISTOL_MAG_CAPACITY;
    cbt.ammoReserve = 14;

    m_net.setPeerNetID(peerIdx, netID);

    ConnectAckPacket cAck{};
    cAck.packetType = static_cast<uint8_t>(PacketType::S2C_ConnectAck);
    cAck.netID  = netID;
    cAck.spawnX = xf.x;
    cAck.spawnY = xf.y;
    cAck.teamID = static_cast<uint8_t>(teamIdx + 1);
    m_net.sendReliable(peerIdx, &cAck, sizeof(cAck));
    syncDoorStatesToPeer(peerIdx);

    // 시작 처리
    if (!m_gameStarted) {
        m_gameStarted = true;
        m_gameTimer = 0.0f;
        m_gameTime = 0.0f;
        m_zombieSpawnTimer = 0.0f;
        m_wasNight = false;
        DZ_LOG_INFO("[Server] Round timer started by first client match join.");
    }
    m_activePlayers++;

    DZ_LOG_INFO("[Server] Player %u joined match (peer %u, team %d) at (%.0f, %.0f)",
                netID, peerIdx, teamIdx + 1, xf.x, xf.y);
}

void GameServer::onClientDisconnect(uint32_t peerIdx) {
    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (net && net->role == NetRole::LocallyOwned && net->ownerID == peerIdx) {
            auto* inv = m_world.tryGet<InventoryComponent>(e);
            if (inv) {
                if (!m_peerUsernames[peerIdx].empty()) {
                    m_db.saveAccount(m_peerUsernames[peerIdx], *inv);
                }
                if (m_lobbyPlayers.find(peerIdx) != m_lobbyPlayers.end()) {
                    m_lobbyPlayers[peerIdx].inv = *inv;
                }
            }
            m_world.destroyEntity(e);
            // 이미 사망(onDeath에서 감소됨)한 경우 중복 감소 방지
            auto* hp = m_world.tryGet<HealthComponent>(e);
            if (!hp || hp->isAlive) m_activePlayers--;
            break;
        }
    }
    m_peerUsernames[peerIdx].clear();
}

void GameServer::onInputReceived(uint32_t peerIdx, const InputPacket& pkt) {
    // 이동은 applyBufferedInputs에서 처리, 전투/건설 액션은 GameLogic으로 전달
    if (m_logic) {
        m_logic->processInput(peerIdx, pkt);
    }

    // 낙원 스타일: F키(ACT_INTERACT)를 눌러 탈출(채널링) 시작
    if (pkt.actions & ACT_INTERACT) {
        uint32_t netID = m_net.peer(peerIdx).playerNetID;
        for (EntityID id : m_world.alive()) {
            Entity e{id};
            auto* net = m_world.tryGet<NetworkComponent>(e);
            if (net && net->netID == netID) {
                auto* xf = m_world.tryGet<TransformComponent>(e);
                auto* hp = m_world.tryGet<HealthComponent>(e);
                if (xf && hp && hp->isAlive) {
                    m_extraction.startChanneling(netID, xf->x, xf->y, hp->currentHp);
                }
                break;
            }
        }
    }
}

void GameServer::onDamage(const DamageResult& result) {
    if (result.damage <= 0.0f) return;
    DamageEventPacket pkt{};
    pkt.packetType  = static_cast<uint8_t>(PacketType::S2C_DamageEvent);
    pkt.victimID    = static_cast<uint16_t>(result.victimID);
    pkt.attackerID  = static_cast<uint16_t>(result.attackerID);
    pkt.damage      = result.damage;
    pkt.damageType  = static_cast<uint8_t>(result.type);
    pkt.remainingHp = result.remainingHp;
    m_net.broadcastReliable(&pkt, sizeof(pkt));

    // 피해를 받은 플레이어에게 HpSync 전송 (출혈·화상 등 상태 포함)
    for (uint32_t pi = 0; pi < MAX_CLIENTS; ++pi) {
        if (!m_net.isConnected(pi)) continue;
        if (m_net.peer(pi).playerNetID == result.victimID)
            sendHpSyncToPeer(pi);
    }

}

void GameServer::onDeath(Entity victim, Entity killer, DamageType type) {
    DeathEventPacket pkt{};
    pkt.packetType = static_cast<uint8_t>(PacketType::S2C_DeathEvent);
    pkt.damageType = static_cast<uint8_t>(type);
    
    if (killer.isValid()) {
        auto* knet = m_world.tryGet<NetworkComponent>(killer);
        if (knet) {
            pkt.killerID = static_cast<uint16_t>(knet->netID);
            for (uint32_t pi = 0; pi < MAX_CLIENTS; ++pi) {
                if (m_net.isConnected(pi) && m_net.peer(pi).playerNetID == knet->netID) {
                    std::strncpy(pkt.killerName, m_peerUsernames[pi].c_str(), sizeof(pkt.killerName) - 1);
                    break;
                }
            }
        }
    }

    auto* net = m_world.tryGet<NetworkComponent>(victim);
    if (net) {
        pkt.victimID = static_cast<uint16_t>(net->netID);
        // 플레이어일 때만 m_activePlayers 감소 (onExtracted/onClientDisconnect 에서 중복 감소 방지)
        // onExtracted는 엔티티를 바로 파괴하므로 이 경로는 순수 전투사망 시만 실행됨
    }
    m_net.broadcastReliable(&pkt, sizeof(pkt));

    // 건물 파괴 처리
    auto* bld = m_world.tryGet<BuildingComponent>(victim);
    if (bld) {
        bool explosion = bld->isTurret();
        onBuildingDestroyed(net ? net->netID : 0, explosion);
        m_build.destroyBuilding(m_world, m_map, victim);
        return;
    }

    // 플레이어 사망 → DB에 사망 기록 및 루트 드랍 후 엔티티 파괴
    if (net && net->role == NetRole::LocallyOwned) {
        m_activePlayers--; // 전투 사망 시 감소
        for (uint32_t pi = 0; pi < MAX_CLIENTS; ++pi) {
            if (m_net.isConnected(pi) &&
                m_net.peer(pi).playerNetID == net->netID &&
                !m_peerUsernames[pi].empty()) {
                m_db.recordDeath(m_peerUsernames[pi]);
                break;
            }
        }
    }

    // 사망 시 아이템 드랍
    onDeathLoot(victim);

    // 만약 좀비가 죽었다면 50% 확률로 아이템 드랍
    auto* ai = m_world.tryGet<ZombieAIComponent>(victim);
    if (ai && (std::rand() % 100) < 50) {
        Entity loot = m_world.createEntity();
        auto& lxf = m_world.addComponent<TransformComponent>(loot);
        auto* vxf = m_world.tryGet<TransformComponent>(victim);
        if (vxf) { lxf.x = vxf->x; lxf.y = vxf->y; }
        
        auto& linv = m_world.addComponent<InventoryComponent>(loot);
        Item item;
        item.quantity = 1;
        int r = std::rand() % 4;
        if (r == 0) {
            item.itemID = 31;
            item.key = "bandage"; item.category = ItemCategory::Consumable; item.weight = 0.3f;
        } else if (r == 1) {
            item.itemID = 10;
            item.key = "ammo_9mm"; item.category = ItemCategory::Ammo; item.weight = 0.3f; item.quantity = 30;
        } else if (r == 2) {
            item.itemID = 30;
            item.key = "medkit"; item.category = ItemCategory::Consumable; item.weight = 1.0f;
        } else {
            item.itemID = 20;
            item.key = "scrap_metal"; item.category = ItemCategory::BuildMaterial; item.weight = 1.0f; item.quantity = 1;
        }
        linv.addItem(item);
        
        auto& lnet = m_world.addComponent<NetworkComponent>(loot);
        lnet.netID  = m_nextPlayerNetID++;
        lnet.role   = NetRole::ServerAuth;
    }
    
    // 시체를 바로 파괴하지 않고 deathTimer가 만료될 때까지 유지
    // m_world.destroyEntity(victim);
}

void GameServer::onExtracted(Entity player, uint8_t zoneID) {
    ExtractionPacket pkt{};
    pkt.packetType = static_cast<uint8_t>(PacketType::S2C_ExtractionResult);
    auto* net = m_world.tryGet<NetworkComponent>(player);
    if (net) pkt.playerID = static_cast<uint16_t>(net->netID);
    pkt.zoneID      = zoneID;
    pkt.channelTime = 0.0f;
    m_net.broadcastReliable(&pkt, sizeof(pkt));

    // 탈출 성공 → 인벤토리 저장 + 통계 기록
    if (net) {
        const std::string& uname = m_peerUsernames[net->ownerID];
        if (!uname.empty()) {
            auto* inv = m_world.tryGet<InventoryComponent>(player);
            if (inv) m_db.saveAccount(uname, *inv);
            m_db.recordExtraction(uname);
        }
        // 로비 인벤토리 동기화 (다음 매치 진입 시 탈출한 템 유지)
        auto* inv = m_world.tryGet<InventoryComponent>(player);
        if (inv && m_lobbyPlayers.find(net->ownerID) != m_lobbyPlayers.end()) {
            m_lobbyPlayers[net->ownerID].inv = *inv;
        }
    }

    m_world.destroyEntity(player);
    m_activePlayers--;
}

void GameServer::onDeathLoot(Entity player) {
    auto* inv = m_world.tryGet<InventoryComponent>(player);
    auto* xf  = m_world.tryGet<TransformComponent>(player);
    if (!inv || !xf) return;

    auto dropItem = [&](const Item& item) {
        if (!item.isValid() || item.quantity <= 0) return;
        Entity loot = m_world.createEntity();
        float ox = static_cast<float>((std::rand() % 48) - 24);
        float oy = static_cast<float>((std::rand() % 48) - 24);
        auto& lxf = m_world.addComponent<TransformComponent>(loot);
        lxf.x = xf->x + ox;
        lxf.y = xf->y + oy;
        auto& linv = m_world.addComponent<InventoryComponent>(loot);
        linv.addItem(item);
        auto& lnet = m_world.addComponent<NetworkComponent>(loot);
        lnet.netID  = m_nextPlayerNetID++;
        lnet.role   = NetRole::ServerAuth;
        lnet.markDirty(DIRTY_TRANSFORM);
        lnet.markDirty(DIRTY_INVENTORY); // 아이템 내용도 클라이언트에 전송
        DZ_LOG_INFO("[Death] LootEntity %u spawned: %s x%d at (%.0f,%.0f)",
            lnet.netID, item.key.c_str(), item.quantity, lxf.x, lxf.y);
    };

    for (int i = 0; i < INVENTORY_GRID_SLOTS; ++i) {
        if (!inv->slots[i].isValid()) continue;
        dropItem(inv->slots[i]);
        inv->removeItem(i);
    }

    dropItem(inv->equipped[0]);
    inv->equipped[0] = {}; // PrimaryWeapon
    dropItem(inv->equipped[1]);
    inv->equipped[1] = {}; // SecondaryWeapon
    inv->usedSlots = 0;

    auto* net = m_world.tryGet<NetworkComponent>(player);
    if (net && m_lobbyPlayers.find(net->ownerID) != m_lobbyPlayers.end()) {
        m_lobbyPlayers[net->ownerID].inv = *inv;
    }
}

void GameServer::onBuildingDestroyed(uint32_t buildingNetID, bool explosion) {
    // Find building world position
    float bx = 0.0f, by = 0.0f;
    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (net && net->netID == buildingNetID) {
            auto* bxf = m_world.tryGet<TransformComponent>(e);
            if (bxf) { bx = bxf->x; by = bxf->y; }
            break;
        }
    }

    if (explosion) {
        m_noise.addEvent(bx, by, NOISE_EXPLOSION_RADIUS, 4, 2.0f); // Deafening
    } else {
        m_noise.addEvent(bx, by, NOISE_MELEE_RADIUS, 2, 1.0f); // Moderate (structure destroyed)
    }
}

void GameServer::updateZombieDoorAttacks(float dt) {
    constexpr float DOOR_ATTACK_RANGE = 30.0f;
    constexpr float DOOR_ATTACK_RANGE2 = DOOR_ATTACK_RANGE * DOOR_ATTACK_RANGE;
    constexpr float DOOR_TARGET_RANGE = TILE_SIZE * 7.0f;
    constexpr float DOOR_TARGET_RANGE2 = DOOR_TARGET_RANGE * DOOR_TARGET_RANGE;
    constexpr float PLAYER_DOOR_AGGRO_RANGE = TILE_SIZE * 14.0f;
    constexpr float PLAYER_DOOR_AGGRO_RANGE2 = PLAYER_DOOR_AGGRO_RANGE * PLAYER_DOOR_AGGRO_RANGE;
    const auto& buildings = m_map.getBuildings();

    struct PlayerHouseTarget {
        bool occupied = false;
        float x = 0.0f;
        float y = 0.0f;
        uint32_t netID = 0;
    };
    std::vector<PlayerHouseTarget> playerTargets(buildings.size());

    for (EntityID id : m_world.alive()) {
        Entity player{id};
        auto* net = m_world.tryGet<NetworkComponent>(player);
        if (!net || net->role != NetRole::LocallyOwned) continue;
        auto* hp = m_world.tryGet<HealthComponent>(player);
        auto* xf = m_world.tryGet<TransformComponent>(player);
        if (!hp || !hp->isAlive || !xf) continue;

        int tx = TileMap::worldToTile(xf->x);
        int ty = TileMap::worldToTile(xf->y);
        for (size_t bi = 0; bi < buildings.size(); ++bi) {
            const auto& b = buildings[bi];
            if (tx >= b.x && tx < b.x + b.w && ty >= b.y && ty < b.y + b.h) {
                auto& target = playerTargets[bi];
                if (!target.occupied) {
                    target.occupied = true;
                    target.x = xf->x;
                    target.y = xf->y;
                    target.netID = net->netID;
                }
                break;
            }
        }
    }

    bool anyPlayerInside = false;
    for (const auto& target : playerTargets) {
        if (target.occupied) { anyPlayerInside = true; break; }
    }
    if (!anyPlayerInside) return;

    auto doorPlayerTarget = [&](const TileMap::DoorDef& door, PlayerHouseTarget& out) {
        if (door.building >= playerTargets.size()) return false;
        const auto& target = playerTargets[door.building];
        if (!target.occupied) return false;
        out = target;
        return true;
    };

    for (EntityID id : m_world.alive()) {
        Entity zombie{id};
        auto* ai = m_world.tryGet<ZombieAIComponent>(zombie);
        auto* hp = m_world.tryGet<HealthComponent>(zombie);
        auto* xf = m_world.tryGet<TransformComponent>(zombie);
        if (!ai || !hp || !hp->isAlive || !xf) continue;

        int bestTargetDoor = -1;
        float bestTargetD2 = DOOR_TARGET_RANGE2;
        PlayerHouseTarget bestTargetPlayer{};
        const auto& doors = m_map.getDoors();
        for (const auto& door : doors) {
            if (door.open || door.broken) continue;
            PlayerHouseTarget target{};
            if (!doorPlayerTarget(door, target)) continue;
            const float pdx = target.x - xf->x;
            const float pdy = target.y - xf->y;
            if (pdx * pdx + pdy * pdy > PLAYER_DOOR_AGGRO_RANGE2) continue;
            const float dx = TileMap::tileCentre(door.tx) - xf->x;
            const float dy = TileMap::tileCentre(door.ty) - xf->y;
            const float d2 = dx * dx + dy * dy;
            if (d2 <= bestTargetD2) {
                bestTargetD2 = d2;
                bestTargetDoor = static_cast<int>(door.id);
                bestTargetPlayer = target;
            }
        }
        if (bestTargetDoor >= 0) {
            const auto& door = doors[bestTargetDoor];
            ai->targetX = TileMap::tileCentre(door.tx);
            ai->targetY = TileMap::tileCentre(door.ty);
            ai->targetNetID = bestTargetPlayer.netID;
            ai->targetDoorID = static_cast<int16_t>(bestTargetDoor);
            if (ai->state == ZombieState::Idle || ai->state == ZombieState::Alert) {
                ai->state = ZombieState::Chase;
                ai->stateTimer = 0.0f;
            }
        }

        int bestDoor = -1;
        float bestD2 = DOOR_ATTACK_RANGE2;
        PlayerHouseTarget attackTargetPlayer{};
        for (const auto& door : doors) {
            if (door.open || door.broken) continue;
            PlayerHouseTarget target{};
            if (!doorPlayerTarget(door, target)) continue;
            const float pdx = target.x - xf->x;
            const float pdy = target.y - xf->y;
            if (pdx * pdx + pdy * pdy > PLAYER_DOOR_AGGRO_RANGE2) continue;
            const float dx = TileMap::tileCentre(door.tx) - xf->x;
            const float dy = TileMap::tileCentre(door.ty) - xf->y;
            const float d2 = dx * dx + dy * dy;
            if (d2 <= bestD2) {
                bestD2 = d2;
                bestDoor = static_cast<int>(door.id);
                attackTargetPlayer = target;
            }
        }
        if (bestDoor < 0) continue;

        float damagePerSecond = 10.0f;
        if (ai->type == ZombieType::Runner) damagePerSecond = 14.0f;
        else if (ai->type == ZombieType::Brute) damagePerSecond = 24.0f;
        if (ai->state == ZombieState::Frenzy) damagePerSecond *= 1.35f;

        if (m_map.damageDoor(static_cast<uint16_t>(bestDoor), damagePerSecond * dt)) {
            const auto& brokenDoor = m_map.getDoors()[bestDoor];
            DZ_LOG_INFO("[Door] Zombie broke door %d at (%d,%d)",
                        bestDoor, brokenDoor.tx, brokenDoor.ty);
            broadcastDoorState(static_cast<uint16_t>(bestDoor), true);
            m_noise.addEvent(TileMap::tileCentre(brokenDoor.tx),
                             TileMap::tileCentre(brokenDoor.ty),
                             480.0f, 3, 1.5f);

            for (EntityID zid : m_world.alive()) {
                Entity nearZombie{zid};
                auto* zai = m_world.tryGet<ZombieAIComponent>(nearZombie);
                auto* zhp = m_world.tryGet<HealthComponent>(nearZombie);
                auto* zxf = m_world.tryGet<TransformComponent>(nearZombie);
                if (!zai || !zhp || !zhp->isAlive || !zxf) continue;

                const float pdx = attackTargetPlayer.x - zxf->x;
                const float pdy = attackTargetPlayer.y - zxf->y;
                if (pdx * pdx + pdy * pdy > PLAYER_DOOR_AGGRO_RANGE2) continue;

                zai->targetX = attackTargetPlayer.x;
                zai->targetY = attackTargetPlayer.y;
                zai->targetNetID = attackTargetPlayer.netID;
                zai->targetDoorID = -1;
                zai->state = ZombieState::Chase;
                zai->stateTimer = 0.0f;
            }
        }
    }
}

void GameServer::onAllianceChanged(uint8_t teamA, uint8_t teamB, bool active) {
    AlliancePacket pkt{};
    pkt.packetType = static_cast<uint8_t>(
        active ? PacketType::S2C_AllianceAck : PacketType::S2C_AllianceAck);
    pkt.teamA  = teamA;
    pkt.teamB  = teamB;
    pkt.active = active ? 1 : 0;
    m_net.broadcastReliable(&pkt, sizeof(pkt));
}

bool GameServer::loadMap(const std::string& path) {
    if (!m_map.loadFromJSON(path)) {
        DZ_LOG_WARN("[Server] Using default 80x80 map");
        m_map = TileMap(80, 80);
    }
    // 건물 외벽을 SOLID로 마킹 — 클라이언트와 동일한 충돌 맵 구성.
    // 이 호출이 없으면 서버에 벽 충돌이 없어서 플레이어가 벽을 통과한다.
    applyBuildingCollisions(m_map);

    m_extraction.clearZones();
    const auto& zones = m_map.getExtractionZones();
    if (!zones.empty()) {
        for (const auto& ez : zones) {
            float cx = (static_cast<float>(ez.tileX) + ez.w * 0.5f) * TILE_SIZE;
            float cy = (static_cast<float>(ez.tileY) + ez.h * 0.5f) * TILE_SIZE;
            m_extraction.addZone({ez.id, cx, cy, false});
        }
    } else {
        float cx = (m_map.width() * 0.5f) * TILE_SIZE;
        float cy = (m_map.height() * 0.5f) * TILE_SIZE;
        m_extraction.addZone({0, cx, cy, false});
    }
    
    m_extraction.onExtractionOpen([this]() {
        SirenEventPacket pkt{};
        m_net.broadcastReliable(&pkt, sizeof(pkt));
    });

    return true;
}

void GameServer::spawnZombies() {
    float timeOfDay = std::fmod(m_gameTime, 180.0f);
    bool isNight = timeOfDay > 120.0f;
    const int targetZombies = isNight ? NIGHT_ZOMBIE_TARGET : DAY_ZOMBIE_TARGET;
    const int livingZombies = countLivingZombies(m_world);
    if (livingZombies >= targetZombies) return;

    const int spawnBudget = std::min(MAX_ZOMBIE_SPAWN_BATCH, targetZombies - livingZombies);
    // 모든 위치는 건물 외부 개방 공간에 배치 (applyBuildingCollisions 충돌 없음)
    struct ZSpawn { float x, y; ZombieType type; float hp; };
    std::vector<ZSpawn> spawns;
    spawns.reserve(spawnBudget);
    
    // 파밍 지역(건물 내부) 위주로 스폰 (안전 지역 확보)
    const auto& bds = m_map.getBuildings();
    if (!bds.empty()) {
        std::vector<int> eligibleBuildings;
        eligibleBuildings.reserve(bds.size());

        auto playerInsideBuilding = [&](const TileMap::BuildingDef& bd) {
            for (EntityID id : m_world.alive()) {
                Entity e{id};
                auto* net = m_world.tryGet<NetworkComponent>(e);
                if (!net || net->role != NetRole::LocallyOwned) continue;
                auto* hp = m_world.tryGet<HealthComponent>(e);
                if (!hp || !hp->isAlive) continue;
                auto* xf = m_world.tryGet<TransformComponent>(e);
                if (!xf) continue;
                int tx = TileMap::worldToTile(xf->x);
                int ty = TileMap::worldToTile(xf->y);
                if (tx >= bd.x && tx < bd.x + bd.w &&
                    ty >= bd.y && ty < bd.y + bd.h) {
                    return true;
                }
            }
            return false;
        };

        auto allDoorsClosed = [&](int buildingIdx) {
            int doorCount = 0;
            for (const auto& door : m_map.getDoors()) {
                if (door.building != buildingIdx) continue;
                ++doorCount;
                if (door.open) return false;
            }
            return doorCount >= 4;
        };
        auto buildingHasBrokenDoor = [&](int buildingIdx) {
            for (const auto& door : m_map.getDoors()) {
                if (door.building == buildingIdx && door.broken) return true;
            }
            return false;
        };

        for (int i = 0; i < static_cast<int>(bds.size()); ++i) {
            if (playerInsideBuilding(bds[i]) &&
                (allDoorsClosed(i) || buildingHasBrokenDoor(i))) continue;
            eligibleBuildings.push_back(i);
        }
        if (eligibleBuildings.empty()) return;

        for (int i = 0; i < spawnBudget; ++i) {
            const auto& bd = bds[eligibleBuildings[std::rand() % eligibleBuildings.size()]];
            int insetX = (bd.w > 6) ? 2 : 1;
            int insetY = (bd.h > 6) ? 2 : 1;
            int usableW = std::max(1, bd.w - insetX * 2);
            int usableH = std::max(1, bd.h - insetY * 2);
            float bx = TileMap::tileCentre(bd.x + insetX + (std::rand() % usableW));
            float by = TileMap::tileCentre(bd.y + insetY + (std::rand() % usableH));
            int roll = std::rand() % 20;
            ZombieType type = (roll == 0)   ? ZombieType::Brute    // 5%
                            : (roll < 5)    ? ZombieType::Runner   // 20%
                            :                 ZombieType::Shambler; // 75%
            float hp = (type == ZombieType::Brute) ? 200.f
                     : (type == ZombieType::Runner) ? 40.f : 60.f;
            spawns.push_back({bx, by, type, hp});
        }
    }

    // 현재 살아있는 좀비들의 위치를 미리 수집
    std::vector<std::pair<float,float>> livingZombiePositions;
    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* ai = m_world.tryGet<ZombieAIComponent>(e);
        if (!ai) continue;
        auto* hp = m_world.tryGet<HealthComponent>(e);
        if (!hp || !hp->isAlive) continue;
        auto* xf = m_world.tryGet<TransformComponent>(e);
        if (xf) livingZombiePositions.push_back({xf->x, xf->y});
    }

    int spawned = 0;
    for (auto& sp : spawns) {
        // 이 스폰 포인트 반경 96px 안에 살아있는 좀비가 있으면 스킵
        bool occupied = false;
        for (auto& [zx, zy] : livingZombiePositions) {
            float dx = zx - sp.x, dy = zy - sp.y;
            if (dx*dx + dy*dy < 96.f*96.f) { occupied = true; break; }
        }
        if (occupied) continue;

        Entity e = m_world.createEntity();

        auto& xf     = m_world.addComponent<TransformComponent>(e);
        xf.x         = sp.x;
        xf.y         = sp.y;

        auto& hp     = m_world.addComponent<HealthComponent>(e);
        hp.maxHp     = sp.hp;
        hp.currentHp = sp.hp;
        hp.team      = Team::Neutral;
        hp.isAlive   = true;

        m_world.addComponent<CombatComponent>(e);

        auto& ai     = m_world.addComponent<ZombieAIComponent>(e);
        ai.type      = sp.type;

        auto& net    = m_world.addComponent<NetworkComponent>(e);
        net.netID    = m_nextPlayerNetID++;
        net.role     = NetRole::ServerAuth;
        ++spawned;
    }
    if (spawned > 0)
        DZ_LOG_INFO("[Server] Respawned %d zombies", spawned);
}


// ─────────────────────────────────────────────────────────────────────────────
// onUseItem — 소모품 사용 처리 후 HP 동기화

// ─────────────────────────────────────────────────────────────────────────────
void GameServer::onUseItem(uint32_t peerIdx, const char* key) {
    if (m_logic) m_logic->handleUseItem(peerIdx, key);
    sendInventorySyncToPeer(peerIdx);
    sendHpSyncToPeer(peerIdx);
}

// ─────────────────────────────────────────────────────────────────────────────
// onAllianceProposeReq — 연합 제안 처리
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::onAllianceProposeReq(uint32_t peerIdx, uint8_t toTeam) {
    // fromTeam = 이 피어가 조종하는 플레이어의 팀
    uint8_t fromTeam = 0;
    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (net && net->role == NetRole::LocallyOwned && net->ownerID == peerIdx) {
            auto* hp = m_world.tryGet<HealthComponent>(e);
            if (hp) fromTeam = static_cast<uint8_t>(hp->team);
            break;
        }
    }
    if (fromTeam == 0 || fromTeam == toTeam) return;
    if (m_logic) m_logic->handleAlliancePropose(fromTeam, toTeam);
}

// ─────────────────────────────────────────────────────────────────────────────
// onBuildPlace — 건설 배치 처리
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::onBuildPlace(uint32_t peerIdx, int16_t tileX, int16_t tileY, uint8_t btype, uint8_t dir) {
    if (static_cast<BuildingType>(btype) == BuildingType::Door) {
        onDoorRepairReq(peerIdx, tileX, tileY);
        return;
    }

    bool ok = m_logic && m_logic->handleBuildRequest(peerIdx, tileX, tileY,
                                                      static_cast<BuildingType>(btype), dir);
    BuildAckPacket ack{};
    ack.packetType = static_cast<uint8_t>(PacketType::S2C_BuildAck);
    ack.success = ok ? 1 : 0;
    if (ok) {
        std::strncpy(ack.message, "건설 완료!", sizeof(ack.message) - 1);
        sendInventorySyncToPeer(peerIdx); // 재료 소모 즉시 반영
    } else {
        std::strncpy(ack.message, "재료 부족 또는 설치 불가 위치입니다.", sizeof(ack.message) - 1);
    }
    m_net.sendReliable(peerIdx, &ack, sizeof(ack));
}

void GameServer::onDoorRepairReq(uint32_t peerIdx, int16_t tileX, int16_t tileY) {
    const int doorID = m_map.findDoorAt(tileX, tileY);
    if (doorID < 0) return;
    const auto& doors = m_map.getDoors();
    if (static_cast<size_t>(doorID) >= doors.size() || !doors[doorID].broken) return;

    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (!net || net->role != NetRole::LocallyOwned || net->ownerID != peerIdx) continue;
        auto* xf = m_world.tryGet<TransformComponent>(e);
        auto* hp = m_world.tryGet<HealthComponent>(e);
        auto* inv = m_world.tryGet<InventoryComponent>(e);
        if (!xf || !hp || !hp->isAlive || !inv) return;

        const float dx = TileMap::tileCentre(tileX) - xf->x;
        const float dy = TileMap::tileCentre(tileY) - xf->y;
        constexpr float REPAIR_RANGE = TILE_SIZE * 3.0f;
        if (dx * dx + dy * dy > REPAIR_RANGE * REPAIR_RANGE) return;

        auto countItem = [&](const char* key) {
            int total = 0;
            for (const auto& slot : inv->slots) {
                if (slot.isValid() && slot.key == key) total += slot.quantity;
            }
            return total;
        };
        auto consumeItem = [&](const char* key, int qty) {
            for (int i = 0; i < INVENTORY_GRID_SLOTS && qty > 0; ++i) {
                if (!inv->slots[i].isValid() || inv->slots[i].key != key) continue;
                int take = std::min(inv->slots[i].quantity, qty);
                inv->slots[i].quantity -= take;
                qty -= take;
                if (inv->slots[i].quantity <= 0) {
                    inv->removeItem(i);
                    --i;
                }
            }
        };

        if (countItem("plank") < 3 || countItem("scrap_metal") < 1) return;
        consumeItem("plank", 3);
        consumeItem("scrap_metal", 1);
        inv->recalculateGridStats();

        if (m_map.repairDoor(static_cast<uint16_t>(doorID))) {
            broadcastDoorState(static_cast<uint16_t>(doorID), false);
            sendInventorySyncToPeer(peerIdx);
            DZ_LOG_INFO("[Door] Player %u rebuilt door %d at (%d,%d)",
                        peerIdx, doorID, tileX, tileY);
        }
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onSelectWeaponReq — 장비 슬롯은 유지하고 활성 무기만 변경
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::onSelectWeaponReq(uint32_t peerIdx, uint8_t slot) {
    if (slot > 1) return;

    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (!net || net->role != NetRole::LocallyOwned || net->ownerID != peerIdx) continue;

        auto* inv = m_world.tryGet<InventoryComponent>(e);
        if (!inv) return;

        Item& selected = inv->equipped[slot];
        if (!selected.isValid() || selected.category != ItemCategory::Weapon) return;

        inv->activeWeaponSlot = (slot == 0) ? EquipSlot::PrimaryWeapon
                                            : EquipSlot::SecondaryWeapon;
        DZ_LOG_DEBUG("[Inventory] Player %u selected %s weapon: %s",
                     peerIdx, slot == 0 ? "primary" : "secondary", selected.key.c_str());
        sendInventorySyncToPeer(peerIdx);
        return;
    }
}

void GameServer::onDoorToggleReq(uint32_t peerIdx, uint16_t doorID) {
    const auto& doors = m_map.getDoors();
    if (doorID >= doors.size()) return;

    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (!net || net->role != NetRole::LocallyOwned || net->ownerID != peerIdx) continue;
        auto* xf = m_world.tryGet<TransformComponent>(e);
        auto* hp = m_world.tryGet<HealthComponent>(e);
        if (!xf || !hp || !hp->isAlive) return;

        const auto& door = doors[doorID];
        if (door.broken) return;
        const float dx = TileMap::tileCentre(door.tx) - xf->x;
        const float dy = TileMap::tileCentre(door.ty) - xf->y;
        constexpr float INTERACT_RANGE = TILE_SIZE * 2.0f;
        if (dx * dx + dy * dy > INTERACT_RANGE * INTERACT_RANGE) return;

        if (m_map.toggleDoor(doorID)) {
            const bool open = m_map.getDoors()[doorID].open;
            DZ_LOG_INFO("[Door] Player %u %s door %u at (%d,%d)",
                        peerIdx, open ? "opened" : "closed", doorID, door.tx, door.ty);
            broadcastDoorState(doorID, open);
        }
        return;
    }
}

void GameServer::sendDoorState(uint32_t peerIdx, uint16_t doorID, bool open) {
    DoorStatePacket pkt{};
    pkt.doorID = doorID;
    pkt.open = open ? 1 : 0;
    const auto& doors = m_map.getDoors();
    if (doorID < doors.size()) pkt.broken = doors[doorID].broken ? 1 : 0;
    m_net.sendReliable(peerIdx, &pkt, sizeof(pkt));
}

void GameServer::broadcastDoorState(uint16_t doorID, bool open) {
    DoorStatePacket pkt{};
    pkt.doorID = doorID;
    pkt.open = open ? 1 : 0;
    const auto& doors = m_map.getDoors();
    if (doorID < doors.size()) pkt.broken = doors[doorID].broken ? 1 : 0;
    m_net.broadcastReliable(&pkt, sizeof(pkt));
}

void GameServer::syncDoorStatesToPeer(uint32_t peerIdx) {
    for (const auto& door : m_map.getDoors()) {
        sendDoorState(peerIdx, door.id, door.open);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onCraftRequest
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::onCraftRequest(uint32_t peerIdx, uint8_t recipeID) {
    bool ok = m_logic && m_logic->handleCraftRequest(peerIdx, recipeID);
    CraftAckPacket ack{};
    ack.packetType = static_cast<uint8_t>(PacketType::S2C_CraftAck);
    ack.success = ok ? 1 : 0;
    if (ok) {
        std::strncpy(ack.message, "조합 완료!", sizeof(ack.message) - 1);
        sendInventorySyncToPeer(peerIdx);
    } else {
        std::strncpy(ack.message, "재료 부족 또는 제작대 필요.", sizeof(ack.message) - 1);
    }
    m_net.sendReliable(peerIdx, &ack, sizeof(ack));
}

// ─────────────────────────────────────────────────────────────────────────────
// sendHpSyncToPeer — HP + 상태 플래그를 특정 피어에게 전송
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::sendHpSyncToPeer(uint32_t peerIdx) {
    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (!net || net->ownerID != peerIdx) continue;
        auto* hp  = m_world.tryGet<HealthComponent>(e);
        auto* cbt = m_world.tryGet<CombatComponent>(e);
        if (!hp) return;

        uint8_t flags = 0;
        if (hp->isAlive)       flags |= STATUS_ALIVE;
        if (!hp->isAlive)      flags |= STATUS_DEAD;
        if (cbt && cbt->isBleeding) flags |= STATUS_BLEEDING;
        if (cbt && cbt->isOnFire)   flags |= STATUS_ON_FIRE;
        if (cbt && cbt->isReloading) flags |= STATUS_RELOADING;

        m_net.sendHpSync(peerIdx,
                         static_cast<uint16_t>(net->netID),
                         hp->currentHp, hp->maxHp, hp->currentStamina, hp->maxStamina, flags);
        return;
    }
}

void GameServer::resetRound() {
    DZ_LOG_INFO("[Server] Resetting round...");
    
    // 1. 살아있는 모든 엔티티 파괴 (플레이어, 루트박스, 좀비, 건물 등 모두)
    for (EntityID id : m_world.alive()) {
        m_world.destroyEntity(Entity{id});
    }
    m_world.flushDestroyQueue();
    
    // 2. 타이머 초기화
    m_gameStarted = false;
    m_gameTimer = 0.0f;
    m_zombieSpawnTimer = 0.0f;
    m_activePlayers = 0;
    m_gameTime = 0.0f; // 전체 서버 진행 시간 리셋
    m_wasNight = false;
    
    // 3. 라운드 중 변한 시스템/맵 상태 리셋
    m_fire.reset();
    m_extraction.reset();
    loadMap("data/map.json");
    
    // 4. 좀비, 루트박스 재생성
    spawnZombies();
    spawnLootBoxes();
    
    // 5. 연결된 피어만 빈 인벤토리로 재등록 (접속 유지 + 재접속 가능)
    {
        std::unordered_map<uint32_t, LobbyPlayer> fresh;
        for (uint32_t pi = 0; pi < MAX_CLIENTS; ++pi) {
            if (m_net.isConnected(pi) && !m_peerUsernames[pi].empty())
                fresh[pi] = { m_peerUsernames[pi], InventoryComponent{} };
        }
        m_lobbyPlayers = std::move(fresh);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// broadcastTeamStatus — 팀별 생존 수 + 연합 비트 전송
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::broadcastTeamStatus() {
    // 연합 비트: 비트0=(1,2), 비트1=(1,3), 비트2=(1,4), 비트3=(2,3), 비트4=(2,4), 비트5=(3,4)
    static const int pairA[] = {1,1,1,2,2,3};
    static const int pairB[] = {2,3,4,3,4,4};
    uint8_t allianceBits = 0;
    for (int k = 0; k < 6; ++k)
        if (m_alliance.isAllied(pairA[k], pairB[k]))
            allianceBits |= (1 << k);

    m_net.broadcastTeamStatus(m_world, allianceBits, static_cast<uint16_t>(m_gameTime));
}

// ─────────────────────────────────────────────────────────────────────────────
// spawnNightWave — 밤 시작 시 플레이어들 주변에 웨이브 생성
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::spawnNightWave() {
    const int livingZombies = countLivingZombies(m_world);
    const int remainingSlots = std::max(0, NIGHT_ZOMBIE_TARGET - livingZombies);
    int waveSize = std::min(remainingSlots, NIGHT_WAVE_BASE + m_activePlayers * NIGHT_WAVE_PER_PLAYER);
    if (waveSize <= 0) {
        DZ_LOG_INFO("[Server] Night wave skipped: zombie cap reached (%d/%d).",
                    livingZombies, NIGHT_ZOMBIE_TARGET);
        return;
    }
    int spawned = 0;
    
    // 현재 살아있는 플레이어 수집
    std::vector<Entity> players;
    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (net && net->role == NetRole::LocallyOwned) {
            players.push_back(e);
        }
    }
    if (players.empty()) return;

    for (int i = 0; i < waveSize; ++i) {
        // 랜덤 플레이어 한 명을 골라서 그 주변에 스폰
        Entity target = players[std::rand() % players.size()];
        auto* txf = m_world.tryGet<TransformComponent>(target);
        if (!txf) continue;

        // 플레이어 반경 600~800 픽셀 위치에서 스폰 (화면 밖)
        float angle = static_cast<float>(std::rand() % 360) * 3.14159f / 180.0f;
        float dist  = 600.0f + static_cast<float>(std::rand() % 200);
        // 맵 경계 클램핑 (200x200 타일 = 6400px)
        float mapMax = 199.0f * 32.0f;
        float sx = std::max(32.0f, std::min(txf->x + std::cos(angle) * dist, mapMax));
        float sy = std::max(32.0f, std::min(txf->y + std::sin(angle) * dist, mapMax));

        Entity z = m_world.createEntity();
        auto& zxf = m_world.addComponent<TransformComponent>(z);
        zxf.x = sx;
        zxf.y = sy;
        zxf.rotation = 0.0f;

        auto& hp = m_world.addComponent<HealthComponent>(z);
        hp.maxHp = 100.0f;
        hp.currentHp = 100.0f;
        hp.team = Team::Neutral;
        hp.isAlive = true;

        auto& ai = m_world.addComponent<ZombieAIComponent>(z);
        ai.state = ZombieState::Frenzy; // 태어나자마자 무조건 광분 추격
        ai.targetX = txf->x;
        ai.targetY = txf->y;
        ai.type  = (std::rand() % 10 == 0) ? ZombieType::Brute : ZombieType::Runner; // 밤에는 빠른 놈들과 강력한 놈들 위주
        
        auto& net = m_world.addComponent<NetworkComponent>(z);
        net.netID = m_nextPlayerNetID++;
        net.role  = NetRole::ServerAuth;
        net.markDirty(DIRTY_TRANSFORM);

        spawned++;
    }
    DZ_LOG_INFO("[Server] Spawned %d wave zombies for night defense.", spawned);
}

// ─────────────────────────────────────────────────────────────────────────────
// onLootPickupReq
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::onLootPickupReq(uint32_t peerIdx, uint32_t lootNetID) {
    if (m_logic) {
        m_logic->handleLootPickup(peerIdx, lootNetID);
        sendInventorySyncToPeer(peerIdx);
    }
}

void GameServer::onItemDropReq(uint32_t peerIdx, uint8_t srcType, uint8_t srcIdx, uint16_t quantity) {
    if (quantity == 0) return;

    for (EntityID id : m_world.alive()) {
        Entity player{id};
        auto* net = m_world.tryGet<NetworkComponent>(player);
        if (!net || net->role != NetRole::LocallyOwned || net->ownerID != peerIdx) continue;

        auto* xf = m_world.tryGet<TransformComponent>(player);
        auto* hp = m_world.tryGet<HealthComponent>(player);
        auto* inv = m_world.tryGet<InventoryComponent>(player);
        if (!xf || !hp || !hp->isAlive || !inv) return;

        Item* src = nullptr;
        if (srcType == 0 && srcIdx < INVENTORY_GRID_SLOTS) {
            src = &inv->slots[srcIdx];
        } else if (srcType == 1) {
            src = &inv->equipped[static_cast<int>(EquipSlot::PrimaryWeapon)];
        } else if (srcType == 2) {
            src = &inv->equipped[static_cast<int>(EquipSlot::SecondaryWeapon)];
        }
        if (!src || !src->isValid() || src->quantity <= 0) return;

        const int dropQty = std::min<int>(quantity, src->quantity);
        if (dropQty <= 0) return;

        Item dropped = *src;
        dropped.quantity = dropQty;

        src->quantity -= dropQty;
        if (src->quantity <= 0) *src = {};
        inv->recalculateGridStats();

        Entity loot = m_world.createEntity();
        auto& lxf = m_world.addComponent<TransformComponent>(loot);
        const float angle = (static_cast<float>(std::rand() % 360)) * 3.14159265f / 180.0f;
        const float dist = 28.0f + static_cast<float>(std::rand() % 18);
        lxf.x = xf->x + std::cos(angle) * dist;
        lxf.y = xf->y + std::sin(angle) * dist;
        m_map.resolveAABB(lxf.x, lxf.y, 10.0f, 10.0f);

        auto& linv = m_world.addComponent<InventoryComponent>(loot);
        linv.addItem(dropped);

        auto& lnet = m_world.addComponent<NetworkComponent>(loot);
        lnet.netID = m_nextPlayerNetID++;
        lnet.role = NetRole::ServerAuth;
        lnet.markDirty(DIRTY_TRANSFORM);
        lnet.markDirty(DIRTY_INVENTORY);

        net->markDirty(DIRTY_INVENTORY);
        sendInventorySyncToPeer(peerIdx);
        DZ_LOG_INFO("[Inventory] Player %u dropped %s x%d", peerIdx, dropped.key.c_str(), dropQty);
        return;
    }
}

void GameServer::onDismantleReq(uint32_t peerIdx, uint8_t srcType, uint8_t srcIdx) {
    struct ResultDef { const char* key; int qty; };
    struct DismantleDef {
        const char* source;
        bool requiresWorkbench;
        ResultDef results[3];
        int resultCount;
    };
    static const DismantleDef RECIPES[] = {
        {"scrap_pipe",   false, {{"scrap_metal", 1}, {"", 0}, {"", 0}}, 1},
        {"nail_bat",     false, {{"plank", 1}, {"scrap_metal", 1}, {"", 0}}, 2},
        {"fire_axe",     false, {{"scrap_metal", 2}, {"", 0}, {"", 0}}, 1},
        {"pistol_9mm",   false, {{"scrap_metal", 2}, {"electronic_part", 1}, {"", 0}}, 2},
        {"smg_9mm",      false, {{"scrap_metal", 3}, {"electronic_part", 1}, {"", 0}}, 2},
        {"flamethrower", false, {{"scrap_metal", 3}, {"oil", 2}, {"electronic_part", 1}}, 3},
        {"molotov",      false, {{"oil", 1}, {"", 0}, {"", 0}}, 1},
    };

    auto makeItem = [](const char* key, int qty) {
        Item item;
        item.key = key;
        item.itemID = itemIDForKey(key);
        item.quantity = qty;
        if (std::strcmp(key, "scrap_metal") == 0) {
            item.category = ItemCategory::BuildMaterial; item.weight = 1.0f;
        } else if (std::strcmp(key, "plank") == 0) {
            item.category = ItemCategory::BuildMaterial; item.weight = 0.8f;
        } else if (std::strcmp(key, "electronic_part") == 0) {
            item.category = ItemCategory::BuildMaterial; item.weight = 0.5f;
        } else if (std::strcmp(key, "oil") == 0) {
            item.category = ItemCategory::BuildMaterial; item.weight = 1.2f;
        } else {
            item.category = ItemCategory::Misc; item.weight = 1.0f;
        }
        return item;
    };

    for (EntityID id : m_world.alive()) {
        Entity player{id};
        auto* net = m_world.tryGet<NetworkComponent>(player);
        if (!net || net->role != NetRole::LocallyOwned || net->ownerID != peerIdx) continue;

        auto* xf = m_world.tryGet<TransformComponent>(player);
        auto* hp = m_world.tryGet<HealthComponent>(player);
        auto* inv = m_world.tryGet<InventoryComponent>(player);
        if (!xf || !hp || !hp->isAlive || !inv) return;

        Item* src = nullptr;
        if (srcType == 0 && srcIdx < INVENTORY_GRID_SLOTS) {
            src = &inv->slots[srcIdx];
        } else if (srcType == 1) {
            src = &inv->equipped[static_cast<int>(EquipSlot::PrimaryWeapon)];
        } else if (srcType == 2) {
            src = &inv->equipped[static_cast<int>(EquipSlot::SecondaryWeapon)];
        }
        if (!src || !src->isValid()) return;

        const DismantleDef* recipe = nullptr;
        for (const auto& r : RECIPES) {
            if (src->key == r.source) { recipe = &r; break; }
        }
        if (!recipe) return;

        if (recipe->requiresWorkbench) {
            bool nearWorkbench = false;
            auto& bldPool = m_world.pool<BuildingComponent>();
            auto& xfPool = m_world.pool<TransformComponent>();
            for (size_t i = 0; i < bldPool.owners().size(); ++i) {
                auto& bld = bldPool.data()[i];
                if (bld.isDestroyed || !bld.isWorkbench()) continue;
                auto* wbxf = xfPool.get(bldPool.owners()[i]);
                if (!wbxf) continue;
                const float dx = wbxf->x - xf->x;
                const float dy = wbxf->y - xf->y;
                if (dx * dx + dy * dy <= 96.0f * 96.0f) {
                    nearWorkbench = true;
                    break;
                }
            }
            if (!nearWorkbench) return;
        }

        InventoryComponent next = *inv;
        Item* nextSrc = nullptr;
        if (srcType == 0 && srcIdx < INVENTORY_GRID_SLOTS) {
            nextSrc = &next.slots[srcIdx];
        } else if (srcType == 1) {
            nextSrc = &next.equipped[static_cast<int>(EquipSlot::PrimaryWeapon)];
        } else if (srcType == 2) {
            nextSrc = &next.equipped[static_cast<int>(EquipSlot::SecondaryWeapon)];
        }
        if (!nextSrc || !nextSrc->isValid()) return;
        --nextSrc->quantity;
        if (nextSrc->quantity <= 0) *nextSrc = {};
        next.recalculateGridStats();

        for (int i = 0; i < recipe->resultCount; ++i) {
            Item result = makeItem(recipe->results[i].key, recipe->results[i].qty);
            if (!next.addItem(result)) return;
        }

        *inv = next;
        net->markDirty(DIRTY_INVENTORY);
        sendInventorySyncToPeer(peerIdx);
        DZ_LOG_INFO("[Inventory] Player %u dismantled %s", peerIdx, recipe->source);
        return;
    }
}

void GameServer::sendInventorySyncToPeer(uint32_t peerIdx) {
    auto sendInv = [&](const InventoryComponent& inv) {
        InventorySyncPacket syncPkt{};
        syncPkt.packetType = static_cast<uint8_t>(PacketType::S2C_InventorySync);
        syncPkt.money = inv.money;
        syncPkt.usedSlots = static_cast<uint8_t>(inv.usedSlots);
        for (int i=0; i<INVENTORY_GRID_SLOTS; ++i) {
            if (inv.slots[i].isValid()) {
                syncPkt.gridSlots[i].itemID = inv.slots[i].itemID;
                std::strncpy(syncPkt.gridSlots[i].key, inv.slots[i].key.c_str(), 19);
                syncPkt.gridSlots[i].category = static_cast<uint8_t>(inv.slots[i].category);
                syncPkt.gridSlots[i].quantity = inv.slots[i].quantity;
                syncPkt.gridSlots[i].weight = inv.slots[i].weight;
            }
        }
        for (int i=0; i<EQUIPMENT_SLOT_COUNT; ++i) {
            if (inv.equipped[i].isValid()) {
                syncPkt.equipped[i].itemID = inv.equipped[i].itemID;
                std::strncpy(syncPkt.equipped[i].key, inv.equipped[i].key.c_str(), 19);
                syncPkt.equipped[i].category = static_cast<uint8_t>(inv.equipped[i].category);
                syncPkt.equipped[i].quantity = inv.equipped[i].quantity;
                syncPkt.equipped[i].weight = inv.equipped[i].weight;
            }
        }
        m_net.sendReliable(peerIdx, &syncPkt, sizeof(syncPkt));
    };

    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (net && net->role == NetRole::LocallyOwned && net->ownerID == peerIdx) {
            auto* inv = m_world.tryGet<InventoryComponent>(e);
            if (inv) {
                sendInv(*inv);
                return;
            }
        }
    }

    auto it = m_lobbyPlayers.find(peerIdx);
    if (it != m_lobbyPlayers.end()) {
        sendInv(it->second.inv);
    }
}

void GameServer::sendStashSyncToPeer(uint32_t peerIdx) {
    auto it = m_lobbyPlayers.find(peerIdx);
    if (it == m_lobbyPlayers.end()) return;

    StashSyncPacket stashPkt{};
    for (int i = 0; i < 40; ++i) {
        const Item& item = it->second.inv.stash[i];
        if (!item.isValid()) continue;
        stashPkt.stashSlots[i].itemID = item.itemID;
        std::strncpy(stashPkt.stashSlots[i].key, item.key.c_str(), 19);
        stashPkt.stashSlots[i].category = static_cast<uint8_t>(item.category);
        stashPkt.stashSlots[i].quantity = item.quantity;
        stashPkt.stashSlots[i].weight = item.weight;
    }
    m_net.sendReliable(peerIdx, &stashPkt, sizeof(stashPkt));
}

// ─────────────────────────────────────────────────────────────────────────────
// spawnLootBoxes
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::spawnLootBoxes() {
    constexpr int TOTAL_BOXES = 95;
    constexpr int INSIDE_BOXES = 55;
    constexpr int OUTSIDE_BOXES = TOTAL_BOXES - INSIDE_BOXES;
    constexpr float MIN_LOOT_SPACING = 96.0f;
    int spawned = 0;
    std::vector<std::pair<float, float>> placed;
    
    struct LootItemDef {
        const char* key;
        ItemCategory cat;
        float weight;
        int quantity;
        float baseWeight;
        uint8_t tag; // 0=medical, 1=food, 2=material, 3=ammo, 4=common weapon, 5=rare weapon
    };
    auto getDrop = [&](int theme) -> std::pair<LootItemDef, int> {
        static constexpr LootItemDef pool[] = {
            {"bandage",         ItemCategory::Consumable,    0.3f, 3, 18.0f, 0},
            {"medkit",          ItemCategory::Consumable,    1.0f, 1,  8.0f, 0},
            {"food_can",        ItemCategory::Consumable,    0.5f, 2, 12.0f, 1},
            {"plank",           ItemCategory::BuildMaterial, 0.8f, 3, 14.0f, 2},
            {"scrap_metal",     ItemCategory::BuildMaterial, 1.0f, 3, 14.0f, 2},
            {"electronic_part", ItemCategory::BuildMaterial, 0.5f, 2,  7.0f, 2},
            {"oil",             ItemCategory::BuildMaterial, 1.2f, 2,  9.0f, 2},
            {"ammo_9mm",        ItemCategory::Ammo,          0.3f, 30, 11.0f, 3},
            {"scrap_pipe",      ItemCategory::Weapon,        2.0f, 1,  7.0f, 4},
            {"fire_axe",        ItemCategory::Weapon,        3.0f, 1,  5.0f, 4},
            {"pistol_9mm",      ItemCategory::Weapon,        1.0f, PISTOL_MAG_CAPACITY, 5.0f, 4},
            {"smg_9mm",         ItemCategory::Weapon,        2.4f, 30, 3.0f, 5},
            {"flamethrower",    ItemCategory::Weapon,        5.0f, 1,  1.5f, 5},
        };
        auto tagMultiplier = [&](uint8_t tag) {
            switch (theme) {
                case 0: // Residential: supplies are common, weapons still possible.
                    if (tag == 0) return 1.7f;
                    if (tag == 1) return 1.8f;
                    if (tag == 2) return 1.1f;
                    if (tag == 5) return 0.35f;
                    return 0.75f;
                case 1: // Commercial: mixed shelves, more ammo and portable weapons.
                    if (tag == 0) return 1.15f;
                    if (tag == 1) return 1.25f;
                    if (tag == 3) return 1.35f;
                    if (tag == 4) return 1.25f;
                    return 0.9f;
                case 2: // Industrial: construction and fuel materials.
                    if (tag == 2) return 1.9f;
                    if (tag == 4) return 1.15f;
                    if (tag == 0) return 0.8f;
                    return 0.75f;
                case 3: // Military: ammo and high-end weapons.
                    if (tag == 3) return 1.8f;
                    if (tag == 5) return 2.2f;
                    if (tag == 4) return 1.25f;
                    if (tag == 1) return 0.45f;
                    return 0.85f;
                default:
                    return 1.0f;
            }
        };

        float total = 0.0f;
        for (const auto& item : pool) total += item.baseWeight * tagMultiplier(item.tag);
        float roll = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * total;
        for (const auto& item : pool) {
            roll -= item.baseWeight * tagMultiplier(item.tag);
            if (roll <= 0.0f) return {item, item.quantity};
        }
        return {pool[0], pool[0].quantity};
    };

    const auto& buildings = m_map.getBuildings();
    if (buildings.empty()) return;

    auto farEnough = [&](float wx, float wy) {
        for (auto& p : placed) {
            float dx = p.first - wx;
            float dy = p.second - wy;
            if (dx * dx + dy * dy < MIN_LOOT_SPACING * MIN_LOOT_SPACING) return false;
        }
        return true;
    };

    auto createLoot = [&](int tx, int ty, int theme) {
        float wx = TileMap::tileCentre(tx);
        float wy = TileMap::tileCentre(ty);
        if (!m_map.inBounds(tx, ty) || m_map.isSolid(tx, ty) || !farEnough(wx, wy)) return false;

        Entity e = m_world.createEntity();
        auto& xf = m_world.addComponent<TransformComponent>(e);
        xf.x = wx;
        xf.y = wy;

        auto& inv = m_world.addComponent<InventoryComponent>(e);
        auto drop = getDrop(theme);
        Item item;
        item.key = drop.first.key;
        item.itemID = itemIDForKey(drop.first.key);
        item.category = drop.first.cat;
        item.weight = drop.first.weight;
        item.quantity = drop.second;
        inv.addItem(item);

        auto& net = m_world.addComponent<NetworkComponent>(e);
        net.netID  = m_nextPlayerNetID++;
        net.role   = NetRole::ServerAuth;
        net.markDirty(DIRTY_TRANSFORM);
        placed.push_back({wx, wy});
        spawned++;
        return true;
    };

    int insideSpawned = 0;
    for (int attempts = 0; insideSpawned < INSIDE_BOXES && attempts < INSIDE_BOXES * 20; ++attempts) {
        const auto& b = buildings[std::rand() % buildings.size()];
        int b_w = std::max(1, b.w - 2);
        int b_h = std::max(1, b.h - 2);
        int tx = b.x + 1 + std::rand() % b_w;
        int ty = b.y + 1 + std::rand() % b_h;
        if (createLoot(tx, ty, b.theme)) ++insideSpawned;
    }

    int outsideSpawned = 0;
    for (int attempts = 0; outsideSpawned < OUTSIDE_BOXES && attempts < OUTSIDE_BOXES * 40; ++attempts) {
        int tx = 2 + std::rand() % std::max(1, m_map.width() - 4);
        int ty = 2 + std::rand() % std::max(1, m_map.height() - 4);
        if (!m_map.inBounds(tx, ty) || m_map.isSolid(tx, ty)) continue;
        TileType type = m_map.at(tx, ty).type;
        if (type == TILE_WOOD_FLOOR || type == TILE_WALL) continue;
        int theme = std::rand() % 4;
        if (createLoot(tx, ty, theme)) ++outsideSpawned;
    }

    DZ_LOG_INFO("[Server] Spawned %d static loot boxes (%d inside, %d outside)",
                spawned, insideSpawned, outsideSpawned);
}

} // namespace dz
