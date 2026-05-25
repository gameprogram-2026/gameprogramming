#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>
#include "Protocol.h"

// ─────────────────────────────────────────────────────────────────────────────
// Wire-format constants
// ─────────────────────────────────────────────────────────────────────────────
namespace dz {

constexpr uint8_t PROTOCOL_VERSION   = 1;
constexpr float   WORLD_TICK_RATE    = 20.0f;        // Hz — server authority rate
constexpr float   WORLD_TICK_DT      = 1.0f / WORLD_TICK_RATE;
constexpr float   RECONCILE_THRESHOLD = 9999.0f;     // 클라이언트 예측 우선 — snap 없음
constexpr float   RECONCILE_HARD      = 9999.0f;     // 로컬호스트에서 순간이동 수준 오차 없음
constexpr int     PREDICTION_BUFFER   = 128;         // max buffered unacked inputs

// ─────────────────────────────────────────────────────────────────────────────
// EntityStatusFlag — packed into StatusFlags byte
// ─────────────────────────────────────────────────────────────────────────────
enum EntityStatusFlag : uint8_t {
    STATUS_ALIVE     = 1 << 0,
    STATUS_SPRINTING = 1 << 1,
    STATUS_CROUCHING = 1 << 2,
    STATUS_BLEEDING  = 1 << 3,
    STATUS_ON_FIRE   = 1 << 4,
    STATUS_RELOADING = 1 << 5,
    STATUS_BUILDING  = 1 << 6,
    STATUS_DEAD      = 1 << 7,
};

// ─────────────────────────────────────────────────────────────────────────────
// EntityRecordType — packed into Header byte 1
// ─────────────────────────────────────────────────────────────────────────────
enum EntityRecordType : uint8_t {
    REC_PLAYER   = 0,
    REC_ZOMBIE   = 1,
    REC_BUILDING = 2,
    REC_LOOT     = 3,
    REC_FIRE     = 4,
};

// ─────────────────────────────────────────────────────────────────────────────
// Fletcher-16 checksum
// ─────────────────────────────────────────────────────────────────────────────
inline uint16_t fletcher16(const uint8_t* data, size_t len) noexcept {
    uint16_t s1 = 0, s2 = 0;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 255u;
        s2 = (s2 + s1) % 255u;
    }
    return static_cast<uint16_t>((s2 << 8) | s1);
}

// ─────────────────────────────────────────────────────────────────────────────
// EntityStateRecord — exactly 17 bytes
// Layout: [Header 4B | EntityID 2B | StatusFlags 1B | Position XY 8B | Checksum 2B]
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)
struct EntityStateRecord {
    // ── Header [4 B] ──────────────────────────────────────────────────────────
    uint8_t  version     = PROTOCOL_VERSION;  // [0]
    uint8_t  recordType  = REC_PLAYER;        // [1] EntityRecordType
    uint16_t seqAck      = 0;                 // [2-3] last input seq the server acked
    // ── EntityID [2 B] ────────────────────────────────────────────────────────
    uint16_t entityID    = 0;                 // [4-5]
    // ── StatusFlags [1 B] ─────────────────────────────────────────────────────
    uint8_t  statusFlags = 0;                 // [6]
    // ── Position XY [8 B] ─────────────────────────────────────────────────────
    float    x           = 0.0f;              // [7-10]
    float    y           = 0.0f;              // [11-14]
    // ── Checksum [2 B] — Fletcher-16 of bytes [0..14] ─────────────────────────
    uint16_t checksum    = 0;                 // [15-16]

    void computeChecksum() noexcept {
        checksum = fletcher16(reinterpret_cast<const uint8_t*>(this), 15);
    }

    bool verifyChecksum() const noexcept {
        return fletcher16(reinterpret_cast<const uint8_t*>(this), 15) == checksum;
    }

    bool isAlive()     const noexcept { return (statusFlags & STATUS_ALIVE)     != 0; }
    bool isSprinting() const noexcept { return (statusFlags & STATUS_SPRINTING) != 0; }
    bool isBleeding()  const noexcept { return (statusFlags & STATUS_BLEEDING)  != 0; }
    bool isOnFire()    const noexcept { return (statusFlags & STATUS_ON_FIRE)   != 0; }
};
static_assert(sizeof(EntityStateRecord) == 17, "EntityStateRecord must be exactly 17 bytes");
#pragma pack(pop)

// ─────────────────────────────────────────────────────────────────────────────
// SnapshotPacket — header + N × EntityStateRecord (CHAN_UNRELIABLE)
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)
struct SnapshotHeader {
    uint8_t  packetType  = static_cast<uint8_t>(PacketType::S2C_WorldSnapshot);
    uint16_t serverTick  = 0;
    uint8_t  entityCount = 0;
};
static_assert(sizeof(SnapshotHeader) == 4, "SnapshotHeader must be 4 bytes");
#pragma pack(pop)

constexpr size_t MAX_SNAPSHOT_ENTITIES = 64;
constexpr size_t SNAPSHOT_MAX_BYTES =
    sizeof(SnapshotHeader) + MAX_SNAPSHOT_ENTITIES * sizeof(EntityStateRecord);

// ─────────────────────────────────────────────────────────────────────────────
// InputPacket — client → server (CHAN_UNRELIABLE, ~60 Hz)
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)
struct InputPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::C2S_Input);
    uint32_t seqNum     = 0;     // monotonic, per-client
    float    moveX      = 0.0f;  // normalized [-1, 1]
    float    moveY      = 0.0f;
    float    aimAngle   = 0.0f;  // degrees [0, 360)
    uint16_t actions    = 0;     // InputAction bitmask
    uint16_t clientTick = 0;     // client's estimated server tick (for lag comp)
    float    clientDt   = 0.0f;  // actual client frame dt (seconds) — server must use this
};
static_assert(sizeof(InputPacket) == 25, "InputPacket size mismatch");
#pragma pack(pop)

// ─────────────────────────────────────────────────────────────────────────────
// EventPacket — reliable game events (CHAN_RELIABLE)
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)
// 서버→클라이언트: 접속 확인 (netID, 스폰 위치, 팀)
struct ConnectAckPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::S2C_ConnectAck);
    uint32_t netID      = 0;
    float    spawnX     = 0.0f;
    float    spawnY     = 0.0f;
    uint8_t  teamID     = 0;
};

struct DamageEventPacket {
    uint8_t  packetType    = static_cast<uint8_t>(PacketType::S2C_DamageEvent);
    uint16_t victimID      = 0;
    uint16_t attackerID    = 0;
    float    damage        = 0.0f;
    uint8_t  damageType    = 0;   // DamageType enum
    float    hitX          = 0.0f;
    float    hitY          = 0.0f;
    float    remainingHp   = 0.0f;
};

struct DeathEventPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::S2C_DeathEvent);
    uint16_t victimID   = 0;
    uint16_t killerID   = 0;
    uint8_t  damageType = 0;
};

struct AlliancePacket {
    uint8_t  packetType = 0;   // S2C_AllianceAck or C2S_AllianceBreak
    uint8_t  teamA      = 0;
    uint8_t  teamB      = 0;
    uint8_t  active     = 0;   // 1 = truce established, 0 = broken
};

struct ExtractionPacket {
    uint8_t  packetType  = static_cast<uint8_t>(PacketType::S2C_ExtractionStart);
    uint16_t playerID    = 0;
    float    channelTime = 5.0f;
    uint8_t  zoneID      = 0;
};

struct SirenEventPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::S2C_SirenEvent);
};

// 클라이언트 → 서버: 파밍 (루트박스/시체) 획득 요청
struct LootPickupPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::C2S_LootPickup);
    uint32_t lootNetID  = 0;
};

// 클라이언트 → 서버: 소모품 사용
struct UseItemPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::C2S_UseItem);
    char     key[20]    = {};  // null-terminated item key
};

// 서버 → 클라이언트: HP + 상태 플래그 동기화
struct HpSyncPacket {
    uint8_t  packetType  = static_cast<uint8_t>(PacketType::S2C_HpSync);
    uint16_t targetNetID = 0;
    float    currentHp   = 100.0f;
    float    maxHp       = 100.0f;
    uint8_t  flags       = 0;  // STATUS_* bitmask
    float    currentStamina = 100.0f;
    float    maxStamina     = 100.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// Crafting (CHAN_RELIABLE)
// ─────────────────────────────────────────────────────────────────────────────
struct CraftRequestPacket {
    uint8_t packetType = static_cast<uint8_t>(PacketType::C2S_CraftRequest);
    uint8_t recipeID   = 0; // 0=Barricade, 1=Turret
};

// 클라이언트 → 서버: 건설 배치
struct BuildPlacePacket {
    uint8_t  packetType   = static_cast<uint8_t>(PacketType::C2S_BuildPlace);
    int16_t  tileX        = 0;
    int16_t  tileY        = 0;
    uint8_t  buildingType = 0;  // BuildingType enum
};

// 서버 → 클라이언트: 팀 상태 브로드캐스트
struct TeamStatusPacket {
    uint8_t  packetType    = static_cast<uint8_t>(PacketType::S2C_TeamStatus);
    uint8_t  aliveCount[4] = {};  // 팀1~4 생존 인원
    uint8_t  allianceBits  = 0;   // 비트 0~5: 팀쌍(1-2, 1-3, 1-4, 2-3, 2-4, 3-4) 연합 여부
    uint16_t gameTimeSec   = 0;   // 서버 라운드 진행 시간(초)
};

// ── Auth & Inventory Sync ─────────────────────────────────────────────────
struct AuthPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::C2S_Auth);
    char     username[16] = {};
    char     password[16] = {};
    uint8_t  isRegister = 0; // 1 if register, 0 if login
};

struct AuthAckPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::S2C_AuthAck);
    uint8_t  success = 0;
    char     message[64] = {};
    uint16_t redirectPort = 0;
};

// struct SyncItem corresponds to shared/ecs/components/InventoryComponent.h -> Item (simplified for net sync)
struct SyncItem {
    uint32_t itemID = 0;
    char     key[20] = {};
    uint8_t  category = 0;
    int      quantity = 0;
    float    weight = 0.0f;
};

struct InventorySyncPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::S2C_InventorySync);
    int      money = 0;
    uint8_t  usedSlots = 0;
    SyncItem gridSlots[20] = {}; // INVENTORY_GRID_SLOTS
    SyncItem equipped[5] = {};   // EQUIPMENT_SLOT_COUNT
};

struct StashSyncPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::S2C_StashSync);
    SyncItem stashSlots[40] = {}; // STASH_SLOTS
};

struct StashTransferPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::C2S_StashTransfer);
    uint8_t  fromLocation = 0; // 0=Grid, 1=Equip, 2=Stash
    int8_t   fromIndex = 0;
    uint8_t  toLocation = 0;   // 0=Grid, 1=Equip, 2=Stash
    int8_t   toIndex = 0;
};

struct JoinMatchPacket {
    uint8_t  packetType = static_cast<uint8_t>(PacketType::C2S_JoinMatch);
};

// 서버 → 클라이언트: 포탑 발사 이벤트 (레이저 빔 시각화용, CHAN_UNRELIABLE)
struct TurretFirePacket {
    uint8_t  packetType  = static_cast<uint8_t>(PacketType::S2C_TurretFire);
    uint16_t turretNetID = 0;   ///< 발사한 포탑의 네트워크 ID
    float    fromX       = 0.0f; ///< 포탑 위치 (월드 좌표)
    float    fromY       = 0.0f;
    float    toX         = 0.0f; ///< 표적 위치 (월드 좌표)
    float    toY         = 0.0f;
    uint8_t  ownerTeam   = 0;    ///< 포탑 소유 팀 (레이저 색상 구분용)
};

#pragma pack(pop)

} // namespace dz
