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

namespace dz {

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
    m_net.onUseItem        ([this](uint32_t i, const char* k)        { onUseItem(i, k); });
    m_net.onAlliancePropose([this](uint32_t i, uint8_t toTeam)       { onAllianceProposeReq(i, toTeam); });
    m_net.onBuildPlace     ([this](uint32_t i, int16_t tx, int16_t ty, uint8_t bt){ onBuildPlace(i,tx,ty,bt); });
    m_net.onCraft          ([this](uint32_t i, uint8_t recipeID)     { onCraftRequest(i, recipeID); });
    m_net.onLootPickup     ([this](uint32_t i, uint32_t nid)         { onLootPickupReq(i, nid); });

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
    m_gameTime += dt;
    m_noise.update(m_world, dt);
    m_movement.update(m_world, m_map, dt);
    m_combat.update(m_world, dt);
    m_zombieAI.update(m_world, m_noise, dt, m_gameTime);
    m_fire.update(m_world, m_map, dt);
    m_build.updateTurrets(m_world, dt);
    m_extraction.update(m_world, dt, m_gameTime);

    // 좀비 리스폰 타이머 (10초마다 체크)
    static float zombieSpawnTimer = 0.0f;
    zombieSpawnTimer += dt;
    if (zombieSpawnTimer >= 10.0f) {
        zombieSpawnTimer = 0.0f;
        spawnZombies(); // 내부에서 maxZombies 체크 후 부족하면 스폰
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
    
    if (isNight && !m_wasNight) {
        m_wasNight = true;
        DZ_LOG_INFO("[Server] Night has fallen! Spawning zombie wave...");
        spawnNightWave();
        // 밤이 되면 비명 소리 재생
        m_noise.addEvent(0, 0, 9999.0f, 4, 1.0f); 
    } else if (!isNight && m_wasNight) {
        m_wasNight = false;
        DZ_LOG_INFO("[Server] Day breaks! Cleaning up excess zombies.");
        // 낮이 되면 남은 웨이브 좀비들을 정리하여 안전 구역을 확보 (최대 15마리만 유지)
        int zCount = 0;
        for (EntityID id : m_world.alive()) {
            Entity e{id};
            if (m_world.tryGet<ZombieAIComponent>(e) && m_world.tryGet<HealthComponent>(e)->isAlive) {
                if (zCount > 15) {
                    m_world.destroyEntity(e);
                } else {
                    zCount++;
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
        pkt.zoneID = 0; // TODO: get zoneID if needed
        m_net.sendUnreliable(pi, &pkt, sizeof(pkt));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// sendSnapshots
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::sendSnapshots() {
    m_net.broadcastSnapshot(m_world, static_cast<uint16_t>(m_tick));
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
        if (m_db.registerAccount(username, password)) {
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
    if (!m_db.loginAccount(username, password, loadedInv)) {
        ack.success = 0;
        std::strncpy(ack.message, "Login failed. Check credentials.", sizeof(ack.message));
        m_net.sendReliable(peerIdx, &ack, sizeof(ack));
        return;
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
    if (m_lobbyPlayers.find(peerIdx) == m_lobbyPlayers.end()) return;
    auto& inv = m_lobbyPlayers[peerIdx].inv;

    auto getItemPtr = [&](uint8_t type, uint8_t idx) -> Item* {
        if (type == 0 && idx < INVENTORY_GRID_SLOTS) return &inv.slots[idx];
        if (type == 1 && idx == 0) return &inv.equipped[0];
        if (type == 2 && idx == 0) return &inv.equipped[1];
        if (type == 3 && idx < 40) return &inv.stash[idx];
        return nullptr;
    };

    Item* src = getItemPtr(srcType, srcIdx);
    Item* dst = getItemPtr(dstType, dstIdx);

    if (src && dst) {
        std::swap(*src, *dst);
    }
}

void GameServer::onJoinMatch(uint32_t peerIdx) {
    if (m_lobbyPlayers.find(peerIdx) == m_lobbyPlayers.end()) return;
    
    auto& lobbyPlayer = m_lobbyPlayers[peerIdx];
    InventoryComponent invToSpawn = lobbyPlayer.inv;

    Entity e = m_world.createEntity();
    uint32_t netID = m_nextPlayerNetID++;

    // 스폰 위치를 무작위로 변경하여 항상 같은 위치(구석)에서 태어나지 않도록 함
    float spawnX = static_cast<float>((std::rand() % 96 + 16) * TILE_SIZE);
    float spawnY = static_cast<float>((std::rand() % 96 + 16) * TILE_SIZE);

    int teamIdx = static_cast<int>(peerIdx / TEAM_SIZE); // 0-based
    if (teamIdx >= 4) teamIdx = 0;

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
        startWeapon.itemID   = 1;
        startWeapon.key      = "pistol_9mm";
        startWeapon.category = ItemCategory::Weapon;
        startWeapon.quantity = 1;
        startWeapon.weight   = 1.5f;
        inv.addItem(startWeapon);
        
        Item medkit;
        medkit.itemID   = 2;
        medkit.key      = "medkit";
        medkit.category = ItemCategory::Consumable;
        medkit.quantity = 3;
        medkit.weight   = 0.5f;
        inv.addItem(medkit);

        Item bandage;
        bandage.itemID   = 3;
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
    cbt.ammoInMag = 7;
    cbt.ammoReserve = 14;

    m_net.setPeerNetID(peerIdx, netID);

    ConnectAckPacket cAck{};
    cAck.packetType = static_cast<uint8_t>(PacketType::S2C_ConnectAck);
    cAck.netID  = netID;
    cAck.spawnX = xf.x;
    cAck.spawnY = xf.y;
    cAck.teamID = static_cast<uint8_t>(teamIdx + 1);
    m_net.sendReliable(peerIdx, &cAck, sizeof(cAck));

    // 시작 처리
    if (!m_gameStarted) {
        m_gameStarted = true;
        m_gameTimer = 0.0f;
    }
    m_activePlayers++;

    DZ_LOG_INFO("[Server] Player %u joined match (peer %u, team %d) at (%.0f, %.0f)",
                netID, peerIdx, teamIdx + 1, xf.x, xf.y);
}

void GameServer::onClientDisconnect(uint32_t peerIdx) {
    for (EntityID id : m_world.alive()) {
        Entity e{id};
        auto* net = m_world.tryGet<NetworkComponent>(e);
        if (net && net->ownerID == peerIdx) {
            auto* inv = m_world.tryGet<InventoryComponent>(e);
            if (inv) {
                // Drop items on disconnect
                onDeathLoot(e);
                // Clear inventory to wipe from DB
                inv->slots.fill({});
                inv->equipped.fill({});
                inv->usedSlots = 0;
                inv->currentWeight = 0;
                if (!m_peerUsernames[peerIdx].empty()) {
                    m_db.saveAccount(m_peerUsernames[peerIdx], *inv);
                }
            }
            m_world.destroyEntity(e);
            m_activePlayers--;
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

    // Betrayal detection
    if (result.betrayal) {
        // Find teams involved and break alliance
        uint8_t teamA = 0, teamB = 0;
        for (EntityID id : m_world.alive()) {
            Entity e{id};
            auto* net = m_world.tryGet<NetworkComponent>(e);
            auto* hp  = m_world.tryGet<HealthComponent>(e);
            if (!net || !hp) continue;
            if (net->netID == result.attackerID) teamA = static_cast<uint8_t>(hp->team);
            if (net->netID == result.victimID)   teamB = static_cast<uint8_t>(hp->team);
        }
        if (teamA && teamB) m_alliance.handleBetrayal(teamA, teamB);
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
        // 플레이어일 때만 m_activePlayers 감소
        if (net->role == NetRole::LocallyOwned) {
            m_activePlayers--;
        }
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

    // 만약 좀비가 죽었다면 30% 확률로 아이템 드랍
    auto* ai = m_world.tryGet<ZombieAIComponent>(victim);
    if (ai && (std::rand() % 100) < 30) {
        Entity loot = m_world.createEntity();
        auto& lxf = m_world.addComponent<TransformComponent>(loot);
        auto* vxf = m_world.tryGet<TransformComponent>(victim);
        if (vxf) { lxf.x = vxf->x; lxf.y = vxf->y; }
        
        auto& linv = m_world.addComponent<InventoryComponent>(loot);
        Item item;
        item.itemID = 1000 + (std::rand() % 1000);
        item.quantity = 1;
        if (std::rand() % 2 == 0) {
            item.key = "bandage"; item.category = ItemCategory::Consumable; item.weight = 0.3f;
        } else {
            item.key = "9mm_ammo"; item.category = ItemCategory::Ammo; item.weight = 0.05f; item.quantity = 5;
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
    }

    m_world.destroyEntity(player);
    m_activePlayers--;
}

void GameServer::onDeathLoot(Entity player) {
    auto* inv = m_world.tryGet<InventoryComponent>(player);
    auto* xf  = m_world.tryGet<TransformComponent>(player);
    if (!inv || !xf) return;

    for (int i = 0; i < INVENTORY_GRID_SLOTS; ++i) {
        if (!inv->slots[i].isValid()) continue;

        Entity loot = m_world.createEntity();

        float ox = static_cast<float>((std::rand() % 48) - 24);
        float oy = static_cast<float>((std::rand() % 48) - 24);

        auto& lxf = m_world.addComponent<TransformComponent>(loot);
        lxf.x = xf->x + ox;
        lxf.y = xf->y + oy;

        auto& linv = m_world.addComponent<InventoryComponent>(loot);
        linv.addItem(inv->slots[i]);

        auto& lnet = m_world.addComponent<NetworkComponent>(loot);
        lnet.netID  = m_nextPlayerNetID++;
        lnet.role   = NetRole::ServerAuth;
        lnet.markDirty(DIRTY_TRANSFORM);

        DZ_LOG_INFO("[Death] LootEntity %u spawned: %s x%d at (%.0f,%.0f)",
            lnet.netID, inv->slots[i].key.c_str(), inv->slots[i].quantity, lxf.x, lxf.y);
        inv->removeItem(i);
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

    // 탈출 지점 로드 (map.json을 무시하고 중앙 랜덤에 배치)
    float tsz = static_cast<float>(TILE_SIZE);
    
    // 맵 중앙 부근에서 랜덤하게 탈출구 1개 배치
    int tx = 30 + std::rand() % 20;
    int ty = 30 + std::rand() % 20;
    float cx = (tx + 0.5f) * tsz;
    float cy = (ty + 0.5f) * tsz;
    m_extraction.addZone({0, cx, cy, false});
    
    m_extraction.onExtractionOpen([this]() {
        SirenEventPacket pkt{};
        m_net.broadcastReliable(&pkt, sizeof(pkt));
    });

    return true;
}

void GameServer::spawnZombies() {
    // 모든 위치는 건물 외부 개방 공간에 배치 (applyBuildingCollisions 충돌 없음)
    struct ZSpawn { float x, y; ZombieType type; float hp; };
    static const ZSpawn spawns[] = {
        // ── 구역A 좌측 도로 ──────────────────────────────────────────────────
        { 3*32.f, 10*32.f, ZombieType::Shambler, 60.f},
        {16*32.f,  3*32.f, ZombieType::Shambler, 60.f},
        { 3*32.f, 28*32.f, ZombieType::Runner,   40.f},

        // ── 구역A 우측 ───────────────────────────────────────────────────────
        {20*32.f, 28*32.f, ZombieType::Shambler, 60.f},
        {35*32.f, 20*32.f, ZombieType::Runner,   40.f},

        // ── 구역B 외곽 (쇼핑몰) ─────────────────────────────────────────────
        {33*32.f, 38*32.f, ZombieType::Shambler, 60.f},
        {58*32.f, 34*32.f, ZombieType::Shambler, 60.f},
        {33*32.f, 55*32.f, ZombieType::Runner,   40.f},
        {58*32.f, 56*32.f, ZombieType::Brute,   200.f},
        {40*32.f, 30*32.f, ZombieType::Shambler, 60.f},
        {57*32.f, 32*32.f, ZombieType::Runner,   40.f},

        // ── 구역C 외곽 (산업단지) ────────────────────────────────────────────
        {56*32.f, 58*32.f, ZombieType::Shambler, 60.f},
        {72*32.f, 56*32.f, ZombieType::Shambler, 60.f},
        {56*32.f, 66*32.f, ZombieType::Runner,   40.f},
        {72*32.f, 74*32.f, ZombieType::Brute,   200.f},
    };

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
        if (net && net->ownerID == peerIdx) {
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
void GameServer::onBuildPlace(uint32_t peerIdx, int16_t tileX, int16_t tileY, uint8_t btype) {
    if (m_logic) m_logic->handleBuildRequest(peerIdx, tileX, tileY,
                                              static_cast<BuildingType>(btype));
}

// ─────────────────────────────────────────────────────────────────────────────
// onCraftRequest
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::onCraftRequest(uint32_t peerIdx, uint8_t recipeID) {
    if (m_logic) m_logic->handleCraftRequest(peerIdx, recipeID);
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
    m_activePlayers = 0;
    m_gameTime = 0.0f; // 전체 서버 진행 시간 리셋
    
    // 3. Extraction system 리셋
    m_extraction.reset();
    
    // 4. 좀비, 루트박스 재생성
    spawnZombies();
    spawnLootBoxes();
    
    // 5. 이전에는 클라이언트를 모두 끊었으나, 이제는 끊지 않고 다음 라운드 매칭을 대기할 수 있게 함
    for (uint32_t pi = 0; pi < MAX_CLIENTS; ++pi) {
        // m_peerUsernames를 유지하면 로비 상태 그대로 유지 가능
        // 만약 완전 초기화하려면 m_lobbyPlayers 등을 건드려야 하지만, 
        // 클라이언트는 죽었을 때 로컬에서만 로비로 돌아가므로 연결을 유지함.
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

    m_net.broadcastTeamStatus(m_world, allianceBits, static_cast<uint16_t>(std::fmod(m_gameTime, 180.0f)));
}

// ─────────────────────────────────────────────────────────────────────────────
// spawnNightWave — 밤 시작 시 플레이어들 주변에 웨이브 생성
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::spawnNightWave() {
    int waveSize = 15 + m_activePlayers * 5; // 웨이브 좀비 수 대폭 하향 (너무 많아지는 것 방지)
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
        float sx = txf->x + std::cos(angle) * dist;
        float sy = txf->y + std::sin(angle) * dist;

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
    if (m_logic) m_logic->handleLootPickup(peerIdx, lootNetID);
}

// ─────────────────────────────────────────────────────────────────────────────
// spawnLootBoxes
// ─────────────────────────────────────────────────────────────────────────────
void GameServer::spawnLootBoxes() {
    int totalBoxes = 50;
    int spawned = 0;
    
    struct LootItemDef { const char* key; ItemCategory cat; float weight; };
    auto getDrop = [&](int theme) -> std::pair<LootItemDef, int> {
        int roll = std::rand() % 100;
        if (theme == 0) { // Residential
            if (roll < 40) return {{"bandage", ItemCategory::Consumable, 0.3f}, 2};
            if (roll < 75) return {{"food_can", ItemCategory::Consumable, 0.5f}, 1};
            if (roll < 95) return {{"scrap_pipe", ItemCategory::Weapon, 2.0f}, 1};
            return {{"medkit", ItemCategory::Consumable, 1.0f}, 1};
        } else if (theme == 1 || theme == 2) { // Commercial / Industrial
            if (roll < 30) return {{"bandage", ItemCategory::Consumable, 0.3f}, 3};
            if (roll < 60) return {{"pistol_9mm", ItemCategory::Weapon, 1.5f}, 1};
            if (roll < 80) return {{"9mm_ammo", ItemCategory::Ammo, 0.05f}, 14};
            return {{"axe", ItemCategory::Weapon, 3.0f}, 1};
        } else { // Military
            if (roll < 40) return {{"medkit", ItemCategory::Consumable, 1.0f}, 2};
            if (roll < 70) return {{"rifle_556", ItemCategory::Weapon, 3.5f}, 1};
            if (roll < 90) return {{"556_ammo", ItemCategory::Ammo, 0.05f}, 30};
            return {{"flamethrower", ItemCategory::Weapon, 5.0f}, 1};
        }
    };

    const auto& buildings = m_map.getBuildings();
    if (buildings.empty()) return;

    for (int i = 0; i < totalBoxes; ++i) {
        // 랜덤 건물 선택
        const auto& b = buildings[std::rand() % buildings.size()];
        
        // 건물 내 랜덤 위치
        int b_w = std::max(1, b.w - 2);
        int b_h = std::max(1, b.h - 2);
        int tx = b.x + 1 + std::rand() % b_w;
        int ty = b.y + 1 + std::rand() % b_h;

        Entity e = m_world.createEntity();
        auto& xf = m_world.addComponent<TransformComponent>(e);
        xf.x = tx * TILE_SIZE + TILE_SIZE / 2.0f;
        xf.y = ty * TILE_SIZE + TILE_SIZE / 2.0f;

        auto& inv = m_world.addComponent<InventoryComponent>(e);
        auto drop = getDrop(b.theme);
        Item item;
        item.itemID = 100 + (std::rand() % 1000);
        item.key = drop.first.key;
        item.category = drop.first.cat;
        item.weight = drop.first.weight;
        item.quantity = drop.second;
        inv.addItem(item);

        auto& net = m_world.addComponent<NetworkComponent>(e);
        net.netID  = m_nextPlayerNetID++;
        net.role   = NetRole::ServerAuth;
        net.markDirty(DIRTY_TRANSFORM);
        spawned++;
    }
    DZ_LOG_INFO("[Server] Spawned %d static loot boxes", spawned);
}

} // namespace dz
