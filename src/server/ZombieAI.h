#pragma once
#include <cstdint>
#include "shared/ecs/World.h"
#include "shared/ecs/components/TransformComponent.h"
#include "shared/ecs/components/SoundEmitterComponent.h"
#include "server/systems/NoiseSystem.h"
#include "server/systems/CombatSystem.h"
#include "shared/TileMap.h"

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// ZombieState FSM
//
//  Idle ──noise≥Soft──► Alert ──noise≥Loud──► Chase ──noise≥Deafening──► Frenzy
//   ▲                     │                     │                           │
//   └─────silence 10s─────┘       silence 8s ───┘       30s timeout ───────┘
//
// Frenzy: 20-metre chain aggro — activates Frenzy on all nearby zombies.
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// ZombieType — 3종 (기획서 3.2)
// ─────────────────────────────────────────────────────────────────────────────
enum class ZombieType : uint8_t {
    Shambler = 0,  ///< HP 60, 느림, 가장 흔함
    Runner   = 1,  ///< HP 40, 매우 빠름, 경계 즉시 추격
    Brute    = 2,  ///< HP 200, 매우 느림, 2배 데미지, 바리케이드 즉시 파괴
};

enum class ZombieState : uint8_t {
    Idle    = 0,   ///< Wander between patrol waypoints
    Alert   = 1,   ///< Investigate noise source
    Chase   = 2,   ///< Run toward locked target
    Frenzy  = 3,   ///< Attack anything; chain aggro 20 m radius
    Dead    = 4,
};

constexpr float ZOMBIE_HEARING_RADIUS   = 384.0f; ///< 12 tiles base hearing range
constexpr float ZOMBIE_FRENZY_CHAIN_R   = 640.0f; ///< 20 m chain aggro radius
constexpr float ZOMBIE_SIGHT_RADIUS     = 320.0f; ///< 10 tiles line-of-sight range
constexpr float ZOMBIE_ATTACK_RADIUS    =  25.0f; ///< Melee reach

// Shambler (default)
constexpr float ZOMBIE_SPEED_IDLE       =  38.0f; ///< px/s
constexpr float ZOMBIE_SPEED_ALERT      =  70.0f;
constexpr float ZOMBIE_SPEED_CHASE      =  90.0f;
constexpr float ZOMBIE_SPEED_FRENZY     = 110.0f;
constexpr float ZOMBIE_ATTACK_DAMAGE    =  20.0f;
constexpr float ZOMBIE_ATTACK_RATE      =   0.9f; ///< s between attacks

// Runner overrides
constexpr float RUNNER_SPEED_CHASE      = 140.0f; ///< 빠름
constexpr float RUNNER_SPEED_FRENZY     = 160.0f;
constexpr float RUNNER_ATTACK_DAMAGE    =  10.0f; ///< 빠르지만 약함
constexpr float RUNNER_ATTACK_RATE      =   0.6f;

// Brute overrides
constexpr float BRUTE_SPEED_CHASE       =  40.0f; ///< 매우 느림
constexpr float BRUTE_SPEED_FRENZY      =  55.0f;
constexpr float BRUTE_ATTACK_DAMAGE     =  40.0f; ///< 기본의 2배
constexpr float BRUTE_ATTACK_RATE       =   2.0f;
constexpr float BRUTE_ATTACK_RADIUS     =  35.0f; ///< 더 긴 팔

constexpr float ZOMBIE_PATROL_INTERVAL  =   4.0f; ///< s per waypoint
constexpr float ZOMBIE_ALERT_DECAY      =   0.12f;///< alert/s in silence
constexpr float ZOMBIE_SILENCE_CHASE    =   8.0f; ///< s of silence to drop Chase
constexpr float ZOMBIE_FRENZY_DURATION  =  30.0f; ///< s Frenzy lasts

struct ZombieAIComponent {
    ZombieType  type        = ZombieType::Shambler;
    ZombieState state       = ZombieState::Idle;
    float       alertLevel  = 0.0f;   ///< [0,1]; rises with noise, decays in silence
    float       stateTimer  = 0.0f;   ///< Time spent in current state
    float       patrolTimer = 0.0f;
    float       patrolWpX   = 0.0f;   ///< Current patrol waypoint
    float       patrolWpY   = 0.0f;
    float       targetX     = 0.0f;   ///< Chase/investigate target world position
    float       targetY     = 0.0f;
    uint32_t    targetNetID = 0;       ///< Entity being chased (0 = sound source)
    float       attackTimer = 0.0f;
    bool        chainTriggered = false;///< Frenzy chain aggro already propagated
};

// ─────────────────────────────────────────────────────────────────────────────
// ZombieAISystem — server-only
// ─────────────────────────────────────────────────────────────────────────────
class ZombieAISystem {
public:
    void setMap(const TileMap* map) noexcept { m_map = map; }
    void setCombatSystem(CombatSystem* cs) noexcept { m_combat = cs; }
    void update(World& world, const NoiseSystem& noise, float dt, float gameTime);

private:
    const TileMap* m_map    = nullptr;
    CombatSystem*  m_combat = nullptr;

    void updateFSM(World& world, Entity zombie, ZombieAIComponent& ai,
                   const NoiseSystem& noise, float dt, float gameTime);
    void doMovement(World& world, Entity zombie, ZombieAIComponent& ai, float dt);
    void doAttack(World& world, Entity zombie, ZombieAIComponent& ai, float dt);
    void pickPatrolWaypoint(ZombieAIComponent& ai,
                            const TransformComponent& xf) noexcept;
    void triggerChainFrenzy(World& world, Entity source, float wx, float wy);
    Entity findNearestEnemy(World& world, Entity zombie);
};

} // namespace dz
