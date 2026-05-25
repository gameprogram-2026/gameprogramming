#pragma once
#include <cstdint>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// Network constants
// ─────────────────────────────────────────────────────────────────────────────
constexpr uint16_t DEFAULT_SERVER_PORT = 7777;
constexpr int      MAX_CLIENTS         = 8;   ///< 4 teams × 2
constexpr int      MAX_TEAMS           = 4;
constexpr int      TEAM_SIZE           = 2;

/// ENet channels
enum Channel : uint8_t {
    CHAN_RELIABLE   = 0,   ///< Game events, inventory, damage (guaranteed)
    CHAN_UNRELIABLE = 1,   ///< Position snapshots, noise events (drop-ok)
    CHAN_COUNT      = 2,
};

// ─────────────────────────────────────────────────────────────────────────────
// InputAction — bitmask packed into InputPacket::actions (shared client/server)
// ─────────────────────────────────────────────────────────────────────────────
enum InputAction : uint16_t {
    ACT_NONE        = 0,
    ACT_SHOOT       = 1 << 0,
    ACT_AIM         = 1 << 1,
    ACT_RELOAD      = 1 << 2,
    ACT_INTERACT    = 1 << 3,
    ACT_SPRINT      = 1 << 4,
    ACT_CROUCH      = 1 << 5,
    ACT_THROW       = 1 << 6,
    ACT_BUILD       = 1 << 7,
    ACT_SWAP_WEAPON = 1 << 8,
    ACT_INVENTORY   = 1 << 9,
    ACT_MAP         = 1 << 10,
    ACT_MELEE       = 1 << 11,
};

// ─────────────────────────────────────────────────────────────────────────────
// PacketType — one byte header on every message
// ─────────────────────────────────────────────────────────────────────────────
enum class PacketType : uint8_t {
    // ── Handshake ─────────────────────────────────────────────────────────────
    C2S_Connect         = 0x01,
    S2C_ConnectAck      = 0x02,
    S2C_Disconnect      = 0x03,

    // ── Auth & Lobby ──────────────────────────────────────────────────────────
    C2S_Auth            = 0x04,
    S2C_AuthAck         = 0x05,
    S2C_InventorySync   = 0x06,
    S2C_StashSync       = 0x07,
    C2S_StashTransfer   = 0x08,
    C2S_JoinMatch       = 0x09,

    // ── Game state ────────────────────────────────────────────────────────────
    C2S_Input           = 0x10,   ///< Client → server: player input this frame
    S2C_WorldSnapshot   = 0x11,   ///< Server → all: full/delta world state
    S2C_EntitySpawn     = 0x12,
    S2C_EntityDestroy   = 0x13,

    // ── Gameplay events (reliable) ────────────────────────────────────────────
    S2C_DamageEvent     = 0x20,
    S2C_DeathEvent      = 0x21,
    S2C_LootDrop        = 0x22,
    C2S_LootPickup      = 0x23,
    C2S_BuildRequest    = 0x24,
    S2C_BuildAck        = 0x25,
    C2S_CraftRequest    = 0x26,
    C2S_FireThrow       = 0x27,   ///< Molotov, flamethrower burst
    S2C_FireUpdate      = 0x28,   ///< BFS fire tile delta

    // ── Alliance ──────────────────────────────────────────────────────────────
    C2S_AlliancePropose = 0x30,
    S2C_AllianceAck     = 0x31,
    C2S_AllianceBreak   = 0x32,

    // ── Extraction ────────────────────────────────────────────────────────────
    C2S_ExtractionReady = 0x40,
    S2C_ExtractionStart = 0x41,
    S2C_ExtractionResult= 0x42,
    S2C_ExtractionUpdate= 0x43,

    // ── Noise (unreliable) ────────────────────────────────────────────────────
    S2C_NoiseEvent      = 0x50,

    // ── Zombie AI ─────────────────────────────────────────────────────────────
    S2C_ZombieStateChange = 0x60,

    // ── Item / HP ─────────────────────────────────────────────────────────────
    C2S_UseItem         = 0x70,   ///< 소모품 사용 요청
    S2C_HpSync          = 0x71,   ///< HP + 상태 플래그 동기화

    // ── Build (reliable) ──────────────────────────────────────────────────────
    C2S_BuildPlace      = 0x73,   ///< 건설 배치 요청

    // ── Team status (reliable, broadcast) ────────────────────────────────────
    S2C_TeamStatus      = 0x74,   ///< 팀별 생존 인원 + 연합 행렬

    // ── Turret events (unreliable) ────────────────────────────────────────────
    S2C_TurretFire      = 0x75,   ///< 포탑 발사 이벤트 (레이저 빔 시각화)

    // ── Global Events ─────────────────────────────────────────────────────────
    S2C_SirenEvent      = 0x76,
};

// ─────────────────────────────────────────────────────────────────────────────
// Packet structs — POD only, packed, little-endian
// All multi-byte integers should be converted with SDL_SwapLE* before sending.
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)

struct PktHeader {
    PacketType type;
    uint32_t   tick;       ///< Server tick number
};

/// Client → Server  (CHAN_UNRELIABLE, ~64 Hz)
struct PktC2SInput {
    PktHeader header;
    uint32_t  seqNum;      ///< Monotonically increasing per-client
    float     moveX;       ///< Normalized movement vector X ∈ [-1, 1]
    float     moveY;
    float     aimAngle;    ///< Degrees [0, 360)
    uint16_t  actionFlags; ///< Bitmask: shoot, interact, sprint, reload…
};

/// Server → Client  (CHAN_UNRELIABLE, ~20 Hz)
struct PktEntityState {
    uint32_t netID;
    float    x, y;
    float    rotation;
    float    hp;
    uint8_t  animFrame;
    uint8_t  flags;        ///< isAlive, isSprinting, etc.
};

struct PktStashTransfer {
    PktHeader header;
    uint8_t   srcType;   ///< 0: Grid, 1: Primary, 2: Secondary, 3: Stash
    uint8_t   srcIdx;
    uint8_t   dstType;
    uint8_t   dstIdx;
};

/// Server → Client  (CHAN_RELIABLE)
struct PktDamageEvent {
    PktHeader header;
    uint32_t  victimNetID;
    uint32_t  attackerNetID;
    float     damage;
    uint8_t   damageType;
    float     hitX, hitY;
};

/// Server → Client  (CHAN_UNRELIABLE)
struct PktExtractionUpdate {
    PktHeader header;
    float     progress;    ///< [0.0, 1.0]
    uint8_t   zoneID;
};

#pragma pack(pop)

} // namespace dz
